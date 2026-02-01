#pragma once

#include "libdragon.h"
#include <stdint.h>

enum eCGAColor
{
    CGA_BLACK = 0,
    CGA_BLUE,
    CGA_GREEN,
    CGA_CYAN,
    CGA_RED,
    CGA_MAGENTA,
    CGA_BROWN,
    CGA_LIGHT_GREY,
    CGA_DARK_GREY,
    CGA_LIGHT_BLUE,
    CGA_LIGHT_GREEN,
    CGA_LIGHT_CYAN,
    CGA_LIGHT_RED,
    CGA_LIGHT_MAGENTA,
    CGA_YELLOW,
    CGA_WHITE,
    CGA_COLOR_COUNT
};

/* Canonical 16-color CGA palette (no alpha variations). */
static const color_t m_aCgaPalette[CGA_COLOR_COUNT] = {
    /* CGA_BLACK          */ RGBA32(0, 0, 0, 255),
    /* CGA_BLUE           */ RGBA32(0, 0, 170, 255),
    /* CGA_GREEN          */ RGBA32(0, 170, 0, 255),
    /* CGA_CYAN           */ RGBA32(0, 170, 170, 255),
    /* CGA_RED            */ RGBA32(170, 0, 0, 255),
    /* CGA_MAGENTA        */ RGBA32(170, 0, 170, 255),
    /* CGA_BROWN          */ RGBA32(170, 85, 0, 255),
    /* CGA_LIGHT_GREY     */ RGBA32(170, 170, 170, 255),
    /* CGA_DARK_GREY      */ RGBA32(85, 85, 85, 255),
    /* CGA_LIGHT_BLUE     */ RGBA32(85, 85, 255, 255),
    /* CGA_LIGHT_GREEN    */ RGBA32(85, 255, 85, 255),
    /* CGA_LIGHT_CYAN     */ RGBA32(85, 255, 255, 255),
    /* CGA_LIGHT_RED      */ RGBA32(255, 85, 85, 255),
    /* CGA_LIGHT_MAGENTA  */ RGBA32(255, 85, 255, 255),
    /* CGA_YELLOW         */ RGBA32(255, 255, 85, 255),
    /* CGA_WHITE          */ RGBA32(255, 255, 255, 255),
};

static inline color_t palette_get_cga_color(enum eCGAColor _eColor)
{
    return m_aCgaPalette[(int)_eColor];
}
