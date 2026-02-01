#pragma once

#include "math2d.h"
#include "sprite.h"
#include <stdbool.h>
#include <stdint.h>

/* Maximum number of tiles that can be loaded (limited by uint8_t indexing) */
#define TILEMAP_IMPORTER_MAX_TILES 255

/* Maximum number of layers in a tilemap */
#define TILEMAP_IMPORTER_MAX_LAYERS 5

/* Layer counts per tilemap type */
#define TILEMAP_LAYER_COUNT_SURFACE 5 /* Surface/Planet tilemaps use 5 layers */
#define TILEMAP_LAYER_COUNT_JNR 4     /* JNR tilemaps use 4 layers */

/* Tilemap type enum - determines layer count and collision configuration */
typedef enum
{
    TILEMAP_TYPE_SURFACE = 0, /* Surface/Planet tilemaps */
    TILEMAP_TYPE_JNR = 1      /* JNR tilemaps */
} tilemap_type_t;

/* Value representing an empty tile in the tilemap data */
#define TILEMAP_IMPORTER_EMPTY_TILE 255

/* Marker for empty slots in sparse layer hash tables */
#define SPARSE_ENTRY_EMPTY 0xFFFF

/* Atlas page configuration */
#define TILE_ATLAS_TILES_PER_PAGE 8
#define TILE_ATLAS_PAGE_WIDTH 64
#define TILE_ATLAS_PAGE_HEIGHT 32
#define TILE_ATLAS_MAX_PAGES ((TILEMAP_IMPORTER_MAX_TILES + TILE_ATLAS_TILES_PER_PAGE - 1) / TILE_ATLAS_TILES_PER_PAGE) /* 32 */

/* Trimmed bounding box for a tile sprite */
typedef struct
{
    struct vec2i vOffset; /* Offset of trimmed rect relative to original sprite top-left */
    struct vec2i vSize;   /* Dimensions of trimmed rect (width, height) */
} tile_trimmed_rect_t;

/* Atlas entry: maps tileId to atlas page and UV coordinates */
typedef struct
{
    uint8_t uPageIndex; /* Which atlas page contains this tile */
    uint8_t uU0;        /* U coordinate (0-63) within the page */
    uint8_t uV0;        /* V coordinate (0-31) within the page */
} tile_atlas_entry_t;

/* Layer storage type - determines memory layout optimization */
typedef enum
{
    TILEMAP_LAYER_STORAGE_DENSE = 0,  /* Full 2D array (optimal for densely filled layers) */
    TILEMAP_LAYER_STORAGE_SPARSE = 1, /* Hash table of non-empty tiles (optimal for sparse layers) */
    TILEMAP_LAYER_STORAGE_SINGLE = 2  /* Single tile ID repeated across the entire layer (or all empty) */
} tilemap_layer_storage_t;

/* Sparse tile entry - stores a single tile at a specific position */
typedef struct
{
    uint16_t uX;      /* X coordinate in layer */
    uint16_t uY;      /* Y coordinate in layer */
    uint8_t uTileId;  /* Tile ID at this position */
    uint8_t uPadding; /* Padding for alignment */
} sparse_tile_entry_t;

/* Sparse layer hash table - open addressing with linear probing */
typedef struct
{
    sparse_tile_entry_t *pEntries; /* Hash table entries (NULL if dense layer) */
    uint16_t uCapacity;            /* Hash table capacity (power of 2) */
    uint16_t uCount;               /* Number of non-empty tiles stored */
} sparse_layer_data_t;

/* Tilemap layer structure - supports both dense and sparse storage */
typedef struct
{
    tilemap_layer_storage_t eStorage; /* Storage type (dense or sparse) */
    uint16_t uWidth;                  /* Width of the layer in tiles */
    uint16_t uHeight;                 /* Height of the layer in tiles */
    uint16_t uTileCount;              /* Number of non-empty tiles (for memory reporting) */

    /* Dense storage (used when eStorage == TILEMAP_LAYER_STORAGE_DENSE) */
    uint8_t **ppData; /* Row pointers (ppData[y][x]) */
    uint8_t *pData;   /* Contiguous backing store (uWidth * uHeight) */

    /* Sparse storage (used when eStorage == TILEMAP_LAYER_STORAGE_SPARSE) */
    sparse_layer_data_t sparse; /* Hash table of non-empty tiles */

    /* Single storage (used when eStorage == TILEMAP_LAYER_STORAGE_SINGLE) */
    uint8_t uSingleTileId; /* Tile ID repeated across layer */
} tilemap_layer_t;

/* Tilemap importer structure */
typedef struct
{
    sprite_t **ppTileSprites;                             /* Array of loaded tile sprites */
    tile_trimmed_rect_t *pTileTrimmedRects;               /* Array of trimmed bounding boxes per tile ID */
    uint16_t uTileCount;                                  /* Number of loaded tiles */
    tilemap_layer_t aLayers[TILEMAP_IMPORTER_MAX_LAYERS]; /* Layers of tile data (max 4) */
    uint8_t uLayerCount;                                  /* Actual number of layers loaded */
    tilemap_type_t eType;                                 /* Type of tilemap */
    bool bInitialized;                                    /* Whether the tilemap has been initialized */

    /* Atlas pages: runtime-optimized texture pages */
    surface_t *pAtlasPages;            /* Array of atlas page surfaces (RGBA16, 64x32 each) */
    uint16_t uAtlasPageCount;          /* Number of allocated atlas pages */
    tile_atlas_entry_t *pAtlasEntries; /* Lookup table: tileId -> {pageIndex, u0, v0} */
} tilemap_importer_t;

bool tilemap_importer_init(tilemap_importer_t *_pImporter, const char *_pMapFolder, tilemap_type_t _eType);
void tilemap_importer_free(tilemap_importer_t *_pImporter);

sprite_t *tilemap_importer_get_tile_sprite(const tilemap_importer_t *_pImporter, uint8_t _uTileIndex);
const tilemap_layer_t *tilemap_importer_get_layer(const tilemap_importer_t *_pImporter, uint8_t _uLayerIndex);

/* Get the trimmed bounding box rectangle for a given tile ID.
 * Returns true if the tile ID is valid and the trimmed rect was successfully retrieved, false otherwise. */
bool tilemap_importer_get_tile_trimmed_rect(const tilemap_importer_t *_pImporter, uint8_t _uTileIndex, struct vec2i *_pOutOffset, struct vec2i *_pOutSize);

/* Get the atlas entry for a given tile ID.
 * Returns true if the tile ID is valid and the atlas entry was successfully retrieved, false otherwise. */
bool tilemap_importer_get_atlas_entry(const tilemap_importer_t *_pImporter, uint8_t _uTileIndex, tile_atlas_entry_t *_pOutEntry);

/* Get an atlas page surface by page index.
 * Returns NULL if page index is invalid. */
const surface_t *tilemap_importer_get_atlas_page(const tilemap_importer_t *_pImporter, uint8_t _uPageIndex);

/* Get tile ID at position (x, y) in a layer. Works for both dense and sparse storage.
 * Returns TILEMAP_IMPORTER_EMPTY_TILE if position is out of bounds or no tile at position.
 * NOTE: Caller must ensure _pLayer is valid and coordinates are within bounds.
 * This is inline for maximum performance - used in collision detection. */
static inline uint8_t tilemap_layer_get_tile(const tilemap_layer_t *_pLayer, int _iX, int _iY)
{
    if (_pLayer->eStorage == TILEMAP_LAYER_STORAGE_DENSE)
    {
        /* Dense: Direct array access */
        return _pLayer->ppData[_iY][_iX];
    }
    else if (_pLayer->eStorage == TILEMAP_LAYER_STORAGE_SPARSE)
    {
        /* Sparse: Hash table lookup - extern function in .c file */
        extern uint8_t tilemap_layer_sparse_get(const sparse_layer_data_t *_pSparse, uint16_t _uX, uint16_t _uY);
        return tilemap_layer_sparse_get(&_pLayer->sparse, (uint16_t)_iX, (uint16_t)_iY);
    }
    else /* TILEMAP_LAYER_STORAGE_SINGLE */
    {
        /* Single: Return the repeated tile ID */
        return _pLayer->uSingleTileId;
    }
}

void tilemap_importer_debug(const tilemap_importer_t *_pImporter);
