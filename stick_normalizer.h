#pragma once

#include <stdint.h>

/* Stick calibration constants (hardware normalization) */
#define STICK_NORMALIZED_MAX 85            /* Target normalized range (standard N64 analog stick) */
#define STICK_CALIBRATION_MIN_THRESHOLD 30 /* Minimum value required for valid calibration */
#define STICK_CALIBRATION_MAX_RANGE 127    /* Absolute maximum value a controller can report */

/* Stick input constants (gameplay) */
#define STICK_MAX_MAGNITUDE 80.0f                           /* Max expected stick magnitude for normalization */
#define STICK_DEADZONE 16.0f                                /* Gameplay deadzone threshold for stick input */
#define STICK_DEADZONE_SQ (STICK_DEADZONE * STICK_DEADZONE) /* Squared deadzone for magnitude checks */

/* Stick input constants (UI/menu navigation) */
#define STICK_DEADZONE_MENU 50.0f /* Menu navigation deadzone (larger for deliberate input) */

/* Initialize stick normalizer - call after save system loads */
void stick_normalizer_init(void);

/* Update normalizer with raw stick input each frame */
void stick_normalizer_update(int8_t raw_x, int8_t raw_y);

/* Get normalized stick values (0-85 range) */
int8_t stick_normalizer_get_x(void);
int8_t stick_normalizer_get_y(void);

/* Set calibration values (min/max for each axis) */
void stick_normalizer_set_calibration(int8_t min_x, int8_t max_x, int8_t min_y, int8_t max_y);

/* Get current calibration values (for debugging/UI) */
void stick_normalizer_get_calibration(int8_t *min_x, int8_t *max_x, int8_t *min_y, int8_t *max_y);
