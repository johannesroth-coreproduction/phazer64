/* tilemap.c */
#include "tilemap.h"
#include "game_objects/gp_state.h"
#include "libdragon.h"
#include "math_helper.h"
#include "palette.h"
#include "profiler.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "rdpq_tri.h"
#include "resource_helper.h"
#include "ui.h"
#include <assert.h>
#include <fmath.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Rendering mode for unified render function */
typedef enum
{
    TILEMAP_RENDER_MODE_TEXTURE, /* Normal textured rendering */
    TILEMAP_RENDER_MODE_DEBUG    /* Debug collision box rendering */
} tilemap_render_mode_t;

/* =========================
   Layer Structure
   =========================

   JNR Mode (4 layers):
   - Layer 0: Background
   - Layer 1: Background detail
   - Layer 2: Collision (TILEMAP_LAYER_JNR_COLLISION)
   - Layer 3: Foreground detail

   SURFACE Mode (3 layers):
   - Layer 0: Background
   - Layer 1: Walkable/Ground (TILEMAP_LAYER_SURFACE_WALKABLE)
   - Layer 2: Collision/Blocking (TILEMAP_LAYER_SURFACE_COLLISION)
   ========================= */

/* =========================
   Tunables / feature flags
   ========================= */

#define TILEMAP_SPHERE_STRENGTH 0.065f /* existing subtle spherical X-shrink */
#define TILEMAP_SPHERE_CACHE_MAX 32
#define TILEMAP_CULL_MARGIN_X_TILES 1                                           /* render extra columns left+right */
#define TILEMAP_RENDER_ROWS 48 /* render rows count for spherical distortion */ // 48 SMALLEST NUMBER that fits in TMEM. 82% vs 91% with 120.

/* Main tilemap instance - accessible globally */
tilemap_t g_mainTilemap;

/* Intermediate surface for pre-distortion rendering */
static surface_t g_surfTemp;

/* Tilemap type - determines layer count, collision, and rendering behavior */
static tilemap_type_t s_eTilemapType = TILEMAP_TYPE_SURFACE;

/* Store current map folder name */
static char s_szCurrentMapFolder[256] = {0};

/* =========================
   Helpers
   ========================= */

/* Calculate scaled tile size from zoom (ensures >= 1) */
static inline int tilemap_calculate_scaled_tile_size(float _fZoom)
{
    float fTileStep = (float)TILE_SIZE * _fZoom;
    int iScaledSize = (int)fm_ceilf(fTileStep);
    if (iScaledSize <= 0)
        iScaledSize = 1;
    return iScaledSize;
}

/* Get tile data from bucket and atlas entry. Returns false if tile is invalid. */
static inline bool tilemap_get_tile_data(const tile_bucket_t *_pBucket, uint16_t _uTileIndex, int16_t *_piTileX, int16_t *_piTileY, uint8_t *_puTileId,
                                         tile_atlas_entry_t *_pAtlasEntry)
{
    *_piTileX = _pBucket->aTileX[_uTileIndex];
    *_piTileY = _pBucket->aTileY[_uTileIndex];
    *_puTileId = _pBucket->aTileId[_uTileIndex];

    if (!tilemap_importer_get_atlas_entry(&g_mainTilemap.importer, *_puTileId, _pAtlasEntry))
        return false;

    return true;
}

static inline void tilemap_layer_visibility_reset(tile_layer_visibility_t *_pVis)
{
    for (int i = 0; i < TILE_ATLAS_MAX_PAGES; ++i)
        _pVis->aBucketIndexByPageId[i] = -1;

    _pVis->uBucketCount = 0;
}

/* Optimized modulo: uses bitwise AND for power-of-2, falls back to % otherwise */
static inline int tilemap_mod_i(int _iX, int _iM, uint16_t _uMask)
{
    if (_uMask != 0)
    {
        /* Power-of-2: use bitwise AND (much faster) */
        int r = _iX & (int)_uMask;
        return r;
    }
    else
    {
        /* Not power-of-2: use standard modulo */
        int r = _iX % _iM;
        if (r < 0)
            r += _iM;
        return r;
    }
}

/* Optimized X wrapping: splits into tile coordinate + fractional part, wraps tile with fast mask */
static inline float tilemap_wrap_x_no_fmod(float _fX, uint16_t _uWorldWidthTiles, uint16_t _uWorldWidthMask)
{
    if (_uWorldWidthTiles == 0)
        return _fX;

    /* Split into whole tiles + fractional part in pixels */
    float fTileF = _fX * (1.0f / (float)TILE_SIZE);
    int32_t iTile = (int32_t)fm_floorf(fTileF);
    float fFracPx = _fX - (float)iTile * (float)TILE_SIZE; /* in [0..TILE_SIZE) */

    uint32_t uWrappedTile;
    if (_uWorldWidthMask != 0)
        uWrappedTile = ((uint32_t)iTile) & (uint32_t)_uWorldWidthMask;
    else
        uWrappedTile = (uint32_t)tilemap_mod_i(iTile, (int)_uWorldWidthTiles, 0);

    return (float)uWrappedTile * (float)TILE_SIZE + fFracPx;
}

static inline float tilemap_wrap01(float _f)
{
    _f -= fm_floorf(_f);
    if (_f < 0.0f)
        _f += 1.0f;
    return _f;
}

static inline int tilemap_round_to_int(float _f)
{
    return (int)fm_floorf(_f + 0.5f);
}

/* Convert world coordinates to tile coordinates */
static inline void tilemap_world_to_tile_coords(struct vec2 _vWorldPos, int *_piTileX, int *_piTileY)
{
    *_piTileX = (int)fm_floorf(_vWorldPos.fX / (float)TILE_SIZE);
    *_piTileY = (int)fm_floorf(_vWorldPos.fY / (float)TILE_SIZE);
}

/* Check if a layer is valid and has data */
static inline bool tilemap_layer_is_valid(const tilemap_layer_t *_pLayer)
{
    if (!_pLayer || _pLayer->uWidth == 0 || _pLayer->uHeight == 0)
        return false;

    /* Check appropriate storage is allocated */
    if (_pLayer->eStorage == TILEMAP_LAYER_STORAGE_DENSE)
        return _pLayer->ppData != NULL;
    else if (_pLayer->eStorage == TILEMAP_LAYER_STORAGE_SPARSE)
        return _pLayer->sparse.pEntries != NULL;
    else /* TILEMAP_LAYER_STORAGE_SINGLE */
        return true;
}

/* Helper: Quantize a value for rendering stability (inline to avoid function call overhead) */
static inline float tilemap_quantize_for_rendering(float _fValue, float _fZoom)
{
    float fQuantizeStep = 1.0f / _fZoom;
    return (float)round_to_int(_fValue / fQuantizeStep) * fQuantizeStep;
}

/* Helper: Calculate surface center and wrapped camera position (reused by rendering and conversion) */
static inline void tilemap_get_surface_transform(float *_pOutSurfCenterX, float *_pOutSurfCenterY, float *_pOutCamX, bool _bQuantize)
{
    float fZoom = camera_get_zoom(&g_mainCamera);
    float fCamX = g_mainCamera.vPos.fX;

    /* Optionally quantize camera position for stable rendering (prevents sub-pixel wobble).
     * The actual camera position stays smooth for proper lerping. */
    if (_bQuantize)
    {
        fCamX = tilemap_quantize_for_rendering(fCamX, fZoom);
    }

    fCamX = tilemap_wrap_x_no_fmod(fCamX, g_mainTilemap.uWorldWidthTiles, g_mainTilemap.uWorldWidthMask);

    float fSurfCenterX = (float)g_surfTemp.width * 0.5f;
    float fSurfCenterY = (float)g_surfTemp.height * 0.5f;

    if (_pOutSurfCenterX)
        *_pOutSurfCenterX = fSurfCenterX;
    if (_pOutSurfCenterY)
        *_pOutSurfCenterY = fSurfCenterY;
    if (_pOutCamX)
        *_pOutCamX = fCamX;
}

/* Wrap X coordinate and clamp Y coordinate for a layer (repeat top/bottom rows) */
static inline void tilemap_wrap_x_clamp_y(const tilemap_layer_t *_pLayer, int _iTileX, int _iTileY, int *_piWrappedX, int *_piClampedY, uint16_t _uWidthMask)
{
    *_piWrappedX = tilemap_mod_i(_iTileX, (int)_pLayer->uWidth, _uWidthMask);
    *_piClampedY = _iTileY;
    if (*_piClampedY < 0)
        *_piClampedY = 0; /* Repeat top row */
    else if (*_piClampedY >= (int)_pLayer->uHeight)
        *_piClampedY = (int)_pLayer->uHeight - 1; /* Repeat bottom row */
}

/* Resolve tile coordinates based on tilemap type (JNR: clamp both, SURFACE: wrap X clamp Y) */
static inline void tilemap_resolve_tile_coords(const tilemap_layer_t *_pLayer, int _iTileX, int _iTileY, int *_piSampleX, int *_piSampleY)
{
    if (s_eTilemapType == TILEMAP_TYPE_JNR)
    {
        /* JNR mode: repeat (clamp) on both axes */
        *_piSampleX = clampi(_iTileX, 0, (int)_pLayer->uWidth - 1);
        *_piSampleY = clampi(_iTileY, 0, (int)_pLayer->uHeight - 1);
    }
    else
    {
        /* SURFACE mode: wrap X, clamp Y */
        tilemap_wrap_x_clamp_y(_pLayer, _iTileX, _iTileY, _piSampleX, _piSampleY, g_mainTilemap.uWorldWidthMask);
    }
}

/* Get tile ID at world position for a specific layer. Returns TILEMAP_IMPORTER_EMPTY_TILE if layer is invalid or position is out of bounds. */
static inline uint8_t tilemap_get_tile_id_at_world_pos(struct vec2 _vWorldPos, uint8_t _uLayerIndex)
{
    if (!g_mainTilemap.bInitialized)
        return TILEMAP_IMPORTER_EMPTY_TILE;

    const tilemap_layer_t *pLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, _uLayerIndex);
    if (!tilemap_layer_is_valid(pLayer))
        return TILEMAP_IMPORTER_EMPTY_TILE;

    int iTileX, iTileY;
    tilemap_world_to_tile_coords(_vWorldPos, &iTileX, &iTileY);

    int iSampleX, iSampleY;
    tilemap_resolve_tile_coords(pLayer, iTileX, iTileY, &iSampleX, &iSampleY);

    return tilemap_layer_get_tile(pLayer, iSampleX, iSampleY);
}

static inline void tilemap_compute_camera_tile_rect(int16_t *_pLeft, int16_t *_pTop, int16_t *_pRight, int16_t *_pBottom)
{
    float fZoom = camera_get_zoom(&g_mainCamera);
    float fInvZoom = 1.0f / fZoom;

    float fHalfW = (float)g_mainCamera.vHalf.iX * fInvZoom;
    float fHalfH = (float)g_mainCamera.vHalf.iY * fInvZoom;

    /* For JNR mode, don't wrap X coordinates */
    float fCamX;

    if (s_eTilemapType == TILEMAP_TYPE_JNR)
    {
        fCamX = g_mainCamera.vPos.fX;
    }
    else
    {
        /* SURFACE/PLANET mode: Quantize BEFORE wrapping to match render logic.
         * This prevents a single-frame mismatch where Raw wraps differently than Quantized,
         * causing the visible rect to be on one side of the world and the render offset on the other. */
        fCamX = tilemap_quantize_for_rendering(g_mainCamera.vPos.fX, fZoom);
        fCamX = tilemap_wrap_x_no_fmod(fCamX, g_mainTilemap.uWorldWidthTiles, g_mainTilemap.uWorldWidthMask);
    }

    float fLeft = fCamX - fHalfW;
    float fTop = g_mainCamera.vPos.fY - fHalfH;
    float fRight = fCamX + fHalfW;
    float fBottom = g_mainCamera.vPos.fY + fHalfH;

    int iLeft = (int)fm_floorf(fLeft / (float)TILE_SIZE);
    int iTop = (int)fm_floorf(fTop / (float)TILE_SIZE);
    int iRight = (int)fm_ceilf(fRight / (float)TILE_SIZE) - 1;
    int iBottom = (int)fm_ceilf(fBottom / (float)TILE_SIZE) - 1;

#if TILEMAP_CULL_MARGIN_X_TILES > 0
    iLeft -= TILEMAP_CULL_MARGIN_X_TILES;
    iRight += TILEMAP_CULL_MARGIN_X_TILES;
#endif

    if (iLeft < INT16_MIN)
        iLeft = INT16_MIN;
    if (iTop < INT16_MIN)
        iTop = INT16_MIN;
    if (iRight > INT16_MAX)
        iRight = INT16_MAX;
    if (iBottom > INT16_MAX)
        iBottom = INT16_MAX;

    *_pLeft = (int16_t)iLeft;
    *_pTop = (int16_t)iTop;
    *_pRight = (int16_t)iRight;
    *_pBottom = (int16_t)iBottom;
}

/* =========================
   Sphere factor cache (quadrant-optimized: cache by absolute Y distance)
   ========================= */

static inline int32_t tilemap_get_sphere_factor_q16(int16_t _iSampleY, int16_t *_pACacheY, int32_t *_pACacheFq, uint8_t *_pUCacheCount, int16_t _iCenterY)
{
    /* Use absolute Y distance from center for quadrant mirroring */
    int16_t iAbsDeltaY = (int16_t)abs((int)_iSampleY - (int)_iCenterY);

    /* Search cache by absolute distance (not signed Y) */
    for (uint8_t i = 0; i < *_pUCacheCount; ++i)
    {
        if (_pACacheY[i] == iAbsDeltaY)
            return _pACacheFq[i];
    }

    float fFactor = 1.0f;

    if (_iCenterY > 0)
    {
        /* Use absolute deltaY since cos is even: cos(-x) = cos(x) */
        float fDeltaY = (float)iAbsDeltaY;
        float fLatScale = (FM_PI * 0.5f) / (float)_iCenterY;
        float fLatitude = fDeltaY * fLatScale;

        float fCosLat = fm_cosf(fLatitude);
        float fStrength = TILEMAP_SPHERE_STRENGTH;

        fFactor = (1.0f - fStrength) + (fStrength * fCosLat);

        if (fFactor < 0.0f)
            fFactor = 0.0f;
        if (fFactor > 1.0f)
            fFactor = 1.0f;
    }

    int32_t iFactorQ = (int32_t)(fFactor * 65536.0f + 0.5f);

    if (*_pUCacheCount < TILEMAP_SPHERE_CACHE_MAX)
    {
        /* Cache by absolute distance, not signed Y */
        _pACacheY[*_pUCacheCount] = iAbsDeltaY;
        _pACacheFq[*_pUCacheCount] = iFactorQ;
        (*_pUCacheCount)++;
    }

    return iFactorQ;
}

/* Apply spherical distortion to an X coordinate offset.
 * Formula: distortedX = centerX + (offsetX * factorQ) >> 16
 * Uses Q16 fixed-point arithmetic for precision. */
static inline int tilemap_apply_sphere_distortion_x(int _iCenterX, int _iOffsetX, int32_t _iFactorQ)
{
    return (int)_iCenterX + (int)(((int64_t)_iOffsetX * (int64_t)_iFactorQ + 0x8000) >> 16);
}

/* =========================
   Init / Free
   ========================= */

bool tilemap_init(const char *_pMapFolder, tilemap_type_t _eType)
{
    debugf("tilemap_init: %s\n", _pMapFolder);

    /* Always free existing tilemap first to avoid leaks if init is called multiple times */
    tilemap_free();

    if (!_pMapFolder)
        return false;

    s_eTilemapType = _eType;

    memset(&g_mainTilemap, 0, sizeof(tilemap_t));

    /* Store folder name */
    strncpy(s_szCurrentMapFolder, _pMapFolder, sizeof(s_szCurrentMapFolder) - 1);
    s_szCurrentMapFolder[sizeof(s_szCurrentMapFolder) - 1] = '\0';

    if (!tilemap_importer_init(&g_mainTilemap.importer, _pMapFolder, _eType))
    {
        debugf("Failed to initialize tilemap importer\n");
        return false;
    }

    /* cache world width (1 revolution) from layer 0 */
    const tilemap_layer_t *pL0 = tilemap_importer_get_layer(&g_mainTilemap.importer, 0);
    g_mainTilemap.uWorldWidthTiles = pL0 ? pL0->uWidth : 0;
    g_mainTilemap.uWorldHeightTiles = pL0 ? pL0->uHeight : 0;

    /* Check if width is power-of-2 and compute mask for fast modulo */
    if (g_mainTilemap.uWorldWidthTiles > 0)
    {
        uint16_t uWidth = g_mainTilemap.uWorldWidthTiles;
        /* Check if width is power-of-2: (width & (width - 1)) == 0 */
        if ((uWidth & (uWidth - 1)) == 0)
        {
            /* Power-of-2: mask is width - 1 */
            g_mainTilemap.uWorldWidthMask = uWidth - 1;
        }
        else
        {
            /* Not power-of-2: mask is 0 (use standard modulo) */
            g_mainTilemap.uWorldWidthMask = 0;
        }
    }
    else
    {
        g_mainTilemap.uWorldWidthMask = 0;
    }

    bool bAllocationSuccess = true;

    for (uint8_t i = 0; i < TILEMAP_IMPORTER_MAX_LAYERS; ++i)
    {
        tile_layer_visibility_t *pVis = &g_mainTilemap.aLayerVisibility[i];

        pVis->uMaxBuckets = (uint16_t)TILE_ATLAS_MAX_PAGES;
        pVis->uBucketCount = 0;

        pVis->pBuckets = (tile_bucket_t *)malloc(sizeof(tile_bucket_t) * (size_t)pVis->uMaxBuckets);
        if (!pVis->pBuckets)
        {
            debugf("Failed to allocate visibility buckets for layer %d\n", i);
            bAllocationSuccess = false;
            break;
        }

        memset(pVis->pBuckets, 0, sizeof(tile_bucket_t) * (size_t)pVis->uMaxBuckets);

        /* Flush cache after initial memset */
        CACHE_FLUSH_DATA(pVis->pBuckets, sizeof(tile_bucket_t) * (size_t)pVis->uMaxBuckets);

        tilemap_layer_visibility_reset(pVis);

        pVis->bLastRectValid = false;
        pVis->iLastLeft = 0;
        pVis->iLastTop = 0;
        pVis->iLastRight = -1;
        pVis->iLastBottom = -1;
    }

    if (!bAllocationSuccess)
    {
        for (uint8_t i = 0; i < TILEMAP_IMPORTER_MAX_LAYERS; ++i)
        {
            if (g_mainTilemap.aLayerVisibility[i].pBuckets)
            {
                free(g_mainTilemap.aLayerVisibility[i].pBuckets);
                g_mainTilemap.aLayerVisibility[i].pBuckets = NULL;
            }
            g_mainTilemap.aLayerVisibility[i].uBucketCount = 0;
            g_mainTilemap.aLayerVisibility[i].uMaxBuckets = 0;
            g_mainTilemap.aLayerVisibility[i].bLastRectValid = false;
        }

        tilemap_importer_free(&g_mainTilemap.importer);
        return false;
    }

    g_mainTilemap.bInitialized = true;

    /* Allocate intermediate surface (not needed for JNR mode) */
    if (s_eTilemapType != TILEMAP_TYPE_JNR)
    {
        /* Width: Screen (320) + Margin * 2 (16*2=32) = 352 */
        /* Height: Screen (240) */
        int iSurfWidth = 320 + (TILEMAP_CULL_MARGIN_X_TILES * TILE_SIZE * 2);
        g_surfTemp = surface_alloc(FMT_RGBA16, iSurfWidth, 240);
    }

    return true;
}

void tilemap_free(void)
{
    if (g_surfTemp.buffer)
    {
        surface_free(&g_surfTemp);
        memset(&g_surfTemp, 0, sizeof(surface_t));
    }

    for (uint8_t i = 0; i < TILEMAP_IMPORTER_MAX_LAYERS; ++i)
    {
        tile_layer_visibility_t *pVis = &g_mainTilemap.aLayerVisibility[i];

        if (pVis->pBuckets)
        {
            free(pVis->pBuckets);
            pVis->pBuckets = NULL;
        }

        pVis->uBucketCount = 0;
        pVis->uMaxBuckets = 0;

        for (int j = 0; j < TILE_ATLAS_MAX_PAGES; ++j)
            pVis->aBucketIndexByPageId[j] = -1;

        pVis->bLastRectValid = false;
    }

    tilemap_importer_free(&g_mainTilemap.importer);
    g_mainTilemap.bInitialized = false;
    s_szCurrentMapFolder[0] = '\0';
}

const char *tilemap_get_loaded_folder(void)
{
    if (!g_mainTilemap.bInitialized)
        return NULL;
    return s_szCurrentMapFolder;
}

/* =========================
   Update (now with X wrap sampling)
   ========================= */

void tilemap_update(void)
{
    if (!g_mainTilemap.bInitialized)
        return;

    int16_t iCamLeft = 0, iCamTop = 0, iCamRight = -1, iCamBottom = -1;
    tilemap_compute_camera_tile_rect(&iCamLeft, &iCamTop, &iCamRight, &iCamBottom);

    for (uint8_t uLayerIndex = 0; uLayerIndex < TILEMAP_IMPORTER_MAX_LAYERS; ++uLayerIndex)
    {
        const tilemap_layer_t *pLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, uLayerIndex);
        if (!tilemap_layer_is_valid(pLayer))
            continue;

        if (pLayer->eStorage == TILEMAP_LAYER_STORAGE_SINGLE)
            continue;

        tile_layer_visibility_t *pVis = &g_mainTilemap.aLayerVisibility[uLayerIndex];
        if (!pVis->pBuckets || pVis->uMaxBuckets == 0)
            continue;

        int16_t iLeft = iCamLeft;
        int16_t iTop = iCamTop;
        int16_t iRight = iCamRight;
        int16_t iBottom = iCamBottom;

        if (iBottom < iTop)
        {
            pVis->bLastRectValid = true;
            pVis->iLastLeft = iLeft;
            pVis->iLastTop = iTop;
            pVis->iLastRight = iRight;
            pVis->iLastBottom = iBottom;
            pVis->uBucketCount = 0;
            continue;
        }

        if (pVis->bLastRectValid && pVis->iLastLeft == iLeft && pVis->iLastTop == iTop && pVis->iLastRight == iRight && pVis->iLastBottom == iBottom)
        {
            continue;
        }

        pVis->bLastRectValid = true;
        pVis->iLastLeft = iLeft;
        pVis->iLastTop = iTop;
        pVis->iLastRight = iRight;
        pVis->iLastBottom = iBottom;

        tilemap_layer_visibility_reset(pVis);

        uint16_t uVisibleCount = 0;

        /* Sparse layer optimization: iterate stored tiles instead of visible area */
        if (pLayer->eStorage == TILEMAP_LAYER_STORAGE_SPARSE)
        {
            /* Iterate through all tiles in sparse hash table */
            const sparse_layer_data_t *pSparse = &pLayer->sparse;

            /* Empty layer fast path (0 tiles) */
            if (pSparse->uCapacity == 0 || !pSparse->pEntries)
                continue;

            /* Precompute wrapping logic once per layer (SURFACE mode only) */
            bool bNeedWrapCheck = false;
            int16_t iLayerWidth = (int16_t)pLayer->uWidth;

            if (s_eTilemapType != TILEMAP_TYPE_JNR && iLayerWidth > 0)
            {
                /* SURFACE mode: rect can have negative/wrapped coordinates */
                bNeedWrapCheck = (iLeft < 0 || iRight >= iLayerWidth);
            }

            for (uint16_t i = 0; i < pSparse->uCapacity; ++i)
            {
                const sparse_tile_entry_t *pEntry = &pSparse->pEntries[i];

                /* Skip empty slots */
                if (pEntry->uX == SPARSE_ENTRY_EMPTY)
                    continue;

                int16_t iTileX = (int16_t)pEntry->uX;
                int16_t iTileY = (int16_t)pEntry->uY;
                uint8_t uTileId = pEntry->uTileId;

                /* Y bounds check */
                if (iTileY < iTop || iTileY > iBottom)
                    continue;

                /* X visibility check with optional wrapping */
                bool bVisible = false;
                int16_t iTileXAdjusted = iTileX;

                if (!bNeedWrapCheck)
                {
                    /* Fast path: no wrapping needed (JNR or rect fully in bounds) */
                    bVisible = (iTileX >= iLeft && iTileX <= iRight);
                }
                else
                {
                    /* SURFACE mode wrapping: check if tile or wrapped equivalent is visible
                     * Example: tile X=63 in width 64, rect [-2, 10] → check 63, 63-64=-1 (visible!) */
                    if (iTileX >= iLeft && iTileX <= iRight)
                    {
                        bVisible = true;
                    }
                    else
                    {
                        /* Check wrapped positions */
                        int16_t iTileXMinus = iTileX - iLayerWidth;
                        int16_t iTileXPlus = iTileX + iLayerWidth;

                        if (iTileXMinus >= iLeft && iTileXMinus <= iRight)
                        {
                            bVisible = true;
                            iTileXAdjusted = iTileXMinus;
                        }
                        else if (iTileXPlus >= iLeft && iTileXPlus <= iRight)
                        {
                            bVisible = true;
                            iTileXAdjusted = iTileXPlus;
                        }
                    }
                }

                if (!bVisible)
                    continue;

                if (uVisibleCount >= (uint16_t)TILEMAP_MAX_VISIBLE_TILES)
                    break;

                /* Look up atlas entry to get pageId */
                tile_atlas_entry_t tAtlasEntry;
                if (!tilemap_importer_get_atlas_entry(&g_mainTilemap.importer, uTileId, &tAtlasEntry))
                    continue;

                uint8_t uPageId = tAtlasEntry.uPageIndex;
                int16_t iBucketIndex = pVis->aBucketIndexByPageId[uPageId];
                tile_bucket_t *pBucket = NULL;

                if (iBucketIndex < 0)
                {
                    if (pVis->uBucketCount >= pVis->uMaxBuckets)
                        continue;

                    uint16_t uNewIndex = pVis->uBucketCount++;
                    pBucket = &pVis->pBuckets[uNewIndex];
                    pBucket->uPageId = (uint16_t)uPageId;
                    pBucket->uCount = 0;

                    pVis->aBucketIndexByPageId[uPageId] = (int16_t)uNewIndex;
                }
                else
                {
                    pBucket = &pVis->pBuckets[(uint16_t)iBucketIndex];
                }

                if (pBucket->uCount < TILEMAP_BUCKET_SIZE)
                {
                    pBucket->aTileX[pBucket->uCount] = iTileXAdjusted; /* Use wrapped coordinate for correct rendering */
                    pBucket->aTileY[pBucket->uCount] = iTileY;
                    pBucket->aTileId[pBucket->uCount] = uTileId;
                    pBucket->uCount++;
                    uVisibleCount++;
                }
            }
            continue; /* Skip dense iteration path */
        }

        /* Dense layer path: iterate visible area (original algorithm) */

        /* Determine wrapping/clamping behavior based on tilemap type */
        /* Check if clamping/wrapping is needed for outer tiles */
        bool bNeedWrapX = (iLeft < 0) || (iRight >= (int16_t)pLayer->uWidth);
        bool bNeedClampY = (iTop < 0) || (iBottom >= (int16_t)pLayer->uHeight);

        for (int16_t iTileY = iTop; iTileY <= iBottom; ++iTileY)
        {
            int iSampleY = (int)iTileY;

            /* Clamp Y coordinate to repeat top and bottom rows (same logic for both modes) */
            if (bNeedClampY)
            {
                if (iSampleY < 0)
                    iSampleY = 0; /* Repeat top row */
                else if (iSampleY >= (int)pLayer->uHeight)
                    iSampleY = (int)pLayer->uHeight - 1; /* Repeat bottom row */
            }
            else
            {
                /* in-range fast path */
                if (iSampleY < 0 || iSampleY >= (int)pLayer->uHeight)
                    continue;
            }

            /* Get row pointer for maximum performance (dense storage guaranteed here) */
            const uint8_t *pRow = pLayer->ppData[iSampleY];

            for (int16_t iTileX = iLeft; iTileX <= iRight; ++iTileX)
            {
                int iSampleX = (int)iTileX;

                if (s_eTilemapType == TILEMAP_TYPE_JNR)
                {
                    /* JNR mode: clamp X coordinate to repeat left and right columns */
                    if (bNeedWrapX)
                    {
                        if (iSampleX < 0)
                            iSampleX = 0; /* Repeat left column */
                        else if (iSampleX >= (int)pLayer->uWidth)
                            iSampleX = (int)pLayer->uWidth - 1; /* Repeat right column */
                    }
                    else
                    {
                        /* in-range fast path */
                        if (iSampleX < 0 || iSampleX >= (int)pLayer->uWidth)
                            continue;
                    }
                }
                else
                {
                    /* SURFACE mode: wrap X coordinate */
                    if (bNeedWrapX)
                        iSampleX = tilemap_mod_i(iSampleX, (int)pLayer->uWidth, g_mainTilemap.uWorldWidthMask);
                    else
                    {
                        /* in-range fast path */
                        if (iSampleX >= (int)pLayer->uWidth)
                            continue;
                    }
                }

                uint8_t uTileId = pRow[iSampleX];
                if (uTileId == TILEMAP_IMPORTER_EMPTY_TILE)
                    continue;

                if (uVisibleCount >= (uint16_t)TILEMAP_MAX_VISIBLE_TILES)
                    break;

                /* Look up atlas entry to get pageId */
                tile_atlas_entry_t tAtlasEntry;
                if (!tilemap_importer_get_atlas_entry(&g_mainTilemap.importer, uTileId, &tAtlasEntry))
                    continue; /* Invalid tile or no atlas entry */

                uint8_t uPageId = tAtlasEntry.uPageIndex;
                int16_t iBucketIndex = pVis->aBucketIndexByPageId[uPageId];
                tile_bucket_t *pBucket = NULL;

                if (iBucketIndex < 0)
                {
                    if (pVis->uBucketCount >= pVis->uMaxBuckets)
                        continue;

                    uint16_t uNewIndex = pVis->uBucketCount++;
                    pBucket = &pVis->pBuckets[uNewIndex];
                    pBucket->uPageId = (uint16_t)uPageId;
                    pBucket->uCount = 0;

                    pVis->aBucketIndexByPageId[uPageId] = (int16_t)uNewIndex;
                }
                else
                {
                    pBucket = &pVis->pBuckets[(uint16_t)iBucketIndex];
                }

                if (pBucket->uCount < TILEMAP_BUCKET_SIZE)
                {
                    pBucket->aTileX[pBucket->uCount] = iTileX; /* store UNWRAPPED */
                    pBucket->aTileY[pBucket->uCount] = iTileY;
                    pBucket->aTileId[pBucket->uCount] = uTileId; /* Store tileId for u/v lookup */
                    pBucket->uCount++;
                    uVisibleCount++;
                }
            }

            if (uVisibleCount >= (uint16_t)TILEMAP_MAX_VISIBLE_TILES)
                break;
        }

        /* Flush cache for this layer's buckets after populating them */
        if (pVis->pBuckets && pVis->uBucketCount > 0)
        {
            CACHE_FLUSH_DATA(pVis->pBuckets, sizeof(tile_bucket_t) * pVis->uBucketCount);
        }
    }
}

/* =========================
   Render (Render to surface then composite with distortion)
   ========================= */

/* Debug colors for layers (used by tilemap_render_layers) */
static const enum eCGAColor s_aLayerColors[TILEMAP_IMPORTER_MAX_LAYERS] = {
    CGA_WHITE,       /* Layer 0: White */
    CGA_LIGHT_RED,   /* Layer 1: Red */
    CGA_YELLOW,      /* Layer 2: Yellow */
    CGA_LIGHT_GREEN, /* Layer 3: Green */
    CGA_LIGHT_CYAN   /* Layer 4: Cyan */
};

/* Unified internal rendering function
 * Handles iteration through layers/buckets/tiles with mode-specific rendering
 * Automatically adapts coordinate systems and behavior based on s_eTilemapType */
static void tilemap_render_layers(uint8_t _uStartLayer, uint8_t _uEndLayer, tilemap_render_mode_t _eMode)
{
    if (!g_mainTilemap.bInitialized)
        return;

    /* Flush all layer buckets before rendering to ensure cache coherency */
    for (uint8_t uLayerIndex = _uStartLayer; uLayerIndex <= _uEndLayer; ++uLayerIndex)
    {
        tile_layer_visibility_t *pVis = &g_mainTilemap.aLayerVisibility[uLayerIndex];
        if (pVis->pBuckets && pVis->uBucketCount > 0)
        {
            CACHE_FLUSH_DATA(pVis->pBuckets, sizeof(tile_bucket_t) * pVis->uBucketCount);
        }
    }

    float fZoom = camera_get_zoom(&g_mainCamera);
    float fTileStep = (float)TILE_SIZE * fZoom;

    /* Calculate base position for rendering based on tilemap type */
    float fBaseX, fBaseY;
    float fCenterX, fCenterY, fCamX;

    /* Quantize Y position for stability (common to both modes) */
    float fCamY = tilemap_quantize_for_rendering(g_mainCamera.vPos.fY, fZoom);

    if (s_eTilemapType == TILEMAP_TYPE_JNR)
    {
        /* JNR: Render relative to screen center, using quantized camera position for stability */
        fCenterX = (float)g_mainCamera.vHalf.iX;
        fCenterY = (float)g_mainCamera.vHalf.iY;
        fCamX = tilemap_quantize_for_rendering(g_mainCamera.vPos.fX, fZoom);
    }
    else
    {
        /* SURFACE: Render relative to surface center, using wrapped & quantized camera position */
        tilemap_get_surface_transform(&fCenterX, &fCenterY, &fCamX, true);
    }

    /* Calculate base position (common formula for both modes) */
    fBaseX = fCenterX - fCamX * fZoom;
    fBaseY = fCenterY - fCamY * fZoom;

    /* Setup for texture mode */
    int iScaledSize = tilemap_calculate_scaled_tile_size(fZoom);

    /* Optimization: Check if we can use integer math (zoom = 1.0, no fractional offset) */
    bool bZoom1 = (fabsf(fZoom - 1.0f) < 1e-6f);
    int iBaseXInt = tilemap_round_to_int(fBaseX);
    int iBaseYInt = tilemap_round_to_int(fBaseY);

    bool bBaseXIsInt = (fabsf(fBaseX - (float)iBaseXInt) < 1e-4f);
    bool bBaseYIsInt = (fabsf(fBaseY - (float)iBaseYInt) < 1e-4f);
    bool bUseIntegerMath = bZoom1 && bBaseXIsInt && bBaseYIsInt;
    int iTileStepInt = TILE_SIZE;

    /* Iterate layers */
    for (uint8_t uLayerIndex = _uStartLayer; uLayerIndex <= _uEndLayer; ++uLayerIndex)
    {
        const tilemap_layer_t *pLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, uLayerIndex);
        if (!tilemap_layer_is_valid(pLayer))
            continue;

        /* Fast path for single-tile layers (e.g. background fill) */
        if (pLayer->eStorage == TILEMAP_LAYER_STORAGE_SINGLE)
        {
            /* Skip empty layers */
            if (pLayer->uSingleTileId == TILEMAP_IMPORTER_EMPTY_TILE)
                continue;

            if (_eMode == TILEMAP_RENDER_MODE_TEXTURE)
            {
                /* Get atlas entry */
                tile_atlas_entry_t tAtlasEntry;
                if (!tilemap_importer_get_atlas_entry(&g_mainTilemap.importer, pLayer->uSingleTileId, &tAtlasEntry))
                    continue;

                const surface_t *pAtlasPage = tilemap_importer_get_atlas_page(&g_mainTilemap.importer, tAtlasEntry.uPageIndex);
                if (!pAtlasPage)
                    continue;

                /* Force standard mode for wrapping (Copy mode cannot wrap) */
                rdpq_set_mode_standard();
                rdpq_mode_alphacompare(uLayerIndex == 0 ? 0 : 1);

                /* Upload specific 16x16 tile to TMEM with repeating enabled */
                rdpq_texparms_t parms = {.s.repeats = REPEAT_INFINITE, .t.repeats = REPEAT_INFINITE};

                rdpq_tex_upload_sub(TILE0, pAtlasPage, &parms, tAtlasEntry.uU0, tAtlasEntry.uV0, tAtlasEntry.uU0 + TILE_SIZE, tAtlasEntry.uV0 + TILE_SIZE);

                /* Determine render bounds */
                int iRenderX1 = (s_eTilemapType == TILEMAP_TYPE_JNR) ? SCREEN_W : (int)g_surfTemp.width;
                int iRenderY1 = (s_eTilemapType == TILEMAP_TYPE_JNR) ? SCREEN_H : (int)g_surfTemp.height;

                /* Calculate S, T coordinates */
                float fS0 = -fBaseX / fZoom;
                float fT0 = -fBaseY / fZoom;

                if (bZoom1)
                {
                    rdpq_texture_rectangle(TILE0, 0, 0, iRenderX1, iRenderY1, fS0, fT0);
                }
                else
                {
                    float fS1 = ((float)iRenderX1 - fBaseX) / fZoom;
                    float fT1 = ((float)iRenderY1 - fBaseY) / fZoom;
                    rdpq_texture_rectangle_scaled(TILE0, 0, 0, iRenderX1, iRenderY1, fS0, fT0, fS1, fT1);
                }
            }
            else /* DEBUG */
            {
                /* Debug mode: set fill color for this layer using CGA palette */
                rdpq_set_mode_fill(palette_get_cga_color(s_aLayerColors[uLayerIndex]));

                /* Fill screen/surface with layer debug color */
                int iRenderX1 = (s_eTilemapType == TILEMAP_TYPE_JNR) ? SCREEN_W : (int)g_surfTemp.width;
                int iRenderY1 = (s_eTilemapType == TILEMAP_TYPE_JNR) ? SCREEN_H : (int)g_surfTemp.height;
                rdpq_fill_rectangle(0, 0, iRenderX1, iRenderY1);
            }
            continue;
        }

        /* Set RDP mode per layer based on render mode (DENSE/SPARSE path) */
        if (_eMode == TILEMAP_RENDER_MODE_TEXTURE)
        {
            /* Texture rendering mode setup */
            if (uLayerIndex == 0)
            {
                if (bZoom1)
                    rdpq_set_mode_copy(false);
                else
                    rdpq_set_mode_standard();
                rdpq_mode_alphacompare(0);
            }
            else if (uLayerIndex >= 1)
            {
                if (bZoom1)
                    rdpq_set_mode_copy(false);
                else
                    rdpq_set_mode_standard();
                rdpq_mode_alphacompare(1);
            }
        }
        else /* TILEMAP_RENDER_MODE_DEBUG */
        {
            /* Debug mode: set fill color for this layer using CGA palette */
            rdpq_set_mode_fill(palette_get_cga_color(s_aLayerColors[uLayerIndex]));
        }

        const tile_layer_visibility_t *pVis = &g_mainTilemap.aLayerVisibility[uLayerIndex];

        /* Iterate buckets */
        for (uint16_t uBucketIndex = 0; uBucketIndex < pVis->uBucketCount; ++uBucketIndex)
        {
            const tile_bucket_t *pBucket = &pVis->pBuckets[uBucketIndex];
            if (pBucket->uCount == 0)
                continue;

            /* Texture mode: upload atlas page once per bucket */
            if (_eMode == TILEMAP_RENDER_MODE_TEXTURE)
            {
                const surface_t *pAtlasPage = tilemap_importer_get_atlas_page(&g_mainTilemap.importer, (uint8_t)pBucket->uPageId);
                if (!pAtlasPage)
                    continue;

                rdpq_tex_upload(TILE0, pAtlasPage, NULL);
            }

            /* Iterate tiles */
            for (uint16_t uTileIndex = 0; uTileIndex < pBucket->uCount; ++uTileIndex)
            {
                int16_t iTileX, iTileY;
                uint8_t uTileId;
                tile_atlas_entry_t tAtlasEntry;

                if (!tilemap_get_tile_data(pBucket, uTileIndex, &iTileX, &iTileY, &uTileId, &tAtlasEntry))
                    continue;

                /* Calculate screen position */
                int iScreenX, iScreenY;
                if (bUseIntegerMath)
                {
                    iScreenX = iBaseXInt + iTileX * iTileStepInt;
                    iScreenY = iBaseYInt + iTileY * iTileStepInt;
                }
                else
                {
                    float fScreenX = fBaseX + (float)iTileX * fTileStep;
                    float fScreenY = fBaseY + (float)iTileY * fTileStep;
                    iScreenX = (int)fm_floorf(fScreenX);
                    iScreenY = (int)fm_floorf(fScreenY);
                }

                /* Render based on mode */
                if (_eMode == TILEMAP_RENDER_MODE_TEXTURE)
                {
                    /* Bounds check (JNR only - SURFACE renders to intermediate buffer) */
                    if (s_eTilemapType == TILEMAP_TYPE_JNR)
                    {
                        int iSize = bZoom1 ? TILE_SIZE : iScaledSize;
                        if (iScreenX + iSize < 0 || iScreenX >= SCREEN_W || iScreenY + iSize < 0 || iScreenY >= SCREEN_H)
                            continue;
                    }

                    /* Render textured tile */
                    /* Use simple rectangle if zoom is 1.0 (even if position was fractional/calculated via floats).
                     * This ensures compatibility with COPY mode which is set when bZoom1 is true. */
                    if (bZoom1)
                    {
                        rdpq_texture_rectangle(TILE0, iScreenX, iScreenY, iScreenX + TILE_SIZE, iScreenY + TILE_SIZE, (int)tAtlasEntry.uU0, (int)tAtlasEntry.uV0);
                    }
                    else
                    {
                        rdpq_texture_rectangle_scaled(TILE0,
                                                      iScreenX,
                                                      iScreenY,
                                                      iScreenX + iScaledSize,
                                                      iScreenY + iScaledSize,
                                                      (float)tAtlasEntry.uU0,
                                                      (float)tAtlasEntry.uV0,
                                                      (float)(tAtlasEntry.uU0 + TILE_SIZE),
                                                      (float)(tAtlasEntry.uV0 + TILE_SIZE));
                    }
                }
                else /* TILEMAP_RENDER_MODE_DEBUG */
                {
                    /* Get trimmed rect for debug visualization */
                    struct vec2i vTrimmedOffset = {0, 0};
                    struct vec2i vTrimmedSize = {0, 0};
                    bool bHasTrimmedRect = tilemap_importer_get_tile_trimmed_rect(&g_mainTilemap.importer, uTileId, &vTrimmedOffset, &vTrimmedSize);

                    if (!bHasTrimmedRect || vTrimmedSize.iX == 0 || vTrimmedSize.iY == 0)
                    {
                        vTrimmedOffset.iX = 0;
                        vTrimmedOffset.iY = 0;
                        vTrimmedSize.iX = TILE_SIZE;
                        vTrimmedSize.iY = TILE_SIZE;
                    }

                    /* Calculate trimmed rect in screen space */
                    float fTrimmedOffsetX = (float)vTrimmedOffset.iX * fZoom;
                    float fTrimmedOffsetY = (float)vTrimmedOffset.iY * fZoom;
                    float fTrimmedSizeX = (float)vTrimmedSize.iX * fZoom;
                    float fTrimmedSizeY = (float)vTrimmedSize.iY * fZoom;

                    float fTrimmedLeft = (float)iScreenX + fTrimmedOffsetX;
                    float fTrimmedRight = fTrimmedLeft + fTrimmedSizeX;
                    float fTrimmedTop = (float)iScreenY + fTrimmedOffsetY;
                    float fTrimmedBottom = fTrimmedTop + fTrimmedSizeY;

                    int iTrimmedLeftInt = (int)fm_floorf(fTrimmedLeft);
                    int iTrimmedRightInt = (int)fm_ceilf(fTrimmedRight);
                    int iTrimmedTopInt = (int)fm_floorf(fTrimmedTop);
                    int iTrimmedBottomInt = (int)fm_ceilf(fTrimmedBottom);

                    if (iTrimmedRightInt <= iTrimmedLeftInt)
                        iTrimmedRightInt = iTrimmedLeftInt + 1;
                    if (iTrimmedBottomInt <= iTrimmedTopInt)
                        iTrimmedBottomInt = iTrimmedTopInt + 1;

                    /* Draw debug rectangle */
                    rdpq_fill_rectangle(iTrimmedLeftInt, iTrimmedTopInt, iTrimmedRightInt, iTrimmedBottomInt);
                }
            }
        }
    }
}

void tilemap_render_surface_begin(void)
{
    if (!g_mainTilemap.bInitialized)
        return;

    if (!g_surfTemp.buffer)
        return;

    /* Attach to intermediate surface.
     * Note: We don't clear if we assume full tile coverage (opaque background).
     * If there are gaps in the tilemap, rdpq_attach_clear(&g_surfTemp, NULL) should be used.
     * Using rdpq_attach avoids the clear cost. */
    rdpq_attach(&g_surfTemp, NULL);

    /* Render layers 0-3 only (layer 4 is rendered after player in tilemap_render_surface_end) */
    tilemap_render_layers(0, TILEMAP_LAYER_SURFACE_DECO_BG, TILEMAP_RENDER_MODE_TEXTURE);

    /* Note: Do NOT detach here - caller may want to render additional objects to surface.
     * Caller must call rdpq_detach_wait() before calling tilemap_composite_distorted(). */
}

void tilemap_render_surface_end(void)
{
    if (!g_mainTilemap.bInitialized)
        return;

    if (!g_surfTemp.buffer)
        return;

    /* Render layer 4 (DECO FG) to intermediate surface - overdraws player */
    /* Note: We're still attached to g_surfTemp from tilemap_render_surface_begin() */
    tilemap_render_layers(TILEMAP_LAYER_SURFACE_DECO_FG, TILEMAP_LAYER_SURFACE_DECO_FG, TILEMAP_RENDER_MODE_TEXTURE);

    /* Composite surface to screen with row-based spherical distortion */

    /* Detach from temp surface and wait for RDP to finish rendering.
     * We MUST wait here because we immediately use g_surfTemp as a texture source.
     * Without waiting, we could read incomplete/partial data from the surface.
     * Note: Display surface is already attached from phazer.c render() function. */
    rdpq_detach_wait();

    /* Invalidate CPU cache for the surface we just rendered to.
     * The RDP wrote to RDRAM, so any CPU cache lines covering this area are stale/dirty.
     * If they are dirty (from previous CPU usage), they must be invalidated to prevent
     * random writebacks overwriting the RDP data or confusing the RSP.
     *
     * NOTE: We only do this if the buffer is in the CACHED segment (KSEG0: 0x80000000).
     * If surface_alloc returned uncached memory (KSEG1: 0xA0000000), invalidation is
     * unnecessary and invalid. */
    if (g_surfTemp.buffer)
    {
        uintptr_t uAddr = (uintptr_t)g_surfTemp.buffer;
        if (uAddr >= 0x80000000UL && uAddr < 0xA0000000UL)
        {
            data_cache_hit_invalidate(g_surfTemp.buffer, (g_surfTemp.stride * g_surfTemp.height));
        }
    }

    int iScreenW = SCREEN_W;
    int iScreenH = SCREEN_H;
    int iCenterY = iScreenH / 2;
    int iRows = TILEMAP_RENDER_ROWS;
    int iRowHeight = iScreenH / iRows;
    if (iRowHeight < 1)
        iRowHeight = 1;

    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);

    /* Distortion cache (per frame) */
    int16_t aCacheY[TILEMAP_SPHERE_CACHE_MAX];
    int32_t aCacheFq[TILEMAP_SPHERE_CACHE_MAX];
    uint8_t uCacheCount = 0;

    /* Pre-calculate constants outside loop (optimization) */
    float fSourceCenter = (float)g_surfTemp.width * 0.5f;
    float fScreenW = (float)iScreenW;
    const float fQ16ToFloat = 1.0f / 65536.0f; /* Pre-calculate division constant */
    int iSurfWidth = (int)g_surfTemp.width;    /* Pre-calculate surface width (used in loop) */

    /* Render each row with spherical distortion
     * NOTE: Cannot batch rows vertically due to TMEM limits (4KB).
     * Each row (352x5 RGBA16 = 3,520 bytes) fits, but 2 rows (7,040 bytes) exceeds TMEM.
     * The 48 uploads per frame is the main performance cost, but necessary for the distortion effect.
     *
     * PERFORMANCE BREAKDOWN:
     * - Tile rendering: ~hundreds of rdpq_texture_rectangle_scaled calls (one per visible tile)
     * - Distortion: 48 texture uploads + 48 render calls (unavoidable due to TMEM)
     * - Distortion factor calculation: Already cached, minimal cost
     */
    for (int iY = 0; iY < iScreenH; iY += iRowHeight)
    {
        int iH = iRowHeight;
        if (iY + iH > iScreenH)
            iH = iScreenH - iY;

        /* Calculate distortion factor for this row center (cached, fast) */
        int iSampleY = iY + iH / 2;
        int32_t iFactorQ = tilemap_get_sphere_factor_q16((int16_t)iSampleY, aCacheY, aCacheFq, &uCacheCount, (int16_t)iCenterY);
        float fFactor = (float)iFactorQ * fQ16ToFloat; /* Optimized: use pre-calculated constant */

        /* Calculate source rectangle width based on distortion */
        /* ScreenWidth = SourceWidth * Factor -> SourceWidth = ScreenWidth / Factor */
        float fSourceWidth = fScreenW / fFactor;

        /* Center source rect on surface */
        float fSourceLeft = fSourceCenter - fSourceWidth * 0.5f;
        float fSourceRight = fSourceLeft + fSourceWidth;

        /* Upload full-width row sub-rectangle from source texture (auto-handles TMEM) */
        /* Note: Uses rdpq_tex_upload_sub instead of building temporary surface */
        /* We upload the full row width (0 to width), then use texture coordinates to select the distorted portion */
        /* Y bounds: iY to iY+iH (row region in original surface) */
        rdpq_tex_upload_sub(TILE0, &g_surfTemp, NULL, 0, iY, iSurfWidth, iY + iH);

        /* Render rectangle - use non-scaled version if no scaling is applied */
        /* Destination: full screen width, current row height */
        /* Source: Use ORIGINAL texture coordinates (rdpq_tex_upload_sub requires original coords) */
        /* X coordinates: fSourceLeft to fSourceRight (the calculated distorted region) */
        /* Y coordinates: iY to iY+iH (the row region in original surface) */
        if (fabsf(fFactor - 1.0f) < 1e-6f)
        {
            /* No scaling: use integer math with rdpq_texture_rectangle */
            int iSourceLeft = tilemap_round_to_int(fSourceLeft);
            rdpq_texture_rectangle(TILE0, 0, iY, iScreenW, iY + iH, iSourceLeft, iY);
        }
        else
        {
            /* Scaling required: use rdpq_texture_rectangle_scaled */
            rdpq_texture_rectangle_scaled(TILE0, 0, iY, iScreenW, iY + iH, fSourceLeft, (float)iY, fSourceRight, (float)(iY + iH));
        }
    }
}

/* =========================
   JNR Render (direct to screen, no distortion)
   ========================= */

/* Public API wrappers for unified rendering function */
void tilemap_render_jnr_begin(void)
{
    tilemap_render_layers(0, 2, TILEMAP_RENDER_MODE_TEXTURE);
}

void tilemap_render_jnr_end(void)
{
    tilemap_render_layers(3, 3, TILEMAP_RENDER_MODE_TEXTURE);
}

void tilemap_render_debug(void)
{
    /* Debug mode renders all layers at once for visualization, no distortion in surface mode */
    uint8_t uMaxLayer = (s_eTilemapType == TILEMAP_TYPE_JNR) ? (TILEMAP_LAYER_COUNT_JNR - 1) : (TILEMAP_LAYER_COUNT_SURFACE - 1);
    tilemap_render_layers(0, uMaxLayer, TILEMAP_RENDER_MODE_DEBUG);
}

/* Debug function to output tilemap information */
void tilemap_debug(void)
{
    debugf("Tilemap: Initialized=%s, TileSize=%dx%d\n", g_mainTilemap.bInitialized ? "true" : "false", TILE_SIZE, TILE_SIZE);

    if (g_mainTilemap.bInitialized)
    {
        /* Check Layer 0 status */
        const tilemap_layer_t *pLayer0 = tilemap_importer_get_layer(&g_mainTilemap.importer, 0);
        if (pLayer0 && pLayer0->eStorage != TILEMAP_LAYER_STORAGE_SINGLE)
        {
            /* If dense/sparse and not empty */
            if (pLayer0->uTileCount > 0)
            {
                debugf("WARNING: Layer 0 not optimized (mixed tiles or holes). Consider using a single tile for background.\n");
            }
        }

        tilemap_importer_debug(&g_mainTilemap.importer);
    }
}

int tilemap_get_highest_tile_layer(int _iScreenX, int _iScreenY)
{
    if (!g_mainTilemap.bInitialized)
        return -1;

    /* Convert screen coordinates to world coordinates */
    struct vec2i vScreen = {_iScreenX, _iScreenY};
    struct vec2 vWorld;
    camera_screen_to_world(&g_mainCamera, vScreen, &vWorld);

    /* Check layers from highest (2) to lowest (0) */
    for (int iLayer = TILEMAP_IMPORTER_MAX_LAYERS - 1; iLayer >= 0; --iLayer)
    {
        uint8_t uTileId = tilemap_get_tile_id_at_world_pos(vWorld, (uint8_t)iLayer);
        if (uTileId != TILEMAP_IMPORTER_EMPTY_TILE)
        {
            return iLayer;
        }
    }

    return -1; /* No tile found */
}

bool tilemap_can_walk(struct vec2 _vWorldPos, bool _bCheckLanding)
{
    if (!g_mainTilemap.bInitialized)
        return false;

    /* Check walkable layer (ground) - must have a tile */
    uint8_t uTileWalkable = tilemap_get_tile_id_at_world_pos(_vWorldPos, TILEMAP_LAYER_SURFACE_WALKABLE);
    if (uTileWalkable == TILEMAP_IMPORTER_EMPTY_TILE)
        return false; /* No ground at this position */

    /* Check collision layer (blocking) - must have no tile */
    uint8_t uTileCollision = tilemap_get_tile_id_at_world_pos(_vWorldPos, TILEMAP_LAYER_SURFACE_COLLISION);
    if (uTileCollision != TILEMAP_IMPORTER_EMPTY_TILE)
        return false; /* Ground is blocked by collision layer */

    /* If checking landing, also verify decoration layers don't block landing */
    if (_bCheckLanding)
    {
        /* Check decoration background layer (layer 3) - must have no tile to allow landing */
        uint8_t uTileDecoBG = tilemap_get_tile_id_at_world_pos(_vWorldPos, TILEMAP_LAYER_SURFACE_DECO_BG);
        if (uTileDecoBG != TILEMAP_IMPORTER_EMPTY_TILE)
            return false; /* Landing blocked by decoration background */

        /* Check decoration foreground layer (layer 4) - must have no tile to allow landing */
        uint8_t uTileDecoFG = tilemap_get_tile_id_at_world_pos(_vWorldPos, TILEMAP_LAYER_SURFACE_DECO_FG);
        if (uTileDecoFG != TILEMAP_IMPORTER_EMPTY_TILE)
            return false; /* Landing blocked by decoration foreground */
    }

    return true; /* Ground exists and is not blocked */
}

/* Helper: Check if two axis-aligned boxes intersect */
static inline bool boxes_intersect(float _fBox1Left, float _fBox1Right, float _fBox1Top, float _fBox1Bottom, float _fBox2Left, float _fBox2Right, float _fBox2Top,
                                   float _fBox2Bottom)
{
    return !(_fBox1Right < _fBox2Left || _fBox1Left > _fBox2Right || _fBox1Bottom < _fBox2Top || _fBox1Top > _fBox2Bottom);
}

/* Helper: Check if a box collides with tiles in a specific layer.
 * Returns true if collision found, false otherwise.
 * _bUseTileBoundingBoxes: if true, uses full tile boxes; if false, uses trimmed rects. */
static bool tilemap_check_collision_with_layer(float _fPlayerLeft, float _fPlayerRight, float _fPlayerTop, float _fPlayerBottom, int _iTileLeft, int _iTileRight, int _iTileTop,
                                               int _iTileBottom, const tilemap_layer_t *_pLayer, bool _bUseTileBoundingBoxes)
{
    if (!tilemap_layer_is_valid(_pLayer))
        return false;

    for (int iTileY = _iTileTop; iTileY <= _iTileBottom; ++iTileY)
    {
        for (int iTileX = _iTileLeft; iTileX <= _iTileRight; ++iTileX)
        {
            int iSampleX, iSampleY;
            tilemap_resolve_tile_coords(_pLayer, iTileX, iTileY, &iSampleX, &iSampleY);

            uint8_t uTileId = tilemap_layer_get_tile(_pLayer, iSampleX, iSampleY);
            if (uTileId == TILEMAP_IMPORTER_EMPTY_TILE)
                continue;

            /* Calculate tile bounds */
            float fTileWorldX = (float)iTileX * (float)TILE_SIZE;
            float fTileWorldY = (float)iTileY * (float)TILE_SIZE;
            float fTileLeft, fTileRight, fTileTop, fTileBottom;

            if (_bUseTileBoundingBoxes)
            {
                /* Full tile bounding box */
                fTileLeft = fTileWorldX;
                fTileRight = fTileWorldX + (float)TILE_SIZE;
                fTileTop = fTileWorldY;
                fTileBottom = fTileWorldY + (float)TILE_SIZE;
            }
            else
            {
                /* Get trimmed rect */
                struct vec2i vTrimmedOffset = {0, 0};
                struct vec2i vTrimmedSize = {0, 0};
                if (!tilemap_importer_get_tile_trimmed_rect(&g_mainTilemap.importer, uTileId, &vTrimmedOffset, &vTrimmedSize))
                {
                    /* Fallback to full tile */
                    vTrimmedSize.iX = TILE_SIZE;
                    vTrimmedSize.iY = TILE_SIZE;
                }

                if (vTrimmedSize.iX <= 0 || vTrimmedSize.iY <= 0)
                    continue;

                fTileLeft = fTileWorldX + (float)vTrimmedOffset.iX;
                fTileRight = fTileLeft + (float)vTrimmedSize.iX;
                fTileTop = fTileWorldY + (float)vTrimmedOffset.iY;
                fTileBottom = fTileTop + (float)vTrimmedSize.iY;
            }

            /* Check collision */
            if (boxes_intersect(_fPlayerLeft, _fPlayerRight, _fPlayerTop, _fPlayerBottom, fTileLeft, fTileRight, fTileTop, fTileBottom))
                return true;
        }
    }

    return false;
}

bool tilemap_can_walk_box(struct vec2 _vCenterPos, struct vec2 _vHalfExtents, bool _bUseTileBoundingBoxes, bool _bCheckLanding)
{
    if (!g_mainTilemap.bInitialized)
        return false;

    /* Calculate box bounds */
    float fBoxLeft = _vCenterPos.fX - _vHalfExtents.fX;
    float fBoxRight = _vCenterPos.fX + _vHalfExtents.fX;
    float fBoxTop = _vCenterPos.fY - _vHalfExtents.fY;
    float fBoxBottom = _vCenterPos.fY + _vHalfExtents.fY;

    /* Check north/south boundaries: treat out-of-bounds as unwalkable */
    if (g_mainTilemap.uWorldHeightTiles > 0)
    {
        float fSouthBoundary = (float)(g_mainTilemap.uWorldHeightTiles * TILE_SIZE);
        if (fBoxTop < 0.0f || fBoxBottom > fSouthBoundary)
            return false; /* Out of bounds */
    }

    /* Convert to tile coordinates once */
    int iTileLeft = (int)fm_floorf(fBoxLeft / (float)TILE_SIZE);
    int iTileRight = (int)fm_floorf(fBoxRight / (float)TILE_SIZE);
    int iTileTop = (int)fm_floorf(fBoxTop / (float)TILE_SIZE);
    int iTileBottom = (int)fm_floorf(fBoxBottom / (float)TILE_SIZE);

    /* Get SURFACE layers */
    const tilemap_layer_t *pCollisionLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, TILEMAP_LAYER_SURFACE_COLLISION);
    const tilemap_layer_t *pWalkableLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, TILEMAP_LAYER_SURFACE_WALKABLE);

    /* 1. Check collision layer (layer 2) - return false if box collides with blocking tiles */
    if (tilemap_check_collision_with_layer(fBoxLeft, fBoxRight, fBoxTop, fBoxBottom, iTileLeft, iTileRight, iTileTop, iTileBottom, pCollisionLayer, _bUseTileBoundingBoxes))
    {
        return false; /* Blocked by collision tiles */
    }

    /* 2. Check walkable layer (layer 1) - must have ground support
     * For walkability, we need to verify the box is supported by ground tiles.
     * We check corners to ensure sufficient ground coverage (fast and effective for most cases).
     * Note: Using corners instead of full collision check is intentional for performance and
     * because we want point-sampling for ground existence, not continuous collision. */
    if (!tilemap_layer_is_valid(pWalkableLayer))
        return false;

    struct vec2 aCorners[4] = {{fBoxLeft, fBoxTop}, {fBoxRight, fBoxTop}, {fBoxLeft, fBoxBottom}, {fBoxRight, fBoxBottom}};
    for (int i = 0; i < 4; ++i)
    {
        if (tilemap_get_tile_id_at_world_pos(aCorners[i], TILEMAP_LAYER_SURFACE_WALKABLE) == TILEMAP_IMPORTER_EMPTY_TILE)
            return false; /* No ground support at corner */
    }

    /* 3. If checking landing, verify decoration layers don't block (early exit optimization) */
    if (_bCheckLanding)
    {
        /* Check decoration background layer (layer 3) - return false if box collides */
        const tilemap_layer_t *pDecoBGLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, TILEMAP_LAYER_SURFACE_DECO_BG);
        if (tilemap_check_collision_with_layer(fBoxLeft, fBoxRight, fBoxTop, fBoxBottom, iTileLeft, iTileRight, iTileTop, iTileBottom, pDecoBGLayer, _bUseTileBoundingBoxes))
        {
            return false; /* Landing blocked by decoration background */
        }

        /* Check decoration foreground layer (layer 4) - return false if box collides */
        const tilemap_layer_t *pDecoFGLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, TILEMAP_LAYER_SURFACE_DECO_FG);
        if (tilemap_check_collision_with_layer(fBoxLeft, fBoxRight, fBoxTop, fBoxBottom, iTileLeft, iTileRight, iTileTop, iTileBottom, pDecoFGLayer, _bUseTileBoundingBoxes))
        {
            return false; /* Landing blocked by decoration foreground */
        }
    }

    return true;
}

/* Check if a box collides with a specific layer using trimmed rects */
bool tilemap_check_collision_layer(struct vec2 _vCenterPos, struct vec2 _vHalfExtents, uint8_t _uLayerIndex)
{
    if (!g_mainTilemap.bInitialized)
        return false;

    /* Calculate player box bounds */
    float fPlayerLeft = _vCenterPos.fX - _vHalfExtents.fX;
    float fPlayerRight = _vCenterPos.fX + _vHalfExtents.fX;
    float fPlayerTop = _vCenterPos.fY - _vHalfExtents.fY;
    float fPlayerBottom = _vCenterPos.fY + _vHalfExtents.fY;

    /* Check north/south boundaries */
    if (g_mainTilemap.uWorldHeightTiles > 0)
    {
        float fSouthBoundary = (float)(g_mainTilemap.uWorldHeightTiles * TILE_SIZE);
        if (fPlayerTop < 0.0f || fPlayerBottom > fSouthBoundary)
            return true; /* Collision with boundary */
    }

    /* Convert to tile coordinates */
    int iTileLeft = (int)fm_floorf(fPlayerLeft / (float)TILE_SIZE);
    int iTileRight = (int)fm_floorf(fPlayerRight / (float)TILE_SIZE);
    int iTileTop = (int)fm_floorf(fPlayerTop / (float)TILE_SIZE);
    int iTileBottom = (int)fm_floorf(fPlayerBottom / (float)TILE_SIZE);

    /* Get the specified layer */
    const tilemap_layer_t *pLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, _uLayerIndex);

    /* Check collision with layer using trimmed rects */
    return tilemap_check_collision_with_layer(fPlayerLeft, fPlayerRight, fPlayerTop, fPlayerBottom, iTileLeft, iTileRight, iTileTop, iTileBottom, pLayer, false);
}

/* Helper: Sweep AABB vs AABB (Ray vs AABB in Minkowski space) */
static bool sweep_aabb(struct vec2 _vOrigin, struct vec2 _vDelta, struct vec2 _vPadding, struct vec2 _vTargetMin, struct vec2 _vTargetMax, float *_pfTime, struct vec2 *_pvNormal,
                       bool *_pbCornerish)
{
    /* Expand target by padding (Minkowski sum) */
    struct vec2 vMin = vec2_sub(_vTargetMin, _vPadding);
    struct vec2 vMax = vec2_add(_vTargetMax, _vPadding);

    /* Note: "Starting inside" check removed to allow penetrating objects to get a valid normal/time (0.0)
       so slide response can work (push out) instead of getting stuck. */

    /* Avoid division by zero */
    float fScaleX = 1.0f / (_vDelta.fX == 0.0f ? 1e-8f : _vDelta.fX);
    float fScaleY = 1.0f / (_vDelta.fY == 0.0f ? 1e-8f : _vDelta.fY);

    float t1x = (vMin.fX - _vOrigin.fX) * fScaleX;
    float t2x = (vMax.fX - _vOrigin.fX) * fScaleX;
    float t1y = (vMin.fY - _vOrigin.fY) * fScaleY;
    float t2y = (vMax.fY - _vOrigin.fY) * fScaleY;

    float tNearX = fminf(t1x, t2x);
    float tFarX = fmaxf(t1x, t2x);
    float tNearY = fminf(t1y, t2y);
    float tFarY = fmaxf(t1y, t2y);

    if (tNearX > tFarY || tNearY > tFarX)
        return false;

    float tNear = fmaxf(tNearX, tNearY);
    float tFar = fminf(tFarX, tFarY);

    if (tNear >= 1.0f || tFar <= 0.0f)
        return false;

    *_pfTime = tNear < 0.0f ? 0.0f : tNear;

    if (_pbCornerish)
        *_pbCornerish = (fabsf(tNearX - tNearY) < 1e-2f);

    /* Determine normal with bias to keep corner hits stable.
       If tNearX and tNearY are very close, pick the axis whose movement component is larger.
       This avoids alternating normals on corners and tiny bumps. */
    float fNearDiff = fabsf(tNearX - tNearY);
    bool bCornerish = (fNearDiff < 1e-3f);

    bool bUseX;
    if (bCornerish)
    {
        float fAbsDx = fabsf(_vDelta.fX);
        float fAbsDy = fabsf(_vDelta.fY);

        /* Prefer the dominant axis; if nearly equal, default to X for determinism */
        if (fAbsDx > fAbsDy * 1.001f)
            bUseX = true;
        else if (fAbsDy > fAbsDx * 1.001f)
            bUseX = false;
        else
            bUseX = true; /* diagonal tie */
    }
    else
    {
        bUseX = (tNearX > tNearY);
    }

    if (bUseX)
    {
        *_pvNormal = (_vDelta.fX < 0.0f) ? vec2_make(1.0f, 0.0f) : vec2_make(-1.0f, 0.0f);
    }
    else
    {
        *_pvNormal = (_vDelta.fY < 0.0f) ? vec2_make(0.0f, 1.0f) : vec2_make(0.0f, -1.0f);
    }

    return true;
}

tilemap_sweep_result_t tilemap_sweep_box(struct vec2 _vStartPos, struct vec2 _vDelta, struct vec2 _vHalfExtents, tilemap_collision_type_t _eType)
{
    tilemap_sweep_result_t result = {1.0f, {0.0f, 0.0f}, false, false};

    if (!g_mainTilemap.bInitialized)
        return result;

    /* Determine which layer(s) to check based on collision type */
    /* JNR: Check Layer 1 (Geometry). Surface: Check Layer 2 (Blocking) AND Layer 1 (Water/Hole) */

    /* Calculate bounds of swept box */
    float fStartLeft = _vStartPos.fX - _vHalfExtents.fX;
    float fStartRight = _vStartPos.fX + _vHalfExtents.fX;
    float fStartTop = _vStartPos.fY - _vHalfExtents.fY;
    float fStartBottom = _vStartPos.fY + _vHalfExtents.fY;

    float fEndLeft = fStartLeft + _vDelta.fX;
    float fEndRight = fStartRight + _vDelta.fX;
    float fEndTop = fStartTop + _vDelta.fY;
    float fEndBottom = fStartBottom + _vDelta.fY;

    float fMinX = fminf(fStartLeft, fEndLeft);
    float fMaxX = fmaxf(fStartRight, fEndRight);
    float fMinY = fminf(fStartTop, fEndTop);
    float fMaxY = fmaxf(fStartBottom, fEndBottom);

    /* Convert to tile coordinates (range to check) */
    /* Add a small margin to be safe */
    int iTileMinX = (int)fm_floorf((fMinX - 1.0f) / (float)TILE_SIZE);
    int iTileMaxX = (int)fm_ceilf((fMaxX + 1.0f) / (float)TILE_SIZE);
    int iTileMinY = (int)fm_floorf((fMinY - 1.0f) / (float)TILE_SIZE);
    int iTileMaxY = (int)fm_ceilf((fMaxY + 1.0f) / (float)TILE_SIZE);

    /* Check north/south boundaries */
    if (g_mainTilemap.uWorldHeightTiles > 0)
    {
        float fWorldHeight = (float)(g_mainTilemap.uWorldHeightTiles * TILE_SIZE);
        if (fMaxY > fWorldHeight)
        {
            /* Check collision with bottom boundary */
            struct vec2 vBottomMin = {-100000.0f, fWorldHeight};
            struct vec2 vBottomMax = {100000.0f, fWorldHeight + 100.0f};
            float fTime;
            struct vec2 vNormal;
            bool bCornerish;
            if (sweep_aabb(_vStartPos, _vDelta, _vHalfExtents, vBottomMin, vBottomMax, &fTime, &vNormal, &bCornerish))
            {
                if (fTime < result.fTime)
                {
                    result.fTime = fTime;
                    result.vNormal = vNormal;
                    result.bHit = true;
                    result.bCornerish = bCornerish;
                }
            }
        }
        if (fMinY < 0.0f)
        {
            /* Check collision with top boundary */
            struct vec2 vTopMin = {-100000.0f, -100.0f};
            struct vec2 vTopMax = {100000.0f, 0.0f};
            float fTime;
            struct vec2 vNormal;
            bool bCornerish;
            if (sweep_aabb(_vStartPos, _vDelta, _vHalfExtents, vTopMin, vTopMax, &fTime, &vNormal, &bCornerish))
            {
                if (fTime < result.fTime)
                {
                    result.fTime = fTime;
                    result.vNormal = vNormal;
                    result.bHit = true;
                    result.bCornerish = bCornerish;
                }
            }
        }
    }

    /* Pre-fetch reference layer (Layer 0) for wrapping calculations */
    const tilemap_layer_t *pRefLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, 0);
    if (!tilemap_layer_is_valid(pRefLayer))
        return result;

    /* Iterate tiles */
    for (int iTileY = iTileMinY; iTileY <= iTileMaxY; ++iTileY)
    {
        for (int iTileX = iTileMinX; iTileX <= iTileMaxX; ++iTileX)
        {
            int iSampleX, iSampleY;
            tilemap_resolve_tile_coords(pRefLayer, iTileX, iTileY, &iSampleX, &iSampleY);

            /* Perform collision check based on type */
            bool bIsCollision = false;
            uint8_t uTileIdToCheck = TILEMAP_IMPORTER_EMPTY_TILE;

            if (_eType == TILEMAP_COLLISION_SURFACE)
            {
                /* SURFACE: Collision if walkable has NO tile (water/hole) OR collision has tile (blocking) */
                const tilemap_layer_t *pCollisionLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, TILEMAP_LAYER_SURFACE_COLLISION);
                const tilemap_layer_t *pWalkableLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, TILEMAP_LAYER_SURFACE_WALKABLE);

                uint8_t uTileCollision = (tilemap_layer_is_valid(pCollisionLayer)) ? tilemap_layer_get_tile(pCollisionLayer, iSampleX, iSampleY) : TILEMAP_IMPORTER_EMPTY_TILE;
                uint8_t uTileWalkable = (tilemap_layer_is_valid(pWalkableLayer)) ? tilemap_layer_get_tile(pWalkableLayer, iSampleX, iSampleY) : TILEMAP_IMPORTER_EMPTY_TILE;

                if (uTileWalkable == TILEMAP_IMPORTER_EMPTY_TILE)
                {
                    /* Hit water/hole (empty walkable) */
                    bIsCollision = true;
                    uTileIdToCheck = TILEMAP_IMPORTER_EMPTY_TILE;
                }
                else if (uTileCollision != TILEMAP_IMPORTER_EMPTY_TILE)
                {
                    /* Hit collision/blocking layer */
                    bIsCollision = true;
                    uTileIdToCheck = uTileCollision;
                }
            }
            else /* TILEMAP_COLLISION_JNR */
            {
                /* JNR: Collision if collision layer has tile */
                const tilemap_layer_t *pLayer = tilemap_importer_get_layer(&g_mainTilemap.importer, TILEMAP_LAYER_JNR_COLLISION);
                uint8_t uTileId = (tilemap_layer_is_valid(pLayer)) ? tilemap_layer_get_tile(pLayer, iSampleX, iSampleY) : TILEMAP_IMPORTER_EMPTY_TILE;

                if (uTileId != TILEMAP_IMPORTER_EMPTY_TILE)
                {
                    bIsCollision = true;
                    uTileIdToCheck = uTileId;
                }
            }

            if (!bIsCollision)
                continue;

            /* Calculate tile bounds */
            float fTileWorldX = (float)iTileX * (float)TILE_SIZE;
            float fTileWorldY = (float)iTileY * (float)TILE_SIZE;
            struct vec2 vTileMin, vTileMax;

            /* Use trimmed rect if available (and valid tile ID) */
            struct vec2i vTrimmedOffset = {0, 0};
            struct vec2i vTrimmedSize = {0, 0};
            bool bHasTrimmed = false;

            if (uTileIdToCheck != TILEMAP_IMPORTER_EMPTY_TILE)
            {
                bHasTrimmed = tilemap_importer_get_tile_trimmed_rect(&g_mainTilemap.importer, uTileIdToCheck, &vTrimmedOffset, &vTrimmedSize);
            }

            if (bHasTrimmed && vTrimmedSize.iX > 0 && vTrimmedSize.iY > 0)
            {
                vTileMin.fX = fTileWorldX + (float)vTrimmedOffset.iX;
                vTileMin.fY = fTileWorldY + (float)vTrimmedOffset.iY;
                vTileMax.fX = vTileMin.fX + (float)vTrimmedSize.iX;
                vTileMax.fY = vTileMin.fY + (float)vTrimmedSize.iY;
            }
            else
            {
                /* Fallback to full tile (always used for "Water" collision) */
                vTileMin.fX = fTileWorldX;
                vTileMin.fY = fTileWorldY;
                vTileMax.fX = fTileWorldX + (float)TILE_SIZE;
                vTileMax.fY = fTileWorldY + (float)TILE_SIZE;
            }

            float fTime;
            struct vec2 vNormal;
            bool bCornerish;
            if (sweep_aabb(_vStartPos, _vDelta, _vHalfExtents, vTileMin, vTileMax, &fTime, &vNormal, &bCornerish))
            {
                /* If we found a closer collision */
                if (fTime < result.fTime)
                {
                    result.fTime = fTime;
                    result.vNormal = vNormal;
                    result.bHit = true;
                    result.bCornerish = bCornerish;
                }
            }
        }
    }

    return result;
}

/* Internal helper: Convert world position to surface position with optional quantization */
static inline bool tilemap_world_to_surface_internal(struct vec2 _vWorldPos, struct vec2i *_pOutSurface, bool _bQuantize)
{
    if (!_pOutSurface)
        return false;

    if (!g_mainTilemap.bInitialized || !g_surfTemp.buffer)
    {
        /* Fallback to standard camera conversion if tilemap not initialized */
        camera_world_to_screen(&g_mainCamera, _vWorldPos, _pOutSurface);
        return true;
    }

    /* Get surface transform with optional quantization */
    float fSurfCenterX, fSurfCenterY, fCamX;
    tilemap_get_surface_transform(&fSurfCenterX, &fSurfCenterY, &fCamX, _bQuantize);

    /* Convert world to surface: surface_pos = (world - wrapped_cam) * zoom + surf_center */
    float fZoom = camera_get_zoom(&g_mainCamera);
    float fCamY = _bQuantize ? tilemap_quantize_for_rendering(g_mainCamera.vPos.fY, fZoom) : g_mainCamera.vPos.fY;

    float fBaseX = fSurfCenterX - fCamX * fZoom;
    float fBaseY = fSurfCenterY - fCamY * fZoom;

    float fSurfX = fBaseX + _vWorldPos.fX * fZoom;
    float fSurfY = fBaseY + _vWorldPos.fY * fZoom;

    /* Output (floored to match tile rendering) */
    _pOutSurface->iX = (int)fm_floorf(fSurfX);
    _pOutSurface->iY = (int)fm_floorf(fSurfY);

    /* Check if the position is within surface bounds */
    return (_pOutSurface->iX >= 0 && _pOutSurface->iX < (int)g_surfTemp.width && _pOutSurface->iY >= 0 && _pOutSurface->iY < (int)g_surfTemp.height);
}

/* Convert world position to surface position (for rendering objects to surface before distortion) */
bool tilemap_world_to_surface(struct vec2 _vWorldPos, struct vec2i *_pOutSurface)
{
    return tilemap_world_to_surface_internal(_vWorldPos, _pOutSurface, true);
}

/* Convert world position to surface position with smooth (non-quantized) camera for player rendering */
bool tilemap_world_to_surface_smooth(struct vec2 _vWorldPos, struct vec2i *_pOutSurface)
{
    return tilemap_world_to_surface_internal(_vWorldPos, _pOutSurface, false);
}

/* Convert world position to screen position, adjusted by the spherical distortion of the tilemap */
bool tilemap_world_to_screen_distorted(struct vec2 _vWorldPos, struct vec2i *_pOutScreen)
{
    if (!_pOutScreen)
        return false;

    if (!g_mainTilemap.bInitialized)
    {
        /* Fallback to standard camera conversion if tilemap not initialized */
        camera_world_to_screen(&g_mainCamera, _vWorldPos, _pOutScreen);
        return true;
    }

    /* Convert world to screen using standard camera (with quantization in SURFACE mode) */
    struct vec2i vScreenBase;
    camera_world_to_screen(&g_mainCamera, _vWorldPos, &vScreenBase);

    /* Get screen center for distortion calculation */
    int16_t iCenterX = (int16_t)g_mainCamera.vHalf.iX;
    int16_t iCenterY = (int16_t)g_mainCamera.vHalf.iY;

    /* Calculate distortion factor for this Y coordinate */
    int16_t aCacheY[TILEMAP_SPHERE_CACHE_MAX];
    int32_t aCacheFq[TILEMAP_SPHERE_CACHE_MAX];
    uint8_t uCacheCount = 0;

    int32_t iFactorQ = tilemap_get_sphere_factor_q16((int16_t)vScreenBase.iY, aCacheY, aCacheFq, &uCacheCount, iCenterY);

    /* Apply spherical distortion to X coordinate */
    int iOffsetX = vScreenBase.iX - (int)iCenterX;
    int iDistortedX = tilemap_apply_sphere_distortion_x((int)iCenterX, iOffsetX, iFactorQ);

    /* Y coordinate is not distorted, only X */
    _pOutScreen->iX = iDistortedX;
    _pOutScreen->iY = vScreenBase.iY;

    /* Check if the position is within screen bounds */
    return (iDistortedX >= 0 && iDistortedX < SCREEN_W && vScreenBase.iY >= 0 && vScreenBase.iY < SCREEN_H);
}

/* Wrap/normalize world X coordinate to canonical range [0, worldWidth * TILE_SIZE) */
float tilemap_wrap_world_x(float _fWorldX)
{
    /* Only wrap if tilemap is initialized and in wrapping mode (not JNR) */
    if (!g_mainTilemap.bInitialized || s_eTilemapType == TILEMAP_TYPE_JNR || g_mainTilemap.uWorldWidthTiles == 0)
        return _fWorldX;

    /* Use existing wrap function that handles the wrapping logic */
    return tilemap_wrap_x_no_fmod(_fWorldX, g_mainTilemap.uWorldWidthTiles, g_mainTilemap.uWorldWidthMask);
}

/* Get world width in pixels */
float tilemap_get_world_width_pixels(void)
{
    if (!g_mainTilemap.bInitialized)
        return 0.0f;

    return (float)g_mainTilemap.uWorldWidthTiles * (float)TILE_SIZE;
}
