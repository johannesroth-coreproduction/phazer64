#pragma once

#include "entity2d.h"
#include "math2d.h"
#include "tilemap.h"

/* Initialize player JNR (position will be set separately by gp_state). */
void player_jnr_init(void);

/* Free player JNR resources */
void player_jnr_free(void);

/* Update player JNR position based on stick input, button A for jumping, and button L for fly mode */
void player_jnr_update(int _iStickX, bool _bButtonA, bool _bButtonLPressed);

/* Render player JNR sprite */
void player_jnr_render(void);

/* Get player JNR world position */
struct vec2 player_jnr_get_position(void);

/* Get player JNR collision box half extents */
struct vec2 player_jnr_get_collision_half_extents(void);

/* Get player JNR velocity vector */
struct vec2 player_jnr_get_velocity(void);

/* Get player JNR speed (magnitude of velocity) */
float player_jnr_get_speed(void);

/* Check if player JNR is on ground */
bool player_jnr_is_on_ground(void);

/* Set player JNR world position */
void player_jnr_set_position(struct vec2 _vPos);

/* Set player JNR position from a folder's logic.csv file (loads "spawn,x,y" entry). */
void player_jnr_set_position_from_data(const char *_pFolderName);

/* Get player JNR entity (for collision detection) */
const struct entity2D *player_jnr_get_entity(void);