#pragma once

/* Set per-frame timing values; call once per frame from the main loop. */
void frame_time_set(float _fDeltaSeconds);

/* Delta seconds of the last frame (clamped to a small epsilon). */
float frame_time_delta_seconds(void);

/* Frame multiplier normalized to 60fps (delta_seconds * 60, clamped). */
float frame_time_mul(void);

