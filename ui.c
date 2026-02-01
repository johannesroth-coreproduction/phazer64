#include "ui.h"
#include "camera.h"
#include "libdragon.h"
#include "rdpq.h"
#include "rdpq_mode.h"

/* Screen dimensions - initialized via ui_init() */
int SCREEN_W = 320;
int SCREEN_H = 240;

/* User-adjustable overscan padding */
int UI_OVERSCAN_PADDING = 0;

/* Cached total padding (overscan + designer) - updated on init and when overscan changes */
static int s_iTotalPadding = UI_DESIGNER_PADDING;

void ui_init(int _iScreenW, int _iScreenH)
{
    SCREEN_W = _iScreenW;
    SCREEN_H = _iScreenH;
}

void ui_set_overscan_padding(int _iPadding)
{
    UI_OVERSCAN_PADDING = _iPadding;
    s_iTotalPadding = UI_OVERSCAN_PADDING + UI_DESIGNER_PADDING;
    /* Note: Clamping will be handled in menu system later */
}

int ui_get_overscan_padding(void)
{
    return UI_OVERSCAN_PADDING;
}

struct vec2i ui_get_safe_area_size(void)
{
    struct vec2i vSize;
    int iPadding = UI_OVERSCAN_PADDING;
    vSize.iX = SCREEN_W - (iPadding * 2);
    vSize.iY = SCREEN_H - (iPadding * 2);
    return vSize;
}

struct vec2i ui_get_pos_top_left(int _iSpriteWidth, int _iSpriteHeight)
{
    struct vec2i vPos;
    vPos.iX = s_iTotalPadding;
    vPos.iY = s_iTotalPadding;
    return vPos;
}

struct vec2i ui_get_pos_top_right(int _iSpriteWidth, int _iSpriteHeight)
{
    struct vec2i vPos;
    vPos.iX = SCREEN_W - s_iTotalPadding - _iSpriteWidth;
    vPos.iY = s_iTotalPadding;
    return vPos;
}

struct vec2i ui_get_pos_top_center(int _iSpriteWidth, int _iSpriteHeight)
{
    struct vec2i vPos;
    vPos.iX = (SCREEN_W - _iSpriteWidth) / 2;
    vPos.iY = s_iTotalPadding;
    return vPos;
}

struct vec2i ui_get_pos_middle_left(int _iSpriteWidth, int _iSpriteHeight)
{
    struct vec2i vPos;
    vPos.iX = s_iTotalPadding;
    vPos.iY = (SCREEN_H - _iSpriteHeight) / 2;
    return vPos;
}

struct vec2i ui_get_pos_middle_right(int _iSpriteWidth, int _iSpriteHeight)
{
    struct vec2i vPos;
    vPos.iX = SCREEN_W - s_iTotalPadding - _iSpriteWidth;
    vPos.iY = (SCREEN_H - _iSpriteHeight) / 2;
    return vPos;
}

struct vec2i ui_get_pos_middle_center(int _iSpriteWidth, int _iSpriteHeight)
{
    struct vec2i vPos;
    vPos.iX = (SCREEN_W - _iSpriteWidth) / 2;
    vPos.iY = (SCREEN_H - _iSpriteHeight) / 2;
    return vPos;
}

struct vec2i ui_get_pos_bottom_left(int _iSpriteWidth, int _iSpriteHeight)
{
    struct vec2i vPos;
    vPos.iX = s_iTotalPadding;
    vPos.iY = SCREEN_H - s_iTotalPadding - _iSpriteHeight;
    return vPos;
}

struct vec2i ui_get_pos_bottom_right(int _iSpriteWidth, int _iSpriteHeight)
{
    struct vec2i vPos;
    vPos.iX = SCREEN_W - s_iTotalPadding - _iSpriteWidth;
    vPos.iY = SCREEN_H - s_iTotalPadding - _iSpriteHeight;
    return vPos;
}

struct vec2i ui_get_pos_bottom_center(int _iSpriteWidth, int _iSpriteHeight)
{
    struct vec2i vPos;
    vPos.iX = (SCREEN_W - _iSpriteWidth) / 2;
    vPos.iY = SCREEN_H - s_iTotalPadding - _iSpriteHeight;
    return vPos;
}

void ui_draw_darkening_overlay(void)
{
    ui_draw_darkening_overlay_alpha((uint8_t)128);
}

void ui_draw_darkening_overlay_alpha(uint8_t _uAlpha)
{
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

    if (_uAlpha < 255)
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    rdpq_set_prim_color(RGBA32(0, 0, 0, _uAlpha));
    rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);
}

void ui_draw_overlay_alpha_rgb(uint8_t _uAlpha, uint8_t _r, uint8_t _g, uint8_t _b)
{
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

    if (_uAlpha < 255)
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    rdpq_set_prim_color(RGBA32(_r, _g, _b, _uAlpha));
    rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);
}

void ui_render_button_above_world_pos(struct vec2 _vWorldPos, struct vec2i _vHalfExtents, sprite_t *_pButtonSprite, float _fVerticalScale)
{
    if (!_pButtonSprite)
        return;

    /* Convert world position to screen */
    struct vec2i vScreenPos;
    camera_world_to_screen(&g_mainCamera, _vWorldPos, &vScreenPos);

    float fZoom = camera_get_zoom(&g_mainCamera);
    float fScaledPadding = (UI_DESIGNER_PADDING / 2.0f) * fZoom;

    /* Draw button above the position */
    int iBtnX = vScreenPos.iX - (_pButtonSprite->width / 2);
    int iBtnY = vScreenPos.iY - (int)(_vHalfExtents.iY * fZoom * _fVerticalScale) - _pButtonSprite->height - (int)fScaledPadding;

    rdpq_set_mode_copy(false);
    rdpq_mode_alphacompare(1);
    rdpq_sprite_blit(_pButtonSprite, iBtnX, iBtnY, NULL);
}