#include "gp_state.h"
#include "../audio.h"
#include "../camera.h"
#include "../csv_helper.h"
#include "../dialogue.h"
#include "../fade_manager.h"
#include "../font_helper.h"
#include "../frame_time.h"
#include "../minimap.h"
#include "../player_jnr.h"
#include "../player_surface.h"
#include "../rng.h"
#include "../satellite_pieces.h"
#include "../save.h"
#include "../script_handler.h"
#include "../string_helper.h"
#include "../tilemap.h"
#include "../ui.h"
#include "../upgrade_shop.h"
#include "currency_handler.h"
#include "debug.h"
#include "gp_camera.h"
#include "item_turbo.h"
#include "libdragon.h"
#include "meteors.h"
#include "npc_handler.h"
#include "obstacle_bounce.h"
#include "planets.h"
#include "race_handler.h"
#include "rdpq.h"
#include "rdpq_mode.h"
#include "space_objects.h"
#include "starfield.h"
#include "tractor_beam.h"
#include "triggers_dialogue.h"
#include "triggers_load.h"
#include "ufo.h"
#include "weapons.h"
#include <ctype.h>
#include <string.h>
#include <sys/types.h>

#define TRANSITION_HOLD_SEC 0.0f

////////////////////////////////////////////////////////////
/// SAVE
gp_state_t gp_state_current = SPACE;
static gp_state_t gp_state_previous = SPACE; /* Previous state we came from */

/* Layer data array - indexed by gp_state_t enum: [SPACE], [PLANET], [SURFACE], [JNR] */
static layer_data_t m_layers[4];

static uint16_t m_uUnlockFlags = 0;
static uint16_t m_uCurrency = 0;
static float m_fBestLapTime = 0.0f;
static gp_act_t gp_act_current = ACT_INTRO;
static currency_collection_entry_t m_aCurrencyCollection[MAX_CURRENCY_COLLECTION_FOLDERS] = {0};
////////////////////////////////////////////////////////////

typedef enum
{
    TRANS_NONE,
    TRANS_LAND_ANIM,
    TRANS_LAND_FADE_TO_BLACK,
    TRANS_LAND_FADE_FROM_BLACK,
    TRANS_LAUNCH_FADE_TO_BLACK,
    TRANS_LAUNCH_ANIM,
} trans_state_t;

static trans_state_t m_transState = TRANS_NONE;
static gp_state_t m_targetState = SPACE;
static float m_fHoldTimer = 0.0f;

/* Cached sprite for UI rendering */
static sprite_t *m_pBtnCDownSprite = NULL;
static sprite_t *m_pBtnCUpSprite = NULL;
static sprite_t *m_pHudStarsIconSprite = NULL;
static sprite_t *m_pHudLandIconSprite = NULL;
static sprite_t *m_pHudLandBlockedIconSprite = NULL;

/* Cached display name for current layer (used for top-center text with auto-centering) */
static char m_szCurrentDisplayName[64] = {0};

/* Cached text width for trigger UI rendering (optimizes world-position text rendering) */
static const char *m_pLastTriggerDisplayName = NULL;
static float m_fCachedTriggerTextWidth = 0.0f;

/* Cutscene mode flag - when true, blocks gameplay input (ufo, weapons, etc.) */
static bool m_bCutsceneMode = false;

gp_state_t gp_state_get(void)
{
    return gp_state_current;
}

gp_state_t gp_state_get_previous(void)
{
    return gp_state_previous;
}

/* Helper: Get the data folder for a given layer. Returns NULL if folder is not set. */
static const char *get_layer_folder(gp_state_t layer)
{
    return m_layers[layer].folder_name[0] ? m_layers[layer].folder_name : NULL;
}

const char *gp_state_get_current_folder(void)
{
    return get_layer_folder(gp_state_current);
}

bool gp_state_accepts_input(void)
{
    /* Block input during state transitions */
    if (m_transState != TRANS_NONE)
        return false;

    /* Block input while UFO is busy with landing/launching animation */
    if (ufo_is_transition_playing())
        return false;

    /* Block input during fade transitions */
    if (fade_manager_is_busy())
        return false;

    /* Block input when screen is fully opaque (fully black) */
    if (fade_manager_is_opaque())
        return false;

    /* Block input when minimap is active */
    if (minimap_is_active())
        return false;

    /* Block input during cutscene mode */
    if (m_bCutsceneMode)
        return false;

    if (dialogue_is_active())
        return false;

    return true;
}

/* Helper: Get display name for a folder (uses centralized nice location name formatting).
 * Writes result to provided buffer. Returns true on success. */
static bool get_display_name_from_folder(const char *folder, char *out_buffer, size_t buffer_size)
{
    return string_helper_nice_location_name(folder, out_buffer, buffer_size);
}

/* Helper: Snap camera to current player/vehicle and reset zoom (used during transitions) */
static void snap_camera_and_reset_zoom(void)
{
    /* Snap to appropriate position based on current state */
    struct vec2 vPos;
    if (gp_state_current == JNR)
        vPos = player_jnr_get_position();
    else if (gp_state_current == SURFACE)
        vPos = player_surface_get_position();
    else
        vPos = ufo_get_position();

    camera_set_position(&g_mainCamera, vPos);
    camera_set_zoom(&g_mainCamera, CAMERA_ZOOM_DEFAULT);
}

/* Public API: Snap camera and sync starfield for space transitions (prevents visual jumps) */
void gp_state_snap_space_transition(void)
{
    if (gp_state_current != SPACE)
        return;

    /* Snap camera to UFO position and reset zoom */
    struct vec2 vUfoPos = ufo_get_position();
    camera_set_position(&g_mainCamera, vUfoPos);
    camera_set_zoom(&g_mainCamera, CAMERA_ZOOM_DEFAULT);

    /* Reset starfield velocity to prevent streaks from old position */
    starfield_reset_velocity();
}

void gp_state_init(void)
{
    /* Load UI sprites */
    m_pBtnCDownSprite = sprite_load("rom:/btn_c_down_00.sprite");
    m_pBtnCUpSprite = sprite_load("rom:/btn_c_up_00.sprite");
    m_pHudStarsIconSprite = sprite_load("rom:/hud_stars_icon_00.sprite");
    m_pHudLandIconSprite = sprite_load("rom:/hud_land_icon_00.sprite");
    m_pHudLandBlockedIconSprite = sprite_load("rom:/hud_land_blocked_icon_00.sprite");

    /* Initialize layer data */
    memset(m_layers, 0, sizeof(m_layers));

    /* Set default SPACE folder (safety net before save_load_gp_state() overwrites with saved data) */
    strncpy(m_layers[SPACE].folder_name, "space", sizeof(m_layers[SPACE].folder_name));
    m_layers[SPACE].saved_position = (struct vec2){0.0f, 0.0f};

    /* Initialize display name to SPACE (temporary default; save_load_gp_state() will update based on loaded layer) */
    strncpy(m_szCurrentDisplayName, "SPACE", sizeof(m_szCurrentDisplayName));
}

/* --- Internal State Initialization Helpers --- */

/* Check for script.csv in data folder and execute script if found */
static void check_and_execute_state_script(const char *_pFolder, bool _bStopOthers)
{
    if (!_pFolder)
        return;

    /* Build path: rom:/<folder>/script.csv */
    char szPath[256];
    snprintf(szPath, sizeof(szPath), "rom:/%s/script.csv", _pFolder);

    FILE *pFile = fopen(szPath, "r");
    if (!pFile)
    {
        /* File doesn't exist, which is fine - not all planets have scripts */
        debugf("Script CSV file not found: %s\n", szPath);
        return;
    }

    /* Read first line */
    char szLine[256];
    bool bTruncated = false;
    if (!csv_helper_fgets_checked(szLine, sizeof(szLine), pFile, &bTruncated))
    {
        debugf("Failed to read first line from %s\n", szPath);
        fclose(pFile);
        return;
    }

    fclose(pFile);

    if (bTruncated)
    {
        debugf("Script CSV line too long in %s\n", szPath);
        return;
    }

    /* Strip EOL characters */
    csv_helper_strip_eol(szLine);

    /* Skip empty lines */
    if (szLine[0] == '\0')
    {
        return;
    }

    /* Copy line for tokenizing (strtok modifies the string) */
    char szLineCopy[256];
    if (!csv_helper_copy_line_for_tokenizing(szLine, szLineCopy, sizeof(szLineCopy)))
    {
        debugf("Failed to copy line for tokenizing from %s\n", szPath);
        return;
    }

    /* Parse first element (script name) */
    char szScriptName[64];
    if (!csv_helper_parse_name(szLineCopy, szScriptName, sizeof(szScriptName)))
    {
        debugf("Failed to parse script name from %s (line: '%s')\n", szPath, szLine);
        return;
    }

    /* Execute the script */
    script_handler_start(szScriptName, _bStopOthers);
}

static void enter_state_space(void)
{
    starfield_init(SCREEN_W, SCREEN_H, g_uGameSeed);
    planets_init();
    item_turbo_init();
    obstacle_bounce_init();
    tractor_beam_init();
    weapons_init();
    ufo_init();
    space_objects_init();
    upgrade_shop_init();

    /* Set UFO position from saved layer data */
    ufo_set_position(m_layers[SPACE].saved_position);

    /* Audio */
    const char *pFolder = get_layer_folder(SPACE);
    audio_play_music(MUSIC_NORMAL, pFolder);

    /* Refresh currency handler (loads currency.csv and spawns currency meteors in SPACE mode) */
    currency_handler_refresh(pFolder, SPACE);

    gp_state_snap_space_transition();
}

static void enter_state_planet(bool _bFromAbove)
{
    /* Initialize entities valid in PLANET mode */
    ufo_init();
    weapons_init();
    tractor_beam_init();

    const char *pFolder = get_layer_folder(PLANET);
    if (pFolder)
    {
        /* Load tilemap if not initialized or if folder doesn't match.
           PLANET and SURFACE share the same tilemap, so skip reload if already loaded. */
        const char *pLoadedFolder = tilemap_get_loaded_folder();
        if (!pLoadedFolder || strcmp(pLoadedFolder, pFolder) != 0)
        {
            tilemap_init(pFolder, TILEMAP_TYPE_SURFACE);
        }

        /* Refresh currency handler (loads currency.csv and creates currency entities).
           PLANET and SURFACE share the same currency, so skip reload if already loaded. */
        currency_handler_refresh(pFolder, PLANET);

        audio_play_music(MUSIC_NORMAL, pFolder);

        /* Set UFO position based on transition direction */
        if (_bFromAbove)
        {
            /* Coming from SPACE (DOWN): use spawn position from CSV */
            ufo_set_position_from_data(pFolder);
        }
        else
        {
            /* Coming from SURFACE (UP): use saved orbit position */
            ufo_set_position(m_layers[PLANET].saved_position);

            /* Recover animation state for SURFACE->PLANET launch animation */
            ufo_recover_planet_position_mode();
        }
    }
}

static void enter_state_surface(bool _bFromAbove)
{
    /* Initialize entities */
    ufo_init(); /* Reset UFO (prop) */
    /* No weapons/tractor beam in Surface mode, so no init needed (freed in exit code) */

    const char *pFolder = get_layer_folder(SURFACE);
    if (pFolder)
    {
        /* Load tilemap if not initialized or if folder doesn't match.
           PLANET and SURFACE share the same tilemap, so skip reload if already loaded. */
        const char *pLoadedFolder = tilemap_get_loaded_folder();
        if (!pLoadedFolder || strcmp(pLoadedFolder, pFolder) != 0)
        {
            tilemap_init(pFolder, TILEMAP_TYPE_SURFACE);
        }

        /* Refresh currency handler (loads currency.csv and creates currency entities).
           PLANET and SURFACE share the same currency, so skip reload if already loaded. */
        currency_handler_refresh(pFolder, SURFACE);

        triggers_load_init(pFolder);
        triggers_dialogue_init(pFolder);
        audio_play_music(MUSIC_NORMAL, pFolder);

        /* Recalculate UFO position (shadow) based on PLANET orbit position */
        struct vec2 vOrbitPos = m_layers[PLANET].saved_position;
        ufo_set_position(vOrbitPos);
        ufo_recover_surface_position_mode();

        /* Init Player Surface with appropriate position based on transition direction */
        struct vec2 vPlayerPos;
        if (_bFromAbove)
        {
            /* Coming from PLANET (DOWN): use UFO shadow position as spawn */
            vPlayerPos = ufo_get_shadow_position();
        }
        else
        {
            /* Coming from JNR (UP): use saved surface position */
            vPlayerPos = m_layers[SURFACE].saved_position;
        }
        player_surface_init(vPlayerPos);
    }
}

static void enter_state_jnr(void)
{
    const char *pFolder = get_layer_folder(JNR);
    if (pFolder)
    {
        tilemap_init(pFolder, TILEMAP_TYPE_JNR);
        triggers_load_init(pFolder);
        triggers_dialogue_init(pFolder);
        audio_play_music(MUSIC_NORMAL, pFolder);

        /* Init Player JNR and set spawn position (always comes from SURFACE DOWN) */
        player_jnr_init();
        player_jnr_set_position_from_data(pFolder);

        /* Refresh currency handler (loads currency.csv and creates currency entities) */
        currency_handler_refresh(pFolder, JNR);
    }
}

void gp_state_land(void)
{
    if (gp_state_current >= JNR || m_transState != TRANS_NONE || fade_manager_is_busy())
        return;

    /* Special handling for terra planet: execute script instead of state transition */
    if (gp_state_current == SPACE)
    {
        const char *pPlanetName = planets_get_selected_data_name();
        if (pPlanetName && strcmp(pPlanetName, PLANET_HOME) == 0)
        {
            script_handler_start("terra_land", false);
            return; /* Early return prevents m_transState setup and ufo_start_transition_animation() call below */
        }
    }

    /* Save current position in the CURRENT layer */
    struct vec2 vCurrentPos;
    if (gp_state_current == SURFACE)
    {
        /* On SURFACE, save PLAYER position (rover), not UFO */
        vCurrentPos = player_surface_get_position();
    }
    else
    {
        /* SPACE or PLANET: save UFO position */
        vCurrentPos = ufo_get_position();
    }

    m_layers[gp_state_current].saved_position = vCurrentPos;

    /* Store folder name for the TARGET layer */
    if (gp_state_current == SPACE)
    {
        /* SPACE -> PLANET: Get planet folder from selected planet */
        const char *pPlanetName = planets_get_selected_data_name();
        if (pPlanetName)
        {
            STRING_COPY(m_layers[PLANET].folder_name, pPlanetName);
        }
    }
    else if (gp_state_current == PLANET)
    {
        /* PLANET -> SURFACE: Copy planet folder (they share the same tilemap) */
        STRING_COPY(m_layers[SURFACE].folder_name, m_layers[PLANET].folder_name);
    }
    else if (gp_state_current == SURFACE)
    {
        /* SURFACE -> JNR: Get JNR folder from selected load trigger */
        const char *pJnrName = triggers_load_get_selected_data_name();
        if (pJnrName)
        {
            STRING_COPY(m_layers[JNR].folder_name, pJnrName);
        }
    }

    m_targetState = gp_state_current + 1;
    m_transState = TRANS_LAND_ANIM;
    /* Only play UFO animation for states where UFO exists (SPACE, PLANET, SURFACE) */
    /* Skip animation for SURFACE->JNR transition as UFO is not active in JNR */
    if (gp_state_current != SURFACE || m_targetState != JNR)
    {
        if (gp_state_current == SPACE && m_targetState == PLANET)
        {
            /* Fade out space music as soon as transition starts */
            audio_stop_music();
        }
        ufo_start_transition_animation(gp_state_current, m_targetState);
    }
}

void gp_state_launch(void)
{
    if (gp_state_current <= SPACE || m_transState != TRANS_NONE || fade_manager_is_busy())
        return;

    m_targetState = gp_state_current - 1;
    m_transState = TRANS_LAUNCH_FADE_TO_BLACK;

    if (gp_state_current == PLANET && m_targetState == SPACE)
    {
        /* Fade out planet music as soon as launch transition starts */
        audio_stop_music();
    }

    if (!(gp_state_current == SURFACE && m_targetState == PLANET))
    {
        fade_manager_start(TO_BLACK);
    }
    m_fHoldTimer = 0.0f;
}

/* Internal helper: Perform the actual state switch (cleanup old, init new) */
static void perform_state_switch(gp_state_t oldState, gp_state_t newState)
{
    /* Stop any running gameplay scripts before changing layers to avoid
       scripts referencing entities or systems being torn down during the switch. */
    script_handler_stop();

    /* Update previous state before switching */
    gp_state_previous = oldState;

    /* 1. Cleanup Old State Resources */
    if (oldState == PLANET && newState == SPACE)
    {
        tilemap_free();
        ufo_free();
        weapons_free();
        tractor_beam_free();
        /* starfield_reset_velocity handled in enter_state_space */
    }
    else if (oldState == SURFACE && newState == PLANET)
    {
        player_surface_free();
        triggers_load_free();
        triggers_dialogue_free();
        /* Do not free tilemap (shared) */
        ufo_free(); /* Reset UFO props */
    }
    else if (oldState == JNR && newState == SURFACE)
    {
        player_jnr_free();
        /* Currency handler is now global, no need to free */
        triggers_load_free();
        triggers_dialogue_free();
        tilemap_free(); /* Free JNR tilemap */
    }
    else if (oldState == SPACE && newState == PLANET)
    {
        /* Free space entities */
        planets_free();
        item_turbo_free();
        obstacle_bounce_free();
        starfield_free();
        race_handler_free(); /* Also frees race_track */
        space_objects_free();
        upgrade_shop_free();

        /* Free UFO/weapons before re-init to be clean (although init calls free now, explicit is good) */
        ufo_free();
        weapons_free();
        tractor_beam_free();
    }
    else if (oldState == SURFACE && newState == JNR)
    {
        /* Going into cave: free surface player and triggers */
        player_surface_free();
        triggers_load_free();
        triggers_dialogue_free();
        ufo_free();

        tilemap_free();
    }
    else if (oldState == PLANET && newState == SURFACE)
    {
        weapons_free();
        tractor_beam_free();
        ufo_free(); /* Will be re-inited as prop */
        /* Do NOT free tilemap (shared) */
    }

    /* Clear folder name when moving UP (launching), except for SPACE which is static */
    if (newState < oldState && oldState > SPACE && oldState <= JNR)
    {
        m_layers[oldState].folder_name[0] = '\0';
    }

    /* 2. Initialize New State Resources */
    if (newState == SPACE)
    {
        enter_state_space();
    }
    else if (newState == PLANET)
    {
        /* Determine if coming from above (DOWN) or below (UP) based on state enum ordering */
        bool bFromAbove = (oldState < newState);
        enter_state_planet(bFromAbove);
    }
    else if (newState == SURFACE)
    {
        /* Determine if coming from above (DOWN) or below (UP) based on state enum ordering */
        bool bFromAbove = (oldState < newState);
        enter_state_surface(bFromAbove);
    }
    else if (newState == JNR)
    {
        enter_state_jnr();
        /* If we're “refreshing” JNR (e.g. load-from-save where old==new),
           override spawn position with the saved one from persistence. */
        if (oldState == JNR)
        {
            struct vec2 vSavedPos = m_layers[JNR].saved_position;
            player_jnr_set_position(vSavedPos);
        }
    }

    /* 3. Finalize State Change */
    gp_state_current = newState;

    if (!((oldState == PLANET && newState == SURFACE) || (oldState == SURFACE && newState == PLANET)))
    {
        snap_camera_and_reset_zoom();
    }

    /* Refresh satellite pieces for new layer (called on all layer switches) */
    satellite_pieces_refresh();

    /* Update cached display name */
    const char *pFolder = get_layer_folder(newState);
    if (pFolder)
    {
        get_display_name_from_folder(pFolder, m_szCurrentDisplayName, sizeof(m_szCurrentDisplayName));
    }

    // AUTO SAVE
    save_sync_gp_state();
    save_write();

    check_and_execute_state_script(pFolder, true);
}

void gp_state_init_scene(void)
{
    /* Use the same path as runtime transitions: a state “refresh” from the same layer.
     * This reinitializes systems for the current state using saved positions, snaps
     * the camera, updates display name, and runs any optional state script. */
    perform_state_switch(gp_state_current, gp_state_current);
}

void gp_state_update(void)
{
    switch (m_transState)
    {
    case TRANS_NONE:
        break;

    case TRANS_LAND_ANIM:
        if (!ufo_is_transition_playing())
        {
            /* Animation done -> Fade Out */
            if (!(gp_state_current == PLANET && m_targetState == SURFACE))
            {
                fade_manager_start(TO_BLACK);
            }
            m_transState = TRANS_LAND_FADE_TO_BLACK;
            m_fHoldTimer = 0.0f;
        }
        break;

    case TRANS_LAND_FADE_TO_BLACK:
        if (!fade_manager_is_busy())
        {
            m_fHoldTimer += frame_time_delta_seconds();
            if (m_fHoldTimer < TRANSITION_HOLD_SEC)
                break;

            /* Screen is fully dark - switch state */
            bool bLandingOnSurface = (gp_state_current == PLANET && m_targetState == SURFACE);
            perform_state_switch(gp_state_current, m_targetState);

            /* Fade Out done -> End Anim -> Fade In */
            ufo_end_transition_animation(gp_state_current); /* gp_state_current is now new state */

            if (bLandingOnSurface)
            {
                ufo_play_door(true); /* Door opening after landing */
            }

            if (!bLandingOnSurface)
            {
                fade_manager_start(FROM_BLACK);
            }
            m_transState = TRANS_LAND_FADE_FROM_BLACK;
        }
        break;

    case TRANS_LAND_FADE_FROM_BLACK:
        if (!fade_manager_is_busy())
        {
            m_transState = TRANS_NONE;
        }
        break;

    case TRANS_LAUNCH_FADE_TO_BLACK:
        /* Play door close sound when fade to black starts (launching from surface to planet) */
        /* if (gp_state_current == SURFACE && m_fHoldTimer == 0.0f)
        {
            ufo_play_door(false);  // Door closing
            m_fHoldTimer = 0.001f; // Mark that we've played the sound
        } */
        if (!fade_manager_is_busy())
        {
            /* Fade Out done (screen is black) */
            m_fHoldTimer += frame_time_delta_seconds();
            if (m_fHoldTimer < TRANSITION_HOLD_SEC)
                break;

            /* Capture old state for animation trigger */
            gp_state_t old_state = gp_state_current;

            /* Screen is fully dark - switch state */
            perform_state_switch(gp_state_current, m_targetState);

            /* Kick off animation */
            /* Only play UFO animation for states where UFO exists (SPACE, PLANET, SURFACE) */
            /* Skip animation for JNR->SURFACE transition as UFO was not active in JNR */
            if (old_state != JNR || gp_state_current != SURFACE)
            {
                ufo_start_transition_animation(old_state, gp_state_current);
            }

            /* Fade from black */
            if (!(old_state == SURFACE && m_targetState == PLANET))
            {
                fade_manager_start(FROM_BLACK);
            }

            m_transState = TRANS_LAUNCH_ANIM;
        }
        break;

    case TRANS_LAUNCH_ANIM:
        /* Wait for animation */
        if (!ufo_is_transition_playing())
        {
            ufo_end_transition_animation(gp_state_current);
            m_transState = TRANS_NONE;
        }
        break;
    }
}

void gp_update_handle_layer_switch(bool c_up, bool c_down)
{
    /* Block state switching during minimap mode */
    if (minimap_is_active() || dialogue_is_active())
        return;

    /* Gameplay state switching */
    if (!tractor_beam_is_active())
    {
        if (c_up)
        {
            /* In SURFACE state, only launch if player is in UFO trigger */
            if (gp_state_current == SURFACE)
            {
                if (player_surface_near_ufo())
                {
                    gp_state_launch();
                }
            }
            /* In JNR state, only launch if player is in load trigger */
            else if (gp_state_current == JNR)
            {
                const char *pLoadTrigger = triggers_load_get_selected_data_name();
                if (pLoadTrigger != NULL)
                {
                    gp_state_launch();
                }
            }
            else
            {
                /* In other states (SPACE, PLANET), launch normally */
                gp_state_launch();
            }
        }
        else if (c_down)
        {
            if (gp_state_get() == SPACE)
            {
                const char *pPlanet = planets_get_selected_data_name();
                if (pPlanet != NULL) // only can land if a planet is selected
                {
                    gp_state_land();
                }
            }
            else if (gp_state_get() == PLANET)
            {
                /* Check if landing is possible at current position (checks box area) */
                if (ufo_can_land())
                {
                    gp_state_land();
                }
            }
            else if (gp_state_get() == SURFACE)
            {
                /* Check for load trigger selection */
                const char *pJnrDataName = triggers_load_get_selected_data_name();
                if (pJnrDataName != NULL) // only can enter JNR if a trigger is selected
                {
                    gp_state_land();
                }
            }
        }
    }
}

/* Helper: Render trigger UI at world position (name below, C button above) */
static void render_trigger_ui_at_world_pos(const char *_pDisplayName, struct vec2 _vWorldPos, struct vec2i _vHalfExtents, sprite_t *_pButtonSprite)
{
    if (!_pDisplayName || !_pButtonSprite)
        return;

    /* Convert world position to screen */
    struct vec2i vScreenPos;
    camera_world_to_screen(&g_mainCamera, _vWorldPos, &vScreenPos);

    float fZoom = camera_get_zoom(&g_mainCamera);
    float fScaledPadding = (UI_DESIGNER_PADDING / 2.0f) * fZoom;

    /* Update cached text width if display name changed (optimization for trigger UI) */
    if (_pDisplayName != m_pLastTriggerDisplayName)
    {
        m_pLastTriggerDisplayName = _pDisplayName;
        m_fCachedTriggerTextWidth = font_helper_get_text_width(FONT_NORMAL, _pDisplayName);
    }

    /* Draw name below the trigger */
    int iTextX = (int)(vScreenPos.iX - m_fCachedTriggerTextWidth / 2.0f);
    int iTextY = vScreenPos.iY + (int)(_vHalfExtents.iY * fZoom) + (int)fScaledPadding + UI_FONT_Y_OFFSET;
    rdpq_text_printf(NULL, FONT_NORMAL, iTextX, iTextY, "%s", _pDisplayName);

    /* Draw C button above the trigger */
    int iBtnX = vScreenPos.iX - (_pButtonSprite->width / 2);
    int iBtnY = vScreenPos.iY - (int)(_vHalfExtents.iY * fZoom) - _pButtonSprite->height - (int)fScaledPadding;

    rdpq_set_mode_copy(false);
    rdpq_mode_alphacompare(1);
    rdpq_sprite_blit(_pButtonSprite, iBtnX, iBtnY, NULL);
}

void gp_state_render_ui(void)
{
    if (!gp_state_accepts_input())
        return;

    /* SPACE state: planet name below entity, C button above */
    if (gp_state_current == SPACE)
    {
        const char *pPlanetName = planets_get_selected_display_name();
        const struct entity2D *pPlanetEntity = planets_get_selected_entity();

        if (pPlanetEntity && pPlanetName)
        {
            render_trigger_ui_at_world_pos(pPlanetName, pPlanetEntity->vPos, pPlanetEntity->vHalf, m_pBtnCDownSprite);
        }
    }
    /* PLANET state: draw C_UP + stars icon, then C_DOWN + land icon at top left (top-down layout) */
    else if (gp_state_current == PLANET)
    {
        bool bCanLand = ufo_can_land();

        /* Get top left position using sprite-based positioning (matches minimap) */
        struct vec2i vTopLeft = ui_get_pos_top_left_sprite(m_pBtnCUpSprite);
        vTopLeft.iX += 2;
        vTopLeft.iY += 2;

        /* Start X position (left-aligned) */
        int iStartX = vTopLeft.iX;
        int iCurrentY = vTopLeft.iY;

        /* Set up rendering mode */
        rdpq_set_mode_copy(false);
        rdpq_mode_alphacompare(1);
        rdpq_mode_filter(FILTER_POINT);

        /* Draw first group: C_UP + stars icon (matches minimap positioning exactly) */
        if (m_pBtnCUpSprite)
        {
            rdpq_sprite_blit(m_pBtnCUpSprite, iStartX, iCurrentY, NULL);
            iStartX += m_pBtnCUpSprite->width + MINIMAP_UI_BUTTON_ICON_PADDING;
        }
        if (m_pHudStarsIconSprite)
        {
            rdpq_sprite_blit(m_pHudStarsIconSprite, iStartX, iCurrentY - 2, NULL);
        }

        /* Get icon height for spacing calculation */
        int iIconHeight = m_pHudStarsIconSprite ? m_pHudStarsIconSprite->height : 0;

        /* Add UI padding (applied to Y instead of X) */
        iCurrentY += iIconHeight + (UI_DESIGNER_PADDING);
        iStartX = vTopLeft.iX; /* Reset X for second line */

        /* Draw second group: C_DOWN + land icon (or blocked icon) */
        if (m_pBtnCDownSprite)
        {
            rdpq_sprite_blit(m_pBtnCDownSprite, iStartX, iCurrentY, NULL);
            iStartX += m_pBtnCDownSprite->width + MINIMAP_UI_BUTTON_ICON_PADDING;
        }
        /* Switch between normal and blocked icon based on landing availability */
        sprite_t *pLandIcon = bCanLand ? m_pHudLandIconSprite : m_pHudLandBlockedIconSprite;
        if (pLandIcon)
        {
            rdpq_sprite_blit(pLandIcon, iStartX, iCurrentY - 2, NULL);
        }
    }
    /* SURFACE state: C_UP above UFO, load trigger UI if selected */
    else if (gp_state_current == SURFACE)
    {
        /* Draw C_UP button above UFO when player is in trigger */
        if (player_surface_near_ufo() && m_pBtnCUpSprite != NULL)
        {
            const struct entity2D *pUfoEntity = ufo_get_entity();
            if (pUfoEntity)
            {
                ui_render_button_above_world_pos(ufo_get_position(), pUfoEntity->vHalf, m_pBtnCUpSprite, 0.5f);
            }
        }

        /* Draw load trigger UI if selected */
        const char *pLoadName = triggers_load_get_selected_display_name();
        if (pLoadName && m_pBtnCDownSprite)
        {
            struct vec2 vLoadCenter;
            struct vec2i vHalfExtents;
            if (triggers_load_get_selected_pos_and_size(&vLoadCenter, &vHalfExtents))
            {
                render_trigger_ui_at_world_pos(pLoadName, vLoadCenter, vHalfExtents, m_pBtnCDownSprite);
            }
        }
    }
    /* JNR state: show C_UP button above load trigger when player is colliding */
    else if (gp_state_current == JNR)
    {
        if (triggers_load_get_selected_display_name() != NULL && m_pBtnCUpSprite != NULL)
        {
            struct vec2 vLoadCenter;
            struct vec2i vHalfExtents;
            if (triggers_load_get_selected_pos_and_size(&vLoadCenter, &vHalfExtents))
            {
                ui_render_button_above_world_pos(vLoadCenter, vHalfExtents, m_pBtnCUpSprite, 1.0f);
            }
        }
    }
}

////////////////////////////////////////////////////////////
/// SAVE API / RUNTIME ACCESSORS

void gp_state_get_persist(gp_state_persist_t *_pOut)
{
    int i = 0;

    if (_pOut == NULL)
        return;

    memset(_pOut, 0, sizeof(*_pOut));

    /* Layers */
    memcpy(_pOut->aLayers, m_layers, sizeof(m_layers));
    for (i = 0; i < 4; i++)
    {
        /* Defensive: always force NUL termination */
        _pOut->aLayers[i].folder_name[sizeof(_pOut->aLayers[i].folder_name) - 1] = '\0';
    }

    /* Enums / flags / values */
    _pOut->uGpStateCurrent = (uint8_t)gp_state_current;
    _pOut->uAct = (uint8_t)gp_act_current;
    _pOut->uUnlockFlags = m_uUnlockFlags;
    _pOut->uCurrency = m_uCurrency;

    _pOut->uReserved = 0;

    /* Current position (live query) */
    struct vec2 vPos = gp_state_current_pos_get();
    _pOut->fCurrentPosX = vPos.fX;
    _pOut->fCurrentPosY = vPos.fY;

    /* Best lap time */
    _pOut->fBestLapTime = m_fBestLapTime;

    /* Currency collection */
    memcpy(_pOut->aCurrencyCollection, m_aCurrencyCollection, sizeof(m_aCurrencyCollection));
}

void gp_state_set_persist(const gp_state_persist_t *_pIn)
{
    int i = 0;

    if (_pIn == NULL)
        return;

    /* Copy layers verbatim, then force termination (handles non-terminated inputs safely). */
    memcpy(m_layers, _pIn->aLayers, sizeof(m_layers));
    for (i = 0; i < 4; i++)
    {
        m_layers[i].folder_name[sizeof(m_layers[i].folder_name) - 1] = '\0';
    }

    /* Ensure SPACE folder is always set (matches init hack; prevents NULL folder lookups). */
    if (m_layers[SPACE].folder_name[0] == '\0')
    {
        strncpy(m_layers[SPACE].folder_name, "space", sizeof(m_layers[SPACE].folder_name));
        m_layers[SPACE].folder_name[sizeof(m_layers[SPACE].folder_name) - 1] = '\0';
    }

    /* Restore enums with range guards */
    if (_pIn->uGpStateCurrent < 4u)
        gp_state_current = (gp_state_t)_pIn->uGpStateCurrent;
    else
        gp_state_current = SPACE;

    if (_pIn->uAct < (uint8_t)ACT_COUNT)
        gp_act_current = (gp_act_t)_pIn->uAct;
    else
        gp_act_current = ACT_INTRO;

    /* Flags/currency */
    m_uUnlockFlags = (uint16_t)(_pIn->uUnlockFlags & (uint16_t)GP_UNLOCK_KNOWN_MASK);
    m_uCurrency = _pIn->uCurrency;

    /* Best lap time */
    m_fBestLapTime = _pIn->fBestLapTime;

    /* Currency collection */
    memcpy(m_aCurrencyCollection, _pIn->aCurrencyCollection, sizeof(m_aCurrencyCollection));

    /*
        Current position:
        Store the saved position into the layer data for the current state.
        This ensures that when gp_state_init_scene() is called, it picks up this
        authoritative position from m_layers[gp_state_current].saved_position.

        We do NOT call gp_state_current_pos_set() here because systems are not yet initialized.
    */
    m_layers[gp_state_current].saved_position = (struct vec2){_pIn->fCurrentPosX, _pIn->fCurrentPosY};

    /*
        Important: This function only restores persisted data.
        It does not (and should not) trigger tilemap loads, player init, etc.
        That remains controlled by your state-transition code or init code.
    */

    /* Reset any transition-in-progress state to avoid “half-transition” after load */
    m_transState = TRANS_NONE;
    m_targetState = gp_state_current;
    m_fHoldTimer = 0.0f;

    /* Cached UI display name will be updated in gp_state_init_scene() */
    m_szCurrentDisplayName[0] = '\0';

    /* Reset trigger UI cache (safe; prevents stale width/name pairing) */
    m_pLastTriggerDisplayName = NULL;
    m_fCachedTriggerTextWidth = 0.0f;
}

bool gp_state_unlock_get(uint16_t _uFlag)
{
    return (m_uUnlockFlags & _uFlag) != 0;
}

void gp_state_unlock_set(uint16_t _uFlag, bool _bEnabled)
{
    uint16_t uOldFlags = m_uUnlockFlags;

    if (_bEnabled)
        m_uUnlockFlags |= _uFlag;
    else
        m_uUnlockFlags &= (uint16_t)~_uFlag;

    uint16_t uNewFlags = m_uUnlockFlags;

    /* If any weapon-related unlock flag changed, refresh weapons state.
     * This keeps icons, bullet sprites, and selection in sync with progression. */
    const uint16_t uWeaponMask = (uint16_t)(GP_UNLOCK_BULLETS_NORMAL | GP_UNLOCK_BULLETS_UPGRADED | GP_UNLOCK_LASER | GP_UNLOCK_BOMB);
    if (((uOldFlags ^ uNewFlags) & uWeaponMask) != 0)
    {
        weapons_refresh_state();
    }
}

uint16_t gp_state_currency_get(void)
{
    return m_uCurrency;
}

void gp_state_currency_set(uint16_t _uAmount)
{
    m_uCurrency = _uAmount;
}

gp_act_t gp_state_act_get(void)
{
    return gp_act_current;
}

void gp_state_act_set(gp_act_t _eAct)
{
    if (_eAct < ACT_COUNT)
        gp_act_current = _eAct;
}

float gp_state_get_best_lap_time(void)
{
    return m_fBestLapTime;
}

void gp_state_set_best_lap_time(float _fBestLapTime)
{
    m_fBestLapTime = _fBestLapTime;
}

/* Currency collection helpers - direct access to internal array */
currency_collection_entry_t *gp_state_get_currency_collection_array(void)
{
    return m_aCurrencyCollection;
}

struct vec2 gp_state_current_pos_get(void)
{
    /* Live query: return the authoritative position source per gameplay state */
    if (gp_state_current == JNR)
    {
        return player_jnr_get_position();
    }
    else if (gp_state_current == SURFACE)
    {
        return player_surface_get_position();
    }
    else
    {
        /* SPACE or PLANET */
        return ufo_get_position();
    }
}

void gp_state_current_pos_set(struct vec2 _vPos)
{
    /*  Live set: apply to the authoritative position sink per gameplay state.

        NOTE: If your surface/jnr player modules use different setter names or require
        additional side effects (e.g., syncing entity/collider), update the calls below.
        Keep this routing logic here.
    */
    if (gp_state_current == JNR)
    {
        player_jnr_set_position(_vPos);
    }
    else if (gp_state_current == SURFACE)
    {
        player_surface_set_position(_vPos);
    }
    else
    {
        /* SPACE or PLANET */
        ufo_set_position(_vPos);
    }
}

void gp_state_set(gp_state_t _eState)
{
    if (_eState < SPACE || _eState > JNR)
        return;

    /* Update previous state before changing current state */
    gp_state_previous = gp_state_current;

    gp_state_current = _eState;

    /* Keep internals coherent if called externally */
    m_transState = TRANS_NONE;
    m_targetState = gp_state_current;
    m_fHoldTimer = 0.0f;

    /* Update display name cache */
    const char *pFolder = get_layer_folder(gp_state_current);
    if (pFolder)
        get_display_name_from_folder(pFolder, m_szCurrentDisplayName, sizeof(m_szCurrentDisplayName));
    else
        m_szCurrentDisplayName[0] = '\0';

    m_pLastTriggerDisplayName = NULL;
    m_fCachedTriggerTextWidth = 0.0f;
}

bool gp_state_cutscene_get(void)
{
    return m_bCutsceneMode;
}

void gp_state_cutscene_set(bool _bActive)
{
    m_bCutsceneMode = _bActive;
}
