#pragma once

#include "libdragon.h"
#include <stdbool.h>

/* Credits text data - shared between menu and finish slideshow */
extern const char *g_credits_text_lines[];
extern const int g_credits_text_line_count;

/* Reset credits scroll position to 0 */
void credits_reset(void);

/* Update credits scroll based on input and time
 * _pInputs: Joypad inputs (can be NULL if no input control needed)
 * _bAllowInput: Whether to allow stick Y input to control scrolling
 */
void credits_update(const joypad_inputs_t *_pInputs, bool _bAllowInput);

/* Render scrolling credits */
/* _iStartY: Starting Y position for credits text */
void credits_render(int _iStartY);
