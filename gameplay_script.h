#pragma once

#include "entity2d.h"
#include "game_objects/gp_state.h"
#include "game_objects/npc_handler.h"
#include "minimap_marker.h"
#include "path_mover.h"
#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct PathInstance PathInstance;
typedef struct entity2D entity2D;

/* Condition Types */
typedef enum
{
    SC_NONE,                   /* No condition (immediate) */
    SC_ANIM_FINISHED,          /* UFO animation finished */
    SC_DIALOGUE_FINISHED,      /* Dialogue finished */
    SC_TIMER,                  /* Timer elapsed */
    SC_PATH_FINISHED,          /* Path finished */
    SC_PATH_ACTIVE,            /* NPC has an active path (playing or paused) */
    SC_NPC_TARGET_REACHED,     /* NPC reached target (direct or path) */
    SC_ENTITY_DISTANCE,        /* Entity within distance */
    SC_UFO_DISTANCE_NPC,       /* UFO (player) within distance of NPC (NPC retrieved at execution time) */
    SC_SAVE_FLAG_SET,          /* Save flag is set */
    SC_SAVE_FLAG_NOT_SET,      /* Save flag is not set */
    SC_NPC_SPAWNED,            /* NPC type is spawned */
    SC_NPC_NOT_SPAWNED,        /* NPC type is not spawned */
    SC_FADE_FINISHED,          /* Fade finished */
    SC_RACE_FINISHED,          /* Race was started and finished */
    SC_RACE_WARMED_UP,         /* Race handler is initialized/warmed up */
    SC_ACT_IS,                 /* Current act matches specified act */
    SC_GP_STATE_IS,            /* Current gp_state matches specified state */
    SC_GP_STATE_WAS,           /* Previous gp_state matches specified state */
    SC_SATELLITE_REPAIRED,     /* Satellite has been repaired */
    SC_CURRENCY_LE,            /* Currency <= threshold */
    SC_CURRENCY_GE,            /* Currency >= threshold */
    SC_CURRENCY_ALL_COLLECTED, /* All currency collected for current folder */
    SC_RACE_TIME_LE,           /* Total race time <= threshold (seconds) */
    SC_BULLETS_UNLOCKED,       /* Bullets are unlocked (normal or upgraded) */
    SC_PIECE_OBTAINED,         /* Piece has been obtained */
    SC_SOUND_FINISHED,         /* Sound finished playing on specified channel */
    SC_CUSTOM                  /* Custom callback */
} script_condition_t;

/* Action Types */
typedef enum
{
    SA_NONE,                  /* No action */
    SA_START_ANIM,            /* Start UFO animation */
    SA_END_ANIM,              /* End UFO animation */
    SA_START_DIALOGUE,        /* Start dialogue */
    SA_LOAD_PATH,             /* Load path from name */
    SA_CONFIGURE_PATH,        /* Configure path with settings (uses callback) */
    SA_START_PATH,            /* Start path mover */
    SA_EXECUTE_PATH,          /* Load, configure, and start path in one action */
    SA_FREE_PATH,             /* Free path mover */
    SA_SET_TARGET,            /* Set UFO target */
    SA_SET_TARGET_NPC,        /* Set UFO target to NPC entity (retrieved at execution time) */
    SA_OPEN_CALIBRATION,      /* Open calibration screen */
    SA_CLOSE_CALIBRATION,     /* Close calibration screen */
    SA_SET_MENU_STATE,        /* Set menu state */
    SA_SET_SAVE_FLAG,         /* Set save flag */
    SA_CLEAR_SAVE_FLAG,       /* Clear save flag */
    SA_SPAWN_NPC,             /* Spawn NPC by type */
    SA_DESPAWN_NPC,           /* Despawn NPC by type */
    SA_SET_NPC_DIRECT_TARGET, /* Set NPC direct target from POI */
    SA_FADE_TO_BLACK,         /* Fade to black */
    SA_FADE_FROM_BLACK,       /* Fade from black */
    SA_ENABLE_CUTSCENE,       /* Enable cutscene mode (blocks gameplay input) */
    SA_DISABLE_CUTSCENE,      /* Disable cutscene mode (allows gameplay input) */
    SA_SET_MARKER,            /* Set minimap marker */
    SA_SET_MARKER_TO_PIECE,   /* Set minimap marker to piece position (if piece exists/is active) */
    SA_CLEAR_MARKER,          /* Clear minimap marker */
    SA_START_SCRIPT,          /* Start another script by name */
    SA_START_SCRIPT_PARALLEL, /* Start another script by name (parallel, doesn't stop other scripts) */
    SA_STOP_SCRIPT,           /* Stop the current script */
    SA_WARMUP_RACE_TRACK,     /* Warm up race track (initialize without starting race) */
    SA_START_RACE,            /* Start the race (no parameters needed) */
    SA_RESET_RACE_FINISHED,   /* Reset the race finished flag (call after detecting race finish) */
    SA_SET_ACT,               /* Set game act */
    SA_FINISH_GAME,           /* Finish the game */
    SA_SET_SPAWN,             /* Set UFO spawn position from folder's logic.csv and reset camera/starfield */
    SA_SAVE_GAME,             /* Save game state to EEPROM */
    SA_CHANGE_CURRENCY,       /* Add/remove currency (positive to add, negative to remove) */
    SA_CREATE_PIECE_AT_NPC,   /* Create satellite piece at NPC position */
    SA_CREATE_PIECE_AT_POI,   /* Create satellite piece at POI position */
    SA_SPAWN_ASSEMBLE_PIECES, /* Spawn all four satellite pieces around UFO in assemble mode */
    SA_PLAY_SOUND,            /* Play sound file on specified channel */
    SA_SKIP,                  /* Skip this step (no-op, used as else_action to indicate skip behavior) */
    SA_CALLBACK               /* Custom callback */
} script_action_t;

/* Parameter Union */
typedef union
{
    /* String parameters */
    struct
    {
        const char *str;
    } str_param;

    /* Entity parameters */
    struct
    {
        const struct entity2D *entity;
    } entity_param;

    /* Path parameters */
    struct
    {
        PathInstance *path;                         /* For actions that use existing path (optional) */
        const char *path_name;                      /* For LOAD_PATH/EXECUTE_PATH action */
        npc_type_t npc_type;                        /* NPC type to load path for (NPC_TYPE_COUNT = invalid) */
        void (*configure_callback)(PathInstance *); /* For EXECUTE_PATH: callback to configure path */
        bool wait_for_player;                       /* Whether to wait for player when using path */
    } path_param;

    /* Animation parameters */
    struct
    {
        gp_state_t from_state;
        gp_state_t to_state;
    } anim_param;

    /* Timer parameters */
    struct
    {
        float duration;
    } timer_param;

    /* Distance parameters */
    struct
    {
        const struct entity2D *entity; /* Direct entity pointer (for SC_ENTITY_DISTANCE) */
        npc_type_t npc_type;           /* NPC type to lookup (for SC_UFO_DISTANCE_NPC, NPC_TYPE_COUNT = invalid) */
        float distance;
    } distance_param;

    /* Save flag parameters */
    struct
    {
        uint32_t flag_index; /* Or use string ID if you have flag names */
    } flag_param;

    /* Item count parameters */
    struct
    {
        uint32_t item_type; /* Item type identifier */
        uint32_t threshold; /* Count threshold */
    } item_count_param;

    /* Menu state parameters */
    struct
    {
        int state; /* eMenuState - using int to avoid circular dependency */
    } menu_param;

    /* Custom callback */
    struct
    {
        void (*callback)(void *user_data);
        void *user_data;
    } callback_param;

    /* NPC parameters */
    struct
    {
        npc_type_t type; /* NPC type enum */
    } npc_param;

    /* NPC direct target parameters */
    struct
    {
        npc_type_t type;      /* NPC type enum */
        const char *poi_name; /* POI name to load */
        bool wait_for_player; /* Whether to wait for player */
    } npc_direct_target_param;

    /* Marker parameters */
    struct
    {
        const char *name;           /* Marker name (for POI loading/clearing) */
        minimap_marker_type_t type; /* Marker type enum */
        bool auto_set_target;       /* Whether to automatically set marker as UFO next target */
    } marker_param;

    /* Race warmup parameters */
    struct
    {
        const char *race_name;              /* Race name to load from race.csv */
        uint16_t coins_per_lap;             /* Number of coins per lap */
        float coin_turbo_burst_duration_ms; /* Turbo burst duration when coin is collected (ms) */
        uint16_t max_laps;                  /* Maximum number of laps required */
    } race_warmup_param;

    /* Act parameters */
    struct
    {
        uint8_t act; /* gp_act_t - using uint8_t to avoid circular dependency */
    } act_param;

    /* gp_state parameters */
    struct
    {
        uint8_t state; /* gp_state_t - using uint8_t to avoid circular dependency */
    } gp_state_param;

    /* Currency parameters */
    struct
    {
        uint32_t threshold; /* Currency threshold value (for conditions) */
        int32_t delta;      /* Currency delta value (for actions: positive to add, negative to remove) */
    } currency_param;

    /* Create piece at NPC parameters */
    struct
    {
        npc_type_t npc_type;  /* NPC type to get position from */
        uint16_t unlock_flag; /* Unlock flag for the piece (GP_UNLOCK_PIECE_A, etc.) */
    } create_piece_param;

    /* Create piece at POI parameters */
    struct
    {
        const char *poi_name; /* POI name to get position from */
        uint16_t unlock_flag; /* Unlock flag for the piece (GP_UNLOCK_PIECE_A, etc.) */
    } create_piece_at_poi_param;

    /* Set marker to piece parameters */
    struct
    {
        uint16_t unlock_flag; /* Unlock flag for the piece (GP_UNLOCK_PIECE_A, etc.) */
        bool auto_set_target; /* Whether to automatically set marker as UFO next target */
    } marker_to_piece_param;

    /* Sound parameters */
    struct
    {
        const char *sound_path; /* Path to sound file (e.g., "rom:/crankhorn_installed.wav64") */
        int channel;            /* Mixer channel to play on (MIXER_CHANNEL_WEAPONS, etc.) */
    } sound_param;
} script_param_t;

/* Script Step */
typedef struct
{
    script_condition_t condition;    /* What to wait for */
    script_param_t condition_params; /* Parameters for condition evaluation */
    script_action_t action;          /* What to do when condition is met */
    script_param_t action_params;    /* Parameters for action execution */

    /* Optional else branch: executed when condition is NOT met */
    script_action_t else_action;       /* Action when condition fails (SA_NONE = no else) */
    script_param_t else_action_params; /* Parameters for else action */
} script_step_t;

/* Script Instance */
#define SCRIPT_MAX_STEPS 48

typedef struct ScriptInstance
{
    script_step_t steps[SCRIPT_MAX_STEPS];
    uint16_t step_count;
    uint16_t current_step;
    bool active;

    /* Internal state for conditions */
    float timer_accum;        /* Timer accumulator */
    uint16_t last_timer_step; /* Last step index that used timer (for reset detection) */

#ifdef DEV_BUILD
    /* Debug logging state: track last condition result to avoid verbose logging */
    bool last_condition_result; /* Last condition result for current step */
    uint16_t last_logged_step;  /* Step index for which last_condition_result is valid */

    /* Optional debug name (set by script handler) */
    const char *debug_name;
#endif
} ScriptInstance;

/* Script context helpers to avoid repeating the ScriptInstance pointer */
#define SCRIPT_BEGIN()                                                                                                                                                             \
    ScriptInstance *script_ctx = script_create();                                                                                                                                  \
    if (!script_ctx)                                                                                                                                                               \
    return NULL
#define SCRIPT_END() return script_ctx
#define NO_PARAMS ((script_param_t){0})
/* Script step macros:
 *   STEP - Execute action immediately (no condition)
 *   WAIT_THEN - Wait for condition, then execute action (blocks until condition is true)
 *   WAIT - Wait for condition, then advance (blocks until condition is true, no action)
 *   IF - Check condition: if true execute action, if false skip (non-blocking)
 *   IF_ELSE - Check condition: if true execute action, if false execute else_action (non-blocking)
 *   IF_NOT - Check condition: if false execute action, if true skip (non-blocking, inverted logic)
 */
#define STEP(_action, _action_params) script_add_step(script_ctx, SC_NONE, NO_PARAMS, (_action), (_action_params), SA_NONE, NO_PARAMS)
#define WAIT_THEN(_cond, _cond_params, _action, _action_params) script_add_step(script_ctx, (_cond), (_cond_params), (_action), (_action_params), SA_NONE, NO_PARAMS)
#define WAIT(_cond, _cond_params) script_add_step(script_ctx, (_cond), (_cond_params), SA_SKIP, NO_PARAMS, SA_NONE, NO_PARAMS)
#define IF(_cond, _cond_params, _action, _action_params) script_add_step(script_ctx, (_cond), (_cond_params), (_action), (_action_params), SA_SKIP, NO_PARAMS)
#define IF_ELSE(_cond, _cond_params, _action, _action_params, _else_action, _else_action_params)                                                                                   \
    script_add_step(script_ctx, (_cond), (_cond_params), (_action), (_action_params), (_else_action), (_else_action_params))
#define IF_NOT(_cond, _cond_params, _action, _action_params) script_add_step(script_ctx, (_cond), (_cond_params), SA_SKIP, NO_PARAMS, (_action), (_action_params))

/* Typed inline helpers to improve autocomplete and reduce mistakes */
static inline script_param_t p_dialogue(const char *str)
{
    return (script_param_t){.str_param = {.str = str}};
}
static inline script_param_t p_entity(const struct entity2D *entity)
{
    return (script_param_t){.entity_param = {.entity = entity}};
}
static inline script_param_t p_anim(gp_state_t from_state, gp_state_t to_state)
{
    return (script_param_t){.anim_param = {.from_state = from_state, .to_state = to_state}};
}
static inline script_param_t p_timer(float duration)
{
    return (script_param_t){.timer_param = {.duration = duration}};
}
static inline script_param_t p_distance(const struct entity2D *entity, float distance)
{
    return (script_param_t){.distance_param = {.entity = entity, .npc_type = NPC_TYPE_COUNT, .distance = distance}};
}
static inline script_param_t p_distance_npc(npc_type_t npc_type, float distance)
{
    return (script_param_t){.distance_param = {.entity = NULL, .npc_type = npc_type, .distance = distance}};
}
static inline script_param_t p_npc(npc_type_t type)
{
    return (script_param_t){.npc_param = {.type = type}};
}
static inline script_param_t p_path_exec(const char *path_name, npc_type_t npc_type, void (*configure_callback)(PathInstance *), bool wait_for_player)
{
    return (script_param_t){
        .path_param = {.path = NULL, .path_name = path_name, .npc_type = npc_type, .configure_callback = configure_callback, .wait_for_player = wait_for_player}};
}
static inline script_param_t p_path_reached(npc_type_t npc_type)
{
    return (script_param_t){.path_param = {.path = NULL, .path_name = NULL, .npc_type = npc_type}};
}
static inline script_param_t p_npc_direct_target(npc_type_t type, const char *poi_name, bool wait_for_player)
{
    return (script_param_t){.npc_direct_target_param = {.type = type, .poi_name = poi_name, .wait_for_player = wait_for_player}};
}
static inline script_param_t p_flag(uint16_t flag)
{
    return (script_param_t){.flag_param = {.flag_index = (uint32_t)flag}};
}
static inline script_param_t p_piece(uint16_t piece_flag)
{
    return (script_param_t){.flag_param = {.flag_index = (uint32_t)piece_flag}};
}
static inline script_param_t p_marker(const char *name, minimap_marker_type_t type, bool auto_set_target)
{
    return (script_param_t){.marker_param = {.name = name, .type = type, .auto_set_target = auto_set_target}};
}
static inline script_param_t p_script(const char *name)
{
    return (script_param_t){.str_param = {.str = name}};
}
static inline script_param_t p_race_warmup(const char *race_name, uint16_t coins_per_lap, float coin_turbo_burst_duration_ms, uint16_t max_laps)
{
    return (script_param_t){
        .race_warmup_param = {.race_name = race_name, .coins_per_lap = coins_per_lap, .coin_turbo_burst_duration_ms = coin_turbo_burst_duration_ms, .max_laps = max_laps}};
}
static inline script_param_t p_act(uint8_t act)
{
    return (script_param_t){.act_param = {.act = act}};
}
static inline script_param_t p_gp_state(uint8_t state)
{
    return (script_param_t){.gp_state_param = {.state = state}};
}
static inline script_param_t p_spawn(const char *folder_name)
{
    return (script_param_t){.str_param = {.str = folder_name}};
}
static inline script_param_t p_currency_threshold(uint32_t threshold)
{
    return (script_param_t){.currency_param = {.threshold = threshold, .delta = 0}};
}
static inline script_param_t p_currency_delta(int32_t delta)
{
    return (script_param_t){.currency_param = {.threshold = 0, .delta = delta}};
}
static inline script_param_t p_create_piece_at_npc(npc_type_t npc_type, uint16_t unlock_flag)
{
    return (script_param_t){.create_piece_param = {.npc_type = npc_type, .unlock_flag = unlock_flag}};
}
static inline script_param_t p_create_piece_at_poi(const char *poi_name, uint16_t unlock_flag)
{
    return (script_param_t){.create_piece_at_poi_param = {.poi_name = poi_name, .unlock_flag = unlock_flag}};
}
static inline script_param_t p_set_marker_to_piece(uint16_t unlock_flag, bool auto_set_target)
{
    return (script_param_t){.marker_to_piece_param = {.unlock_flag = unlock_flag, .auto_set_target = auto_set_target}};
}
static inline script_param_t p_sound(const char *sound_path, int channel)
{
    return (script_param_t){.sound_param = {.sound_path = sound_path, .channel = channel}};
}

/* Public API */
ScriptInstance *script_create(void);
void script_destroy(ScriptInstance *_pScript);
void script_add_step(ScriptInstance *_pScript, script_condition_t _condition, script_param_t _condition_params, script_action_t _action, script_param_t _action_params,
                     script_action_t _else_action, script_param_t _else_action_params);
void script_start(ScriptInstance *_pScript);
void script_stop(ScriptInstance *_pScript);
void script_update(ScriptInstance *_pScript);
bool script_is_active(ScriptInstance *_pScript);
