#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Shared fade timing so audio and visuals stay in sync */
#define FADE_DURATION 0.64f
#define FADE_FROM_BLACK_DELAY_FRAMES 1

/* Fade types */
typedef enum
{
    FROM_BLACK,
    TO_BLACK
} eFadeType;

/* Start a fade */
void fade_manager_start(eFadeType _type);

/* Update fade manager (call every frame) */
void fade_manager_update(void);

/* Render fade overlay (call in render function) */
void fade_manager_render(void);

/* Check if fade manager is busy/active */
bool fade_manager_is_busy(void);

/* Check if screen is fully opaque (fully black) */
bool fade_manager_is_opaque(void);

/* Stop fade immediately */
void fade_manager_stop(void);

/* Set fade color (default is black: 0, 0, 0) */
void fade_manager_set_color(uint8_t _r, uint8_t _g, uint8_t _b);