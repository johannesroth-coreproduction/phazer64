#pragma once

#include "../camera.h"
#include "../entity2d.h"
#include "../math2d.h"
#include "space_objects.h"

/* Compatibility typedef */
typedef struct SpaceObject MeteorInstance;

/* Initialization: loads assets and spawns meteors via space_objects */
void meteors_init(void);

/* Free meteor resources (sprites, audio) */
void meteors_free(void);

/* Rendering helper for space_objects */
void meteor_render_object(SpaceObject *obj, struct vec2i vScreen, float fZoom);

/* Apply damage to a meteor (called by space_objects or direct hit logic) */
void meteor_apply_damage(SpaceObject *obj, int iDamage, struct vec2 vImpactDir);

/* Get crystal sprite for currency meteors */
sprite_t *meteors_get_crystal_sprite(void);

/* Wrappers for compatibility with space_objects system */
/* Legacy functions removed */
