#pragma once

#include "../camera.h"
#include "../math2d.h"
#include "libdragon.h"

/* Camera constants */
#define CAMERA_LOOK_AHEAD_FACTOR 200.0f    // Maximum look-ahead factor (reached at max speed)
#define CAMERA_LOOK_AHEAD_MIN_SPEED 0.0f   // Minimum speed threshold - no look-ahead below this
#define CAMERA_LOOK_AHEAD_MAX_SPEED 3.9f   // Speed at which max look-ahead is reached
#define CAMERA_LOOK_AHEAD_CURVE_POWER 0.7f // Curve power for look-ahead (0.5 = sqrt, gives more look-ahead at slow speeds; 1.0 = linear)
#define CAMERA_LERP 0.2f
#define CAMERA_BOUNCY_LERP_REDUCTION 0.15f // Lerp reduction factor during bounce (15% of normal lerp speed)
#define CAMERA_DEADZONE_RADIUS 40.0f
#define CAMERA_DEADZONE_RADIUS_LOCK_ON 5.0f

// manual zoom
#define CAMERA_ZOOM_MIN 0.01f
#define CAMERA_ZOOM_MAX 4.0f
#define CAMERA_ZOOM_MANUAL_STEP 0.03f // Step size for manual zoom when holding D-Pad Up/Down

// target lock zoom
#define CAMERA_ZOOM_LOCK_ON_MIN 0.5f     // Minimum zoom when lock-on distance is too far
#define CAMERA_ZOOM_LERP_IN 0.025f       // Smooth zoom interpolation speed when zooming in
#define CAMERA_ZOOM_LERP_OUT 0.25f       // Smooth zoom interpolation speed when zooming out
#define CAMERA_ZOOM_START_THRESHOLD 0.5f // Start zooming out when distance reaches X% of max fit distance
#define CAMERA_ZOOM_IN_LAG_MS 1000       // Delay before zooming back in (stabilizes camera)

/* Surface camera constants */
#define CAMERA_LERP_SURFACE 0.05f            // Camera lerp speed for SURFACE mode (higher = faster catch-up)
#define CAMERA_DEADZONE_RADIUS_SURFACE 20.0f // Deadzone radius for SURFACE camera mode (screen pixels)

/* JNR camera look-ahead constants */
#define CAMERA_LOOK_AHEAD_JNR_FACTOR 150.0f    // Maximum look-ahead factor (reached at max speed)
#define CAMERA_LOOK_AHEAD_JNR_MIN_SPEED 0.0f   // Minimum speed threshold - no look-ahead below this
#define CAMERA_LOOK_AHEAD_JNR_MAX_SPEED 100.0f // Speed at which max look-ahead is reached
#define CAMERA_LOOK_AHEAD_JNR_CURVE_POWER 1.0f // Curve power for look-ahead (0.5 = sqrt, gives more look-ahead at slow speeds; 1.0 = linear)
#define CAMERA_LERP_JNR 0.04f                  // Camera lerp speed for JNR mode (slower than UFO for smoother feel)
#define CAMERA_LOOK_AHEAD_JNR_Y_SCALE 0.7f     // Scale factor to reduce Y-axis movement in look-ahead target
#define CAMERA_DEADZONE_RADIUS_JNR 100.0f      // Deadzone radius for JNR camera mode
#define CAMERA_JNR_EDGE_LERP_MARGIN 48.0f      // Pixels from screen edge to start lerp boost

/* JNR camera Y-axis stick control constants */
#define CAMERA_JNR_Y_MAX_TRANSLATION 120.0f // Maximum Y translation
#define CAMERA_JNR_Y_DEADZONE 65.0f         // Deadzone threshold for Y stick input (before look up down triggers)
#define CAMERA_JNR_Y_RETURN_WAIT_MS 1500    // Wait time before lerping back to center (milliseconds)
#define CAMERA_JNR_Y_RETURN_LERP 0.025f     // Lerp speed for returning Y translation to center

/* Initialize gameplay camera state */
void gp_camera_init(void);

/* Update camera target, zoom, and handle controls */
void gp_camera_ufo_update(bool _bDUp, bool _bDDown, bool _bDLeft, bool _bDRight);

/* Render debug information for camera input, velocity, and target */
void gp_camera_render_ufo_debug(void);

/* Get current target zoom level */
float gp_camera_get_target_zoom(void);

/* Get debug target position */
struct vec2 gp_camera_get_debug_target(void);

/* Update camera target, zoom, and handle controls for JNR mode */
void gp_camera_jnr_update(bool _bDUp, bool _bDDown, bool _bDLeft, bool _bDRight, int _iStickY);

/* Render debug information for JNR camera input, velocity, and target */
void gp_camera_render_jnr_debug(void);

/* Apply a vertical inset (e.g., dialogue box) so camera centers the remaining view.
 * _iHeightPx: height of inset in pixels (0 to disable), _bTop: true if inset is at top. */
void gp_camera_set_dialogue_inset(int _iHeightPx, bool _bTop);

/* Update camera for surface player (wraps camera, calculates wrapped delta, follows target) */
void gp_camera_surface_update(void);

/* Calculate wrapped delta between two positions (for camera following) */
struct vec2 gp_camera_calc_wrapped_delta(struct vec2 _vFrom, struct vec2 _vTo);

/* Check if entity is visible with wrapping support for PLANET/SURFACE modes */
bool gp_camera_is_entity_visible_wrapped(const struct camera2D *_pCamera, const struct entity2D *_pEnt);

/* Check if point is visible with wrapping support for PLANET/SURFACE modes */
bool gp_camera_is_point_visible_wrapped(const struct camera2D *_pCamera, struct vec2 _vPos, float _fMargin);

/* Convert world position to screen with wrapping support for PLANET/SURFACE modes */
void gp_camera_world_to_screen_wrapped(const struct camera2D *_pCamera, struct vec2 _vWorldPos, struct vec2i *_pOutScreen);

/* Entity visibility + world→screen with wrapping support for PLANET/SURFACE modes */
bool gp_camera_entity_world_to_screen_wrapped(const struct camera2D *_pCamera, const struct entity2D *_pEnt, struct vec2i *_pOutScreen);

/* Camera follow with wrapping support - handles wrapped delta calculation */
void gp_camera_follow_target_ellipse_with_wrapping(struct camera2D *_pCamera, struct vec2 _vTarget, float _fDeadZone, float _fLerp);

/* Camera follow with wrapping support - RECT deadzone with aspect correction */
void gp_camera_follow_target_rect_with_wrapping(struct camera2D *_pCamera, struct vec2 _vTarget, float _fDeadZone, float _fLerp);