#pragma once

#include <stdbool.h>

#include "../camera.h"

/* Initialize bomb weapon (load assets) */
void bomb_init(void);

/* Free bomb resources */
void bomb_free(void);

/* Update bomb weapon state and apply damage.
 * _bFire: B button pressed (fires bomb if not already active)
 */
void bomb_update(bool _bFire);

/* Render the bomb shockwave (if active) */
void bomb_render(void);

/* Check if bomb is currently firing (within defined ms after spawn) */
bool bomb_is_firing(void);