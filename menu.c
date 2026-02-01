#include "menu.h"
#include "audio.h"
#include "credits.h"
#include "debug_cheats.h"
#include "fade_manager.h"
#include "font_helper.h"
#include "frame_time.h"
#include "game_objects/race_handler.h"
#include "game_objects/ufo.h"
#include "libdragon.h"
#include "n64sys.h"
#include "rdpq.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "rdpq_text.h"
#include "resource_helper.h"
#include "satellite_pieces.h"
#include "save.h"
#include "stick_calibration.h"
#include "stick_normalizer.h"
#include "tv_helper.h"
#include "ui.h"
#include "upgrade_shop.h"
#include "vi.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Current menu state */
static eMenuState s_eMenuState = MENU_STATE_START_SCREEN;
static eMenuResult s_eMenuResult = MENU_RESULT_NONE;

/* Menu layout defines - Y offsets for menu items */
#define MENU_START_Y_OFFSET 50            /* Starting Y position relative to screen center */
#define MENU_SETTINGS_Y_OFFSET 18         /* Starting Y position for settings menu relative to screen center */
#define MENU_ITEM_SPACING 16              /* Vertical spacing between menu items */
#define MENU_DELETE_QUESTION_Y_OFFSET -30 /* Y offset for delete confirmation question */
#define MENU_DELETE_OPTIONS_Y_OFFSET 10   /* Y offset for delete confirmation options (relative to center) */
#define MENU_FLASH_TEXT_Y_OFFSET 80       /* Y offset for "PUSH START" text (lower third) */
#define MENU_CREDITS_Y_OFFSET -30         /* Y offset for credits menu relative to screen center */

/* Flashing text timing */
#define FLASH_INTERVAL_MS 640
static uint32_t s_uFlashTimer = 0;

/* Slide types for the intro slideshow */
typedef enum
{
    SLIDE_TYPE_TEXT,
    SLIDE_TYPE_SINGLE_SPRITE,
    SLIDE_TYPE_MULTIPANEL
} eSlideType;

/* Fade types for intro slides */
typedef enum
{
    INTRO_FADE_IN,
    INTRO_FADE_OUT,
    INTRO_FADE_IN_OUT,
    INTRO_NO_FADE
} eIntroFadeType;

/* Individual slide data structure - with flexible duration handling */
typedef struct
{
    eSlideType type;
    eIntroFadeType fadeType;
    const char *content; /* Text for text slides, sprite path for sprite/multi-panel slides */
    union
    {
        float singleDuration;        /* For text and single-sprite slides */
        const float *panelDurations; /* For multi-panel slides (pointer to duration array) */
    } duration;
} sIntroSlide;

/* Fade time for intro slides */
#define FADE_TIME 0.5f

/* Repeat delay for continuous cycling (frames) */
#define ITEM_CHANGE_DELAY_NORMAL 10 /* Normal delay for settings like overscan */
#define ITEM_CHANGE_DELAY_QUICK 3   /* Quick delay (1/3 of normal) for volume settings */
static uint32_t s_uOverscanRepeatTimer = 0;

/* Button state tracking for edge detection */
static bool s_bPrevStart = false;
static bool s_bPrevA = false;
static bool s_bPrevB = false;
static bool s_bPrevZ = false;
static bool s_bPrevL = false;
static bool s_bPrevUp = false;
static bool s_bPrevDown = false;
static bool s_bPrevLeft = false;
static bool s_bPrevRight = false;

/* Main menu selection */
typedef enum
{
    MAIN_MENU_NEW_GAME = 0,
    MAIN_MENU_SETTINGS = 1,
    MAIN_MENU_CONTROLS = 2,
    MAIN_MENU_CREDITS = 3,
    MAIN_MENU_COUNT,
} eMainMenuItem;
static int s_iMainMenuSelection = 0;

/* Pause menu selection */
typedef enum
{
    PAUSE_MENU_SETTINGS = 0,
    PAUSE_MENU_SAVE = 1,
    PAUSE_MENU_CLOSE = 2,
    PAUSE_MENU_COUNT,
} ePauseMenuItem;
static int s_iPauseMenuSelection = 0;

/* Settings menu selection */
typedef enum
{
    SETTINGS_TARGET_LOCK = 0,
    SETTINGS_OVERSCAN = 1,
    SETTINGS_PAL60 = 2,
    SETTINGS_MUSIC_VOLUME = 3,
    SETTINGS_SFX_VOLUME = 4,
    SETTINGS_CALIBRATION = 5,
    SETTINGS_DELETE_SAVE = 6,
    SETTINGS_MENU_COUNT,
} eSettingsMenuItem;
static int s_iSettingsMenuSelection = 0;

/* Delete confirmation selection */
static bool s_bDeleteConfirmSelection = false; /* false = NO, true = YES */

/* Save confirmation selection */
static bool s_bSaveConfirmSelection = false; /* false = NO, true = YES */

/* Exit race confirmation selection */
static bool s_bExitRaceConfirmSelection = false; /* false = NO, true = YES */

/* PAL60 confirmation state */
static float s_fPal60ConfirmTimer = 0.0f;
static bool s_bPal60ConfirmWaiting = false;
static eMenuState s_ePal60ConfirmPreviousState = MENU_STATE_SETTINGS; /* Track where we came from */

/* Calibration state */
static eMenuState s_eCalibrationPreviousState = MENU_STATE_SETTINGS;

/* Sprites */
static sprite_t *s_pStartScreenSprite = NULL;
static sprite_t *s_pControlsScreenSprite = NULL;
static sprite_t *s_pCreditsScreenSprite = NULL;

/* Intro slideshow configuration */
#define INTRO_SEQUENCE_LENGTH 15 /* Total number of slides in sequence */
#define INTRO_MULTIPANEL_COUNT 4 /* Number of panels in any multi-panel slide */

/* Intro slideshow sequence - unified slide data */
static sIntroSlide s_pIntroSlides[INTRO_SEQUENCE_LENGTH] = {
    {SLIDE_TYPE_TEXT, INTRO_FADE_IN_OUT, "One fateful night ...", {.singleDuration = 3.0f}},
    {SLIDE_TYPE_SINGLE_SPRITE, INTRO_FADE_IN_OUT, "rom:/intro_00_00.sprite", {.singleDuration = 3.5f}},
    {SLIDE_TYPE_SINGLE_SPRITE, INTRO_FADE_IN, "rom:/intro_01_00.sprite", {.singleDuration = 3.0f}},
    {SLIDE_TYPE_SINGLE_SPRITE, INTRO_NO_FADE, "rom:/intro_02_00.sprite", {.singleDuration = 2.0f}}, /* (first showing) */
    {SLIDE_TYPE_SINGLE_SPRITE, INTRO_NO_FADE, "rom:/intro_03_00.sprite", {.singleDuration = 0.7f}},
    {SLIDE_TYPE_SINGLE_SPRITE, INTRO_NO_FADE, "rom:/intro_02_00.sprite", {.singleDuration = 1.3f}}, /* (second showing) */
    {SLIDE_TYPE_SINGLE_SPRITE, INTRO_FADE_OUT, "rom:/intro_04_00.sprite", {.singleDuration = 3.0f}},
    {SLIDE_TYPE_SINGLE_SPRITE, INTRO_FADE_IN, "rom:/intro_05_00.sprite", {.singleDuration = 2.0f}},
    {SLIDE_TYPE_SINGLE_SPRITE, INTRO_NO_FADE, "rom:/intro_06_00.sprite", {.singleDuration = 1.5f}},
    {SLIDE_TYPE_MULTIPANEL, INTRO_FADE_OUT, "rom:/intro_07_00.sprite", {.panelDurations = (const float[]){2.5f, 0.75f, 2.0f, 3.75f}}},
    {SLIDE_TYPE_TEXT, INTRO_NO_FADE, "", {.singleDuration = 2.5f}},
    {SLIDE_TYPE_SINGLE_SPRITE, INTRO_FADE_IN, "rom:/intro_08_00.sprite", {.singleDuration = 3.0f}},
    {SLIDE_TYPE_SINGLE_SPRITE, INTRO_FADE_OUT, "rom:/intro_09_00.sprite", {.singleDuration = 1.5f}},
    //{SLIDE_TYPE_TEXT, INTRO_NO_FADE, "Let's repair this before my parents come home!", {.singleDuration = 3.0f}},
    {SLIDE_TYPE_TEXT, INTRO_NO_FADE, "", {.singleDuration = 2.5f}},
    {SLIDE_TYPE_SINGLE_SPRITE, INTRO_FADE_IN_OUT, "rom:/intro_10_00.sprite", {.singleDuration = 4.0f}},
};

/* Loaded sprites for slides (populated during initialization) */
static sprite_t *s_pLoadedSprites[INTRO_SEQUENCE_LENGTH] = {NULL};

static float s_fIntroTimer = 0.0f;
static int s_iIntroCurrentSlide = 0;
static int s_iIntroPanelReveal = 0; /* For special 4-panel reveal */

/* Cached sprite rendering parameters (updated when overscan changes) */

/* Save exists check (set at init and updated when save is deleted) */
static bool s_bProgressExists = false;

/* Track whether fade-to-black is for continuing (skip intro) or new game (show intro) */
static bool s_bFadeToBlackForContinue = false;

/* Sound effects */
static wav64_t *s_pSoundSelect = NULL;
static wav64_t *s_pSoundConfirm = NULL;
static wav64_t *s_pSoundCancel = NULL;
static wav64_t *s_pSoundStartScreen = NULL;

/* Intro audio */
static wav64_t *s_pIntroAudio = NULL;

/* Helper: Check if button was just pressed (edge detection) */
static bool button_pressed(bool _bCurrent, bool *_pPrev)
{
    bool bPressed = _bCurrent && !(*_pPrev);
    *_pPrev = _bCurrent;
    return bPressed;
}

/* Helper: Check horizontal navigation input */
static int get_horizontal_nav(const joypad_inputs_t *_pInputs)
{
    int8_t iStickX = stick_normalizer_get_x();
    if (_pInputs->btn.d_left || _pInputs->btn.c_left || iStickX < -STICK_DEADZONE_MENU)
        return -1;
    if (_pInputs->btn.d_right || _pInputs->btn.c_right || iStickX > STICK_DEADZONE_MENU)
        return 1;
    return 0;
}

/* Helper: Render text with selection markers */
static void render_menu_text(int _iX, int _iY, const char *_pText, bool _bSelected)
{
    if (_bSelected)
    {
        rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, _iY, "> %s <", _pText);
    }
    else
    {
        rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, _iY, "%s", _pText);
    }
}

/* Helper: Start menu music - now handled by audio.c */
static void start_menu_music(void)
{
    /* Menu music is now handled by audio_play_music() */
    audio_play_music(MUSIC_STARTSCREEN, NULL);
}

/* Helper: Load intro sprites and sound on demand */
static void load_intro_assets(void)
{
    /* Load intro slideshow sprites (with reuse for identical paths) */
    for (int i = 0; i < INTRO_SEQUENCE_LENGTH; i++)
    {
        if (s_pIntroSlides[i].type == SLIDE_TYPE_TEXT)
        {
            /* Text slides don't need sprites */
            continue;
        }

        const char *spritePath = s_pIntroSlides[i].content;

        /* Check if we already loaded this sprite (for reuse) */
        sprite_t *existingSprite = NULL;
        for (int j = 0; j < i; j++)
        {
            if (s_pIntroSlides[j].type != SLIDE_TYPE_TEXT && strcmp(s_pIntroSlides[j].content, spritePath) == 0)
            {
                existingSprite = s_pLoadedSprites[j];
                break;
            }
        }

        /* Load sprite or reuse existing one */
        if (existingSprite)
        {
            s_pLoadedSprites[i] = existingSprite;
        }
        else
        {
            s_pLoadedSprites[i] = sprite_load(spritePath);
        }
    }

    /* Load and play intro audio */
    if (!s_pIntroAudio)
    {
        s_pIntroAudio = wav64_load("rom:/intro_audio.wav64", &(wav64_loadparms_t){.streaming_mode = WAV64_STREAMING_FULL});
        if (s_pIntroAudio)
        {
            wav64_set_loop(s_pIntroAudio, false);
            wav64_play(s_pIntroAudio, MIXER_CHANNEL_MUSIC);
        }
    }
}

/* Helper: Unload intro sprites and sound */
static void unload_intro_assets(void)
{
    /* Free all loaded sprites (handle shared sprites carefully) */
    for (int i = 0; i < INTRO_SEQUENCE_LENGTH; i++)
    {
        if (s_pLoadedSprites[i])
        {
            /* Check if this sprite is shared with other slides */
            bool bIsShared = false;
            for (int j = 0; j < INTRO_SEQUENCE_LENGTH; j++)
            {
                if (i != j && s_pLoadedSprites[j] == s_pLoadedSprites[i])
                {
                    bIsShared = true;
                    break;
                }
            }

            /* Only free if not shared */
            if (!bIsShared)
            {
                sprite_free(s_pLoadedSprites[i]);
            }
            s_pLoadedSprites[i] = NULL;
        }
    }

    /* Free intro audio */
    SAFE_CLOSE_WAV64(s_pIntroAudio);
}

/* Helper: Reset button states for navigation */
static void reset_nav_button_states(void)
{
    s_bPrevUp = false;
    s_bPrevDown = false;
    s_bPrevLeft = false;
    s_bPrevRight = false;
}

/* Helper: Handle vertical navigation and return direction (-1 up, 1 down, 0 none) */
static int handle_vertical_nav(const joypad_inputs_t *_pInputs)
{
    int8_t iStickY = stick_normalizer_get_y();
    bool bUpHeld = _pInputs->btn.d_up || _pInputs->btn.c_up || iStickY > STICK_DEADZONE_MENU;
    bool bDownHeld = _pInputs->btn.d_down || _pInputs->btn.c_down || iStickY < -STICK_DEADZONE_MENU;
    bool bUpPressed = bUpHeld && !s_bPrevUp;
    bool bDownPressed = bDownHeld && !s_bPrevDown;

    s_bPrevUp = bUpHeld;
    s_bPrevDown = bDownHeld;

    if (bUpPressed)
        return -1;
    if (bDownPressed)
        return 1;
    return 0;
}

/* Helper: Handle vertical menu navigation with wrapping */
static void handle_menu_navigation(const joypad_inputs_t *_pInputs, int *_piSelection, int _iItemCount)
{
    int iNavDir = handle_vertical_nav(_pInputs);
    if (iNavDir != 0)
    {
        wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
        *_piSelection += iNavDir;
        if (*_piSelection < 0)
            *_piSelection = _iItemCount - 1;
        else if (*_piSelection >= _iItemCount)
            *_piSelection = 0;
    }
}

/* Helper: Handle horizontal navigation edge detection, return direction (-1 left, 1 right, 0 none) */
static int handle_horizontal_nav_edge(const joypad_inputs_t *_pInputs)
{
    int8_t iStickX = stick_normalizer_get_x();
    bool bLeftHeld = _pInputs->btn.d_left || _pInputs->btn.c_left || iStickX < -STICK_DEADZONE_MENU;
    bool bRightHeld = _pInputs->btn.d_right || _pInputs->btn.c_right || iStickX > STICK_DEADZONE_MENU;
    bool bLeftPressed = bLeftHeld && !s_bPrevLeft;
    bool bRightPressed = bRightHeld && !s_bPrevRight;

    s_bPrevLeft = bLeftHeld;
    s_bPrevRight = bRightHeld;

    if (bLeftPressed)
        return -1;
    if (bRightPressed)
        return 1;
    return 0;
}

/* Helper: Handle numeric value adjustment with clamping and repeat delay */
static bool handle_numeric_adjustment(const joypad_inputs_t *_pInputs, int *_piValue, int _iMin, int _iMax, int _iStep, int _iRepeatDelay)
{
    int iHorizNav = get_horizontal_nav(_pInputs);
    bool bHorizHeld = iHorizNav != 0;
    int iHorizEdge = handle_horizontal_nav_edge(_pInputs);
    bool bChanged = false;

    if (iHorizEdge != 0)
    {
        /* Change on edge press */
        *_piValue += iHorizEdge * _iStep;
        if (*_piValue < _iMin)
            *_piValue = _iMin;
        else if (*_piValue > _iMax)
            *_piValue = _iMax;
        bChanged = true;
        s_uOverscanRepeatTimer = 0;
    }
    else if (bHorizHeld)
    {
        /* Cycle continuously when holding (with repeat delay) */
        s_uOverscanRepeatTimer++;
        if (s_uOverscanRepeatTimer >= _iRepeatDelay)
        {
            *_piValue += iHorizNav * _iStep;
            if (*_piValue < _iMin)
                *_piValue = _iMin;
            else if (*_piValue > _iMax)
                *_piValue = _iMax;
            bChanged = true;
            s_uOverscanRepeatTimer = 0;
        }
    }
    else
    {
        s_uOverscanRepeatTimer = 0;
    }

    return bChanged;
}

/* Helper: Handle simple button-based screen transition (for start screen and controls screen) */
static bool handle_simple_screen_transition(const joypad_inputs_t *_pInputs, eMenuState _eNextState)
{
    if (button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.b, &s_bPrevB) || button_pressed(_pInputs->btn.z, &s_bPrevZ) ||
        button_pressed(_pInputs->btn.start, &s_bPrevStart))
    {
        wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
        s_eMenuState = _eNextState;
        return true;
    }
    return false;
}

/* Update start screen */
static void update_start_screen(const joypad_inputs_t *_pInputs)
{
    /* Update flash timer */
    s_uFlashTimer = get_ticks_ms();

    /* Check for START button press */
    if (button_pressed(_pInputs->btn.start, &s_bPrevStart))
    {
        wav64_play(s_pSoundStartScreen, MIXER_CHANNEL_USER_INTERFACE);
        s_eMenuState = MENU_STATE_MAIN_MENU;
        s_iMainMenuSelection = 0;
        reset_nav_button_states();
    }
}

/* Helper: Render fullscreen sprite centered on screen with overscan scaling */
static void render_sprite_fullscreen(sprite_t *_pSprite)
{
    if (_pSprite)
    {
        struct vec2i vSafeSize = ui_get_safe_area_size();
        float fScaleW = (float)vSafeSize.iX / (float)_pSprite->width;
        float fScaleH = (float)vSafeSize.iY / (float)_pSprite->height;
        float fScale = (fScaleW < fScaleH) ? fScaleW : fScaleH;

        int iScaledW = (int)(_pSprite->width * fScale);
        int iScaledH = (int)(_pSprite->height * fScale);
        int iSpriteX = (SCREEN_W - iScaledW) / 2;
        int iSpriteY = (SCREEN_H - iScaledH) / 2;

        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_sprite_blit(_pSprite, iSpriteX, iSpriteY, &(rdpq_blitparms_t){.scale_x = fScale, .scale_y = fScale});
    }
}

/* Render start screen */
static void render_start_screen(void)
{
    render_sprite_fullscreen(s_pStartScreenSprite);

    /* Render flashing "PUSH START" text */
    bool bShowText = (s_uFlashTimer / FLASH_INTERVAL_MS) % 2 == 0;
    if (bShowText)
    {
        int iY = SCREEN_H / 2 + MENU_FLASH_TEXT_Y_OFFSET;
        rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, iY, "PUSH START");
    }
}

/* Update main menu */
static void update_main_menu(const joypad_inputs_t *_pInputs)
{
    /* Vertical navigation */
    handle_menu_navigation(_pInputs, &s_iMainMenuSelection, MAIN_MENU_COUNT);

    /* Confirm (A or Z) */
    if (button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.z, &s_bPrevZ))
    {
        wav64_play(s_pSoundConfirm, MIXER_CHANNEL_USER_INTERFACE);
        switch (s_iMainMenuSelection)
        {
        case MAIN_MENU_NEW_GAME:
            mixer_ch_stop(MIXER_CHANNEL_MUSIC);

            if (s_bProgressExists)
            {
                /* CONTINUE: fade menu to black before resuming game (skip intro) */
                s_bFadeToBlackForContinue = true;
                fade_manager_start(TO_BLACK);
                s_eMenuState = MENU_STATE_MAIN_MENU_FADE_TO_BLACK;
            }
            else
            {
                /* NEW GAME: fade to black before intro */
                s_bFadeToBlackForContinue = false;
                fade_manager_start(TO_BLACK);
                s_eMenuState = MENU_STATE_MAIN_MENU_FADE_TO_BLACK;
            }

            break;
        case MAIN_MENU_SETTINGS:
            s_iSettingsMenuSelection = 0;
            reset_nav_button_states();
            s_eMenuState = MENU_STATE_SETTINGS;
            break;
        case MAIN_MENU_CONTROLS:
            /* Load newsletter sprite on demand */
            if (!s_pControlsScreenSprite)
            {
                s_pControlsScreenSprite = sprite_load("rom:/qr_screen_00.sprite");
            }
            reset_nav_button_states();
            s_eMenuState = MENU_STATE_NEWSLETTER;
            break;
        case MAIN_MENU_CREDITS:
            /* Load credits sprite on demand */
            if (!s_pCreditsScreenSprite)
            {
                s_pCreditsScreenSprite = sprite_load("rom:/credits_screen_00.sprite");
            }
            /* Reset credits scroll when entering */
            credits_reset();
            reset_nav_button_states();
            s_eMenuState = MENU_STATE_CREDITS;
            break;
        }
    }

    /* Back (B) */
    if (button_pressed(_pInputs->btn.b, &s_bPrevB))
    {
        wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
        s_eMenuState = MENU_STATE_START_SCREEN;
    }
}

/* Render main menu */
static void render_main_menu(void)
{
    render_sprite_fullscreen(s_pStartScreenSprite);

    /* Render menu items */
    int iStartY = SCREEN_H / 2 + MENU_START_Y_OFFSET;

    /* NEW GAME or CONTINUE */
    const char *pNewGameText = s_bProgressExists ? "CONTINUE" : "NEW GAME";
    render_menu_text(0, iStartY, pNewGameText, s_iMainMenuSelection == MAIN_MENU_NEW_GAME);

    /* SETTINGS */
    render_menu_text(0, iStartY + MENU_ITEM_SPACING, "SETTINGS", s_iMainMenuSelection == MAIN_MENU_SETTINGS);

    /* NEWSLETTER */
    render_menu_text(0, iStartY + MENU_ITEM_SPACING * 2, "NEWSLETTER", s_iMainMenuSelection == MAIN_MENU_CONTROLS);

    /* CREDITS */
    render_menu_text(0, iStartY + MENU_ITEM_SPACING * 3, "CREDITS", s_iMainMenuSelection == MAIN_MENU_CREDITS);
}

/* Helper: Get next valid settings menu item, skipping hidden items */
static int get_next_valid_settings_item(int _iCurrent, int _iDirection, bool _bIsPal, bool _bIsPause)
{
    int iNext = _iCurrent + _iDirection;

    /* Wrap around */
    if (iNext < 0)
    {
        iNext = SETTINGS_MENU_COUNT - 1;
    }
    else if (iNext >= SETTINGS_MENU_COUNT)
    {
        iNext = 0;
    }

    /* Skip PAL60 if not PAL system */
    if (!_bIsPal && iNext == SETTINGS_PAL60)
    {
        iNext += _iDirection;
        if (iNext < 0)
            iNext = SETTINGS_MENU_COUNT - 1;
        else if (iNext >= SETTINGS_MENU_COUNT)
            iNext = 0;
    }

    /* Skip DELETE SAVE if in pause mode */
    if (_bIsPause && iNext == SETTINGS_DELETE_SAVE)
    {
        iNext += _iDirection;
        if (iNext < 0)
            iNext = SETTINGS_MENU_COUNT - 1;
        else if (iNext >= SETTINGS_MENU_COUNT)
            iNext = 0;
    }

    return iNext;
}

/* Update settings menu */
static void update_settings_menu(const joypad_inputs_t *_pInputs)
{
    bool bIsPal = (get_tv_type() == TV_PAL);
    bool bIsPause = (s_eMenuState == MENU_STATE_PAUSE_SETTINGS);

    /* Handle navigation - skip hidden items */
    int iNavDir = handle_vertical_nav(_pInputs);
    if (iNavDir != 0)
    {
        wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
        s_iSettingsMenuSelection = get_next_valid_settings_item(s_iSettingsMenuSelection, iNavDir, bIsPal, bIsPause);
    }

    /* Ensure current selection is valid */
    if (!bIsPal && s_iSettingsMenuSelection == SETTINGS_PAL60)
    {
        s_iSettingsMenuSelection = SETTINGS_MUSIC_VOLUME;
    }
    if (bIsPause && s_iSettingsMenuSelection == SETTINGS_DELETE_SAVE)
    {
        s_iSettingsMenuSelection = SETTINGS_SFX_VOLUME;
    }

    /* Handle item-specific input */
    switch (s_iSettingsMenuSelection)
    {
    case SETTINGS_TARGET_LOCK:
        /* Toggle with A/Z or left/right - only on edge press */
        if (button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.z, &s_bPrevZ) || handle_horizontal_nav_edge(_pInputs) != 0)
        {
            wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
            bool bNewValue = !save_get_target_lock_toggle_mode();
            save_set_target_lock_toggle_mode(bNewValue);
        }
        break;

    case SETTINGS_OVERSCAN:
    {
        int iOverscanValue = save_get_overscan_padding();
        bool bChanged = false;

        if (button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.z, &s_bPrevZ))
        {
            /* Increment on A/Z press */
            iOverscanValue++;
            if (iOverscanValue > 20)
                iOverscanValue = 0;
            bChanged = true;
            s_uOverscanRepeatTimer = 0;
        }
        else
        {
            bChanged = handle_numeric_adjustment(_pInputs, &iOverscanValue, 0, 20, 1, ITEM_CHANGE_DELAY_NORMAL);
        }

        if (bChanged)
        {
            wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
            save_set_overscan_padding(iOverscanValue);
            ui_set_overscan_padding(iOverscanValue);
        }
        break;
    }

    case SETTINGS_PAL60:
        /* Only allow on PAL systems */
        if (get_tv_type() == TV_PAL)
        {
            /* Toggle with A/Z or left/right - only on edge press */
            if (button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.z, &s_bPrevZ) || handle_horizontal_nav_edge(_pInputs) != 0)
            {
                bool bCurrentValue = save_get_pal60_enabled();
                if (!bCurrentValue)
                {
                    /* Turning ON - activate PAL60 and enter confirmation */
                    wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
                    tv_activate_pal60();
                    s_fPal60ConfirmTimer = 0.0f;
                    s_bPal60ConfirmWaiting = true;
                    /* Remember which settings menu we came from */
                    s_ePal60ConfirmPreviousState = s_eMenuState;
                    reset_nav_button_states();
                    s_eMenuState = MENU_STATE_PAL60_CONFIRM;
                }
                else
                {
                    /* Turning OFF - revert to PAL immediately */
                    wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
                    tv_revert_to_pal50();
                    save_set_pal60_enabled(false);
                    /* Only sync gp_state if we're in pause menu (game is running) */
                    if (bIsPause)
                    {
                        save_sync_gp_state();
                    }
                    save_write();
                }
            }
        }
        break;

    case SETTINGS_MUSIC_VOLUME:
    {
        int iMusicVolume = save_get_music_volume();
        if (handle_numeric_adjustment(_pInputs, &iMusicVolume, 0, 100, 1, ITEM_CHANGE_DELAY_QUICK))
        {
            save_set_music_volume(iMusicVolume);
            audio_refresh_volumes();
            wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
        }
        break;
    }

    case SETTINGS_SFX_VOLUME:
    {
        int iSfxVolume = save_get_sfx_volume();
        if (handle_numeric_adjustment(_pInputs, &iSfxVolume, 0, 100, 1, ITEM_CHANGE_DELAY_QUICK))
        {
            save_set_sfx_volume(iSfxVolume);
            audio_refresh_volumes();
            wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
        }
        break;
    }

    case SETTINGS_CALIBRATION:
        /* Enter calibration screen */
        if (button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.z, &s_bPrevZ))
        {
            wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
            s_eCalibrationPreviousState = s_eMenuState;
            stick_calibration_init();
            reset_nav_button_states();
            s_eMenuState = MENU_STATE_CALIBRATION;
        }
        break;

    case SETTINGS_DELETE_SAVE:
        /* Confirm delete with A/Z */
        if (button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.z, &s_bPrevZ))
        {
            wav64_play(s_pSoundConfirm, MIXER_CHANNEL_USER_INTERFACE);
            s_bDeleteConfirmSelection = false; /* Default to NO */
            reset_nav_button_states();
            mixer_ch_stop(MIXER_CHANNEL_MUSIC);
            s_eMenuState = MENU_STATE_DELETE_CONFIRM;
        }
        break;
    }

    /* Back (B) */
    if (button_pressed(_pInputs->btn.b, &s_bPrevB))
    {
        wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
        /* Settings are saved immediately when changed, just write to persist */
        /* Only sync gp_state if we're in pause menu (game is running) */
        if (bIsPause)
        {
            save_sync_gp_state();
        }
        save_write();
        /* Return to pause menu if we came from pause, otherwise return to main menu */
        if (s_eMenuState == MENU_STATE_PAUSE_SETTINGS)
        {
            s_eMenuState = MENU_STATE_PAUSE;
        }
        else
        {
            s_eMenuState = MENU_STATE_MAIN_MENU;
        }
    }
}

/* Render settings menu */
static void render_settings_menu(void)
{
    /* Only render startscreen sprite if not in pause mode */
    if (s_eMenuState == MENU_STATE_SETTINGS)
    {
        render_sprite_fullscreen(s_pStartScreenSprite);
    }

    /* Draw transparent darkening overlay */
    ui_draw_darkening_overlay();

    /* Render menu items */
    bool bIsPal = (get_tv_type() == TV_PAL);
    /* Move up by one line if PAL60 is shown */
    int iStartY = SCREEN_H / 2 + MENU_SETTINGS_Y_OFFSET - (bIsPal ? MENU_ITEM_SPACING : 0);
    int iItemIndex = 0;

    /* TARGET LOCK */
    const char *pTargetLockValue = save_get_target_lock_toggle_mode() ? "TOGGLE" : "HOLD";
    char szTargetLock[64];
    snprintf(szTargetLock, sizeof(szTargetLock), "TARGET LOCK: %s", pTargetLockValue);
    render_menu_text(0, iStartY + MENU_ITEM_SPACING * iItemIndex, szTargetLock, s_iSettingsMenuSelection == SETTINGS_TARGET_LOCK);
    iItemIndex++;

    /* OVERSCAN */
    char szOverscan[64];
    snprintf(szOverscan, sizeof(szOverscan), "OVERSCAN: %d", save_get_overscan_padding());
    render_menu_text(0, iStartY + MENU_ITEM_SPACING * iItemIndex, szOverscan, s_iSettingsMenuSelection == SETTINGS_OVERSCAN);
    iItemIndex++;

    /* PAL60 - only show if PAL system */
    if (bIsPal)
    {
        const char *pPal60Value = save_get_pal60_enabled() ? "ON" : "OFF";
        char szPal60[64];
        snprintf(szPal60, sizeof(szPal60), "PAL60: %s", pPal60Value);
        render_menu_text(0, iStartY + MENU_ITEM_SPACING * iItemIndex, szPal60, s_iSettingsMenuSelection == SETTINGS_PAL60);
        iItemIndex++;
    }

    /* MUSIC VOLUME */
    char szMusicVolume[64];
    snprintf(szMusicVolume, sizeof(szMusicVolume), "MUSIC VOLUME: %d", save_get_music_volume());
    render_menu_text(0, iStartY + MENU_ITEM_SPACING * iItemIndex, szMusicVolume, s_iSettingsMenuSelection == SETTINGS_MUSIC_VOLUME);
    iItemIndex++;

    /* SFX VOLUME */
    char szSfxVolume[64];
    snprintf(szSfxVolume, sizeof(szSfxVolume), "SFX VOLUME: %d", save_get_sfx_volume());
    render_menu_text(0, iStartY + MENU_ITEM_SPACING * iItemIndex, szSfxVolume, s_iSettingsMenuSelection == SETTINGS_SFX_VOLUME);
    iItemIndex++;

    /* CALIBRATION */
    render_menu_text(0, iStartY + MENU_ITEM_SPACING * iItemIndex, "CALIBRATION", s_iSettingsMenuSelection == SETTINGS_CALIBRATION);
    iItemIndex++;

    /* DELETE SAVE - only show if not in pause mode */
    if (s_eMenuState != MENU_STATE_PAUSE_SETTINGS)
    {
        render_menu_text(0, iStartY + MENU_ITEM_SPACING * iItemIndex, "DELETE SAVE", s_iSettingsMenuSelection == SETTINGS_DELETE_SAVE);
    }
}

/* Update delete confirmation */
static void update_delete_confirm(const joypad_inputs_t *_pInputs)
{
    /* Vertical navigation */
    int iNavDir = handle_vertical_nav(_pInputs);
    if (iNavDir != 0)
    {
        wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
        s_bDeleteConfirmSelection = !s_bDeleteConfirmSelection;
    }

    /* Confirm (A or Z) */
    if (button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.z, &s_bPrevZ))
    {
        if (s_bDeleteConfirmSelection)
        {
            /* YES - wipe save */
            wav64_play(s_pSoundConfirm, MIXER_CHANNEL_USER_INTERFACE);
            save_wipe();
            /* Reset PAL60 to default (disabled) - revert display mode if on PAL system */
            if (get_tv_type() == TV_PAL)
            {
                tv_revert_to_pal50();
            }
            menu_init(); /* Re-initialize menu to reset all state */
            // happens in init anyway: s_eMenuState = MENU_STATE_START_SCREEN;
        }
        else
        {
            /* NO - return to settings */
            wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
            start_menu_music();
            s_eMenuState = MENU_STATE_SETTINGS;
        }
    }

    /* Back (B) */
    if (button_pressed(_pInputs->btn.b, &s_bPrevB))
    {
        wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
        start_menu_music();
        s_eMenuState = MENU_STATE_SETTINGS;
    }
}

/* Update PAL60 confirmation */
static void update_pal60_confirm(const joypad_inputs_t *_pInputs)
{
    /* Update timer */
    s_fPal60ConfirmTimer += frame_time_delta_seconds();

    /* Check for L button press */
    static bool s_bPrevL = false;
    if (_pInputs->btn.l && !s_bPrevL)
    {
        /* User confirmed - save setting and return to previous settings menu */
        wav64_play(s_pSoundConfirm, MIXER_CHANNEL_USER_INTERFACE);
        save_set_pal60_enabled(true);
        /* Only sync gp_state if we came from pause menu (game is running) */
        if (s_ePal60ConfirmPreviousState == MENU_STATE_PAUSE_SETTINGS)
        {
            save_sync_gp_state();
        }
        save_write();
        s_bPal60ConfirmWaiting = false;
        s_fPal60ConfirmTimer = 0.0f;
        reset_nav_button_states();
        /* Return to the menu state we came from (pause settings or main settings) */
        s_eMenuState = s_ePal60ConfirmPreviousState;
    }
    s_bPrevL = _pInputs->btn.l;

    /* Check timeout (3 seconds) */
    if (s_fPal60ConfirmTimer >= 3.0f)
    {
        /* Timeout - revert to PAL and disable setting */
        wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
        tv_revert_to_pal50();
        save_set_pal60_enabled(false);
        /* Only sync gp_state if we came from pause menu (game is running) */
        if (s_ePal60ConfirmPreviousState == MENU_STATE_PAUSE_SETTINGS)
        {
            save_sync_gp_state();
        }
        save_write();
        s_bPal60ConfirmWaiting = false;
        s_fPal60ConfirmTimer = 0.0f;
        reset_nav_button_states();
        /* Return to the menu state we came from (pause settings or main settings) */
        s_eMenuState = s_ePal60ConfirmPreviousState;
    }
}

/* Render PAL60 confirmation */
static void render_pal60_confirm(void)
{
    /* Black background */
    rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
    rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);

    /* Message text */
    int iY = SCREEN_H / 2 - MENU_ITEM_SPACING;
    rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, iY, "PAL60 ACTIVE");
    rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, iY + MENU_ITEM_SPACING, "PRESS L TO CONFIRM");

    /* Show remaining time */
    float fRemainingTime = 3.0f - s_fPal60ConfirmTimer;
    if (fRemainingTime > 0.0f)
    {
        rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, iY + MENU_ITEM_SPACING * 3, "%.1f", fRemainingTime);
    }
}

/* Render delete confirmation */
static void render_delete_confirm(void)
{
    /* Black background */
    rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
    rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);

    /* Question text - above */
    rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, SCREEN_H / 2.0f + MENU_DELETE_QUESTION_Y_OFFSET, "DELETE ALL SAVE DATA?");

    /* NO and YES options - below question, vertically stacked */
    int iStartY = SCREEN_H / 2 + MENU_DELETE_OPTIONS_Y_OFFSET;
    render_menu_text(0, iStartY, "NO", !s_bDeleteConfirmSelection);
    render_menu_text(0, iStartY + MENU_ITEM_SPACING, "YES", s_bDeleteConfirmSelection);
}

/* Update calibration screen */
static void update_calibration(const joypad_inputs_t *_pInputs)
{
    stick_calibration_update(_pInputs);

    /* Only close on START (not B) - Return to settings */
    if (button_pressed(_pInputs->btn.start, &s_bPrevStart))
    {
        wav64_play(s_pSoundConfirm, MIXER_CHANNEL_USER_INTERFACE);
        stick_calibration_close();
        s_eMenuState = s_eCalibrationPreviousState;
    }
}

/* Update upgrade shop screen */
static void update_upgrade_shop(const joypad_inputs_t *_pInputs)
{
    eUpgradeShopResult eResult = upgrade_shop_update(_pInputs->btn.c_down);
    if (eResult == UPGRADE_SHOP_RESULT_EXIT)
    {
        s_eMenuResult = MENU_RESULT_EXIT;
    }
}

/* Render calibration screen */
static void render_calibration(void)
{
    stick_calibration_render();
}

/* Render upgrade shop screen */
static void render_upgrade_shop(void)
{
    upgrade_shop_render();
}

/* Update controls screen */
static void update_controls(const joypad_inputs_t *_pInputs)
{
    /* Any button returns to main menu */
    if (handle_simple_screen_transition(_pInputs, MENU_STATE_MAIN_MENU))
    {
        /* Unload controls sprite when leaving */
        SAFE_FREE_SPRITE(s_pControlsScreenSprite);
    }
}

/* Render controls screen */
static void render_controls(void)
{
    render_sprite_fullscreen(s_pControlsScreenSprite);
}

/* Update credits screen */
static void update_credits(const joypad_inputs_t *_pInputs)
{
    /* Update credits scroll */
    credits_update(_pInputs, true);

    /* Any button returns to main menu */
    if (handle_simple_screen_transition(_pInputs, MENU_STATE_MAIN_MENU))
    {
        /* Unload credits sprite when leaving */
        SAFE_FREE_SPRITE(s_pCreditsScreenSprite);
    }
}

/* Render credits screen */
static void render_credits(void)
{
    render_sprite_fullscreen(s_pCreditsScreenSprite);

    int iStartY = SCREEN_H / 2 + MENU_CREDITS_Y_OFFSET;

    /* Use shared credits rendering */
    credits_render(iStartY);
}

/* Render intro slideshow */
static void render_intro(void)
{
    /* Handle different slide types */
    switch (s_pIntroSlides[s_iIntroCurrentSlide].type)
    {
    case SLIDE_TYPE_TEXT:
    {
        /* Clear screen to black for text slides */
        rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
        rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);

        /* Render centered text */
        rdpq_text_printf(&m_tpCenterBoth, FONT_NORMAL, 0, 0, "%s", s_pIntroSlides[s_iIntroCurrentSlide].content);
        break;
    }

    case SLIDE_TYPE_SINGLE_SPRITE:
    {
        /* Render sprite centered and scaled to fit screen */
        render_sprite_fullscreen(s_pLoadedSprites[s_iIntroCurrentSlide]);
        break;
    }

    case SLIDE_TYPE_MULTIPANEL:
    {
        /* Render multi-panel sprite */
        render_sprite_fullscreen(s_pLoadedSprites[s_iIntroCurrentSlide]);

        /* Special handling for intro_07_00.png - render black rectangles over unrevealed quadrants */
        rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));

        /* Screen quadrants (top-left, top-right, bottom-left, bottom-right) */
        int iHalfWidth = SCREEN_W / 2;
        int iHalfHeight = SCREEN_H / 2;

        // /* Panel 0: Top-left quadrant - revealed when s_iIntroPanelReveal >= 0 (always shown after first reveal) */
        // if (s_iIntroPanelReveal < 1)
        // {
        //     rdpq_fill_rectangle(0, 0, iHalfWidth, iHalfHeight);
        // }

        /* Panel 1: Top-right quadrant - revealed when s_iIntroPanelReveal >= 1 */
        if (s_iIntroPanelReveal < 1)
        {
            rdpq_fill_rectangle(iHalfWidth, 0, SCREEN_W, iHalfHeight);
        }

        /* Panel 2: Bottom-left quadrant - revealed when s_iIntroPanelReveal >= 2 */
        if (s_iIntroPanelReveal < 2)
        {
            rdpq_fill_rectangle(0, iHalfHeight, iHalfWidth, SCREEN_H);
        }

        /* Panel 3: Bottom-right quadrant - revealed when s_iIntroPanelReveal >= 3 */
        if (s_iIntroPanelReveal < 3)
        {
            rdpq_fill_rectangle(iHalfWidth, iHalfHeight, SCREEN_W, SCREEN_H);
        }
        break;
    }
    }

    /* Handle fade effects */
    eIntroFadeType fadeType = s_pIntroSlides[s_iIntroCurrentSlide].fadeType;
    if (fadeType != INTRO_NO_FADE)
    {
        /* Calculate total slide duration (sum of all panels for multipanel slides) */
        float totalSlideDuration = 0.0f;
        if (s_pIntroSlides[s_iIntroCurrentSlide].type == SLIDE_TYPE_MULTIPANEL)
        {
            /* Sum all panel durations */
            for (int i = 0; i < INTRO_MULTIPANEL_COUNT; i++)
            {
                totalSlideDuration += s_pIntroSlides[s_iIntroCurrentSlide].duration.panelDurations[i];
            }
        }
        else
        {
            totalSlideDuration = s_pIntroSlides[s_iIntroCurrentSlide].duration.singleDuration;
        }

        /* Calculate total elapsed time for this slide (accounts for multipanel progression) */
        float totalElapsedTime = s_fIntroTimer;
        if (s_pIntroSlides[s_iIntroCurrentSlide].type == SLIDE_TYPE_MULTIPANEL)
        {
            /* Add durations of completed panels */
            for (int i = 0; i < s_iIntroPanelReveal; i++)
            {
                totalElapsedTime += s_pIntroSlides[s_iIntroCurrentSlide].duration.panelDurations[i];
            }
        }

        float alpha = 0.0f;

        switch (fadeType)
        {
        case INTRO_FADE_IN:
            /* Fade from black to transparent over first 0.5s of total slide */
            if (totalElapsedTime < FADE_TIME)
            {
                alpha = 255.0f * (1.0f - (totalElapsedTime / FADE_TIME));
            }
            break;

        case INTRO_FADE_OUT:
            /* Fade from transparent to black over last 0.5s of total slide */
            if (totalElapsedTime >= (totalSlideDuration - FADE_TIME))
            {
                float fadeProgress = (totalElapsedTime - (totalSlideDuration - FADE_TIME)) / FADE_TIME;
                alpha = 255.0f * fadeProgress;
            }
            break;

        case INTRO_FADE_IN_OUT:
            /* Fade in over first 0.5s, fade out over last 0.5s of total slide */
            if (totalElapsedTime < FADE_TIME)
            {
                /* Fade in */
                alpha = 255.0f * (1.0f - (totalElapsedTime / FADE_TIME));
            }
            else if (totalElapsedTime >= (totalSlideDuration - FADE_TIME))
            {
                /* Fade out */
                float fadeProgress = (totalElapsedTime - (totalSlideDuration - FADE_TIME)) / FADE_TIME;
                alpha = 255.0f * fadeProgress;
            }
            break;

        case INTRO_NO_FADE:
        default:
            /* No fade effect */
            break;
        }

        /* Draw fade overlay if alpha > 0 */
        if (alpha > 0.0f)
        {
            ui_draw_darkening_overlay_alpha((uint8_t)alpha);
        }
    }
}

/* Update fade to black before intro or continue */
static void update_fade_to_black(const joypad_inputs_t *_pInputs)
{
    /* Check if fade is complete */
    if (!fade_manager_is_busy())
    {
        fade_manager_stop();

        if (s_bFadeToBlackForContinue)
        {
            /* CONTINUE: skip intro and go directly to game */
            s_eMenuResult = MENU_RESULT_CONTINUE_GAME;
            s_eMenuState = MENU_STATE_TRANSITION_OUT;
        }
        else
        {
            /* NEW GAME: unload startscreen sprite and load intro */
            SAFE_FREE_SPRITE(s_pStartScreenSprite);
            /* Load intro sprites on demand */
            load_intro_assets();
            s_fIntroTimer = 0.0f;
            s_iIntroCurrentSlide = 0;
            s_iIntroPanelReveal = 0;
            s_eMenuState = MENU_STATE_MAIN_MENU_INTRO;
        }
    }
}

/* Render fade to black before intro */
static void render_fade_to_black(void)
{
    /* Render main menu in background */
    render_main_menu();
}

/* Update main menu intro slideshow */
static void update_intro(const joypad_inputs_t *_pInputs)
{
    /* Skip one slide with START, A, B, or Z buttons */
    if (button_pressed(_pInputs->btn.start, &s_bPrevStart) || button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.b, &s_bPrevB) ||
        button_pressed(_pInputs->btn.z, &s_bPrevZ))
    {
        /* Skip to next slide/panel */
        if (s_pIntroSlides[s_iIntroCurrentSlide].type == SLIDE_TYPE_MULTIPANEL)
        {
            s_fIntroTimer = s_pIntroSlides[s_iIntroCurrentSlide].duration.panelDurations[s_iIntroPanelReveal];
        }
        else
        {
            s_fIntroTimer = s_pIntroSlides[s_iIntroCurrentSlide].duration.singleDuration;
        }

        /* Seek audio to match the skip */
        if (s_pIntroAudio && mixer_ch_playing(MIXER_CHANNEL_MUSIC))
        {
            /* Calculate cumulative time up to current position */
            float fCumulativeTime = 0.0f;

            /* Add durations of all completed slides */
            for (int i = 0; i < s_iIntroCurrentSlide; i++)
            {
                if (s_pIntroSlides[i].type == SLIDE_TYPE_MULTIPANEL)
                {
                    /* Sum all panel durations for multipanel slides */
                    for (int j = 0; j < INTRO_MULTIPANEL_COUNT; j++)
                    {
                        fCumulativeTime += s_pIntroSlides[i].duration.panelDurations[j];
                    }
                }
                else
                {
                    fCumulativeTime += s_pIntroSlides[i].duration.singleDuration;
                }
            }

            /* Add durations of completed panels in current slide (if multipanel) */
            if (s_pIntroSlides[s_iIntroCurrentSlide].type == SLIDE_TYPE_MULTIPANEL)
            {
                for (int j = 0; j < s_iIntroPanelReveal; j++)
                {
                    fCumulativeTime += s_pIntroSlides[s_iIntroCurrentSlide].duration.panelDurations[j];
                }
            }

            /* Add current timer progress (which was just set to skip to next slide/panel) */
            fCumulativeTime += s_fIntroTimer;

            /* Seek to the calculated timestamp */
            wav64_seek(s_pIntroAudio, MIXER_CHANNEL_MUSIC, fCumulativeTime);
        }
    }

    /* Update timer with delta time */
    s_fIntroTimer += frame_time_delta_seconds();

    /* Check if current slide/panel duration has elapsed */
    bool bAdvance = false;
    if (s_pIntroSlides[s_iIntroCurrentSlide].type == SLIDE_TYPE_MULTIPANEL)
    {
        /* Multi-panel slide uses per-slide panel timings */
        bAdvance = (s_fIntroTimer >= s_pIntroSlides[s_iIntroCurrentSlide].duration.panelDurations[s_iIntroPanelReveal]);
    }
    else
    {
        /* Other slides use slide timings */
        bAdvance = (s_fIntroTimer >= s_pIntroSlides[s_iIntroCurrentSlide].duration.singleDuration);
    }

    if (bAdvance)
    {
        /* Handle advancement */
        if (s_pIntroSlides[s_iIntroCurrentSlide].type == SLIDE_TYPE_MULTIPANEL && s_iIntroPanelReveal < (INTRO_MULTIPANEL_COUNT - 1))
        {
            /* Still revealing panels of multi-panel slide */
            s_iIntroPanelReveal++;
        }
        else
        {
            /* Advance to next slide */
            s_iIntroCurrentSlide++;
            s_iIntroPanelReveal = 0;
        }
        s_fIntroTimer = 0.0f;

        /* Check if slideshow is complete */
        if (s_iIntroCurrentSlide >= INTRO_SEQUENCE_LENGTH)
        {
            /* Stop intro audio */
            if (mixer_ch_playing(MIXER_CHANNEL_MUSIC))
            {
                mixer_ch_stop(MIXER_CHANNEL_MUSIC);
            }

            /* Unload intro sprites */
            unload_intro_assets();
            s_eMenuResult = MENU_RESULT_START_GAME;
            s_eMenuState = MENU_STATE_TRANSITION_OUT;
        }
    }
}

/* Update save confirmation */
static void update_save_confirm(const joypad_inputs_t *_pInputs)
{
    /* Vertical navigation */
    int iNavDir = handle_vertical_nav(_pInputs);
    if (iNavDir != 0)
    {
        wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
        s_bSaveConfirmSelection = !s_bSaveConfirmSelection;
    }

    /* Confirm (A or Z) */
    if (button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.z, &s_bPrevZ))
    {
        if (s_bSaveConfirmSelection)
        {
            /* YES - save game */
            wav64_play(s_pSoundConfirm, MIXER_CHANNEL_USER_INTERFACE);
            /* Sync all current game state to save data before saving */
            save_sync_settings(ui_get_overscan_padding(), save_get_target_lock_toggle_mode(), save_get_music_volume(), save_get_sfx_volume(), save_get_pal60_enabled());
            save_sync_gp_state();
            save_write();
            s_eMenuState = MENU_STATE_PAUSE;
        }
        else
        {
            /* NO - return to pause menu */
            wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
            s_eMenuState = MENU_STATE_PAUSE;
        }
    }

    /* Back (B) */
    if (button_pressed(_pInputs->btn.b, &s_bPrevB))
    {
        wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
        s_eMenuState = MENU_STATE_PAUSE;
    }
}

/* Render save confirmation */
static void render_save_confirm(void)
{
    /* Black background */
    rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
    rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);

    /* Question text - above */
    rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, SCREEN_H / 2.0f + MENU_DELETE_QUESTION_Y_OFFSET, "SAVE GAME?");

    /* NO and YES options - below question, vertically stacked */
    int iStartY = SCREEN_H / 2 + MENU_DELETE_OPTIONS_Y_OFFSET;
    render_menu_text(0, iStartY, "NO", !s_bSaveConfirmSelection);
    render_menu_text(0, iStartY + MENU_ITEM_SPACING, "YES", s_bSaveConfirmSelection);
}

/* Update exit race confirmation */
static void update_exit_race_confirm(const joypad_inputs_t *_pInputs)
{
    /* Vertical navigation */
    int iNavDir = handle_vertical_nav(_pInputs);
    if (iNavDir != 0)
    {
        wav64_play(s_pSoundSelect, MIXER_CHANNEL_USER_INTERFACE);
        s_bExitRaceConfirmSelection = !s_bExitRaceConfirmSelection;
    }

    /* Confirm (A or Z) */
    if (button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.z, &s_bPrevZ))
    {
        if (s_bExitRaceConfirmSelection)
        {
            /* YES - stop race and close screen (unpause) */
            wav64_play(s_pSoundConfirm, MIXER_CHANNEL_USER_INTERFACE);
            race_handler_abort_race();
            s_eMenuResult = MENU_RESULT_EXIT;
        }
        else
        {
            /* NO - return to game (unpause) */
            wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
            s_eMenuResult = MENU_RESULT_EXIT;
        }
    }

    /* Back (B) */
    if (button_pressed(_pInputs->btn.b, &s_bPrevB))
    {
        wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
        s_eMenuResult = MENU_RESULT_EXIT;
    }
}

/* Render exit race confirmation */
static void render_exit_race_confirm(void)
{
    /* Note: Game is rendered first, then this overlay is drawn on top */
    /* Draw transparent darkening overlay */
    ui_draw_darkening_overlay();

    /* Question text - above */
    rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, SCREEN_H / 2.0f + MENU_DELETE_QUESTION_Y_OFFSET, "EXIT RACE?");

    /* NO and YES options - below question, vertically stacked */
    int iStartY = SCREEN_H / 2 + MENU_DELETE_OPTIONS_Y_OFFSET;
    render_menu_text(0, iStartY, "NO", !s_bExitRaceConfirmSelection);
    render_menu_text(0, iStartY + MENU_ITEM_SPACING, "YES", s_bExitRaceConfirmSelection);
}

/* Update pause menu */
static void update_pause_menu(const joypad_inputs_t *_pInputs)
{
    /* Check for L button press to toggle debug mode */
    if (button_pressed(_pInputs->btn.l, &s_bPrevL))
    {
        debug_cheats_toggle();
    }

    /* If debug overlay is active, route inputs to cheats and skip pause menu navigation */
    if (debug_cheats_is_active())
    {
        debug_cheats_update(_pInputs);

        /* Back (B) or START - unpause */
        if (button_pressed(_pInputs->btn.b, &s_bPrevB) || button_pressed(_pInputs->btn.start, &s_bPrevStart))
        {
            wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
            s_eMenuResult = MENU_RESULT_EXIT;
        }

        return;
    }

    /* Vertical navigation */
    handle_menu_navigation(_pInputs, &s_iPauseMenuSelection, PAUSE_MENU_COUNT);

    /* Confirm (A or Z) */
    if (button_pressed(_pInputs->btn.a, &s_bPrevA) || button_pressed(_pInputs->btn.z, &s_bPrevZ))
    {
        wav64_play(s_pSoundConfirm, MIXER_CHANNEL_USER_INTERFACE);
        switch (s_iPauseMenuSelection)
        {
        case PAUSE_MENU_SETTINGS:
            s_iSettingsMenuSelection = 0;
            reset_nav_button_states();
            s_eMenuState = MENU_STATE_PAUSE_SETTINGS;
            break;
        case PAUSE_MENU_SAVE:
            s_bSaveConfirmSelection = false; /* Default to NO */
            reset_nav_button_states();
            s_eMenuState = MENU_STATE_PAUSE_SAVE_CONFIRM;
            break;
        case PAUSE_MENU_CLOSE:
            s_eMenuResult = MENU_RESULT_EXIT;
            break;
        }
    }

    /* Back (B) or START - unpause */
    if (button_pressed(_pInputs->btn.b, &s_bPrevB) || button_pressed(_pInputs->btn.start, &s_bPrevStart))
    {
        wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
        s_eMenuResult = MENU_RESULT_EXIT;
    }
}

/* Render pause menu */
static void render_pause_menu(void)
{
    /* Note: Game is rendered first, then this overlay is drawn on top */
    /* Draw transparent darkening overlay */
    ui_draw_darkening_overlay();

    /* If debug mode is active, render debug info instead of menu */
    if (debug_cheats_is_active())
    {
        debug_cheats_render();
        return;
    }

    /* Render satellite pieces UI */
    satellite_pieces_render_ui();

    /* Render menu items */
    int iStartY = SCREEN_H / 2 + MENU_START_Y_OFFSET;

    /* SETTINGS */
    render_menu_text(0, iStartY, "SETTINGS", s_iPauseMenuSelection == PAUSE_MENU_SETTINGS);

    /* SAVE */
    render_menu_text(0, iStartY + MENU_ITEM_SPACING, "SAVE", s_iPauseMenuSelection == PAUSE_MENU_SAVE);

    /* CLOSE */
    render_menu_text(0, iStartY + MENU_ITEM_SPACING * 2, "CLOSE", s_iPauseMenuSelection == PAUSE_MENU_CLOSE);
}

/* Public API */

void menu_init(void)
{
    bool bFirstInit = (s_pStartScreenSprite == NULL);

    /* Load assets only on first initialization */
    if (bFirstInit)
    {

        /* Load sprites */
        /* Note: startscreen, controls, and credits sprites are loaded on-demand */

        /* Load sound effects */
        s_pSoundSelect = wav64_load("rom:/btn_select.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
        s_pSoundConfirm = wav64_load("rom:/btn_confirm.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
        s_pSoundCancel = wav64_load("rom:/btn_cancel.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
        s_pSoundStartScreen = wav64_load("rom:/start_screen.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

        /* Load startscreen sprite */
        s_pStartScreenSprite = sprite_load("rom:/start_screen_00.sprite");
    }

    /* Reset state (always done, whether first init or re-init) */
    s_eMenuState = MENU_STATE_START_SCREEN;
    s_eMenuResult = MENU_RESULT_NONE;

    /* Initialize button states */
    s_bPrevStart = false;
    s_bPrevA = false;
    s_bPrevB = false;
    s_bPrevZ = false;
    s_bPrevL = false;
    s_bPrevUp = false;
    s_bPrevDown = false;
    s_bPrevLeft = false;
    s_bPrevRight = false;

    /* Initialize debug mode */
    debug_cheats_init();

    /* Initialize menu selections */
    s_iMainMenuSelection = 0;
    s_iSettingsMenuSelection = 0;
    s_iPauseMenuSelection = 0;
    s_bDeleteConfirmSelection = false;
    s_bSaveConfirmSelection = false;
    s_bExitRaceConfirmSelection = false;

    /* Initialize intro slideshow state */
    s_fIntroTimer = 0.0f;
    s_iIntroCurrentSlide = 0;
    s_iIntroPanelReveal = 0;

    /* Initialize PAL60 confirmation state */
    s_fPal60ConfirmTimer = 0.0f;
    s_bPal60ConfirmWaiting = false;
    s_ePal60ConfirmPreviousState = MENU_STATE_SETTINGS;

    /* Initialize save exists */
    s_bProgressExists = save_progress_exists();

    /* Start menu music */
    start_menu_music();
}

eMenuResult menu_update(const joypad_inputs_t *_pInputs)
{
    s_eMenuResult = MENU_RESULT_NONE;

    switch (s_eMenuState)
    {
    case MENU_STATE_START_SCREEN:
        update_start_screen(_pInputs);
        break;
    case MENU_STATE_MAIN_MENU:
        update_main_menu(_pInputs);
        break;
    case MENU_STATE_SETTINGS:
    case MENU_STATE_PAUSE_SETTINGS:
        update_settings_menu(_pInputs);
        break;
    case MENU_STATE_DELETE_CONFIRM:
        update_delete_confirm(_pInputs);
        break;
    case MENU_STATE_NEWSLETTER:
        update_controls(_pInputs);
        break;
    case MENU_STATE_CREDITS:
        update_credits(_pInputs);
        break;
    case MENU_STATE_MAIN_MENU_FADE_TO_BLACK:
        update_fade_to_black(_pInputs);
        break;
    case MENU_STATE_MAIN_MENU_INTRO:
        update_intro(_pInputs);
        break;
    case MENU_STATE_PAUSE:
        update_pause_menu(_pInputs);
        break;
    case MENU_STATE_PAUSE_SAVE_CONFIRM:
        update_save_confirm(_pInputs);
        break;
    case MENU_STATE_PAUSE_EXIT_RACE_CONFIRM:
        update_exit_race_confirm(_pInputs);
        break;
    case MENU_STATE_PAL60_CONFIRM:
        update_pal60_confirm(_pInputs);
        break;
    case MENU_STATE_CALIBRATION:
        update_calibration(_pInputs);
        break;
    case MENU_STATE_UPGRADE_SHOP:
        update_upgrade_shop(_pInputs);
        break;
    case MENU_STATE_TRANSITION_OUT:
        /* Simple transition - state used when transitioning from menu to game */
        break;
    case MENU_STATE_TRANSITION_IN:
        /* Transition in - for pause menu later */
        break;
    }

    return s_eMenuResult;
}

void menu_render(void)
{
    switch (s_eMenuState)
    {
    case MENU_STATE_START_SCREEN:
        render_start_screen();
        break;
    case MENU_STATE_MAIN_MENU:
        render_main_menu();
        break;
    case MENU_STATE_SETTINGS:
    case MENU_STATE_PAUSE_SETTINGS:
        render_settings_menu();
        break;
    case MENU_STATE_DELETE_CONFIRM:
        render_delete_confirm();
        break;
    case MENU_STATE_NEWSLETTER:
        render_controls();
        break;
    case MENU_STATE_CREDITS:
        render_credits();
        break;
    case MENU_STATE_MAIN_MENU_FADE_TO_BLACK:
        render_fade_to_black();
        break;
    case MENU_STATE_MAIN_MENU_INTRO:
        render_intro();
        break;
    case MENU_STATE_PAUSE:
        render_pause_menu();
        break;
    case MENU_STATE_PAUSE_SAVE_CONFIRM:
        render_save_confirm();
        break;
    case MENU_STATE_PAUSE_EXIT_RACE_CONFIRM:
        render_exit_race_confirm();
        break;
    case MENU_STATE_PAL60_CONFIRM:
        render_pal60_confirm();
        break;
    case MENU_STATE_CALIBRATION:
        render_calibration();
        break;
    case MENU_STATE_UPGRADE_SHOP:
        render_upgrade_shop();
        break;
    case MENU_STATE_TRANSITION_OUT:
    case MENU_STATE_TRANSITION_IN:
        /* Render previous state during transition */
        break;
    }
}

eMenuState menu_get_state(void)
{
    return s_eMenuState;
}

void menu_set_state(eMenuState _eState)
{
    s_eMenuState = _eState;
    /* Reset pause menu selection when entering pause state */
    if (_eState == MENU_STATE_PAUSE)
    {
        s_iPauseMenuSelection = 0;
        reset_nav_button_states();
        /* Sync START button state to prevent immediate unpause on first frame */
        /* This will be updated properly in the next frame's menu_update call */
        s_bPrevStart = true; /* Set to true so the current START press isn't detected */
        /* Reset debug mode when entering pause */
        debug_cheats_set_active(false);
        /* Play pause sound */
        wav64_play(s_pSoundConfirm, MIXER_CHANNEL_USER_INTERFACE);
    }
    /* Reset exit race confirmation selection when entering exit race confirm state */
    if (_eState == MENU_STATE_PAUSE_EXIT_RACE_CONFIRM)
    {
        s_bExitRaceConfirmSelection = false; /* Default to NO */
        reset_nav_button_states();
        /* Sync START button state to prevent immediate action on first frame */
        s_bPrevStart = true;
    }
    if (_eState == MENU_STATE_UPGRADE_SHOP)
    {
        reset_nav_button_states();
        s_bPrevStart = true;
    }
}
