#pragma once

#include "../frame_time.h"
#include "libdragon.h"

/* Turbo settings */
#define UFO_TURBO_MULTIPLIER 2.0f            /* turbo boost multiplier when active */
#define UFO_TURBO_FULL_FUEL_DURATION_MS 5000 /* how many ms a full fuel tank (100) will last */
#define UFO_TURBO_FUEL_REGEN_DELAY_MS 1000   /* delay before fuel regeneration starts (ms) */
#define UFO_TURBO_FUEL_REGEN_TIME_MS 3000    /* time to fill fuel from 0 to 100 (ms) */

/* Initialize turbo system (loads sprites) */
void ufo_turbo_init(void);

/* Free turbo system resources */
void ufo_turbo_free(void);

/* Update turbo system (depletes fuel when button pressed) and returns effective multiplier */
float ufo_turbo_update(bool _bTurboPressed);

/* Refill fuel to maximum (100) */
void ufo_turbo_refill(void);

/* Trigger a short turbo burst (behaves like holding A for _fDurationMs, but doesn't deplete fuel) */
void ufo_turbo_trigger_burst(float _fDurationMs);

/* Get current fuel level (0-100) */
float ufo_turbo_get_fuel(void);

/* Get turbo sprite for rendering */
sprite_t *ufo_turbo_get_sprite(void);

/* Render turbo UI (fuel display) */
void ufo_turbo_render_ui(void);
