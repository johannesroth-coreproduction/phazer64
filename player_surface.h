#pragma once

#include "entity2d.h"
#include "math2d.h"
#include "tilemap.h"

/* Initialize player surface with a world position */
void player_surface_init(struct vec2 _vWorldPos);

/* Free player surface resources */
void player_surface_free(void);

/* Update player surface position based on stick input */
void player_surface_update(int _iStickX, int _iStickY);

/* Render player surface sprite */
void player_surface_render(void);

/* Get player surface world position */
struct vec2 player_surface_get_position(void);

/* Get player surface entity (for collision detection) */
const struct entity2D *player_surface_get_entity(void);

/* Get player surface collision box half extents */
struct vec2 player_surface_get_collision_half_extents(void);

/* Check if player surface is near (colliding with) the UFO */
bool player_surface_near_ufo(void);

/* Set player surface world position */
void player_surface_set_position(struct vec2 _vPos);