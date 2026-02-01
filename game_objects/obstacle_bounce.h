#pragma once

#include "../entity2d.h"
#include "../math2d.h"

/* Forward declaration */
struct camera2D;

/* Bounce obstacle instance, embedding entity2D */
typedef struct ObstacleBounceInstance
{
    struct entity2D entity; /* shared header: position, extents, flags, layer, sprite */
} ObstacleBounceInstance;

/* Initialization: loads sprites (must be called before adding bounce obstacles) */
void obstacle_bounce_init(void);

/* Free bounce obstacles (frees sprites and clears obstacles) */
void obstacle_bounce_free(void);

/* Reset bounce obstacles (clears all obstacles but keeps resources) */
void obstacle_bounce_reset(void);

/* Add a bounce obstacle at the specified position */
void obstacle_bounce_add(struct vec2 _vPos);

/* Per-frame logic update (checks collisions) */
void obstacle_bounce_update(void);

/* Rendering */
void obstacle_bounce_render(void);
