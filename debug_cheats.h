#pragma once

#include "libdragon.h"

/* Pause-menu debug overlay + cheat inputs. */

void debug_cheats_init(void);

/* Toggle whether the debug overlay is active (shown instead of the pause menu). */
void debug_cheats_toggle(void);
void debug_cheats_set_active(bool _bActive);
bool debug_cheats_is_active(void);

/* Process cheat inputs (only call while paused). */
void debug_cheats_update(const joypad_inputs_t *_pInputs);

/* Render debug overlay text (call after drawing the pause overlay). */
void debug_cheats_render(void);
