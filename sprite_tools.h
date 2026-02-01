#pragma once

#include "math2d.h"
#include "sprite.h"
#include <stdbool.h>

/**
 * @brief Calculate the trimmed bounding box of a sprite
 *
 * This function scans the sprite's pixel data to find the smallest rectangle
 * that contains all non-transparent pixels. Only fully transparent pixels
 * (100% alpha) are considered for trimming.
 *
 * @param _pSprite      The sprite to analyze (supports RGBA16, RGBA32, CI4, CI8 formats)
 * @param _pOutOffset   Output: vec2i containing the offset (x, y) of the trimmed rect
 *                      relative to the original sprite's top-left corner
 * @param _pOutSize     Output: vec2i containing the dimensions (width, height) of the trimmed rect
 * @return              true if trimming was successful, false if sprite format is unsupported
 *                      or sprite is invalid
 *
 * @note Supports FMT_RGBA16, FMT_RGBA32, FMT_CI4, and FMT_CI8 formats
 * @note For CI4/CI8 formats, the sprite must have a valid palette
 * @note If the entire sprite is transparent, the trimmed rect will be at (0,0) with size (0,0)
 */
bool sprite_tools_get_trimmed_rect(sprite_t *_pSprite, struct vec2i *_pOutOffset, struct vec2i *_pOutSize);
