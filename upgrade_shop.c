#include "upgrade_shop.h"
#include "audio.h"
#include "camera.h"
#include "csv_helper.h"
#include "dialogue.h"
#include "entity2d.h"
#include "fade_manager.h"
#include "font_helper.h"
#include "frame_time.h"
#include "game_objects/currency_handler.h"
#include "game_objects/gp_state.h"
#include "game_objects/tractor_beam.h"
#include "game_objects/ufo.h"
#include "game_objects/weapons.h"
#include "joypad.h"
#include "minimap.h"
#include "rdpq_mode.h"
#include "resource_helper.h"
#include "save.h"
#include "stick_calibration.h"
#include "stick_normalizer.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

// --------------------------------------------------------------------------
// Constants & Configuration
// --------------------------------------------------------------------------

#define SHOP_ITEM_COUNT 3
#define SHOP_COL_SPACING 92
#define SHOP_TOP_ROW_Y (120 - 40)
#define SHOP_TEXT_Y (148 - 40)
#define SHOP_TOP_ROW_GAP 8
#define SHOP_PRICE_ICON_PADDING 4
#define HOLD_TIME_PROCESS_BUY 1.0f

// --------------------------------------------------------------------------
// Types
// --------------------------------------------------------------------------

typedef enum
{
    SHOP_STATE_IDLE,
    SHOP_STATE_OPENING_ANIM,
    SHOP_STATE_BROWSE,
    SHOP_STATE_CONFIRM_POPUP,
    SHOP_STATE_PROCESS_BUY,
    SHOP_STATE_CLOSING_ANIM
} ShopState;

typedef struct
{
    const char *label;
    const char *icon_path;
    uint16_t price;
    uint16_t unlock_flag;
    weapon_type_t weapon_type;
} shop_item_def_t;

typedef struct
{
    ShopState state;
    bool initialized;
    bool trigger_loaded;
    bool trigger_has_rect;
    bool trigger_selected;
    struct vec2 trigger_top_left;
    struct vec2 trigger_size;
    int selection_index;
    bool popup_yes_selected;
    float state_timer;
    const struct entity2D *saved_next_target;
    bool prev_inputs[4]; // left, right, up, down
    sprite_t *spr_item_icons[SHOP_ITEM_COUNT];
    sprite_t *spr_currency;
    sprite_t *spr_btn_c_down;
    wav64_t *sfx_error;
    wav64_t *sfx_select;
    wav64_t *sfx_confirm;
    wav64_t *sfx_cancel;
    wav64_t *sfx_crankhorn_installed;
    bool music_active;
    bool sound_played;
} ShopContext;

// --------------------------------------------------------------------------
// Data Definitions
// --------------------------------------------------------------------------

static const shop_item_def_t kShopItems[SHOP_ITEM_COUNT] = {
    {.label = "BULLETS+", .icon_path = "rom:/bullets_upgraded_icon_00.sprite", .price = 2, .unlock_flag = GP_UNLOCK_BULLETS_UPGRADED, .weapon_type = WEAPON_BULLETS},
    {.label = "LASER", .icon_path = "rom:/laser_icon_00.sprite", .price = 3, .unlock_flag = GP_UNLOCK_LASER, .weapon_type = WEAPON_LASER},
    {.label = "BOMB", .icon_path = "rom:/bomb_icon_00.sprite", .price = 5, .unlock_flag = GP_UNLOCK_BOMB, .weapon_type = WEAPON_BOMB},
};

static ShopContext s_ctx = {0};

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static bool item_is_unlocked(int idx)
{
    return (idx >= 0 && idx < SHOP_ITEM_COUNT) && gp_state_unlock_get(kShopItems[idx].unlock_flag);
}

static int find_first_available_item(void)
{
    for (int i = 0; i < SHOP_ITEM_COUNT; ++i)
    {
        if (!item_is_unlocked(i))
            return i;
    }
    return -1;
}

static int find_next_available_item(int start, int dir)
{
    if (dir == 0)
        return (start >= 0 && start < SHOP_ITEM_COUNT && !item_is_unlocked(start)) ? start : -1;

    for (int i = 0; i < SHOP_ITEM_COUNT; ++i)
    {
        start = (start + dir + SHOP_ITEM_COUNT) % SHOP_ITEM_COUNT;
        if (!item_is_unlocked(start))
            return start;
    }
    return -1;
}

static void apply_purchase(int itemIdx)
{
    if (itemIdx < 0 || itemIdx >= SHOP_ITEM_COUNT)
        return;

    uint16_t currency = gp_state_currency_get();
    if (currency >= kShopItems[itemIdx].price)
        currency -= kShopItems[itemIdx].price;
    gp_state_currency_set(currency);
    gp_state_unlock_set(kShopItems[itemIdx].unlock_flag, true);
    weapons_set_current(kShopItems[itemIdx].weapon_type);
    save_sync_gp_state();
    save_write();
}

static void change_state(ShopState newState)
{
    s_ctx.state = newState;
    s_ctx.state_timer = 0.0f;

    switch (newState)
    {
    case SHOP_STATE_OPENING_ANIM:
        s_ctx.saved_next_target = ufo_get_next_target();
        ufo_start_transition_animation(SPACE, PLANET);
        if (!s_ctx.music_active)
        {
            audio_play_music(MUSIC_SHOP, NULL);
            s_ctx.music_active = true;
        }
        audio_update_music_speed(AUDIO_SPEED_MAX);
        break;
    case SHOP_STATE_BROWSE:
        // Always validate selection_index - if invalid or pointing to unlocked item, find first available
        if (s_ctx.selection_index == -1 || (s_ctx.selection_index >= 0 && s_ctx.selection_index < SHOP_ITEM_COUNT && item_is_unlocked(s_ctx.selection_index)))
            s_ctx.selection_index = find_first_available_item();
        break;
    case SHOP_STATE_CONFIRM_POPUP:
        s_ctx.popup_yes_selected = false;
        break;
    case SHOP_STATE_PROCESS_BUY:
        fade_manager_start(TO_BLACK);
        s_ctx.sound_played = false;
        s_ctx.state_timer = 0.0f;
        break;
    case SHOP_STATE_CLOSING_ANIM:
        ufo_start_transition_animation(PLANET, SPACE);
        if (s_ctx.music_active)
        {
            audio_play_music(MUSIC_NORMAL, gp_state_get_current_folder());
            s_ctx.music_active = false;
        }
        break;
    default:
        break;
    }
}

static void load_trigger_data(void)
{
    if (s_ctx.trigger_loaded)
        return;

    FILE *pFile = fopen("rom:/space/shop.csv", "r");
    if (!pFile)
    {
        s_ctx.trigger_loaded = true;
        return;
    }

    char szLine[512];
    while (csv_helper_fgets_checked(szLine, sizeof(szLine), pFile, NULL))
    {
        csv_helper_strip_eol(szLine);
        if (szLine[0] == '\0')
            continue;

        char szCopy[512];
        if (!csv_helper_copy_line_for_tokenizing(szLine, szCopy, sizeof(szCopy)))
            continue;

        strtok(szCopy, ","); // Skip name
        char *x = strtok(NULL, ","), *y = strtok(NULL, ",");
        char *w = strtok(NULL, ","), *h = strtok(NULL, ",");

        if (x && y && w && h && csv_helper_parse_xy_from_tokens(x, y, &s_ctx.trigger_top_left))
        {
            float fW, fH;
            if (csv_helper_parse_float(w, &fW) && csv_helper_parse_float(h, &fH))
            {
                s_ctx.trigger_size.fX = fW;
                s_ctx.trigger_size.fY = fH;
                s_ctx.trigger_has_rect = true;
            }
        }
        break;
    }
    fclose(pFile);
    s_ctx.trigger_loaded = true;
}

// --------------------------------------------------------------------------
// Public API: Init / Free
// --------------------------------------------------------------------------

void upgrade_shop_init(void)
{
    if (s_ctx.initialized)
        return;
    memset(&s_ctx, 0, sizeof(ShopContext));

    for (int i = 0; i < SHOP_ITEM_COUNT; ++i)
        s_ctx.spr_item_icons[i] = sprite_load(kShopItems[i].icon_path);
    s_ctx.spr_currency = sprite_load("rom:/currency_00.sprite");
    s_ctx.spr_btn_c_down = sprite_load("rom:/btn_c_down_00.sprite");

    s_ctx.sfx_error = wav64_load("rom:/btn_error.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    s_ctx.sfx_select = wav64_load("rom:/btn_select.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    s_ctx.sfx_confirm = wav64_load("rom:/btn_confirm.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    s_ctx.sfx_cancel = wav64_load("rom:/btn_cancel.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    s_ctx.sfx_crankhorn_installed = wav64_load("rom:/crankhorn_installed.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    load_trigger_data();
    s_ctx.state = SHOP_STATE_IDLE;
    s_ctx.selection_index = -1;
    s_ctx.initialized = true;
}

void upgrade_shop_free(void)
{
    if (!s_ctx.initialized)
        return;

    for (int i = 0; i < SHOP_ITEM_COUNT; ++i)
        SAFE_FREE_SPRITE(s_ctx.spr_item_icons[i]);
    SAFE_FREE_SPRITE(s_ctx.spr_currency);
    SAFE_FREE_SPRITE(s_ctx.spr_btn_c_down);

    SAFE_CLOSE_WAV64(s_ctx.sfx_error);
    SAFE_CLOSE_WAV64(s_ctx.sfx_select);
    SAFE_CLOSE_WAV64(s_ctx.sfx_confirm);
    SAFE_CLOSE_WAV64(s_ctx.sfx_cancel);
    SAFE_CLOSE_WAV64(s_ctx.sfx_crankhorn_installed);

    s_ctx.initialized = false;
    s_ctx.music_active = false;
}

// --------------------------------------------------------------------------
// Logic: Update
// --------------------------------------------------------------------------

static eUpgradeShopResult update_idle(bool bCDown)
{
    if (gp_state_get() != SPACE || !s_ctx.trigger_has_rect)
        return UPGRADE_SHOP_RESULT_NONE;
    if (dialogue_is_active() || minimap_is_active() || stick_calibration_is_active_without_menu())
        return UPGRADE_SHOP_RESULT_NONE;

    s_ctx.trigger_selected = false;
    const struct entity2D *pUfo = ufo_get_entity();
    if (pUfo && entity2d_is_active(pUfo) && entity2d_is_collidable(pUfo))
        s_ctx.trigger_selected = entity2d_check_collision_circle_rect(pUfo, s_ctx.trigger_top_left, s_ctx.trigger_size);

    if (s_ctx.trigger_selected && bCDown)
    {
        gp_act_t current_act = gp_state_act_get();
        if (current_act < ACT_MAIN)
        {
            dialogue_start("d_garage_closed");
        }
        else if (item_is_unlocked(0) && item_is_unlocked(1) && item_is_unlocked(2))
        {
            // All three upgrades are already unlocked
            dialogue_start("d_garage_unlocked_all");
        }
        else
        {
            wav64_play(s_ctx.sfx_confirm, MIXER_CHANNEL_USER_INTERFACE);
            change_state(SHOP_STATE_OPENING_ANIM);
        }
    }
    return UPGRADE_SHOP_RESULT_NONE;
}

static eUpgradeShopResult update_browsing(void)
{
    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    // Proper edge detection for stick navigation (prevents jittering)
    int8_t iStickX = stick_normalizer_get_x();
    bool bLeftHeld = inputs.btn.d_left || inputs.btn.c_left || iStickX < -STICK_DEADZONE_MENU;
    bool bRightHeld = inputs.btn.d_right || inputs.btn.c_right || iStickX > STICK_DEADZONE_MENU;
    bool bLeftPressed = bLeftHeld && !s_ctx.prev_inputs[0];
    bool bRightPressed = bRightHeld && !s_ctx.prev_inputs[1];

    s_ctx.prev_inputs[0] = bLeftHeld;
    s_ctx.prev_inputs[1] = bRightHeld;

    int nav_x = 0;
    if (bLeftPressed)
        nav_x = -1;
    else if (bRightPressed)
        nav_x = 1;

    if (nav_x != 0 && s_ctx.selection_index >= 0)
    {
        int newIdx = find_next_available_item(s_ctx.selection_index, nav_x);
        if (newIdx != s_ctx.selection_index && newIdx >= 0)
        {
            wav64_play(s_ctx.sfx_select, MIXER_CHANNEL_USER_INTERFACE);
            s_ctx.selection_index = newIdx;
        }
    }

    if ((pressed.a || pressed.z) && s_ctx.selection_index >= 0)
    {
        if (gp_state_currency_get() >= kShopItems[s_ctx.selection_index].price)
        {
            wav64_play(s_ctx.sfx_confirm, MIXER_CHANNEL_USER_INTERFACE);
            change_state(SHOP_STATE_CONFIRM_POPUP);
        }
        else
            wav64_play(s_ctx.sfx_error, MIXER_CHANNEL_USER_INTERFACE);
    }

    if (pressed.b || pressed.start)
    {
        wav64_play(s_ctx.sfx_cancel, MIXER_CHANNEL_USER_INTERFACE);
        change_state(SHOP_STATE_CLOSING_ANIM);
        return UPGRADE_SHOP_RESULT_EXIT;
    }

    return UPGRADE_SHOP_RESULT_NONE;
}

static eUpgradeShopResult update_confirm_popup(void)
{
    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    int8_t iStickY = stick_normalizer_get_y();
    bool bUpHeld = inputs.btn.d_up || inputs.btn.c_up || iStickY > STICK_DEADZONE_MENU;
    bool bDownHeld = inputs.btn.d_down || inputs.btn.c_down || iStickY < -STICK_DEADZONE_MENU;
    bool up = bUpHeld && !s_ctx.prev_inputs[2];
    bool down = bDownHeld && !s_ctx.prev_inputs[3];

    s_ctx.prev_inputs[2] = bUpHeld;
    s_ctx.prev_inputs[3] = bDownHeld;

    if (up || down)
    {
        wav64_play(s_ctx.sfx_select, MIXER_CHANNEL_USER_INTERFACE);
        s_ctx.popup_yes_selected = !s_ctx.popup_yes_selected;
    }

    if (pressed.a || pressed.z)
    {
        wav64_play(s_ctx.sfx_confirm, MIXER_CHANNEL_USER_INTERFACE);
        if (s_ctx.popup_yes_selected)
            change_state(SHOP_STATE_PROCESS_BUY);
        else
            change_state(SHOP_STATE_BROWSE);
    }
    else if (pressed.b || pressed.start || (pressed.a && !s_ctx.popup_yes_selected))
    {
        if (pressed.b || pressed.start)
            wav64_play(s_ctx.sfx_cancel, MIXER_CHANNEL_USER_INTERFACE);
        change_state(SHOP_STATE_BROWSE);
    }

    return UPGRADE_SHOP_RESULT_NONE;
}

static eUpgradeShopResult update_process_buy(void)
{
    /* Wait for fade to black to finish */
    if (fade_manager_is_busy())
    {
        return UPGRADE_SHOP_RESULT_EXIT;
    }

    /* Play sound if not already played */
    if (!s_ctx.sound_played && s_ctx.sfx_crankhorn_installed)
    {
        wav64_play(s_ctx.sfx_crankhorn_installed, MIXER_CHANNEL_USER_INTERFACE);
        s_ctx.sound_played = true;
        return UPGRADE_SHOP_RESULT_EXIT;
    }

    /* Wait for sound to finish */
    if (mixer_ch_playing(MIXER_CHANNEL_USER_INTERFACE))
    {
        return UPGRADE_SHOP_RESULT_EXIT;
    }

    /* Sound finished - apply purchase and fade from black */
    apply_purchase(s_ctx.selection_index);
    fade_manager_start(FROM_BLACK);
    change_state(SHOP_STATE_CLOSING_ANIM);
    return UPGRADE_SHOP_RESULT_NONE;
}

eUpgradeShopResult upgrade_shop_update(bool bCDown)
{
    if (!s_ctx.initialized)
        return UPGRADE_SHOP_RESULT_NONE;

    if (bCDown && tractor_beam_is_active())
        bCDown = false;

    switch (s_ctx.state)
    {
    case SHOP_STATE_IDLE:
        return update_idle(bCDown);
    case SHOP_STATE_OPENING_ANIM:
        if (!ufo_is_transition_playing())
        {
            change_state(SHOP_STATE_BROWSE);
            return UPGRADE_SHOP_RESULT_OPEN;
        }
        return UPGRADE_SHOP_RESULT_NONE;
    case SHOP_STATE_BROWSE:
        return update_browsing();
    case SHOP_STATE_CONFIRM_POPUP:
        return update_confirm_popup();
    case SHOP_STATE_PROCESS_BUY:
        return update_process_buy();
    case SHOP_STATE_CLOSING_ANIM:
        if (!ufo_is_transition_playing() && !fade_manager_is_busy())
        {
            if (s_ctx.saved_next_target)
            {
                ufo_set_next_target(s_ctx.saved_next_target);
                s_ctx.saved_next_target = NULL;
            }
            ufo_end_transition_animation(SPACE);
            change_state(SHOP_STATE_IDLE);
            return UPGRADE_SHOP_RESULT_EXIT;
        }
        return UPGRADE_SHOP_RESULT_NONE;
    default:
        return UPGRADE_SHOP_RESULT_NONE;
    }
}

// --------------------------------------------------------------------------
// Rendering
// --------------------------------------------------------------------------

static void render_item_column(int idx, int centerX, bool isSelected, bool isPopup)
{
    bool unlocked = item_is_unlocked(idx);
    sprite_t *icon = s_ctx.spr_item_icons[idx];
    int iconW = icon ? icon->width : 0;
    int curW = s_ctx.spr_currency ? s_ctx.spr_currency->width : 0;

    char szPrice[16];
    snprintf(szPrice, sizeof(szPrice), "%d", kShopItems[idx].price);
    float priceW = font_helper_get_text_width(FONT_NORMAL, szPrice);
    int totalW = iconW + SHOP_TOP_ROW_GAP + curW + SHOP_PRICE_ICON_PADDING + (int)priceW;
    int x = centerX - (totalW / 2);

    // Icon
    if (unlocked)
    {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(128, 128, 128, 255));
        rdpq_mode_alphacompare(1);
        rdpq_sprite_blit(icon, x, SHOP_TOP_ROW_Y - 2, NULL);
    }
    else
    {
        rdpq_set_mode_copy(false);
        rdpq_mode_alphacompare(1);
        rdpq_sprite_blit(icon, x, SHOP_TOP_ROW_Y - 2, NULL);
    }
    x += iconW + SHOP_TOP_ROW_GAP;

    // Currency + Price
    if (s_ctx.spr_currency)
    {
        if (unlocked)
        {
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_set_prim_color(RGBA32(128, 128, 128, 255));
            rdpq_mode_alphacompare(1);
            rdpq_sprite_blit(s_ctx.spr_currency, x, SHOP_TOP_ROW_Y, NULL);
        }
        else
        {
            rdpq_set_mode_copy(false);
            rdpq_mode_alphacompare(1);
            rdpq_sprite_blit(s_ctx.spr_currency, x, SHOP_TOP_ROW_Y, NULL);
        }

        rdpq_textparms_t tp = {0};
        uint16_t currency = gp_state_currency_get();
        bool canAfford = currency >= kShopItems[idx].price;
        if (unlocked)
            tp.style_id = FONT_STYLE_GRAY;
        else if (!canAfford)
            tp.style_id = FONT_STYLE_RED;
        else
            tp.style_id = 0;
        rdpq_text_printf(&tp, FONT_NORMAL, x + curW + SHOP_PRICE_ICON_PADDING, SHOP_TOP_ROW_Y + UI_FONT_Y_OFFSET, "%s", szPrice);
    }

    // Label
    float w = font_helper_get_text_width(FONT_NORMAL, kShopItems[idx].label);
    rdpq_textparms_t tp = {0};
    if (isSelected && !isPopup)
    {
        char szSel[64];
        snprintf(szSel, sizeof(szSel), "> %s <", kShopItems[idx].label);
        w = font_helper_get_text_width(FONT_NORMAL, szSel);
        rdpq_text_printf(&tp, FONT_NORMAL, (int)(centerX - w * 0.5f), SHOP_TEXT_Y, "%s", szSel);
    }
    else
    {
        tp.style_id = unlocked ? FONT_STYLE_GRAY : 0;
        rdpq_text_printf(&tp, FONT_NORMAL, (int)(centerX - w * 0.5f), SHOP_TEXT_Y, "%s", kShopItems[idx].label);
    }
}

void upgrade_shop_render(void)
{
    if (!s_ctx.initialized)
        return;

    if (s_ctx.state == SHOP_STATE_IDLE)
    {
        if (s_ctx.trigger_selected && s_ctx.trigger_has_rect && s_ctx.spr_btn_c_down)
        {
            struct vec2 center = vec2_make(s_ctx.trigger_top_left.fX + s_ctx.trigger_size.fX * 0.5f, s_ctx.trigger_top_left.fY + s_ctx.trigger_size.fY * 0.5f);
            struct vec2i screenPos;
            camera_world_to_screen(&g_mainCamera, center, &screenPos);
            float zoom = camera_get_zoom(&g_mainCamera);
            int y = screenPos.iY - (int)((s_ctx.trigger_size.fY * 0.5f * zoom)) - s_ctx.spr_btn_c_down->height - 16;
            rdpq_set_mode_copy(false);
            rdpq_mode_alphacompare(1);
            rdpq_sprite_blit(s_ctx.spr_btn_c_down, screenPos.iX - (int)((float)s_ctx.spr_btn_c_down->width / 2.0f) - 8, y, NULL);
        }
        return;
    }

    if (s_ctx.state == SHOP_STATE_BROWSE || s_ctx.state == SHOP_STATE_CONFIRM_POPUP || s_ctx.state == SHOP_STATE_PROCESS_BUY)
    {
        ui_draw_darkening_overlay();
        int cx = SCREEN_W / 2;
        rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, SHOP_TOP_ROW_Y - 30, "^05Crankhorn's Garage^00");

        for (int i = 0; i < SHOP_ITEM_COUNT; ++i)
            render_item_column(i, cx + (i - 1) * SHOP_COL_SPACING, (i == s_ctx.selection_index), (s_ctx.state == SHOP_STATE_CONFIRM_POPUP));

        currency_handler_render_ui();

        if (s_ctx.state == SHOP_STATE_CONFIRM_POPUP)
        {
            int popupX = cx + (s_ctx.selection_index - 1) * SHOP_COL_SPACING;
            int y = SHOP_TEXT_Y + 30;
            float w = font_helper_get_text_width(FONT_NORMAL, "BUY?");
            rdpq_text_printf(NULL, FONT_NORMAL, (int)(popupX - w * 0.5f), y, "BUY?");
            y += 20;

            if (s_ctx.popup_yes_selected)
            {
                w = font_helper_get_text_width(FONT_NORMAL, "NO");
                rdpq_text_printf(NULL, FONT_NORMAL, (int)(popupX - w * 0.5f), y, "NO");
                w = font_helper_get_text_width(FONT_NORMAL, "> YES <");
                rdpq_text_printf(NULL, FONT_NORMAL, (int)(popupX - w * 0.5f), y + 16, "> YES <");
            }
            else
            {
                w = font_helper_get_text_width(FONT_NORMAL, "> NO <");
                rdpq_text_printf(NULL, FONT_NORMAL, (int)(popupX - w * 0.5f), y, "> NO <");
                w = font_helper_get_text_width(FONT_NORMAL, "YES");
                rdpq_text_printf(NULL, FONT_NORMAL, (int)(popupX - w * 0.5f), y + 16, "YES");
            }
        }
    }
}

bool upgrade_shop_is_active(void)
{
    return s_ctx.state != SHOP_STATE_IDLE && s_ctx.state != SHOP_STATE_CLOSING_ANIM;
}
