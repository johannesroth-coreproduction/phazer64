#pragma once

#include "math2d.h"
#include "sprite.h"

/* Designer padding - always applied for visual spacing (pixels from each edge) */
#define UI_DESIGNER_PADDING 12

/* Y offset for text positioning (adjusts text baseline) */
#define UI_FONT_Y_OFFSET 8

/* Screen dimensions - must be initialized via ui_init() */
extern int SCREEN_W;
extern int SCREEN_H;

/* Screen aspect ratio (Width / Height) */
#define SCREEN_ASPECT_RATIO (320.0f / 240.0f)

/* User-adjustable overscan padding (pixels from each edge) - can be set via ui_set_overscan_padding() */
extern int UI_OVERSCAN_PADDING;

/* Initialize UI system with screen dimensions */
void ui_init(int _iScreenW, int _iScreenH);

/* Set overscan padding (user setting to fight overscan on real TVs) */
void ui_set_overscan_padding(int _iPadding);

/* Get current overscan padding */
int ui_get_overscan_padding(void);

/* Get overscan-safe area dimensions (screen size minus overscan padding on all sides) */
struct vec2i ui_get_safe_area_size(void);

/* Get position for top-left corner of sprite (with padding) */
struct vec2i ui_get_pos_top_left(int _iSpriteWidth, int _iSpriteHeight);

/* Get position for top-right corner of sprite (with padding) */
struct vec2i ui_get_pos_top_right(int _iSpriteWidth, int _iSpriteHeight);

/* Get position for top-center of sprite (with padding) */
struct vec2i ui_get_pos_top_center(int _iSpriteWidth, int _iSpriteHeight);

/* Get position for middle-left of sprite (with padding) */
struct vec2i ui_get_pos_middle_left(int _iSpriteWidth, int _iSpriteHeight);

/* Get position for middle-right of sprite (with padding) */
struct vec2i ui_get_pos_middle_right(int _iSpriteWidth, int _iSpriteHeight);

/* Get position for middle-center of sprite (with padding) */
struct vec2i ui_get_pos_middle_center(int _iSpriteWidth, int _iSpriteHeight);

/* Get position for bottom-left of sprite (with padding) */
struct vec2i ui_get_pos_bottom_left(int _iSpriteWidth, int _iSpriteHeight);

/* Get position for bottom-right of sprite (with padding) */
struct vec2i ui_get_pos_bottom_right(int _iSpriteWidth, int _iSpriteHeight);

/* Get position for bottom-center of sprite (with padding) */
struct vec2i ui_get_pos_bottom_center(int _iSpriteWidth, int _iSpriteHeight);

/* Draw a semi-transparent darkening overlay over the entire screen */
void ui_draw_darkening_overlay(void);

/* Draw a semi-transparent darkening overlay over the entire screen with custom alpha */
void ui_draw_darkening_overlay_alpha(uint8_t _uAlpha);

/* Draw a semi-transparent overlay over the entire screen with custom alpha and RGB color */
void ui_draw_overlay_alpha_rgb(uint8_t _uAlpha, uint8_t _r, uint8_t _g, uint8_t _b);

/* Render button sprite above a world position (converts world to screen coordinates)
 * _vWorldPos: World position (center of entity/trigger)
 * _vHalfExtents: Half extents of the entity/trigger (for vertical offset calculation)
 * _pButtonSprite: Button sprite to render
 * _fVerticalScale: Scale factor for vertical offset (1.0 = full offset, 0.5 = half offset) */
void ui_render_button_above_world_pos(struct vec2 _vWorldPos, struct vec2i _vHalfExtents, sprite_t *_pButtonSprite, float _fVerticalScale);

/* Convenience: Get position from sprite pointer */
static inline struct vec2i ui_get_pos_top_left_sprite(sprite_t *_pSprite)
{
    return ui_get_pos_top_left(_pSprite->width, _pSprite->height);
}

static inline struct vec2i ui_get_pos_top_right_sprite(sprite_t *_pSprite)
{
    return ui_get_pos_top_right(_pSprite->width, _pSprite->height);
}

static inline struct vec2i ui_get_pos_top_center_sprite(sprite_t *_pSprite)
{
    return ui_get_pos_top_center(_pSprite->width, _pSprite->height);
}

static inline struct vec2i ui_get_pos_middle_left_sprite(sprite_t *_pSprite)
{
    return ui_get_pos_middle_left(_pSprite->width, _pSprite->height);
}

static inline struct vec2i ui_get_pos_middle_right_sprite(sprite_t *_pSprite)
{
    return ui_get_pos_middle_right(_pSprite->width, _pSprite->height);
}

static inline struct vec2i ui_get_pos_middle_center_sprite(sprite_t *_pSprite)
{
    return ui_get_pos_middle_center(_pSprite->width, _pSprite->height);
}

static inline struct vec2i ui_get_pos_bottom_left_sprite(sprite_t *_pSprite)
{
    return ui_get_pos_bottom_left(_pSprite->width, _pSprite->height);
}

static inline struct vec2i ui_get_pos_bottom_right_sprite(sprite_t *_pSprite)
{
    return ui_get_pos_bottom_right(_pSprite->width, _pSprite->height);
}

static inline struct vec2i ui_get_pos_bottom_center_sprite(sprite_t *_pSprite)
{
    return ui_get_pos_bottom_center(_pSprite->width, _pSprite->height);
}

/* Convenience: Get position for text at top-left corner (with font Y offset) */
static inline struct vec2i ui_get_pos_top_left_text(void)
{
    struct vec2i vPos = ui_get_pos_top_left(0, 0);
    vPos.iY += UI_FONT_Y_OFFSET;
    return vPos;
}

/* Convenience: Get position for text at top-center (with font Y offset) */
static inline struct vec2i ui_get_pos_top_center_text(void)
{
    struct vec2i vPos = ui_get_pos_top_center(0, 0);
    vPos.iY += UI_FONT_Y_OFFSET;
    return vPos;
}
