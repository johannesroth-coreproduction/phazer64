#pragma once

#include "math2d.h"
#include <stdbool.h>

/* Minimap Configuration */
#define MINIMAP_ZOOM_LEVEL 0.1f
#define MINIMAP_OPEN_TIME 0.8f                    // Seconds to zoom out to minimap (fixed)
#define MINIMAP_CLOSE_MAX_SPEED 2000.0f           // World units per second when closing
#define MINIMAP_CLOSE_TIME_MIN 0.4f               // Minimum seconds to close (to prevent instant snap)
#define MINIMAP_MAX_TRAVEL_BACK_DISTANCE 10000.0f // Max distance before teleporting camera back on zoom in

/* Camera Movement Configuration */
#define MINIMAP_CAMERA_SPEED_MIN 100.0f  // Pixels per second at deadzone edge
#define MINIMAP_CAMERA_SPEED_MAX 2400.0f // Pixels per second at max stick

/* UI Configuration */
#define MINIMAP_UI_BUTTON_ICON_PADDING 4 // Pixels between button and icon

/* Background Configuration */
#define MINIMAP_BG_BORDER_THICKNESS 2    // Thickness of border rectangles
#define MINIMAP_BG_GRID_STEP_X 32        // Horizontal spacing between grid lines
#define MINIMAP_BG_GRID_STEP_Y 32        // Vertical spacing between grid lines
#define MINIMAP_BG_GRID_LINE_THICKNESS 1 // Thickness of grid lines
#define MINIMAP_BG_FADE_IN_TIME 0.2f     // Seconds to fade in background (after zoom out completes)
#define MINIMAP_BG_FADE_OUT_TIME 0.1f    // Seconds to fade out background (starts when zoom back in begins)

/* Initialize minimap system */
void minimap_init(void);

/* Update minimap state and camera movement from input
 * Should be called once per frame early in the update loop */
void minimap_update(bool _bCUp, bool _bCDown, bool _bActivateMarkerBtn, bool _bClearMarkerBtn, int _iStickX, int _iStickY);

/* Check if minimap mode is fully active or transitioning in/out */
bool minimap_is_active(void);

/* Get current zoom interpolation progress (0.0 = normal view, 1.0 = minimap view) */
float minimap_get_zoom_progress(void);

/* Get current camera translation offset accumulated during minimap mode */
struct vec2 minimap_get_camera_translation(void);

/* Render minimap UI (button and icon) - call in SPACE state when not in dialogue */
void minimap_render_ui(void);

/* Render minimap background (grid only) - call BEFORE starfield when minimap is active */
void minimap_render_bg(void);

/* Render minimap foreground (border, markers, crosshair) - call AFTER everything else when minimap is active */
void minimap_render_fg(void);
