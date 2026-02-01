#include "gameplay_script.h"
#include "audio.h"
#include "dialogue.h"
#include "fade_manager.h"
#include "finish_slideshow.h"
#include "frame_time.h"
#include "game_objects/currency_handler.h"
#include "game_objects/gp_state.h"
#include "game_objects/npc_alien.h"
#include "game_objects/npc_handler.h"
#include "game_objects/race_handler.h"
#include "game_objects/ufo.h"
#include "libdragon.h"
#include "math2d.h"
#include "math_helper.h"
#include "menu.h"
#include "minimap_marker.h"
#include "path_mover.h"
#include "poi.h"
#include "satellite_pieces.h"
#include "save.h"
#include "script_handler.h"
#include "stick_calibration.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEV_BUILD
/* Compile-time check: verify all condition enum values are handled in script_condition_to_string().
 * This uses a static array that must be initialized with all enum values.
 * If you add/remove/reorder enum values in gameplay_script.h, you MUST update:
 * 1. The array below (add/remove the enum value)
 * 2. The switch statement in script_condition_to_string() (add/remove the case)
 * Otherwise you'll get a compile error about array size mismatch or missing case. */
static const script_condition_t _all_conditions[] = {SC_NONE,
                                                     SC_ANIM_FINISHED,
                                                     SC_DIALOGUE_FINISHED,
                                                     SC_TIMER,
                                                     SC_PATH_FINISHED,
                                                     SC_PATH_ACTIVE,
                                                     SC_NPC_TARGET_REACHED,
                                                     SC_ENTITY_DISTANCE,
                                                     SC_UFO_DISTANCE_NPC,
                                                     SC_SAVE_FLAG_SET,
                                                     SC_SAVE_FLAG_NOT_SET,
                                                     SC_NPC_SPAWNED,
                                                     SC_NPC_NOT_SPAWNED,
                                                     SC_FADE_FINISHED,
                                                     SC_RACE_FINISHED,
                                                     SC_RACE_WARMED_UP,
                                                     SC_ACT_IS,
                                                     SC_GP_STATE_IS,
                                                     SC_GP_STATE_WAS,
                                                     SC_SATELLITE_REPAIRED,
                                                     SC_CURRENCY_LE,
                                                     SC_CURRENCY_GE,
                                                     SC_CURRENCY_ALL_COLLECTED,
                                                     SC_RACE_TIME_LE,
                                                     SC_BULLETS_UNLOCKED,
                                                     SC_PIECE_OBTAINED,
                                                     SC_SOUND_FINISHED,
                                                     SC_CUSTOM};
/* Verify we have exactly 28 condition enum values (update this count if you add/remove) */
_Static_assert(sizeof(_all_conditions) / sizeof(_all_conditions[0]) == 28, "Condition enum count changed! Update _all_conditions array and script_condition_to_string() switch.");

/* Compile-time check: verify all action enum values are handled in script_action_to_string().
 * This uses a static array that must be initialized with all enum values.
 * If you add/remove/reorder enum values in gameplay_script.h, you MUST update:
 * 1. The array below (add/remove the enum value)
 * 2. The switch statement in script_action_to_string() (add/remove the case)
 * Otherwise you'll get a compile error about array size mismatch or missing case. */
static const script_action_t _all_actions[] = {SA_NONE,
                                               SA_START_ANIM,
                                               SA_END_ANIM,
                                               SA_START_DIALOGUE,
                                               SA_LOAD_PATH,
                                               SA_CONFIGURE_PATH,
                                               SA_START_PATH,
                                               SA_EXECUTE_PATH,
                                               SA_FREE_PATH,
                                               SA_SET_TARGET,
                                               SA_SET_TARGET_NPC,
                                               SA_OPEN_CALIBRATION,
                                               SA_CLOSE_CALIBRATION,
                                               SA_SET_MENU_STATE,
                                               SA_SET_SAVE_FLAG,
                                               SA_CLEAR_SAVE_FLAG,
                                               SA_SPAWN_NPC,
                                               SA_DESPAWN_NPC,
                                               SA_SET_NPC_DIRECT_TARGET,
                                               SA_FADE_TO_BLACK,
                                               SA_FADE_FROM_BLACK,
                                               SA_ENABLE_CUTSCENE,
                                               SA_DISABLE_CUTSCENE,
                                               SA_SET_MARKER,
                                               SA_SET_MARKER_TO_PIECE,
                                               SA_CLEAR_MARKER,
                                               SA_START_SCRIPT,
                                               SA_START_SCRIPT_PARALLEL,
                                               SA_STOP_SCRIPT,
                                               SA_WARMUP_RACE_TRACK,
                                               SA_START_RACE,
                                               SA_RESET_RACE_FINISHED,
                                               SA_SET_ACT,
                                               SA_FINISH_GAME,
                                               SA_SET_SPAWN,
                                               SA_SAVE_GAME,
                                               SA_CHANGE_CURRENCY,
                                               SA_CREATE_PIECE_AT_NPC,
                                               SA_CREATE_PIECE_AT_POI,
                                               SA_SPAWN_ASSEMBLE_PIECES,
                                               SA_PLAY_SOUND,
                                               SA_SKIP,
                                               SA_CALLBACK};
/* Verify we have exactly 43 action enum values (update this count if you add/remove) */
_Static_assert(sizeof(_all_actions) / sizeof(_all_actions[0]) == 43, "Action enum count changed! Update _all_actions array and script_action_to_string() switch.");

/* Helpers for readable debug output */
static const char *script_condition_to_string(script_condition_t condition)
{
    switch (condition)
    {
    case SC_NONE:
        return "SC_NONE";
    case SC_ANIM_FINISHED:
        return "SC_ANIM_FINISHED";
    case SC_DIALOGUE_FINISHED:
        return "SC_DIALOGUE_FINISHED";
    case SC_TIMER:
        return "SC_TIMER";
    case SC_PATH_FINISHED:
        return "SC_PATH_FINISHED";
    case SC_PATH_ACTIVE:
        return "SC_PATH_ACTIVE";
    case SC_NPC_TARGET_REACHED:
        return "SC_NPC_TARGET_REACHED";
    case SC_ENTITY_DISTANCE:
        return "SC_ENTITY_DISTANCE";
    case SC_UFO_DISTANCE_NPC:
        return "SC_UFO_DISTANCE_NPC";
    case SC_SAVE_FLAG_SET:
        return "SC_SAVE_FLAG_SET";
    case SC_SAVE_FLAG_NOT_SET:
        return "SC_SAVE_FLAG_NOT_SET";
    case SC_NPC_SPAWNED:
        return "SC_NPC_SPAWNED";
    case SC_NPC_NOT_SPAWNED:
        return "SC_NPC_NOT_SPAWNED";
    case SC_FADE_FINISHED:
        return "SC_FADE_FINISHED";
    case SC_RACE_FINISHED:
        return "SC_RACE_FINISHED";
    case SC_RACE_WARMED_UP:
        return "SC_RACE_WARMED_UP";
    case SC_ACT_IS:
        return "SC_ACT_IS";
    case SC_GP_STATE_IS:
        return "SC_GP_STATE_IS";
    case SC_GP_STATE_WAS:
        return "SC_GP_STATE_WAS";
    case SC_SATELLITE_REPAIRED:
        return "SC_SATELLITE_REPAIRED";
    case SC_CURRENCY_LE:
        return "SC_CURRENCY_LE";
    case SC_CURRENCY_GE:
        return "SC_CURRENCY_GE";
    case SC_CURRENCY_ALL_COLLECTED:
        return "SC_CURRENCY_ALL_COLLECTED";
    case SC_RACE_TIME_LE:
        return "SC_RACE_TIME_LE";
    case SC_BULLETS_UNLOCKED:
        return "SC_BULLETS_UNLOCKED";
    case SC_PIECE_OBTAINED:
        return "SC_PIECE_OBTAINED";
    case SC_SOUND_FINISHED:
        return "SC_SOUND_FINISHED";
    case SC_CUSTOM:
        return "SC_CUSTOM";
    default:
        return "SC_UNKNOWN";
    }
}

static const char *script_action_to_string(script_action_t action)
{
    switch (action)
    {
    case SA_NONE:
        return "SA_NONE";
    case SA_START_ANIM:
        return "SA_START_ANIM";
    case SA_END_ANIM:
        return "SA_END_ANIM";
    case SA_START_DIALOGUE:
        return "SA_START_DIALOGUE";
    case SA_LOAD_PATH:
        return "SA_LOAD_PATH";
    case SA_CONFIGURE_PATH:
        return "SA_CONFIGURE_PATH";
    case SA_START_PATH:
        return "SA_START_PATH";
    case SA_EXECUTE_PATH:
        return "SA_EXECUTE_PATH";
    case SA_FREE_PATH:
        return "SA_FREE_PATH";
    case SA_SET_TARGET:
        return "SA_SET_TARGET";
    case SA_SET_TARGET_NPC:
        return "SA_SET_TARGET_NPC";
    case SA_OPEN_CALIBRATION:
        return "SA_OPEN_CALIBRATION";
    case SA_CLOSE_CALIBRATION:
        return "SA_CLOSE_CALIBRATION";
    case SA_SET_MENU_STATE:
        return "SA_SET_MENU_STATE";
    case SA_SET_SAVE_FLAG:
        return "SA_SET_SAVE_FLAG";
    case SA_CLEAR_SAVE_FLAG:
        return "SA_CLEAR_SAVE_FLAG";
    case SA_SPAWN_NPC:
        return "SA_SPAWN_NPC";
    case SA_DESPAWN_NPC:
        return "SA_DESPAWN_NPC";
    case SA_SET_NPC_DIRECT_TARGET:
        return "SA_SET_NPC_DIRECT_TARGET";
    case SA_FADE_TO_BLACK:
        return "SA_FADE_TO_BLACK";
    case SA_FADE_FROM_BLACK:
        return "SA_FADE_FROM_BLACK";
    case SA_ENABLE_CUTSCENE:
        return "SA_ENABLE_CUTSCENE";
    case SA_DISABLE_CUTSCENE:
        return "SA_DISABLE_CUTSCENE";
    case SA_SET_MARKER:
        return "SA_SET_MARKER";
    case SA_SET_MARKER_TO_PIECE:
        return "SA_SET_MARKER_TO_PIECE";
    case SA_CLEAR_MARKER:
        return "SA_CLEAR_MARKER";
    case SA_START_SCRIPT:
        return "SA_START_SCRIPT";
    case SA_START_SCRIPT_PARALLEL:
        return "SA_START_SCRIPT_PARALLEL";
    case SA_STOP_SCRIPT:
        return "SA_STOP_SCRIPT";
    case SA_WARMUP_RACE_TRACK:
        return "SA_WARMUP_RACE_TRACK";
    case SA_START_RACE:
        return "SA_START_RACE";
    case SA_RESET_RACE_FINISHED:
        return "SA_RESET_RACE_FINISHED";
    case SA_SET_ACT:
        return "SA_SET_ACT";
    case SA_FINISH_GAME:
        return "SA_FINISH_GAME";
    case SA_SET_SPAWN:
        return "SA_SET_SPAWN";
    case SA_SAVE_GAME:
        return "SA_SAVE_GAME";
    case SA_CHANGE_CURRENCY:
        return "SA_CHANGE_CURRENCY";
    case SA_CREATE_PIECE_AT_NPC:
        return "SA_CREATE_PIECE_AT_NPC";
    case SA_CREATE_PIECE_AT_POI:
        return "SA_CREATE_PIECE_AT_POI";
    case SA_SPAWN_ASSEMBLE_PIECES:
        return "SA_SPAWN_ASSEMBLE_PIECES";
    case SA_PLAY_SOUND:
        return "SA_PLAY_SOUND";
    case SA_SKIP:
        return "SA_SKIP";
    case SA_CALLBACK:
        return "SA_CALLBACK";
    default:
        return "SA_UNKNOWN";
    }
}
#endif /* DEV_BUILD */

/* Static variable to track the last played script sound (for memory management) */
static wav64_t *s_pLastScriptSound = NULL;

/* Helper: Check if condition is met */
static bool script_check_condition(ScriptInstance *_pScript, script_condition_t _condition, script_param_t _params)
{
    switch (_condition)
    {
    case SC_NONE:
        return true; /* Immediate */

    case SC_ANIM_FINISHED:
        return !ufo_is_transition_playing();

    case SC_DIALOGUE_FINISHED:
        return !dialogue_is_active();

    case SC_TIMER:
        _pScript->timer_accum += frame_time_delta_seconds();
        return _pScript->timer_accum >= _params.timer_param.duration;

    case SC_PATH_FINISHED:
        if (_params.path_param.path)
        {
            return path_mover_get_state(_params.path_param.path) == PATH_STATE_FINISHED;
        }
        return false;

    case SC_PATH_ACTIVE:
    {
        /* Check if NPC has an active path (playing or paused) */
        if (_params.path_param.npc_type < NPC_TYPE_COUNT)
        {
            PathInstance **ppPath = npc_handler_get_path_ptr(_params.path_param.npc_type);
            if (ppPath && *ppPath)
            {
                path_state_t state = path_mover_get_state(*ppPath);
                return state == PATH_STATE_PLAYING || state == PATH_STATE_PAUSED;
            }
        }
        return false;
    }

    case SC_NPC_TARGET_REACHED:
    {
        /* Get NPC instance from npc_type */
        if (_params.path_param.npc_type < NPC_TYPE_COUNT)
        {
            NpcAlienInstance *pInstance = npc_handler_get_instance(_params.path_param.npc_type);
            if (pInstance)
            {
                return npc_alien_get_reached_target(pInstance);
            }
        }
        return false;
    }

    case SC_ENTITY_DISTANCE:
        /* Only check distance if: no dialogue is running, no menu is open (including upgrade shop), and no race is running */
        if (dialogue_is_active())
            return false;
        if (race_handler_is_race_active())
            return false;

        if (_params.distance_param.entity)
        {
            struct vec2 vUfoPos = ufo_get_position();
            float fDistance = vec2_dist(vUfoPos, _params.distance_param.entity->vPos);
            return fDistance <= _params.distance_param.distance;
        }
        return false;

    case SC_UFO_DISTANCE_NPC:
        /* Only check distance if: no dialogue is running, no menu is open (including upgrade shop), and no race is running */
        if (dialogue_is_active())
            return false;
        if (race_handler_is_race_active())
            return false;
        if (ufo_is_transition_playing())
            return false;

        if (_params.distance_param.npc_type < NPC_TYPE_COUNT)
        {
            const struct entity2D *pEntity = npc_handler_get_entity(_params.distance_param.npc_type);
            if (pEntity)
            {
                struct vec2 vUfoPos = ufo_get_position();
                float fDistance = vec2_dist(vUfoPos, pEntity->vPos);
                return fDistance <= _params.distance_param.distance;
            }
        }
        return false;

    case SC_SAVE_FLAG_SET:
        return gp_state_unlock_get((uint16_t)_params.flag_param.flag_index);

    case SC_SAVE_FLAG_NOT_SET:
        return !gp_state_unlock_get((uint16_t)_params.flag_param.flag_index);

    case SC_NPC_SPAWNED:
        return npc_handler_is_spawned(_params.npc_param.type);

    case SC_NPC_NOT_SPAWNED:
        return !npc_handler_is_spawned(_params.npc_param.type);

    case SC_FADE_FINISHED:
        return !fade_manager_is_busy();

    case SC_RACE_FINISHED:
        return race_handler_was_started_and_finished();

    case SC_RACE_WARMED_UP:
        return race_handler_is_initialized();

    case SC_ACT_IS:
        return gp_state_act_get() == (gp_act_t)_params.act_param.act;

    case SC_GP_STATE_IS:
        return gp_state_get() == (gp_state_t)_params.gp_state_param.state;

    case SC_GP_STATE_WAS:
        return gp_state_get_previous() == (gp_state_t)_params.gp_state_param.state;

    case SC_SATELLITE_REPAIRED:
        return satellite_pieces_bSatelliteRepaired();

    case SC_CURRENCY_LE:
        return gp_state_currency_get() <= (uint16_t)_params.currency_param.threshold;

    case SC_CURRENCY_GE:
        return gp_state_currency_get() >= (uint16_t)_params.currency_param.threshold;

    case SC_CURRENCY_ALL_COLLECTED:
        return currency_handler_is_all_collected();

    case SC_RACE_TIME_LE:
    {
        float fBestLapTime = gp_state_get_best_lap_time();
        float fThreshold = _params.timer_param.duration; /* Use timer_param for threshold (float, seconds) */
        return fBestLapTime > 0.0f && fBestLapTime <= fThreshold;
    }

    case SC_BULLETS_UNLOCKED:
        return gp_state_unlock_get(GP_UNLOCK_BULLETS_NORMAL) || gp_state_unlock_get(GP_UNLOCK_BULLETS_UPGRADED);

    case SC_PIECE_OBTAINED:
        return gp_state_unlock_get((uint16_t)_params.flag_param.flag_index);

    case SC_SOUND_FINISHED:
    {
        /* Check if sound is finished playing on the specified channel */
        bool bFinished = !mixer_ch_playing(_params.sound_param.channel);
        /* If finished, free the sound */
        if (bFinished && s_pLastScriptSound)
        {
            wav64_close(s_pLastScriptSound);
            s_pLastScriptSound = NULL;
        }
        return bFinished;
    }

    case SC_CUSTOM:
        if (_params.callback_param.callback)
        {
            int (*callback)(void *) = (int (*)(void *))_params.callback_param.callback;
            return callback(_params.callback_param.user_data) != 0;
        }
        return false;

    default:
        return false;
    }
}

/* Helper: Execute action */
static bool script_execute_action(ScriptInstance *_pScript, script_action_t _action, script_param_t _params)
{
    switch (_action)
    {
    case SA_NONE:
        return false;

    case SA_START_ANIM:
        ufo_start_transition_animation(_params.anim_param.from_state, _params.anim_param.to_state);
        return false;

    case SA_END_ANIM:
        ufo_end_transition_animation(_params.anim_param.to_state);
        return false;

    case SA_START_DIALOGUE:
        if (_params.str_param.str)
        {
            dialogue_start(_params.str_param.str);
        }
        return false;

    case SA_LOAD_PATH:
        if (_params.path_param.path_name && _params.path_param.npc_type < NPC_TYPE_COUNT)
        {
            NpcAlienInstance *pInstance = npc_handler_get_instance(_params.path_param.npc_type);
            if (pInstance)
            {
                PathInstance *pPath = path_mover_load(_params.path_param.path_name);
                if (pPath)
                {
                    /* Set path and position entity at path start */
                    npc_alien_set_path(pInstance, pPath, true, _params.path_param.wait_for_player);
                }
            }
        }
        return false;

    case SA_CONFIGURE_PATH:
        /* Uses callback to configure path */
        if (_params.callback_param.callback && _params.path_param.npc_type < NPC_TYPE_COUNT)
        {
            NpcAlienInstance *pInstance = npc_handler_get_instance(_params.path_param.npc_type);
            if (pInstance)
            {
                PathInstance **ppPath = npc_alien_get_path_ptr(pInstance);
                if (ppPath && *ppPath)
                {
                    _params.callback_param.callback(*ppPath);
                }
            }
        }
        return false;

    case SA_START_PATH:
        if (_params.path_param.npc_type < NPC_TYPE_COUNT)
        {
            NpcAlienInstance *pInstance = npc_handler_get_instance(_params.path_param.npc_type);
            if (pInstance)
            {
                PathInstance **ppPath = npc_alien_get_path_ptr(pInstance);
                if (ppPath && *ppPath)
                {
                    path_mover_start(*ppPath);
                }
            }
        }
        return false;

    case SA_EXECUTE_PATH:
        /* Load, configure, and start path in one action */
        if (_params.path_param.path_name && _params.path_param.npc_type < NPC_TYPE_COUNT)
        {
            NpcAlienInstance *pInstance = npc_handler_get_instance(_params.path_param.npc_type);
            if (pInstance)
            {
                /* Load path */
                PathInstance *pPath = path_mover_load(_params.path_param.path_name);
                if (pPath)
                {
                    /* Set path and position entity at path start */
                    npc_alien_set_path(pInstance, pPath, true, _params.path_param.wait_for_player);

                    /* Auto-configure path based on NPC type */
                    npc_alien_configure_path_by_type(pPath, _params.path_param.npc_type);

                    /* Configure path if callback provided (allows override of auto-config) */
                    if (_params.path_param.configure_callback)
                    {
                        _params.path_param.configure_callback(pPath);
                    }

                    /* Start the path */
                    path_mover_start(pPath);
                }
            }
        }
        return false;

    case SA_FREE_PATH:
        if (_params.path_param.npc_type < NPC_TYPE_COUNT)
        {
            NpcAlienInstance *pInstance = npc_handler_get_instance(_params.path_param.npc_type);
            if (pInstance)
            {
                /* Clear path (frees it and resets bReachedTarget) */
                npc_alien_set_path(pInstance, NULL, false, false);
            }
        }
        return false;

    case SA_SET_TARGET:
        ufo_set_next_target(_params.entity_param.entity);
        return false;

    case SA_SET_TARGET_NPC:
        if (_params.npc_param.type < NPC_TYPE_COUNT)
        {
            const struct entity2D *pEntity = npc_handler_get_entity(_params.npc_param.type);
            ufo_set_next_target(pEntity);
        }
        return false;

    case SA_OPEN_CALIBRATION:
        stick_calibration_init_without_menu();
        return false;

    case SA_CLOSE_CALIBRATION:
        stick_calibration_close();
        return false;

    case SA_SET_MENU_STATE:
        menu_set_state((eMenuState)_params.menu_param.state);
        return false;

    case SA_SET_SAVE_FLAG:
        gp_state_unlock_set((uint16_t)_params.flag_param.flag_index, true);
        return false;

    case SA_CLEAR_SAVE_FLAG:
        gp_state_unlock_set((uint16_t)_params.flag_param.flag_index, false);
        return false;

    case SA_SPAWN_NPC:
        npc_handler_spawn(_params.npc_param.type);
        return false;

    case SA_DESPAWN_NPC:
        npc_handler_despawn(_params.npc_param.type);
        return false;

    case SA_SET_NPC_DIRECT_TARGET:
    {
        if (_params.npc_direct_target_param.type < NPC_TYPE_COUNT && _params.npc_direct_target_param.poi_name)
        {
            NpcAlienInstance *pInstance = npc_handler_get_instance(_params.npc_direct_target_param.type);
            if (pInstance)
            {
                struct vec2 vTarget;
                if (poi_load(_params.npc_direct_target_param.poi_name, &vTarget, NULL))
                {
                    npc_alien_set_direct_target(pInstance, vTarget, _params.npc_direct_target_param.wait_for_player);
                }
            }
        }
        return false;
    }

    case SA_FADE_TO_BLACK:
        fade_manager_start(TO_BLACK);
        return false;

    case SA_FADE_FROM_BLACK:
        fade_manager_start(FROM_BLACK);
        return false;

    case SA_ENABLE_CUTSCENE:
        gp_state_cutscene_set(true);
        return false;

    case SA_DISABLE_CUTSCENE:
        gp_state_cutscene_set(false);
        return false;

    case SA_SET_MARKER:
        if (_params.marker_param.name)
        {
            const struct entity2D *pMarkerEntity = minimap_marker_set(_params.marker_param.name, _params.marker_param.type);

            /* Auto-set as UFO next target if requested */
            if (_params.marker_param.auto_set_target && pMarkerEntity)
            {
                ufo_set_next_target(pMarkerEntity);
            }
        }
        return false;

    case SA_SET_MARKER_TO_PIECE:
    {
        /* Set marker linked to piece by unlock flag (marker will track piece position) */
        const struct entity2D *pMarkerEntity = minimap_marker_set_piece(_params.marker_to_piece_param.unlock_flag);

        /* Auto-set as UFO next target if requested */
        if (_params.marker_to_piece_param.auto_set_target && pMarkerEntity)
        {
            ufo_set_next_target(pMarkerEntity);
        }
        return false;
    }

    case SA_CLEAR_MARKER:
        if (_params.marker_param.name)
        {
            minimap_marker_clear(_params.marker_param.name);
        }
        return false;

    case SA_START_SCRIPT:
        if (_params.str_param.str)
        {
            script_handler_start(_params.str_param.str, true);
        }
        return true;

    case SA_START_SCRIPT_PARALLEL:
        if (_params.str_param.str)
        {
            script_handler_start(_params.str_param.str, false);
        }
        return false; /* Return false so the step advances (parallel scripts don't replace current script) */

    case SA_STOP_SCRIPT:
        if (_pScript)
        {
            script_stop(_pScript);
        }
        return false;

    case SA_WARMUP_RACE_TRACK:
        if (_params.race_warmup_param.race_name)
        {
            race_handler_init(_params.race_warmup_param.race_name,
                              _params.race_warmup_param.coins_per_lap,
                              _params.race_warmup_param.coin_turbo_burst_duration_ms,
                              _params.race_warmup_param.max_laps);
        }
        return false;

    case SA_START_RACE:
        race_handler_start_race();
        return false;

    case SA_RESET_RACE_FINISHED:
        race_handler_reset_finished_flag();
        return false;

    case SA_SET_ACT:
        gp_state_act_set((gp_act_t)_params.act_param.act);
        return false;

    case SA_FINISH_GAME:
        finish_slideshow_init();
        return false;

    case SA_SET_SPAWN:
        if (_params.str_param.str)
        {
            /* Set UFO position from folder's logic.csv file */
            ufo_set_position_from_data(_params.str_param.str);
            /* Reset camera and starfield to prevent visual jumps */
            gp_state_snap_space_transition();
        }
        return false;

    case SA_SAVE_GAME:
        /* Sync current game state to save data */
        save_sync_gp_state();
        /* Write save data to EEPROM */
        save_write();
        return false;

    case SA_CHANGE_CURRENCY:
    {
        int32_t delta = _params.currency_param.delta;
        uint16_t uCurrentCurrency = gp_state_currency_get();
        int32_t iNewCurrency = (int32_t)uCurrentCurrency + delta;
        /* Clamp to uint16_t range (0 to 65535) */
        if (iNewCurrency < 0)
            iNewCurrency = 0;
        else if (iNewCurrency > 65535)
            iNewCurrency = 65535;
        gp_state_currency_set((uint16_t)iNewCurrency);
        return false;
    }

    case SA_CREATE_PIECE_AT_NPC:
        if (_params.create_piece_param.npc_type < NPC_TYPE_COUNT)
        {
            NpcAlienInstance *pInstance = npc_handler_get_instance(_params.create_piece_param.npc_type);
            if (pInstance)
            {
                const struct entity2D *pEntity = npc_alien_get_entity(pInstance);
                if (pEntity)
                {
                    satellite_pieces_create(_params.create_piece_param.unlock_flag, pEntity->vPos, false);
                }
            }
        }
        return false;

    case SA_CREATE_PIECE_AT_POI:
        if (_params.create_piece_at_poi_param.poi_name)
        {
            struct vec2 vPos;
            if (poi_load(_params.create_piece_at_poi_param.poi_name, &vPos, NULL))
            {
                satellite_pieces_create(_params.create_piece_at_poi_param.unlock_flag, vPos, false);
            }
        }
        return false;

    case SA_SPAWN_ASSEMBLE_PIECES:
        satellite_pieces_spawn_assemble_pieces();
        return false;

    case SA_PLAY_SOUND:
        if (_params.sound_param.sound_path)
        {
            /* Free previous sound if it exists and is not playing */
            if (s_pLastScriptSound && !mixer_ch_playing(_params.sound_param.channel))
            {
                wav64_close(s_pLastScriptSound);
                s_pLastScriptSound = NULL;
            }

            /* Load and play sound on specified channel */
            wav64_t *pSound = wav64_load(_params.sound_param.sound_path, &(wav64_loadparms_t){.streaming_mode = 0});
            if (pSound)
            {
                wav64_set_loop(pSound, false);
                /* Stop any currently playing sound on the channel */
                if (mixer_ch_playing(_params.sound_param.channel))
                {
                    mixer_ch_stop(_params.sound_param.channel);
                    /* Free the old sound if it was the last script sound */
                    if (s_pLastScriptSound)
                    {
                        wav64_close(s_pLastScriptSound);
                        s_pLastScriptSound = NULL;
                    }
                }
                wav64_play(pSound, _params.sound_param.channel);
                s_pLastScriptSound = pSound;
            }
        }
        return false;

    case SA_SKIP:
        /* Skip this step (no-op, just advance) */
        return false;

    case SA_CALLBACK:
        if (_params.callback_param.callback)
        {
            _params.callback_param.callback(_params.callback_param.user_data);
        }
        return false;

    default:
        return false;
    }

    return false;
}

/* Public API Implementation */

ScriptInstance *script_create(void)
{
    ScriptInstance *pScript = (ScriptInstance *)calloc(1, sizeof(ScriptInstance));
    if (!pScript)
        return NULL;

    pScript->step_count = 0;
    pScript->current_step = 0;
    pScript->active = false;
    pScript->timer_accum = 0.0f;
    pScript->last_timer_step = UINT16_MAX;
#ifdef DEV_BUILD
    pScript->last_condition_result = false;
    pScript->last_logged_step = UINT16_MAX;
#endif

    return pScript;
}

void script_destroy(ScriptInstance *_pScript)
{
    if (!_pScript)
        return;

    free(_pScript);
}

void script_add_step(ScriptInstance *_pScript, script_condition_t _condition, script_param_t _condition_params, script_action_t _action, script_param_t _action_params,
                     script_action_t _else_action, script_param_t _else_action_params)
{
    if (!_pScript || _pScript->step_count >= SCRIPT_MAX_STEPS)
    {
        debugf("[ERROR] script_add_step: script is full (step count: %d, max steps: %d)\n", _pScript->step_count, SCRIPT_MAX_STEPS);
        return;
    }

    script_step_t *pStep = &_pScript->steps[_pScript->step_count];
    pStep->condition = _condition;
    pStep->condition_params = _condition_params;
    pStep->action = _action;
    pStep->action_params = _action_params;
    pStep->else_action = _else_action;
    pStep->else_action_params = _else_action_params;

    _pScript->step_count++;
}

void script_start(ScriptInstance *_pScript)
{
    if (!_pScript)
        return;

    _pScript->active = true;
    _pScript->current_step = 0;
    _pScript->timer_accum = 0.0f;
    _pScript->last_timer_step = UINT16_MAX;
#ifdef DEV_BUILD
    _pScript->last_condition_result = false;
    _pScript->last_logged_step = UINT16_MAX;
#endif
}

void script_stop(ScriptInstance *_pScript)
{
    if (!_pScript)
        return;

    _pScript->active = false;
}

void script_update(ScriptInstance *_pScript)
{
    if (!_pScript || !_pScript->active)
        return;

    /* Process steps in a loop - continue as long as we can advance immediately (no waiting) */
    while (_pScript->active && _pScript->current_step < _pScript->step_count)
    {
        script_step_t *pStep = &_pScript->steps[_pScript->current_step];

        /* Reset timer when we start waiting on a timer condition */
        if (pStep->condition == SC_TIMER)
        {
            /* If this is a different step than last time, reset the timer */
            if (_pScript->last_timer_step != _pScript->current_step)
            {
                _pScript->timer_accum = 0.0f;
                _pScript->last_timer_step = _pScript->current_step;
            }
        }
        else
        {
            /* Not a timer step: always clear tracking so next timer step resets */
            _pScript->last_timer_step = UINT16_MAX;
        }

        /* Check condition */
        uint32_t uGenerationBefore = script_handler_get_generation();
        bool bConditionMet = script_check_condition(_pScript, pStep->condition, pStep->condition_params);
        if (uGenerationBefore != script_handler_get_generation())
        {
            /* Script was replaced during condition evaluation (eg SC_CUSTOM), stop this update */
            return;
        }

#ifdef DEV_BUILD
        /* Only log condition check if result changed or if this is the first check for this step */
        bool bShouldLog = false;
        if (_pScript->last_logged_step != _pScript->current_step)
        {
            /* First check for this step - always log */
            bShouldLog = true;
            _pScript->last_logged_step = _pScript->current_step;
        }
        else if (_pScript->last_condition_result != bConditionMet)
        {
            /* Result changed - log the change */
            bShouldLog = true;
        }

        if (bShouldLog)
        {
            script_handler_debug_log(_pScript->debug_name, _pScript, "COND ", "check %s -> %s", script_condition_to_string(pStep->condition), bConditionMet ? "true" : "false");
            _pScript->last_condition_result = bConditionMet;
        }
#endif

        if (bConditionMet)
        {
            /* Condition met: execute action */
#ifdef DEV_BUILD
            script_handler_debug_log(_pScript->debug_name, _pScript, "ACT  ", "action %s", script_action_to_string(pStep->action));
#endif
            bool bStartedNewScript = script_execute_action(_pScript, pStep->action, pStep->action_params);
            if (bStartedNewScript)
            {
                return;
            }

            /* Move to next step (only if script is still active - it may have been destroyed by SA_START_SCRIPT) */
            if (_pScript->active)
            {
#ifdef DEV_BUILD
                script_handler_debug_log(_pScript->debug_name, _pScript, "STEP ", "advance to next step");
                /* Reset condition tracking for new step */
                _pScript->last_logged_step = UINT16_MAX;
#endif
                _pScript->current_step++;
            }
        }
        else if (pStep->else_action != SA_NONE)
        {
            /* Condition not met but else action exists: execute else action */
#ifdef DEV_BUILD
            script_handler_debug_log(_pScript->debug_name, _pScript, "ELSE ", "else %s", script_action_to_string(pStep->else_action));
#endif
            bool bStartedNewScript = script_execute_action(_pScript, pStep->else_action, pStep->else_action_params);
            if (bStartedNewScript)
            {
                return;
            }
            /* Move to next step (only if script is still active - it may have been destroyed by SA_START_SCRIPT) */
            if (_pScript->active)
            {
#ifdef DEV_BUILD
                script_handler_debug_log(_pScript->debug_name, _pScript, "STEP ", "advance to next step");
                /* Reset condition tracking for new step */
                _pScript->last_logged_step = UINT16_MAX;
#endif
                _pScript->current_step++;
            }
        }
        else if (pStep->action == SA_NONE && pStep->else_action == SA_NONE)
        {
            /* Both action and else_action are SA_NONE: invalid state, script deadlock (should never happen) */
            debugf("[ERROR] Script step has both action and else_action as SA_NONE - script will never advance!\n");
            /* Don't advance step - break to stop processing */
            break;
        }
        else
        {
            /* Condition not met, no else action, but action is not SA_NONE: this is WAIT/WAIT_THEN - wait for condition
             * Keep waiting (don't advance step) - break to stop processing this frame */
            break;
        }
    }

    /* Check if script finished */
    if (_pScript->current_step >= _pScript->step_count)
    {
        /* Script finished */
        _pScript->active = false;
    }
}

bool script_is_active(ScriptInstance *_pScript)
{
    return _pScript && _pScript->active;
}
