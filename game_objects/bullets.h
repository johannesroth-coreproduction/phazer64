#pragma once

#include "../camera.h"
#include "../math2d.h"

/* Initialize bullets (load assets) */
void bullets_init(void);

/* Refresh bullets visuals/state after progression flags change (e.g. upgrade) */
void bullets_refresh_state(void);

/* Free bullets resources */
void bullets_free(void);

/* Update bullets (requires camera for bounds checking, plus input state for shooting) */
void bullets_update(bool _bShootDown);

/* Render bullets */
void bullets_render(void);

/* Check if bullets are currently firing (within defined ms after spawn) */
bool bullets_is_firing(void);