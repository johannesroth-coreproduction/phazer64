/* tilemap.h (relevant parts) */

#pragma once

#include "camera.h"
#include "tilemap_importer.h"
#include <stdbool.h>
#include <stdint.h>

#define TILEMAP_MAX_VISIBLE_TILES 4096
#define TILEMAP_BUCKET_SIZE 512
#define TILE_SIZE 16

/* Tile IDs are uint8_t; keep lookup tables at 256 entries */
#define TILEMAP_TILE_ID_COUNT 256

/* Collision layer configuration */
#define TILEMAP_LAYER_JNR_COLLISION 2     /* JNR collision layer index */
#define TILEMAP_LAYER_SURFACE_WALKABLE 1  /* Surface walkable/ground layer index */
#define TILEMAP_LAYER_SURFACE_COLLISION 2 /* Surface collision/blocking layer index */
#define TILEMAP_LAYER_SURFACE_DECO_BG 3   /* Surface decoration background layer (player overdraws) */
#define TILEMAP_LAYER_SURFACE_DECO_FG 4   /* Surface decoration foreground layer (overdraws player) */

/* Include atlas constants */
#include "tilemap_importer.h"

/* Tile bucket for batch rendering (now per-atlas-page instead of per-tile-id) */
typedef struct
{
    uint16_t uPageId;                     /* Atlas page index */
    uint16_t uCount;                      /* Number of tile instances in this bucket */
    int16_t aTileX[TILEMAP_BUCKET_SIZE];  /* Tile X positions */
    int16_t aTileY[TILEMAP_BUCKET_SIZE];  /* Tile Y positions */
    uint8_t aTileId[TILEMAP_BUCKET_SIZE]; /* Tile IDs (for looking up u/v in atlas) */
} tile_bucket_t;

/* Layer visibility data */
typedef struct
{
    tile_bucket_t *pBuckets;
    uint16_t uBucketCount;
    uint16_t uMaxBuckets;

    int16_t aBucketIndexByPageId[TILE_ATLAS_MAX_PAGES]; /* Lookup: pageId -> bucket index */

    bool bLastRectValid;
    int16_t iLastLeft, iLastTop, iLastRight, iLastBottom;
} tile_layer_visibility_t;

typedef struct
{
    tilemap_importer_t importer;
    tile_layer_visibility_t aLayerVisibility[TILEMAP_IMPORTER_MAX_LAYERS];
    bool bInitialized;

    /* Planet wrap */
    uint16_t uWorldWidthTiles; /* layer0 width; 1 revolution */
    uint16_t uWorldHeightTiles;

    /* Power-of-2 optimization: mask for fast modulo (width - 1), or 0 if not POT */
    uint16_t uWorldWidthMask;

} tilemap_t;

/* Main tilemap instance - accessible globally */
extern tilemap_t g_mainTilemap;

/* existing API */
bool tilemap_init(const char *_pMapFolder, tilemap_type_t _eType);
void tilemap_free(void);
void tilemap_update(void);
void tilemap_render_debug(void);
void tilemap_debug(void);

/* Get the folder name of the currently loaded tilemap.
 * Returns NULL if no tilemap is initialized. */
const char *tilemap_get_loaded_folder(void);

/* SURFACE rendering API (renders to intermediate surface with distortion) */
void tilemap_render_surface_begin(void); /* Render layers 0-3 (before player) */
void tilemap_render_surface_end(void);   /* Render layer 4 (after player) and composite with distortion */

/* JNR rendering API (direct to screen, no distortion) */
void tilemap_render_jnr_begin(void); /* Render layers 0-2 (before player) */
void tilemap_render_jnr_end(void);   /* Render layer 3 (after player) */

/* Convert world position to surface position (for rendering objects to surface before distortion) */
bool tilemap_world_to_surface(struct vec2 _vWorldPos, struct vec2i *_pOutSurface);

/* Convert world position to surface position with smooth (non-quantized) camera for player rendering */
bool tilemap_world_to_surface_smooth(struct vec2 _vWorldPos, struct vec2i *_pOutSurface);

/* Get the highest layer (2, 1, or 0) that contains a tile at the given screen position.
 * Returns -1 if no tile is found at any layer. */
int tilemap_get_highest_tile_layer(int _iScreenX, int _iScreenY);

/* Check if something can walk/land at the given world position.
 * _bCheckLanding: if true, also checks decoration layers 3 and 4 as landing blockers (for UFO landing validation).
 * Returns true if layer 1 has a tile (ground exists) and layer 2 has no tile (ground not blocked).
 * When _bCheckLanding is true, also returns false if layers 3 or 4 have tiles (decoration blocks landing). */
bool tilemap_can_walk(struct vec2 _vWorldPos, bool _bCheckLanding);

/* Check if a box (defined by center position and half extents) can walk/land on the tilemap.
 * _bUseTileBoundingBoxes: if true, uses full tile boxes; if false, uses trimmed collision rects.
 * _bCheckLanding: if true, also checks decoration layers 3 and 4 as landing blockers (for UFO landing validation).
 * Only checks adjacent tiles that could overlap with the box. Returns true if no collisions with layer 2 blocking tiles.
 * When _bCheckLanding is true, also returns false if layers 3 or 4 have tiles (decoration blocks landing). */
bool tilemap_can_walk_box(struct vec2 _vCenterPos, struct vec2 _vHalfExtents, bool _bUseTileBoundingBoxes, bool _bCheckLanding);

/* Check if a box (defined by center position and half extents) collides with a specific layer using trimmed rects.
 * Returns true if there is a collision with any solid tile in the specified layer. */
bool tilemap_check_collision_layer(struct vec2 _vCenterPos, struct vec2 _vHalfExtents, uint8_t _uLayerIndex);

/* Collision sweep result */
typedef struct
{
    float fTime;         /* Time of impact (0.0 to 1.0) */
    struct vec2 vNormal; /* Normal of surface hit */
    bool bHit;           /* True if collision detected */
    bool bCornerish;     /* True if collision is a corner (tNearX≈tNearY) */
} tilemap_sweep_result_t;

/* Collision types for sweep function */
typedef enum
{
    TILEMAP_COLLISION_JNR = 0,    /* Check collision with Layer 1 (Geometry) only */
    TILEMAP_COLLISION_SURFACE = 1 /* Check collision with Layer 2 (Blocking) OR empty Layer 1 (Water/Hole) */
} tilemap_collision_type_t;

/* Sweep a box against the tilemap to find the first collision point.
 * _vStartPos: center of box at start
 * _vDelta: movement vector (velocity * dt)
 * _vHalfExtents: half-width/height of box
 * _eType: collision type (JNR or SURFACE)
 * Returns sweep result containing time of impact (0..1) and normal. */
tilemap_sweep_result_t tilemap_sweep_box(struct vec2 _vStartPos, struct vec2 _vDelta, struct vec2 _vHalfExtents, tilemap_collision_type_t _eType);

/* Convert world position to screen position, adjusted by the spherical distortion of the tilemap.
 * This accounts for the spherical distortion effect applied during tilemap rendering.
 * Returns true if the position is within screen bounds, false otherwise. */
bool tilemap_world_to_screen_distorted(struct vec2 _vWorldPos, struct vec2i *_pOutScreen);

/* Wrap/normalize world X coordinate to canonical range [0, worldWidth * TILE_SIZE).
 * Returns the input unchanged when wrapping is disabled (JNR mode or tilemap not initialized).
 * This is used to keep entities within the wrapped world bounds. */
float tilemap_wrap_world_x(float _fWorldX);

/* Get world width in pixels (worldWidthTiles * TILE_SIZE).
 * Returns 0.0f if tilemap is not initialized. */
float tilemap_get_world_width_pixels(void);
