#pragma once

#include "entity2d.h"
#include "libdragon.h"
#include "math2d.h"
#include "math_helper.h"
#include "ui.h"

typedef struct camera2D
{
    struct vec2 vPos;   /* world center (current frame) */
    struct vec2 vPrev;  /* world center (previous frame) */
    float fZoom;        /* uniform zoom factor (1.0 = default scale) */
    struct vec2i vHalf; /* half viewport width and height in pixels */
} camera2D;

/* Camera zoom constants */
#define CAMERA_ZOOM_DEFAULT 1.0f
#define CAMERA_ZOOM_DEFAULT_SNAP_THRESHOLD 0.01f /* Snap to default zoom when within this threshold (for clean filtering) */

/* Main camera instance - accessible globally */
extern camera2D g_mainCamera;

/* Initialize camera centered at (0,0) in world space. */
void camera_init(camera2D *_pCamera, int _iScreenW, int _iScreenH);

/* Set/query zoom (values <= 0 are clamped to a small positive epsilon). */
void camera_set_zoom(camera2D *_pCamera, float _fZoom);
float camera_get_zoom(const camera2D *_pCamera);

/*
 * Keep the target within a deadzone ELLIPSE around the viewport center.
 * _fDeadZoneRadius is the vertical radius in screen-pixels.
 * The Horizontal radius will be _fDeadZoneRadius * viewport_aspect_ratio.
 * _vViewportOffset: (x, y) offset of viewport from screen origin
 * _vViewportSize: (width, height) of viewport
 */
void camera_follow_target_ellipse_custom_viewport(camera2D *_pCamera, struct vec2 _vTarget, float _fDeadZoneRadius, float _fLerp, struct vec2i _vViewportOffset,
                                                  struct vec2i _vViewportSize);

/*
 * Keep the target within a deadzone RECT around the viewport center.
 * _fDeadZoneRadius is the vertical half-height in screen-pixels.
 * The horizontal half-width will be _fDeadZoneRadius * viewport_aspect_ratio.
 * _vViewportOffset: (x, y) offset of viewport from screen origin
 * _vViewportSize: (width, height) of viewport
 */
void camera_follow_target_rect_custom_viewport(camera2D *_pCamera, struct vec2 _vTarget, float _fDeadZoneRadius, float _fLerp, struct vec2i _vViewportOffset,
                                               struct vec2i _vViewportSize);

/*
 * Keep the target within a deadzone ELLIPSE around the screen center.
 * _fDeadZoneRadius is the vertical radius in screen-pixels.
 * The Horizontal radius will be _fDeadZoneRadius * SCREEN_ASPECT_RATIO.
 * This is an inline wrapper that calls camera_follow_target_ellipse_custom_viewport with full screen.
 */
static inline void camera_follow_target_ellipse(camera2D *_pCamera, struct vec2 _vTarget, float _fDeadZoneRadius, float _fLerp)
{
    /* Use compound literals for zero-cost initialization (compiler optimizes these away) */
    camera_follow_target_ellipse_custom_viewport(_pCamera, _vTarget, _fDeadZoneRadius, _fLerp, (struct vec2i){0, 0}, (struct vec2i){SCREEN_W, SCREEN_H});
}

/*
 * Keep the target within a deadzone RECT around the screen center.
 * _fDeadZoneRadius is the vertical half-height in screen-pixels.
 * The horizontal half-width will be _fDeadZoneRadius * SCREEN_ASPECT_RATIO.
 * This is an inline wrapper that calls camera_follow_target_rect_custom_viewport with full screen.
 */
static inline void camera_follow_target_rect(camera2D *_pCamera, struct vec2 _vTarget, float _fDeadZoneRadius, float _fLerp)
{
    /* Use compound literals for zero-cost initialization (compiler optimizes these away) */
    camera_follow_target_rect_custom_viewport(_pCamera, _vTarget, _fDeadZoneRadius, _fLerp, (struct vec2i){0, 0}, (struct vec2i){SCREEN_W, SCREEN_H});
}

/* Instantly set camera position (no lerping). Also updates previous position to match. */
void camera_set_position(camera2D *_pCamera, struct vec2 _vPos);

/* Update camera previous position to current position. */
void camera_update(camera2D *_pCamera);

/* Convert world coordinates to integer screen coordinates. */
void camera_world_to_screen(const camera2D *_pCamera, struct vec2 _vWorld, struct vec2i *_pOutScreen);

/* Convert world coordinates to screen with quantized camera position (prevents sub-pixel wobble). */
void camera_world_to_screen_quantized(const camera2D *_pCamera, struct vec2 _vWorld, struct vec2i *_pOutScreen);

/* Convert screen coordinates to world coordinates. */
void camera_screen_to_world(const camera2D *_pCamera, struct vec2i _vScreen, struct vec2 *_pOutWorld);

/* Test if an entity's AABB overlaps the camera view (world-space). */
bool camera_is_entity_visible(const struct camera2D *_pCamera, const struct entity2D *_pEnt);

/* Test if a world-space point is within the camera view + margin. */
bool camera_is_point_visible(const struct camera2D *_pCamera, struct vec2 _vPos, float _fMargin);

/* Test if a screen-space point is within the screen bounds + margin. */
bool camera_is_screen_point_visible(const struct camera2D *_pCamera, struct vec2i _vScreen, float _fMargin);

/* Combined: visibility test + world→screen center transform. */
bool camera_entity_world_to_screen(const struct camera2D *_pCamera, const struct entity2D *_pEnt, struct vec2i *_pOutScreen);

/* -------------------------------------------------------------------------
 * Screen-space helpers (no camera dependency)
 * ------------------------------------------------------------------------- */

/*
 * Simple screen-space culling for an AABB in pixel coordinates.
 *
 * _pMin / _pMax  : inclusive min / exclusive max (x2,y2) of the rect.
 * _iScreenW/H    : framebuffer size in pixels.
 *
 * Returns true if the rect is completely outside the screen.
 */
static inline bool screen_cull_rect(const struct vec2i *_pMin, const struct vec2i *_pMax, int _iScreenW, int _iScreenH)
{
    if (_pMax->iX <= 0 || _pMax->iY <= 0)
        return true;

    if (_pMin->iX >= _iScreenW || _pMin->iY >= _iScreenH)
        return true;

    return false;
}
