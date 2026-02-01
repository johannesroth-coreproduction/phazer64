#include "font_helper.h"
#include "palette.h"
#include "resource_helper.h"
#include "ui.h"
#include <alloca.h>
#include <string.h>

rdpq_textparms_t m_tpCenterHorizontally;
rdpq_textparms_t m_tpCenterBoth;

/* Use internal paragraph build helper to avoid heap alloc/free for measurements. */
extern rdpq_paragraph_t *__rdpq_paragraph_build(const rdpq_textparms_t *parms, uint8_t initial_font_id, const char *utf8_text, int *nbytes, rdpq_paragraph_t *layout);

void font_helper_init(void)
{
    rdpq_font_t *fontBWOutline = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    rdpq_text_register_font(FONT_NORMAL, fontBWOutline);

    /* Default style (style 0): Black fill, white outline */
    rdpq_font_style(fontBWOutline,
                    0,
                    &(rdpq_fontstyle_t){
                        .color = RGBA32(0, 0, 0, 255),
                        .outline_color = RGBA32(255, 255, 255, 255),
                    });

    rdpq_font_style(fontBWOutline,
                    FONT_STYLE_GREEN,
                    &(rdpq_fontstyle_t){
                        .color = palette_get_cga_color(CGA_BLACK),
                        .outline_color = palette_get_cga_color(CGA_LIGHT_GREEN),
                    });

    rdpq_font_style(fontBWOutline,
                    FONT_STYLE_BLUE,
                    &(rdpq_fontstyle_t){
                        .color = palette_get_cga_color(CGA_BLACK),
                        .outline_color = palette_get_cga_color(CGA_LIGHT_BLUE),
                    });

    rdpq_font_style(fontBWOutline,
                    FONT_STYLE_YELLOW,
                    &(rdpq_fontstyle_t){
                        .color = palette_get_cga_color(CGA_BLACK),
                        .outline_color = palette_get_cga_color(CGA_YELLOW),
                    });

    rdpq_font_style(fontBWOutline,
                    FONT_STYLE_PURPLE,
                    &(rdpq_fontstyle_t){
                        .color = palette_get_cga_color(CGA_BLACK),
                        .outline_color = palette_get_cga_color(CGA_LIGHT_MAGENTA),
                    });

    rdpq_font_style(fontBWOutline,
                    FONT_STYLE_GRAY,
                    &(rdpq_fontstyle_t){
                        .color = palette_get_cga_color(CGA_BLACK),
                        .outline_color = palette_get_cga_color(CGA_DARK_GREY),
                    });

    rdpq_font_style(fontBWOutline,
                    FONT_STYLE_LIGHT_GRAY,
                    &(rdpq_fontstyle_t){
                        .color = palette_get_cga_color(CGA_BLACK),
                        .outline_color = palette_get_cga_color(CGA_LIGHT_GREY),
                    });

    rdpq_font_style(fontBWOutline,
                    FONT_STYLE_RED,
                    &(rdpq_fontstyle_t){
                        .color = palette_get_cga_color(CGA_BLACK),
                        .outline_color = palette_get_cga_color(CGA_RED),
                    });

    m_tpCenterHorizontally = (rdpq_textparms_t){.align = ALIGN_CENTER, .width = SCREEN_W};
    m_tpCenterBoth = (rdpq_textparms_t){.align = ALIGN_CENTER, .valign = VALIGN_CENTER, .width = SCREEN_W, .height = SCREEN_H};
}

float font_helper_get_text_width(uint8_t font_id, const char *text)
{
    if (text == NULL || text[0] == '\0')
        return 0.0f;

    int nbytes = strlen(text);
    /* Ensure text data is coherent before any potential DMA in text layout/rendering. */
    CACHE_FLUSH_DATA((void *)text, (size_t)nbytes + 1);
    int capacity = nbytes + 1;
    rdpq_paragraph_t *pLayout = alloca(sizeof(*pLayout) + sizeof(rdpq_paragraph_char_t) * (size_t)capacity);
    pLayout->capacity = capacity;
    pLayout->flags = 0;
    pLayout = __rdpq_paragraph_build(NULL, font_id, text, &nbytes, pLayout);
    if (pLayout == NULL)
        return 0.0f;

    /* Return width that, when divided by 2, properly centers the text.
     * This accounts for bbox.x0 offset: width = 2 * centerOffset = bbox.x0 + bbox.x1 */
    float fWidth = pLayout->bbox.x0 + pLayout->bbox.x1;
    return fWidth;
}
