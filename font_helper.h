#pragma once

#include "libdragon.h"

#define FONT_NORMAL 1
#define FONT_STYLE_RED 1
#define FONT_STYLE_GREEN 2
#define FONT_STYLE_BLUE 3
#define FONT_STYLE_YELLOW 4
#define FONT_STYLE_PURPLE 5
#define FONT_STYLE_GRAY 6
#define FONT_STYLE_LIGHT_GRAY 7

extern rdpq_textparms_t m_tpCenterHorizontally;
extern rdpq_textparms_t m_tpCenterBoth;

void font_helper_init(void);

/**
 * Calculate the width of a text string in pixels.
 * The returned width, when divided by 2, can be used to properly center the text,
 * accounting for the bounding box offset (bbox.x0).
 * @param font_id Font ID to use for measurement
 * @param text Text string to measure (UTF-8, NULL-terminated)
 * @return Width of the text in pixels (bbox.x0 + bbox.x1), or 0.0f on error
 */
float font_helper_get_text_width(uint8_t font_id, const char *text);