#pragma once

#include "../entity2d.h"
#include "../math2d.h"
#include <stdbool.h>
#include <stdint.h>

/* Coin state enum */
typedef enum
{
    COIN_STATE_EMPTY,
    COIN_STATE_COLLECTED,
    COIN_STATE_MISSED
} coin_state_t;

/* Race start sequence states */
typedef enum
{
    RACE_START_NONE,
    RACE_START_FADE_TO_BLACK,
    RACE_START_FADE_FROM_BLACK,
    RACE_START_COUNTDOWN,
    RACE_START_RACING
} race_start_state_t;

/* Constants */
#define RACE_HANDLER_MAX_COINS_PER_LAP 20
#define RACE_HANDLER_COLLECTION_RADIUS 8.0f
#define RACE_HANDLER_MISS_DISTANCE 50.0f     /* World units - distance past coin before marking as missed */
#define RACE_HANDLER_COIN_OFFSET_INNER 30.0f /* Offset towards inner edge */
#define RACE_HANDLER_MAX_LAPS 3              /* Fixed to 3 laps */
#define RACE_HANDLER_COUNTDOWN_DURATION 1.0f /* Seconds per countdown number */
#define RACE_HANDLER_COUNTDOWN_TOTAL 4       /* 3, 2, 1, GO */

/* Initialize/warm up race handler - creates race_track, prepares coin placements but doesn't place them yet */
/* Does NOT enable collision - track is rendered but inactive */
void race_handler_init(const char *_pRaceName, uint16_t _uCoinsPerLap, float _fCoinTurboBurstDurationMs, uint16_t _uMaxLaps);

/* Free race handler resources (also frees race_track) */
void race_handler_free(void);

/* Start a new race - handles full sequence: fade, teleport, countdown, enable collision */
void race_handler_start_race(void);

/* Stop the current race - resets race state and disables collision */
void race_handler_stop_race(void);

/* Abort the current race (eg. via pause menu) - stops race but does NOT record last-run best lap time */
/* This ensures UI "LAST:" only reflects a completed run, not an aborted race. */
void race_handler_abort_race(void);

/* Update race handler (call every frame) - handles race logic, countdown, etc. */
/* Internally calls race_track_update() when race is active */
/* _bCDown: C-down button pressed this frame (for restarting race at finish line) */
void race_handler_update(bool _bCDown);

/* Render race track and coin entity (world objects) */
/* Internally calls race_track_render() */
void race_handler_render(void);

/* Render race UI (coin slots, lap times, countdown) */
/* Should be called after UFO is rendered to ensure proper z-ordering */
/* Only renders if race is active or countdown is in progress */
void race_handler_render_ui(void);

/* Get current lap number */
uint16_t race_handler_get_current_lap(void);

/* Get total coins collected */
uint16_t race_handler_get_total_coins_collected(void);

/* Get coins collected for a specific lap (1-indexed) */
uint16_t race_handler_get_lap_coins_collected(uint16_t _uLap);

/* Get coins missed for a specific lap (1-indexed) */
uint16_t race_handler_get_lap_coins_missed(uint16_t _uLap);

/* Get lap time for a specific lap in seconds (1-indexed, returns 0.0f if lap not completed) */
float race_handler_get_lap_time(uint16_t _uLap);

/* Check if race is active */
bool race_handler_is_race_active(void);

/* Check if race handler is initialized/warmed up */
bool race_handler_is_initialized(void);

/* Check if race was started and then finished (for script conditions) */
bool race_handler_was_started_and_finished(void);

/* Reset the "was started and finished" flag (call after detecting race finish in scripts) */
void race_handler_reset_finished_flag(void);