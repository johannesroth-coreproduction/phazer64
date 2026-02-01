#pragma once

#include "../camera.h"
#include "libdragon.h"
#include <stdint.h>

/**
 * Initialize the starfield simulation.
 *
 * _iScreenW / _iScreenH: current render resolution in pixels.
 *
 * Also initializes decorative background planets which are
 * integrated into the starfield as an additional parallax layer.
 */
void starfield_init(int _iScreenW, int _iScreenH, uint32_t _uSeed);

/* Free starfield resources (planet sprites) */
void starfield_free(void);

/**
 * Advance the starfield simulation by one frame.
 * Call this once per frame from your main update(). Uses frame_time_mul()
 * internally, staying 60fps-tuned.
 *
 * Automatically computes starfield velocity from camera movement to create
 * parallax effects where stars move opposite to camera movement.
 */
void starfield_update(void);

/**
 * Reset the starfield velocity to zero, skipping the smooth lerping effect.
 * Use this when transitioning states to avoid long streaks from accumulated
 * velocity differences.
 *
 * _pCamera: Camera (unused parameter, kept for API compatibility).
 */
void starfield_reset_velocity();

/**
 * Render the starfield (stars + planets).
 *
 * Assumes:
 *  - display surface is already attached with rdpq_attach_*()
 *  - a standard 2D mode is active (e.g., rdpq_set_mode_standard())
 *
 * Does NOT call rdpq_attach/detach.
 */
void starfield_render(void);
