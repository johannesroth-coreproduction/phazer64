#pragma once

#include "../entity2d.h"
#include "../math2d.h"
#include "gp_state.h"

/* Forward declaration */
struct camera2D;

/* Currency instance, embedding entity2D */
typedef struct CurrencyInstance
{
    struct entity2D entity; /* shared header: position, extents, flags, layer, sprite */
    uint8_t uCurrencyId;    /* Currency ID (1-based: 1, 2, 3...) for collection tracking */
} CurrencyInstance;

/* Initialize currency handler (loads sprites, called once at startup) */
void currency_handler_init(void);

/* Refresh currency handler (loads CSV data from folder, called during state switches) */
/* _pFolder: Folder name to load currency.csv from (e.g., "cave", "mine") */
/* _eState: Current game state (SPACE, PLANET, SURFACE, or JNR) */
void currency_handler_refresh(const char *_pFolder, gp_state_t _eState);

/* Free currency handler (frees sprites and clears currency instances) */
void currency_handler_free(void);

/* Reset currency handler (clears all currency instances but keeps resources) */
void currency_handler_reset(void);

/* Per-frame logic update (checks collisions with player) */
void currency_handler_update(void);

/* Rendering */
void currency_handler_render(void);

/* Render currency UI (sprite and amount in lower right corner) */
void currency_handler_render_ui(void);

/* Spawn currency entity from destroyed meteor (called by meteor_apply_damage) */
void currency_handler_spawn_from_meteor(struct vec2 vPos, uint8_t uCurrencyId);

/* Check if all currency has been collected for the current folder */
bool currency_handler_is_all_collected(void);