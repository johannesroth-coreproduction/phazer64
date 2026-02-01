#include "display.h"
#include "fade_manager.h"
#include "finish_slideshow.h"
#include "joypad.h"
#include "libdragon.h"
#include "n64sys.h"
#include "tv_helper.h"
#include "vi.h"
#include <malloc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// my game
#include "anim_effects.h"
#include "audio.h"
#include "bootup_logos.h"
#include "camera.h"
#include "dialogue.h"
#include "font_helper.h"
#include "frame_time.h"
#include "game_objects/bullets.h"
#include "game_objects/currency_handler.h"
#include "game_objects/gp_camera.h"
#include "game_objects/gp_state.h"
#include "game_objects/item_turbo.h"
#include "game_objects/laser.h"
#include "game_objects/meteors.h"
#include "game_objects/npc_handler.h"
#include "game_objects/obstacle_bounce.h"
#include "game_objects/planets.h"
#include "game_objects/race_handler.h"
#include "game_objects/space_objects.h"
#include "game_objects/starfield.h"
#include "game_objects/tractor_beam.h"
#include "game_objects/triggers_dialogue.h"
#include "game_objects/ufo.h"
#include "game_objects/ufo_turbo.h"
#include "game_objects/weapons.h"
#include "math2d.h"
#include "math_helper.h"
#include "menu.h"
#include "minimap.h"
#include "n64sys.h"
#include "path_mover.h"
#include "player_jnr.h"
#include "player_surface.h"
#include "profiler.h"
#include "rng.h"
#include "satellite_pieces.h"
#include "save.h"
#include "script_handler.h"
#include "sprite_anim.h"
#include "stick_calibration.h"
#include "stick_normalizer.h"
#include "tilemap.h"
#include "ui.h"
#include "upgrade_shop.h"

// DEV SETTINGS
#ifdef MASTER_BUILD
// Master build: force all debug flags to 0
#define ENABLE_DEBUG_INPUT 0
#define ENABLE_DEBUG_TILEMAP_COLLISION 0
#define ENABLE_DEBUG_PATHS 0
#define DEBUG_SCRIPTS 0
#define DEBUG_RDPQ 0
#define SKIP_START_MENU 0
#define SKIP_BOOTUP_LOGOS 0
#else
// Development build: use configured values
#define ENABLE_DEBUG_INPUT 0
#define ENABLE_DEBUG_TILEMAP_COLLISION 0
#define ENABLE_DEBUG_PATHS 0
#define DEBUG_SCRIPTS 1
#define DEBUG_RDPQ 1
#define SKIP_START_MENU 0
#define SKIP_BOOTUP_LOGOS 1
#endif

// FPS
static float m_fFPS = 0;

// Game state
static bool m_bGameRunning = false;
static bool m_bGamePaused = false;

void init_console_systems()
{
    // Initialize RNG (uses default or set g_uGameSeed)
    rng_init(g_uGameSeed);

    joypad_init();

    audio_init_system();

    dfs_init(DFS_DEFAULT_LOCATION);

    rdpq_init();
#if DEBUG_RDPQ == 1
    rdpq_debug_start();
#endif

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);

    /* Initialize save system and activate PAL60 if configured */
    save_init();
    save_load();
    if (get_tv_type() == TV_PAL && save_get_pal60_enabled())
    {
        tv_activate_pal60();
    }

    ui_set_overscan_padding(save_get_overscan_padding());
    audio_refresh_volumes();

    /* Initialize stick normalizer (after save system loads) */
    stick_normalizer_init();
}

void init_menu()
{
    ui_init(SCREEN_W, SCREEN_H);

    /* Initialize menu system (before game systems, menu needs save system) */
    menu_init();
}

void init_game()
{
    /* Initialize global camera/rendering systems first */
    camera_init(&g_mainCamera, SCREEN_W, SCREEN_H);
    camera_set_zoom(&g_mainCamera, CAMERA_ZOOM_DEFAULT);
    gp_camera_init();
    dialogue_init();

    /* Initialize game state system basics */
    gp_state_init();

    /* Load saved state (this sets gp_state_current and layer data) */
    save_load_gp_state();

    /* Initialize global visual/audio systems */
    sprite_anim_system_init();
    anim_effects_init();
    path_mover_init();

    /* global gp systems */
    currency_handler_init();

    /* Initialize script handler */
    script_handler_init();

#ifdef DEV_BUILD
    /* Enable detailed script flow logging if runtime toggle is enabled (see debug output / USB log) */
    if (DEBUG_SCRIPTS)
    {
        script_handler_set_debug(true);
    }
#endif

    /* Initialize the specific scene/entities based on loaded state */
    gp_state_init_scene();

    // requires planets to be initialized
    minimap_init();
}

void render()
{
    PROF_SECTION_BEGIN(PROF_SECTION_RENDER);
    // this is stalling / v-syncing
    surface_t *sDisplay = display_get();

    rdpq_attach_clear(sDisplay, NULL);

    /* If finish slideshow is active, render only the slideshow */
    if (finish_slideshow_is_active())
    {
        finish_slideshow_render();
        fade_manager_render();
        rdpq_detach_show();
        PROF_SECTION_END(PROF_SECTION_RENDER);
        return;
    }

    gp_state_t currentState = gp_state_get();

    if (currentState == SPACE)
    {
        starfield_render();
        if (minimap_is_active())
        {
            minimap_render_bg();
        }
        planets_render();
        race_handler_render();
        item_turbo_render();
        obstacle_bounce_render();
        satellite_pieces_render_satellite();
        space_objects_render();
        currency_handler_render();
    }
    else if (currentState == SURFACE)
    {
#if ENABLE_DEBUG_TILEMAP_COLLISION
        tilemap_render_debug();
        currency_handler_render();
        ufo_render();
        player_surface_render();
#else
        tilemap_render_surface_begin();
        currency_handler_render(); /* Render currency to surface before distortion */
        ufo_render();              /* Render UFO to surface before distortion */
        player_surface_render();   /* Render player to surface before distortion */
        tilemap_render_surface_end();
#endif
    }
    else if (currentState == JNR)
    {
#if ENABLE_DEBUG_TILEMAP_COLLISION
        tilemap_render_debug(); /* Render all layers (debug visualization) */
        currency_handler_render();
        player_jnr_render(); /* Render player on top */
#else
        tilemap_render_jnr_begin(); /* Render layers 0-2 */
        currency_handler_render();
        player_jnr_render();      /* Render player */
        tilemap_render_jnr_end(); /* Render layer 3 (foreground) */
#endif
    }
    else if (currentState == PLANET)
    {
#if ENABLE_DEBUG_TILEMAP_COLLISION
        tilemap_render_debug();
        currency_handler_render();
#else
        tilemap_render_surface_begin();
        currency_handler_render(); /* Render currency to surface before distortion */
        tilemap_render_surface_end();
#endif
    }

    // ufo gameplay in space and on planets
    if (currentState == SPACE || currentState == PLANET) // for now with surface so game doesnt break))
    {
        // gp layer
        tractor_beam_render();
        weapons_render();
        ufo_render();
        ufo_render_target_lock();
        anim_effects_render();

        // inworld ui layer
        race_handler_render_ui();
        upgrade_shop_render();
        gp_state_render_ui();

        // ui layer
        weapons_render_ui();
        ufo_turbo_render_ui();
        tractor_beam_render_ui();
        if (minimap_is_active()) // can only happen in space anyway
        {
            minimap_render_fg();
        }
        /* Render minimap UI in SPACE state when not in dialogue */
        if (currentState == SPACE && !dialogue_is_active())
        {
            minimap_render_ui();
        }
#if ENABLE_DEBUG_INPUT
        gp_camera_render_ufo_debug();
#endif
    }
    else if (currentState == SURFACE)
    {
        gp_state_render_ui();
        triggers_dialogue_render_ui();
        anim_effects_render();
    }
    else if (currentState == JNR)
    {
        gp_state_render_ui();
        triggers_dialogue_render_ui();
        anim_effects_render();
#if ENABLE_DEBUG_INPUT
        gp_camera_render_jnr_debug();
#endif
    }

    // ALL STATES
    currency_handler_render_ui();

#if ENABLE_DEBUG_PATHS
    path_mover_render_debug();
#endif

    if (stick_calibration_is_active_without_menu())
    {
        stick_calibration_render();
    }

    if (dialogue_is_active())
    {
        dialogue_render();
    }

    /* If paused, render pause menu overlay on top */
    if (m_bGamePaused)
    {
        menu_render();
    }

    fade_manager_render();

#ifdef SHOW_FPS
    struct vec2i vFPS = ui_get_pos_bottom_left(0, 0);
    rdpq_text_printf(NULL, FONT_NORMAL, vFPS.iX, vFPS.iY, "%.1f", m_fFPS);
#endif

    rdpq_detach_show();

    PROF_SECTION_END(PROF_SECTION_RENDER);
}

void render_start_screen_menu(void)
{
    PROF_SECTION_BEGIN(PROF_SECTION_RENDER);
    surface_t *sDisplay = display_get(); // vsyncs

    rdpq_attach_clear(sDisplay, NULL);
    menu_render();
    fade_manager_render();
    rdpq_detach_show();

    PROF_SECTION_END(PROF_SECTION_RENDER);
}

void update(void)
{
    PROF_SECTION_BEGIN(PROF_SECTION_UPDATE);

    joypad_poll(); // read controllers once per frame
    joypad_inputs_t jpInputs = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t jpButtonsPressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    /* Update stick normalizer with raw input first (needed for all input processing) */
    stick_normalizer_update(jpInputs.stick_x, jpInputs.stick_y);

    /* If finish slideshow is active, skip all gameplay updates */
    if (finish_slideshow_is_active())
    {
        finish_slideshow_update(&jpInputs);
        fade_manager_update();
        PROF_SECTION_END(PROF_SECTION_UPDATE);
        return;
    }

    int8_t iStickX = stick_normalizer_get_x();
    int8_t iStickY = stick_normalizer_get_y();

    if (jpButtonsPressed.start && !dialogue_is_active() && !stick_calibration_is_active_without_menu())
    {
        m_bGamePaused = true;
        /* Check if race is active - if so, show exit race confirmation instead of pause menu */
        if (race_handler_is_race_active())
        {
            menu_set_state(MENU_STATE_PAUSE_EXIT_RACE_CONFIRM);
        }
        else
        {
            menu_set_state(MENU_STATE_PAUSE);
        }
    }

#ifdef DEV_BUILD
    // if (jpButtonsPressed.l)
    // {
    //     finish_slideshow_init();
    // }
#endif

    /* Handle layer switching input (c-up/c-down) */
    gp_update_handle_layer_switch(jpButtonsPressed.c_up, jpButtonsPressed.c_down);

    /* Update gp_state to apply any pending state changes after animations complete */
    gp_state_update();

    /* Dialogue update (runs before gameplay logic) - dont pass buttons if calibration is active */
    bool bDlgPressed =
        stick_calibration_is_active_without_menu() ? jpButtonsPressed.start : (jpButtonsPressed.a || jpButtonsPressed.b || jpButtonsPressed.z || jpButtonsPressed.c_up);
    bool bDlgDown = stick_calibration_is_active_without_menu() ? jpInputs.btn.start : (jpInputs.btn.a || jpInputs.btn.b || jpInputs.btn.z || jpInputs.btn.c_up);

    dialogue_update(bDlgPressed, bDlgDown);

    /* Update scripts after dialogue update so they can react to dialogue state changes in the same frame */
    script_handler_update();

    gp_state_t currentState = gp_state_get();

    if (dialogue_is_active())
    {

        /* Keep camera/inset smoothing but skip gameplay logic / minimum */
        if (currentState == SPACE)
        {
            /* Update calibration screen if active (only in SPACE state during dialogue) */
            if (stick_calibration_is_active_without_menu())
            {
                stick_calibration_update(&jpInputs);
            }

            path_mover_update();
            space_objects_update();
            ufo_update(false, false, false, 0, 0);
            gp_camera_ufo_update(false, false, false, false);
            starfield_update();
        }
        else if (currentState == PLANET)
        {
            ufo_update(false, false, false, 0, 0);
            gp_camera_ufo_update(false, false, false, false);
            tilemap_update();
        }
        else if (currentState == SURFACE)
        {
            player_surface_update(0, 0);
            gp_camera_surface_update();
            tilemap_update();
        }
        else if (currentState == JNR)
        {
            player_jnr_update(0, false, false);
            gp_camera_jnr_update(false, false, false, false, 0);
            tilemap_update();
        }

        sprite_anim_system_update_all();
        anim_effects_update();
        fade_manager_update();

        camera_update(&g_mainCamera);

        PROF_SECTION_END(PROF_SECTION_UPDATE);
        return;
    }

    if (currentState == SPACE)
    {
        planets_update();
        item_turbo_update();
        obstacle_bounce_update();
        path_mover_update();

        space_objects_update();
        currency_handler_update();

        eUpgradeShopResult shopResult = upgrade_shop_update(jpButtonsPressed.c_down);
        if (shopResult == UPGRADE_SHOP_RESULT_OPEN)
        {
            m_bGamePaused = true;
            menu_set_state(MENU_STATE_UPGRADE_SHOP);
        }

        bool bActivateMarker = jpButtonsPressed.a || jpButtonsPressed.z;
        bool bClearMarker = jpButtonsPressed.r;
        minimap_update(jpButtonsPressed.c_up, jpButtonsPressed.c_down, bActivateMarker, bClearMarker, iStickX, iStickY);

        race_handler_update(jpButtonsPressed.c_down);
    }

    // ufo gameplay in space and on planets
    if (currentState == SPACE || currentState == PLANET)
    {
        /* In toggle mode, pass R as edge event (only on press)
         * In hold mode, pass R as held state (while button is held) */
        bool bTractorBeamInput = save_get_target_lock_toggle_mode() ? jpButtonsPressed.r : jpInputs.btn.r;

        ufo_update(jpInputs.btn.a, jpInputs.btn.z, bTractorBeamInput, iStickX, iStickY);
        weapons_update(jpInputs.btn.b, jpButtonsPressed.c_left, jpButtonsPressed.c_right);
        tractor_beam_update(jpInputs.btn.r, jpInputs.btn.c_right, jpInputs.btn.c_left, jpInputs.btn.a, jpInputs.btn.b, jpInputs.btn.c_up, jpInputs.btn.c_down);
        gp_camera_ufo_update(jpInputs.btn.d_up, jpInputs.btn.d_down, jpInputs.btn.d_left, jpInputs.btn.d_right);
    }
    else if (currentState == SURFACE)
    {
        player_surface_update(iStickX, iStickY);
        gp_camera_surface_update();
        currency_handler_update();
        triggers_dialogue_update(jpButtonsPressed.a);
    }
    else if (currentState == JNR)
    {
        player_jnr_update(iStickX, jpInputs.btn.a, jpButtonsPressed.l);
        gp_camera_jnr_update(jpInputs.btn.d_up, jpInputs.btn.d_down, jpInputs.btn.d_left, jpInputs.btn.d_right, iStickY);
        currency_handler_update();
        triggers_dialogue_update(jpButtonsPressed.a);
    }

    // calc background based on final camera movement
    if (currentState == SPACE)
    {
        starfield_update();
        if (upgrade_shop_is_active())
        {
            // dont scale down music in shop
            audio_update_music_speed(AUDIO_SPEED_MAX);
        }
        else
        {
            audio_update_music_speed(ufo_get_speed());
        }
    }
    else if (currentState == PLANET || currentState == SURFACE || currentState == JNR)
    {
        tilemap_update();
    }

    /* Update stereo panning for UFO/player audio channels */
    audio_update_player_pan();

    /* Update animation systems, always */
    sprite_anim_system_update_all();
    anim_effects_update();
    fade_manager_update();

    /* Update camera to store its final position as previous position for next frame */
    camera_update(&g_mainCamera);

    PROF_SECTION_END(PROF_SECTION_UPDATE);
}

void update_menu(void)
{
    PROF_SECTION_BEGIN(PROF_SECTION_UPDATE);

    joypad_poll(); // read controllers once per frame
    joypad_inputs_t jpInputs = joypad_get_inputs(JOYPAD_PORT_1);

    /* Update stick normalizer with raw input (needed for credits/menus) */
    stick_normalizer_update(jpInputs.stick_x, jpInputs.stick_y);

    eMenuResult eMenuResult = menu_update(&jpInputs);

    /* Check if we're in pause menu mode */
    if (m_bGamePaused)
    {
        if (eMenuResult == MENU_RESULT_EXIT)
        {
            m_bGamePaused = false;
            menu_set_state(MENU_STATE_TRANSITION_OUT);
        }
    }
    else
    {
        /* Start Screen mode - update menu and check for game start */

        if (eMenuResult == MENU_RESULT_START_GAME || eMenuResult == MENU_RESULT_CONTINUE_GAME)
        {
            /* Only reset to defaults when starting a NEW GAME (not when continuing) */
            if (eMenuResult == MENU_RESULT_START_GAME)
            {
                /* Reset gp_state to defaults (loads spawn position from CSV) */
                /* This ONLY happens for NEW GAME, NOT for CONTINUE */
                save_reset_gp_state_to_defaults();
                save_write();
            }
            /* For CONTINUE, we skip the reset and use the existing save data */
            init_game();
            m_bGameRunning = true;

            /* When entering gameplay (new or continue), fade in from black */
            fade_manager_start(FROM_BLACK);
        }
    }

    fade_manager_update();

    PROF_SECTION_END(PROF_SECTION_UPDATE);
}

int main(void)
{
    // Initial console intro
    debug_init_isviewer();
    debug_init_usblog();

    /* Profiler init (no-op if PROFILER_ENABLED not defined) */
    PROF_INIT();
    {
        init_console_systems();

        font_helper_init();
        init_menu();

#if SKIP_START_MENU
        /* Skip start menu and go directly to gameplay */
        init_game();
        m_bGameRunning = true;
#endif
    }
    /* Mark boot completed for profiling */
    PROF_BOOT_DONE();

#if !SKIP_BOOTUP_LOGOS
    /* Bootup logos sequence */
    bootup_logos_init();
    while (!bootup_logos_is_done())
    {
        frame_time_set(display_get_delta_time());
        bootup_logos_update();
        surface_t *sDisplay = display_get();
        rdpq_attach_clear(sDisplay, NULL);
        bootup_logos_render();
        fade_manager_render();
        rdpq_detach_show();
        audio_poll();
    }

#endif

    fade_manager_start(FROM_BLACK);

    while (1)
    {
        PROF_FRAME_BEGIN();
        {
            audio_poll();
            frame_time_set(display_get_delta_time());
            m_fFPS = display_get_fps();

            if (!m_bGameRunning)
            {
                update_menu();
                audio_poll();
                render_start_screen_menu();
            }
            else
            {
                /* Game paused */
                if (m_bGamePaused)
                {
                    update_menu();
                }
                /* Game running */
                else
                {
                    update();
                }
                audio_poll();
                render();
            }

            audio_poll();
        }
        PROF_FRAME_END(m_fFPS);
    }

    return 0;
}
