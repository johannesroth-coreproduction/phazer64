#pragma once

#include "math2d.h"
#include <stdbool.h>
#include <stdint.h>

/* Path state enumeration */
typedef enum
{
    PATH_STATE_UNPLAYED,
    PATH_STATE_PLAYING,
    PATH_STATE_PAUSED,
    PATH_STATE_FINISHED
} path_state_t;

/* Movement mode enumeration */
typedef enum
{
    PATH_MODE_LINEAR,
    PATH_MODE_SINUS_FLY
} path_mode_t;

/* Forward declaration */
typedef struct PathInstance PathInstance;

/* System Management */
void path_mover_init(void);
void path_mover_update(void);
void path_mover_free_all(void);

/* Path Loading */
PathInstance *path_mover_load(const char *_pPathName);

/* Path Control */
void path_mover_start(PathInstance *_pPath);
void path_mover_pause(PathInstance *_pPath);
void path_mover_resume(PathInstance *_pPath);
void path_mover_stop(PathInstance *_pPath);
void path_mover_free(PathInstance *_pPath);

/* Configuration */
void path_mover_set_speed(PathInstance *_pPath, float _fSpeed);
void path_mover_set_loop(PathInstance *_pPath, bool _bLoop);
void path_mover_set_mode(PathInstance *_pPath, path_mode_t _eMode);
void path_mover_set_sinus_params(PathInstance *_pPath, float _fAmplitude, float _fFrequency);

/* Getters */
float path_mover_get_speed(PathInstance *_pPath);
path_state_t path_mover_get_state(PathInstance *_pPath);
struct vec2 path_mover_get_current_pos(PathInstance *_pPath);

/* Debug Rendering */
void path_mover_render_debug(void);