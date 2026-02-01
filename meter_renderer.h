#pragma once

#include "libdragon.h"
#include "math2d.h"
#include "sprite.h"


/* Shared meter renderer (turbo / overheat style meter with frame, fill and cap). */

/* Initialize shared meter resources (sprites, texture params). Safe to call multiple times. */
void meter_renderer_init(void);

/* Release shared meter resources when no longer needed. Safe to call multiple times. */
void meter_renderer_free(void);

/* Get the meter frame size in pixels (width, height) for UI layout helpers. */
struct vec2i meter_renderer_get_frame_size(void);

/*
 * Render a vertical meter at the given top-left frame position.
 * - _fValue01: fill amount in [0,1] (0 = empty, 1 = full).
 * - _uColor: RGBA32 color used to tint the fill and cap sprites.
 */
void meter_renderer_render(struct vec2i _vFramePos, float _fValue01, color_t _uColor);
