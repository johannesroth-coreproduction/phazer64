#pragma once

#include "../camera.h"
#include "../math2d.h"

/* Lightweight debug geometry representing a hand-authored track and pick-ups. */
/*
 * Compile-time defines:
 *   TRACK_WIDTH_SCALE - Multiply all track widths by this factor (default: 1.0f)
 *                       Example: -DTRACK_WIDTH_SCALE=1.5f to make tracks 50% wider
 */
void debug_track_init(void);
void debug_track_free(void);
void debug_track_render(void);
