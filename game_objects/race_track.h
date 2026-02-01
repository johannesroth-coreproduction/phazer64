#pragma once

#include "../math2d.h"
#include <stdbool.h>
#include <stdint.h>

/* Track sample point (S[i] from spec) */
typedef struct
{
    struct vec2 vPos;     /* World position */
    struct vec2 vTangent; /* Unit direction along track */
    struct vec2 vNormal;  /* Unit perpendicular (for rendering/collision) */
    float fS;             /* Cumulative arc-length distance */
} RaceTrackSample;

/* Track constants */
#define RACE_TRACK_WIDTH 200.0f       /* Border-to-border width */
#define RACE_TRACK_BORDER_THICK 12.0f /* Border thickness */
#define RACE_TRACK_STEP 32.0f         /* Arc-length resampling step */

/* Collision constants */
#define RACE_TRACK_HALF_COLLIDE 84.0f     /* Half-width for collision, matches RACE_TRACK_WIDTH * 0.5f */
#define RACE_TRACK_COLLISION_EPSILON 2.0f /* Small inward push to prevent re-collision */
/* Search window: segments to search around last known position.
 * With RACE_TRACK_STEP=32, window of 8 covers ~256 units.
 * Too low if: full search triggers often (check debug output), or collision feels jittery near sharp curves.
 * Increase if: UFO moves very fast or track has tight curves causing segment jumps > window size. */
#define RACE_TRACK_SEARCH_WINDOW 4
#define RACE_TRACK_BBOX_MARGIN 50.0f      /* Extra margin for bounding box check */
#define RACE_TRACK_BOUNCE_COOLDOWN_MS 200 /* Bounce cooldown duration in milliseconds */

/* System Management */
void race_track_init(const char *_pRaceName); /* Loads race from race.csv in current folder */
void race_track_free(void);

/* Rendering */
void race_track_render(void);

/* Getters */
bool race_track_is_initialized(void);
uint16_t race_track_get_sample_count(void);
float race_track_get_total_length(void);
const RaceTrackSample *race_track_get_samples(void);

/* Update and Collision */
void race_track_update(void);
void race_track_set_collision_enabled(bool _bEnabled);
bool race_track_is_collision_enabled(void);

/* Progress Queries */
/* Get progress coordinate s for a given world position */
float race_track_get_progress_for_position(struct vec2 _vPos);

/* Get world position and tangent for a given progress coordinate s */
bool race_track_get_position_for_progress(float _fS, struct vec2 *_pOutPos, struct vec2 *_pOutTangent);

/* Get world position, tangent, and normal for a given progress coordinate s */
bool race_track_get_position_for_progress_with_normal(float _fS, struct vec2 *_pOutPos, struct vec2 *_pOutTangent, struct vec2 *_pOutNormal);