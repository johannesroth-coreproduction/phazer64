#pragma once

#include "../entity2d.h"
#include "../math2d.h"

/* Forward declaration */
struct camera2D;

/* Turbo item instance, embedding entity2D */
typedef struct ItemTurboInstance
{
    struct entity2D entity; /* shared header: position, extents, flags, layer, sprite */
    uint32_t uSpawnOrder;   /* spawn order for dynamic items (used to find oldest) */
} ItemTurboInstance;

/* Initialization: loads sprites (must be called before adding turbo items) */
void item_turbo_init(void);

/* Free turbo items (frees sprites/sounds and clears items) */
void item_turbo_free(void);

/* Reset turbo items (clears all items but keeps resources) - deprecated, use free for full cleanup */
void item_turbo_reset(void);

/* Add a turbo item at the specified position (static, won't disappear) */
void item_turbo_add(struct vec2 _vPos);

/* Spawn a turbo item during gameplay (dynamic, can be replaced if array is full) */
void item_turbo_spawn(struct vec2 _vPos);

/* Per-frame logic update (checks collisions) */
void item_turbo_update(void);

/* Rendering */
void item_turbo_render(void);
