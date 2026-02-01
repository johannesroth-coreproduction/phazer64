#pragma once

#include "entity2d.h"
#include "math2d.h"
#include <stdbool.h>

/* Marker type enum */
typedef enum
{
    MARKER_RHINO,
    MARKER_PIECE,
    MARKER_TARGET,
    MARKER_BOY,
    MARKER_PIN,
    MARKER_TERRA,
    MARKER_TYPE_COUNT
} minimap_marker_type_t;

/* Marker structure: entity2D base + name + marker type */
typedef struct MinimapMarker
{
    struct entity2D entity;              /* Base entity2D structure */
    const char *pName;                   /* Marker name (for POI loading/clearing) */
    minimap_marker_type_t eType;         /* Marker type */
    bool bSlotInUse;                     /* Whether this slot is in use */
    uint16_t uUnlockFlag;                /* Unlock flag for piece markers (0 for non-piece markers) */
    const struct entity2D *pPieceEntity; /* Direct reference to piece entity (for fast updates, NULL if not a piece marker) */
} MinimapMarker;

/* Configuration */
#define MINIMAP_MARKER_MAX_COUNT 8
#define MINIMAP_MARKER_SCALE_DISTANCE 320.0f
#define MINIMAP_MARKER_SELECT_RADIUS 16.0f
#define MINIMAP_MARKER_BORDER_PADDING 20 /* Padding from screen edge for off-screen markers */

/* Initialize minimap marker system */
void minimap_marker_init(void);

/* Set a marker by loading POI by name, returns entity pointer on success, NULL on failure */
const struct entity2D *minimap_marker_set(const char *_pName, minimap_marker_type_t _eType);

/* Set a marker at a specific world position (for empty markers or direct positioning) */
const struct entity2D *minimap_marker_set_at_pos(struct vec2 _vWorldPos, minimap_marker_type_t _eType);

/* Set a marker linked to a satellite piece by unlock flag (marker will track piece position) */
const struct entity2D *minimap_marker_set_piece(uint16_t _uUnlockFlag);

/* Clear a marker by name (unlinks UFO target if needed) */
void minimap_marker_clear(const char *_pName);

/* Update marker states
 * _bPiecesOnly: if true, only update piece markers (fast path for when minimap is inactive)
 *                if false, update all dynamic markers (marker_boy, pieces, etc.)
 */
void minimap_marker_update(bool _bPiecesOnly);

/* Update/create terra marker (called when minimap activates to ensure terra-pos is available) */
void minimap_marker_update_terra(void);

/* Render all active markers (called from minimap when active) */
void minimap_marker_render(void);

/* Get marker entity by name (for scripting UFO targeting) */
const struct entity2D *minimap_marker_get_entity_by_name(const char *_pName);

/* Get marker entity at world point using point-entity collision detection */
const struct entity2D *minimap_marker_get_at_world_point(struct vec2 _vWorldPos);

/* Get marker entity at screen point using screen-space collision detection */
const struct entity2D *minimap_marker_get_at_screen_point(struct vec2i _vScreenPos);

/* Clean up stale PIN markers that are no longer targeted */
void minimap_marker_cleanup_stale_pin(void);

/* Get marker type from entity pointer (returns MARKER_TYPE_COUNT if not found) */
minimap_marker_type_t minimap_marker_get_type(const struct entity2D *_pEntity);
