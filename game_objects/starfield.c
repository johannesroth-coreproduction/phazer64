#include "starfield.h"
#include "../camera.h" /* for screen_cull_rect helper */
#include "../external/squirrel_noise5.h"
#include "../frame_time.h"
#include "../math2d.h"
#include "../palette.h"
#include "../resource_helper.h"
#include "rdpq_mode.h"
#include "ufo.h" /* for ufo_get_speed() */
#include <math.h>
#include <stdio.h> /* for snprintf */

/* -------------------------------------------------------------------------
 * Compile-time configuration
 * ------------------------------------------------------------------------- */

/* Total number of stars in the starfield. */
#define STARFIELD_NUM_STARS 4096
#define STARFIELD_CELL_SIZE 512

#define STARFIELD_STREAK_LENGTH_SCALE 1.0f

/* Velocity threshold for streak activation (when streaks start growing).
 * Streaks can only grow when speed >= this threshold.
 */
#define STARFIELD_STREAK_ACTIVATION_THRESHOLD 5.0f

/* Velocity threshold for streak deactivation (when streaks should shrink to zero).
 * When speed < this threshold, treat speed as zero for streak length calculation.
 * Should be lower than activation threshold to provide hysteresis and prevent flickering.
 */
#define STARFIELD_STREAK_DEACTIVATION_THRESHOLD 4.5f

/* Length threshold for switching to dot rendering (for performance).
 * When smoothed streak length < size * this factor, render as a dot instead of streak.
 * Small value ensures we only use dots when length is essentially zero.
 */
#define STARFIELD_DOT_RENDER_THRESHOLD 0.5f

/* Smoothing factor for streak acceleration (when growing).
 * Controls how quickly the streak length/direction reacts to velocity increases.
 * value ~ 0.05f -> roughly 60 frames (1 sec) to reach 95% of target.
 */
#define STARFIELD_STREAK_LERP_FACTOR_ACCEL 0.1f

/* Smoothing factor for streak deceleration (when shrinking).
 * Higher value = faster deceleration. Should be > STARFIELD_STREAK_LERP_FACTOR_ACCEL.
 */
#define STARFIELD_STREAK_LERP_FACTOR_DECEL 0.15f

/* Number of parallax layers:
 *  - 0, 1   : stars size 1 (small)
 *  - 2, 3   : stars size 2 (medium)
 *  - 4      : stars size 3 (large)
 *  - 5      : planets
 */
#define STARFIELD_NUM_LAYERS 6
#define STARFIELD_PLANET_LAYER_INDEX (STARFIELD_NUM_LAYERS - 1)

/* Number of decorative planets. */
#define STARFIELD_NUM_PLANETS 256

/* Number of different planet sprite variants. */
#define STARFIELD_ORIGINAL_PLANET_TYPES 0
#define STARFIELD_STARFIELD_PLANET_COUNT 19
#define STARFIELD_NUM_PLANET_TYPES (STARFIELD_ORIGINAL_PLANET_TYPES + STARFIELD_STARFIELD_PLANET_COUNT)

/* Distribution weights per layer (more small stars, fewer big ones).
 * Last entry (planet layer) is 0 so stars never spawn there.
 */
static const int m_aLayerWeights[STARFIELD_NUM_LAYERS] = {8, 6, 4, 2, 1, 0};

/* Pixel size per layer (drawn as squares via rdpq_fill_rectangle).
 * Planet layer uses sprites, so size 0 is fine here.
 */
static const int m_aLayerSizes[STARFIELD_NUM_LAYERS] = {1, 1, 2, 2, 3, 0};

/* Speed factor per layer, multiplied with the global base velocity. */
static const float m_aLayerSpeedFactors[STARFIELD_NUM_LAYERS] = {0.1f, 0.15f, 0.25f, 0.3f, 0.4f, 0.075f}; // planet layer used for nebulas etc now, super slow movement

/* Extra "universe margin" around the screen for each layer (in screen
 * widths / heights). Smaller stars get a small margin, big stars/planets
 * a larger one so they do not disappear too quickly.
 */
static const float m_aLayerMarginFactors[STARFIELD_NUM_LAYERS] = {0.25f, 0.4f, 0.6f, 0.9f, 1.3f, 2.0f};

/* Star color set used in the starfield (subset of CGA palette). */
#define STARFIELD_NUM_COLOR_CHOICES 8

/* Order: must match weights array below. */
static const enum eCGAColor m_aStarColors[STARFIELD_NUM_COLOR_CHOICES] = {
    /* common neutrals */
    CGA_WHITE,
    CGA_LIGHT_GREY,
    CGA_DARK_GREY,
    /* somewhat rare colors */
    CGA_LIGHT_BLUE,
    CGA_LIGHT_RED,
    /* rare colors */
    CGA_YELLOW,
    CGA_LIGHT_GREEN,
    CGA_LIGHT_MAGENTA};

/* Color distribution weights (parallel to m_aStarColors).
 * white, light grey, dark grey most; others less common.
 */
static const int m_aStarColorWeights[STARFIELD_NUM_COLOR_CHOICES] = {
    40, /* white */
    40, /* light grey */
    40, /* dark grey */
    1,  /* bright blue (light blue) */
    1,  /* bright red (light red) */
    1,  /* bright yellow */
    1,  /* bright green (light green) */
    1   /* bright purple (light magenta) */
};

static const char *m_aPlanetSpritePaths[STARFIELD_ORIGINAL_PLANET_TYPES] = {
    /* 56 planets from planets_starfield will be loaded dynamically */
};

/* -------------------------------------------------------------------------
 * LOD Configuration
 * ------------------------------------------------------------------------- */

/* Zoom thresholds for layer culling (Based on GLOBAL Camera Zoom).
 * If the camera's global zoom is LESS than this value, the layer is culled.
 * Set to 0.0f to never cull based on zoom.
 *
 * Examples:
 * - 0.5f: Layer disappears when zoomed out further than 0.5x (half size).
 * - 0.0f: Layer never disappears.
 */
#define STARFIELD_CULL_ZOOM_SMALL 0.5f
#define STARFIELD_CULL_ZOOM_MEDIUM 0.3f
#define STARFIELD_CULL_ZOOM_LARGE 0.0f

/* Planet zoom responsiveness (0.0 = no scaling, 1.0 = full scaling with zoom).
 * Lower values = planets barely shrink/grow with zoom changes.
 * This multiplier is applied to the planet layer's zoom weight factor.
 */
#define STARFIELD_PLANET_ZOOM_RESPONSE 1.0f

/* -------------------------------------------------------------------------
 * Internal data structures
 * ------------------------------------------------------------------------- */

typedef struct star_s
{
    struct vec2 vPos;      /* Layer-local "world" position. */
    int iLayer;            /* Layer index [0..STARFIELD_NUM_LAYERS-1]. */
    enum eCGAColor eColor; /* CGA palette index. */
} star_t;

typedef struct star_layer_bounds_s
{
    struct vec2 vMin;  /* min x/y in layer universe */
    struct vec2 vMax;  /* max x/y in layer universe */
    struct vec2 vSize; /* vMax - vMin (wrap distances) */
} star_layer_bounds_t;

typedef struct starfield_planet_s
{
    struct vec2 vPos; /* Screen-space position. */
    int iLayer;       /* STARFIELD_PLANET_LAYER_INDEX */
    sprite_t *pSprite;
} starfield_planet_t;

typedef struct starfield_grid_bounds_s
{
    float fViewMinX, fViewMinY, fViewMaxX, fViewMaxY;
    int iGridMinX, iGridMinY, iGridMaxX, iGridMaxY;
    struct vec2 vLayerCamPos;
} starfield_grid_bounds_t;

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static int m_iScreenW = 0;
static int m_iScreenH = 0;

/* Static allocation for stars to avoid heap fragmentation */
static star_t m_aStars[STARFIELD_NUM_STARS];
static int m_iStarCount = 0;

static int m_iLayerWeightSum = 0;
static int m_iStarColorWeightSum = 0;

/* Optimization: Precomputed color lookup table to avoid loop in hot path */
static uint8_t m_aColorLookup[256];
static int m_iColorLookupSize = 0;

static star_layer_bounds_t m_aLayerBounds[STARFIELD_NUM_LAYERS];

/* Per-layer motion + geometry (derived every frame from base velocity). */
static struct vec2 m_aLayerVel[STARFIELD_NUM_LAYERS];
static struct vec2 m_aLayerDir[STARFIELD_NUM_LAYERS];         /* normalized dir (post fallback) */
static struct vec2 m_aLayerRight[STARFIELD_NUM_LAYERS];       /* perpendicular (post fallback) */
static float m_aLayerLen[STARFIELD_NUM_LAYERS];               /* final streak length */
static float m_aLayerHalfWidth[STARFIELD_NUM_LAYERS];         /* half of star size */
static int m_aLayerRadius[STARFIELD_NUM_LAYERS];              /* coarse cull radius */
static int m_aLayerCullMargin[STARFIELD_NUM_LAYERS];          /* streak cull margin */
static bool m_aLayerDrawAsDot[STARFIELD_NUM_LAYERS];          /* dot vs streak flag */
static float m_aLayerDiagShift[STARFIELD_NUM_LAYERS];         /* diagonal bias fix per layer */
static struct vec2 m_aLayerBackOffset[STARFIELD_NUM_LAYERS];  /* dir * (-halfSize) */
static struct vec2 m_aLayerFrontOffset[STARFIELD_NUM_LAYERS]; /* dir * (halfSize + len) */
static struct vec2 m_aLayerPerpOffset[STARFIELD_NUM_LAYERS];  /* right * halfSize */
static float m_aLayerZoomScale[STARFIELD_NUM_LAYERS];         /* cached per-layer zoom scale */

/* Decorative planets. */
static starfield_planet_t m_aPlanets[STARFIELD_NUM_PLANETS];

/* Storage for unique planet sprites to allow clean freeing */
static sprite_t *m_aUniquePlanetSprites[STARFIELD_NUM_PLANET_TYPES] = {0};

/* Base velocity (runtime configurable). */
static float m_fBaseVelX = 0.0f;
static float m_fBaseVelY = 0.0f;

/* Smoothed velocity for streak calculations (lagging behind base velocity). */
static float m_fStreakVelX = 0.0f;
static float m_fStreakVelY = 0.0f;

/* Smoothed length factor for streak growth/shrinkage (lagging behind target). */
static float m_fStreakLenFactor = 0.0f;

/* Streak mode flag (set by update, used by render). */
// static int m_bStreakMode = 0;

/* Global flicker phase. */
static float m_fFlickerFrame = 0.0f;

/* Initialization guard. */
static bool m_bInitialized = false;

/* RNG State */
static uint32_t m_uSeed = 0;
// static int m_iNoiseIndex = 0;

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static void starfield_clear_layer_state(int _iLayer)
{
    m_aLayerDir[_iLayer] = vec2_zero();
    m_aLayerRight[_iLayer] = vec2_zero();
    m_aLayerLen[_iLayer] = 0.0f;
    m_aLayerHalfWidth[_iLayer] = 0.0f;
    m_aLayerRadius[_iLayer] = 0;
    m_aLayerCullMargin[_iLayer] = 0;
    m_aLayerDrawAsDot[_iLayer] = true;
    m_aLayerDiagShift[_iLayer] = 0.0f;
    m_aLayerBackOffset[_iLayer] = vec2_zero();
    m_aLayerFrontOffset[_iLayer] = vec2_zero();
    m_aLayerPerpOffset[_iLayer] = vec2_zero();
}

/* Optimization: Calculate grid bounds common to stars and planets */
static void starfield_calc_grid_bounds(int _iLayer, starfield_grid_bounds_t *_pOut)
{
    float fSpeedMul = m_aLayerSpeedFactors[_iLayer];
    _pOut->vLayerCamPos = vec2_scale(g_mainCamera.vPos, fSpeedMul);

    /* Determine visible world bounds for this layer, accounting for Zoom */
    float fScale = m_aLayerZoomScale[_iLayer];
    if (fScale < 0.1f)
        fScale = 0.1f; /* Safety clamp */

    float fScaledScreenW = (float)m_iScreenW / fScale;
    float fScaledScreenH = (float)m_iScreenH / fScale;

    /* Optimization: Use fixed small margins in world units.
     * We don't need margins proportional to screen size, just enough to cover
     * the maximum object size (streaks or planet sprites) to avoid popping.
     * - Planet layer: 128 units (covers large sprites)
     * - Star layers: 64 units (covers max streak length)
     * This works at any zoom level because streaks/sprites scale with zoom.
     */
    float fFixedMargin = (_iLayer == STARFIELD_PLANET_LAYER_INDEX) ? 128.0f : 64.0f;

    _pOut->fViewMinX = _pOut->vLayerCamPos.fX - fScaledScreenW * 0.5f - fFixedMargin;
    _pOut->fViewMinY = _pOut->vLayerCamPos.fY - fScaledScreenH * 0.5f - fFixedMargin;
    _pOut->fViewMaxX = _pOut->vLayerCamPos.fX + fScaledScreenW * 0.5f + fFixedMargin;
    _pOut->fViewMaxY = _pOut->vLayerCamPos.fY + fScaledScreenH * 0.5f + fFixedMargin;

    /* Convert to grid coordinates */
    _pOut->iGridMinX = (int)floorf(_pOut->fViewMinX / STARFIELD_CELL_SIZE);
    _pOut->iGridMinY = (int)floorf(_pOut->fViewMinY / STARFIELD_CELL_SIZE);
    _pOut->iGridMaxX = (int)floorf(_pOut->fViewMaxX / STARFIELD_CELL_SIZE);
    _pOut->iGridMaxY = (int)floorf(_pOut->fViewMaxY / STARFIELD_CELL_SIZE);
}

/* Helper to build color lookup table */
static void starfield_build_color_lookup(void)
{
    m_iColorLookupSize = 0;
    for (int i = 0; i < STARFIELD_NUM_COLOR_CHOICES; ++i)
    {
        int iWeight = m_aStarColorWeights[i];
        for (int w = 0; w < iWeight; ++w)
        {
            if (m_iColorLookupSize < 256)
            {
                m_aColorLookup[m_iColorLookupSize++] = (uint8_t)i;
            }
        }
    }
}

static int starfield_deterministic_choice(const int *_aWeights, int _iCount, int _iWeightSum, uint32_t _uSeed)
{
    /* Use O(1) lookup if initialized */
    if (m_iColorLookupSize > 0)
    {
        return m_aColorLookup[_uSeed % (uint32_t)m_iColorLookupSize];
    }

    /* Fallback to loop if lookup table not built (shouldn't happen) */
    int iSum = _iWeightSum;
    if (iSum <= 0)
        iSum = 1;

    int iR = (int)(_uSeed % (uint32_t)iSum);
    int iAcc = 0;

    for (int i = 0; i < _iCount; ++i)
    {
        iAcc += _aWeights[i];
        if (iR < iAcc)
            return i;
    }
    return _iCount - 1;
}

/* Build per-layer universe bounds (called during init). */
static void starfield_build_layer_bounds(void)
{
    for (int iLayer = 0; iLayer < STARFIELD_NUM_LAYERS; ++iLayer)
    {
        float fMarginX = (float)m_iScreenW * m_aLayerMarginFactors[iLayer];
        float fMarginY = (float)m_iScreenH * m_aLayerMarginFactors[iLayer];

        star_layer_bounds_t *pB = &m_aLayerBounds[iLayer];

        pB->vMin = vec2_make(-fMarginX, -fMarginY);
        pB->vMax = vec2_make((float)m_iScreenW + fMarginX, (float)m_iScreenH + fMarginY);
        pB->vSize = vec2_sub(pB->vMax, pB->vMin);
    }
}

/* Determine the current draw color of a star, handling flickering for white/grey stars. */
static inline enum eCGAColor starfield_get_star_color(const star_t *_pStar, int _iFlickerFrame, int _iStarIndex)
{
    enum eCGAColor eColor = _pStar->eColor;
    int iLayer = _pStar->iLayer;

    /* Only flicker stars in middle layers (1-3) that are White or Light Grey */
    if (iLayer >= 1 && iLayer <= 3 && (eColor == CGA_WHITE || eColor == CGA_LIGHT_GREY))
    {
        int iPhase = (_iFlickerFrame + _iStarIndex * 7) & 0xAF;
        if (iPhase == 0)
        {
            return (eColor == CGA_WHITE) ? CGA_LIGHT_GREY : CGA_DARK_GREY;
        }
    }
    return eColor;
}

static int starfield_compare_star(const void *_pA, const void *_pB)
{
    const star_t *pA = (const star_t *)_pA;
    const star_t *pB = (const star_t *)_pB;

    /* primary: layer (background first) */
    if (pA->iLayer != pB->iLayer)
        return pA->iLayer - pB->iLayer;

    /* secondary: color */
    if ((int)pA->eColor != (int)pB->eColor)
        return (int)pA->eColor - (int)pB->eColor;

    return 0;
}

static void starfield_populate_stars(void)
{
    m_iStarCount = 0;

    /* Rebuild stars based on camera position for each layer.
     * Iterate layers in reverse (largest to smallest) so that if we hit the
     * star limit, we prioritize the large stars (foreground) over background dots. */
    for (int iLayer = STARFIELD_NUM_LAYERS - 1; iLayer >= 0; --iLayer)
    {
        /* Skip empty or planet layers in this pass */
        if (m_aLayerSizes[iLayer] <= 0)
            continue;

        /* Check Global Zoom Culling */
        float fGlobalZoom = camera_get_zoom(&g_mainCamera);
        float fCullThreshold = STARFIELD_CULL_ZOOM_SMALL;
        int iSize = m_aLayerSizes[iLayer];

        if (iSize == 2)
            fCullThreshold = STARFIELD_CULL_ZOOM_MEDIUM;
        else if (iSize >= 3)
            fCullThreshold = STARFIELD_CULL_ZOOM_LARGE;

        /* If threshold > 0, hide layer when zoom is BELOW threshold.
         * When zooming back IN (zoom > threshold), they should naturally reappear
         * because this condition will fail and we'll proceed to generate them.
         */
        if (fCullThreshold > 0.0f && fGlobalZoom < fCullThreshold)
            continue;

        /* Determine star count for this layer per cell.
         * removed density scaling to prevent "popping" of specific stars.
         * We rely on LOD culling and the larger star buffer (4096) to handle the load.
         */
        int iBaseStars = m_aLayerWeights[iLayer] * 8;
        int iCount = iBaseStars;

        /* Calculate common bounds */
        starfield_grid_bounds_t bounds;
        starfield_calc_grid_bounds(iLayer, &bounds);

        for (int gy = bounds.iGridMinY; gy <= bounds.iGridMaxY; ++gy)
        {
            for (int gx = bounds.iGridMinX; gx <= bounds.iGridMaxX; ++gx)
            {
                /* Seed for this cell + layer */
                uint32_t uCellSeed = sq5_get_4d_u32(gx, gy, iLayer, 0, m_uSeed);

                /* No stochastic rounding needed with fixed count */
                if (iCount <= 0)
                    continue;

                for (int i = 0; i < iCount; ++i)
                {
                    if (m_iStarCount >= STARFIELD_NUM_STARS)
                        goto done_stars;

                    /* Use sequential index for stability within cell */
                    uint32_t uStarSeed = sq5_get_1d_u32(i, uCellSeed);

                    /* Optimized: extract X, Y from single u32 (9 bits each for 512 cell size) */
                    float fOffX = (float)(uStarSeed & 0x1FF);
                    float fOffY = (float)((uStarSeed >> 9) & 0x1FF);

                    float fWorldX = (float)(gx * STARFIELD_CELL_SIZE) + fOffX;
                    float fWorldY = (float)(gy * STARFIELD_CELL_SIZE) + fOffY;

                    /* Check coarse bounds using the cached floats */
                    if (fWorldX < bounds.fViewMinX || fWorldX > bounds.fViewMaxX || fWorldY < bounds.fViewMinY || fWorldY > bounds.fViewMaxY)
                    {
                        continue;
                    }

                    /* Convert to Screen Space */
                    float fScreenX = (fWorldX - bounds.vLayerCamPos.fX) + (float)m_iScreenW * 0.5f;
                    float fScreenY = (fWorldY - bounds.vLayerCamPos.fY) + (float)m_iScreenH * 0.5f;

                    /* Generate Color using remaining bits */
                    uint32_t uColorSeed = (uStarSeed >> 18);
                    int iColorIndex = starfield_deterministic_choice(NULL, STARFIELD_NUM_COLOR_CHOICES, m_iStarColorWeightSum, uColorSeed);

                    star_t *pStar = &m_aStars[m_iStarCount++];
                    pStar->vPos.fX = fScreenX;
                    pStar->vPos.fY = fScreenY;
                    pStar->iLayer = iLayer;
                    pStar->eColor = m_aStarColors[iColorIndex];
                }
            }
        }
    }

done_stars:
    /* Sort for batching */
    if (m_iStarCount > 0)
        qsort(m_aStars, (size_t)m_iStarCount, sizeof(star_t), starfield_compare_star);
}

static void starfield_populate_planets(void)
{
    /* Rebuild planets based on camera position */
    int iLayer = STARFIELD_PLANET_LAYER_INDEX;

    starfield_grid_bounds_t bounds;
    starfield_calc_grid_bounds(iLayer, &bounds);

    /* More planets per cell for density */
    /* Compromise: 2 planets/cell to balance density vs performance */
    int iBasePlanets = 2;

    int iPlanetIdx = 0;

    for (int gy = bounds.iGridMinY; gy <= bounds.iGridMaxY; ++gy)
    {
        for (int gx = bounds.iGridMinX; gx <= bounds.iGridMaxX; ++gx)
        {
            uint32_t uCellSeed = sq5_get_4d_u32(gx, gy, iLayer, 1234, m_uSeed);

            int iCount = iBasePlanets;

            for (int i = 0; i < iCount; ++i)
            {
                if (iPlanetIdx >= STARFIELD_NUM_PLANETS)
                    goto done_planets;

                uint32_t uPlanetSeed = sq5_get_1d_u32(i, uCellSeed);

                /* Random sprite */
                int iSpriteIdx = (int)(sq5_get_1d_u32(0, uPlanetSeed) % STARFIELD_NUM_PLANET_TYPES);

                float fCellX = (float)(gx * STARFIELD_CELL_SIZE);
                float fCellY = (float)(gy * STARFIELD_CELL_SIZE);

                float fOffX = sq5_get_1d_zero_to_one(1, uPlanetSeed) * STARFIELD_CELL_SIZE;
                float fOffY = sq5_get_1d_zero_to_one(2, uPlanetSeed) * STARFIELD_CELL_SIZE;

                float fWorldX = fCellX + fOffX;
                float fWorldY = fCellY + fOffY;

                /* Since we use zoom-scaled density, we don't expect to hit the limit often,
                 * so we can skip the strict bounds check inside the loop if we trust grid bounds?
                 * But grid bounds are coarse. Keep it for safety but it's cheap. */
                if (fWorldX < bounds.fViewMinX || fWorldX > bounds.fViewMaxX || fWorldY < bounds.fViewMinY || fWorldY > bounds.fViewMaxY)
                {
                    continue;
                }

                float fScreenX = (fWorldX - bounds.vLayerCamPos.fX) + (float)m_iScreenW * 0.5f;
                float fScreenY = (fWorldY - bounds.vLayerCamPos.fY) + (float)m_iScreenH * 0.5f;

                starfield_planet_t *pPlanet = &m_aPlanets[iPlanetIdx++];
                pPlanet->vPos.fX = fScreenX;
                pPlanet->vPos.fY = fScreenY;
                pPlanet->iLayer = iLayer;
                pPlanet->pSprite = m_aUniquePlanetSprites[iSpriteIdx];
            }
        }
    }

    /* Clear remaining planet slots */
    while (iPlanetIdx < STARFIELD_NUM_PLANETS)
    {
        m_aPlanets[iPlanetIdx++].pSprite = NULL;
    }

done_planets:;
}

/* Initialize decorative planets as an additional parallax layer. */
static void starfield_init_planets(void)
{
    /* Load sprites into the unique array */
    char szPath[64];

    /* Clear array first */
    memset(m_aUniquePlanetSprites, 0, sizeof(m_aUniquePlanetSprites));

    /* Load the original planet sprites */
    for (int i = 0; i < STARFIELD_ORIGINAL_PLANET_TYPES; ++i)
        m_aUniquePlanetSprites[i] = sprite_load(m_aPlanetSpritePaths[i]);

    /* Load the 56 planets from planets_starfield (00.sprite through 55.sprite) */
    for (int i = 0; i < STARFIELD_STARFIELD_PLANET_COUNT; ++i)
    {
        snprintf(szPath, sizeof(szPath), "rom:/planets_starfield/%02d.sprite", i);
        m_aUniquePlanetSprites[STARFIELD_ORIGINAL_PLANET_TYPES + i] = sprite_load(szPath);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void starfield_free(void)
{
    /* Free all unique planet sprites */
    for (int i = 0; i < STARFIELD_NUM_PLANET_TYPES; ++i)
    {
        SAFE_FREE_SPRITE(m_aUniquePlanetSprites[i]);
    }

    /* Clear planet pointers to avoid stale references */
    for (int i = 0; i < STARFIELD_NUM_PLANETS; ++i)
    {
        m_aPlanets[i].pSprite = NULL;
    }

    m_bInitialized = false;
}

void starfield_init(int _iScreenW, int _iScreenH, uint32_t _uSeed)
{
    /* Suppress multi-initialization. */
    if (m_bInitialized)
        return;

    m_bInitialized = true;

    /* Initialize RNG state */
    m_uSeed = _uSeed;
    // m_iNoiseIndex = 0;

    m_iScreenW = _iScreenW;
    m_iScreenH = _iScreenH;
    m_iStarCount = 0;

    /* Pre-clear geometry state for layers that never render streaks (eg planets). */
    for (int iLayer = 0; iLayer < STARFIELD_NUM_LAYERS; ++iLayer)
    {
        if (m_aLayerSizes[iLayer] <= 0)
            starfield_clear_layer_state(iLayer);
    }

    /* Precompute total weight for layer selection. */
    m_iLayerWeightSum = 0;
    for (int iLayer = 0; iLayer < STARFIELD_NUM_LAYERS; ++iLayer)
        m_iLayerWeightSum += m_aLayerWeights[iLayer];

    if (m_iLayerWeightSum <= 0)
        m_iLayerWeightSum = 1; /* Degenerate config safety. */

    /* Precompute total weight for color selection. */
    m_iStarColorWeightSum = 0;
    for (int i = 0; i < STARFIELD_NUM_COLOR_CHOICES; ++i)
        m_iStarColorWeightSum += m_aStarColorWeights[i];

    if (m_iStarColorWeightSum <= 0)
        m_iStarColorWeightSum = 1;

    /* Precompute per-layer universe bounds (stars + planets). */
    starfield_build_layer_bounds();

    /* Precompute color lookup for performance */
    starfield_build_color_lookup();

    /* NOTE: Do NOT call srand() here, to not interfere with global RNG. */

    /* Initialize stars (initial population) */
    starfield_populate_stars();

    /* Initialize decorative planets. */
    starfield_init_planets();
    starfield_populate_planets();

    /* Start with zero velocity */
    starfield_reset_velocity();
}

/* Per-layer zoom influence: closer/faster layers respond more to zoom. */
static inline float starfield_layer_zoom_scale(int _iLayer, float _fZoom)
{
    /* Lowered max speed reference to make zoom more responsive/noticeable */
    float fMaxSpeed = 0.4f;
    float fWeight = m_aLayerSpeedFactors[_iLayer] / fMaxSpeed;

    if (fWeight < 0.0f)
        fWeight = 0.0f;
    else if (fWeight > 1.0f)
        fWeight = 1.0f;

    /* Distant planet layer reacts less to zoom for depth (tunable). */
    bool bIsPlanetLayer = (_iLayer == STARFIELD_PLANET_LAYER_INDEX);
    if (bIsPlanetLayer)
        fWeight *= STARFIELD_PLANET_ZOOM_RESPONSE;

    /* When zooming out (Minimap mode, Zoom < 1.0), we want a more uniform scaling
     * so that background layers (low weight) actually shrink instead of staying huge
     * relative to the foreground.
     * Linearly blend weight towards 1.0 as zoom drops from 1.0 to 0.0.
     *
     * EXCEPTION: Planet layer uses STARFIELD_PLANET_ZOOM_RESPONSE exclusively,
     * so skip this blending to allow planets to NOT scale when set to 0.0.
     */
    if (_fZoom < 1.0f && !bIsPlanetLayer)
    {
        float fBlend = 1.0f - _fZoom; // 0.0 at Zoom 1.0, 0.9 at Zoom 0.1
        /* Clamp blend to max 1.0 just in case */
        if (fBlend > 1.0f)
            fBlend = 1.0f;

        fWeight = fWeight + (1.0f - fWeight) * fBlend;
    }

    float fDelta = _fZoom - 1.0f;
    return 1.0f + fDelta * fWeight;
}

void starfield_reset_velocity()
{
    /* Set velocities to zero */
    m_fBaseVelX = 0.0f;
    m_fBaseVelY = 0.0f;
    m_fStreakVelX = 0.0f;
    m_fStreakVelY = 0.0f;
    m_fStreakLenFactor = 0.0f;
}

void starfield_update(void)
{
    float fFrameMul = frame_time_mul();
    float fZoom = camera_get_zoom(&g_mainCamera);

    /* Compute starfield velocity from camera movement */
    struct vec2 vCamVel = vec2_sub(g_mainCamera.vPos, g_mainCamera.vPrev);
    float fInvFrameMul = 1.0f / fFrameMul;
    m_fBaseVelX = -vCamVel.fX * fInvFrameMul;
    m_fBaseVelY = -vCamVel.fY * fInvFrameMul;

    /* ---------------------------------------------------------------------
     * Global base speed and streak parameters.
     * --------------------------------------------------------------------- */

    /* Apply smoothing to streak velocity. */
    float fStreakLerpVel = 1.0f - powf(1.0f - STARFIELD_STREAK_LERP_FACTOR_ACCEL, fFrameMul);
    m_fStreakVelX += (m_fBaseVelX - m_fStreakVelX) * fStreakLerpVel;
    m_fStreakVelY += (m_fBaseVelY - m_fStreakVelY) * fStreakLerpVel;

    /* Use smoothed velocity for streak direction (from camera movement). */
    float fBaseSpeedSq = m_fStreakVelX * m_fStreakVelX + m_fStreakVelY * m_fStreakVelY;
    float fBaseSpeed = (fBaseSpeedSq > 0.0f) ? sqrtf(fBaseSpeedSq) : 0.0f;

    /* Always calculate streak direction from camera velocity. */
    struct vec2 vGlobalDir = vec2_zero();
    struct vec2 vGlobalRight = vec2_zero();

    if (fBaseSpeed > 0.001f)
    {
        float fInvBase = 1.0f / fBaseSpeed;
        vGlobalDir.fX = m_fStreakVelX * fInvBase;
        vGlobalDir.fY = m_fStreakVelY * fInvBase;

        vGlobalRight.fX = -vGlobalDir.fY;
        vGlobalRight.fY = vGlobalDir.fX;
    }

    /* Calculate target length factor from UFO speed (not camera speed).
     * Below deactivation threshold, treat speed as zero for streak length calculation. */
    float fUfoSpeed = ufo_get_speed();
    float fSpeedForStreak = fUfoSpeed;
    if (fUfoSpeed < STARFIELD_STREAK_DEACTIVATION_THRESHOLD)
    {
        fSpeedForStreak = 0.0f; /* Treat as zero for streak length, but stars still move */
    }

    float fCalculatedTarget = fSpeedForStreak * STARFIELD_STREAK_LENGTH_SCALE;
    float fTargetLenFactor = fCalculatedTarget;

    /* Prevent length from increasing if UFO speed is below activation threshold. */
    if (fUfoSpeed < STARFIELD_STREAK_ACTIVATION_THRESHOLD && fCalculatedTarget > m_fStreakLenFactor)
    {
        fTargetLenFactor = m_fStreakLenFactor; /* Clamp to current, allowing only decrease */
    }

    /* Use different lerp factors for acceleration vs deceleration. */
    bool bAccelerating = (fTargetLenFactor > m_fStreakLenFactor);
    float fLerpFactor = bAccelerating ? STARFIELD_STREAK_LERP_FACTOR_ACCEL : STARFIELD_STREAK_LERP_FACTOR_DECEL;
    float fStreakLerpLen = 1.0f - powf(1.0f - fLerpFactor, fFrameMul);
    m_fStreakLenFactor += (fTargetLenFactor - m_fStreakLenFactor) * fStreakLerpLen;
    float fGlobalLenFactor = m_fStreakLenFactor;

    /* ---------------------------------------------------------------------
     * Per-layer motion + geometry, derived from global base velocity.
     * All star layers share the same mode; only parallax speed and
     * star size differ per layer.
     * --------------------------------------------------------------------- */
    for (int iLayer = 0; iLayer < STARFIELD_NUM_LAYERS; ++iLayer)
    {
        float fSpeedMul = m_aLayerSpeedFactors[iLayer];
        float fZoomScale = starfield_layer_zoom_scale(iLayer, fZoom);
        m_aLayerZoomScale[iLayer] = fZoomScale;

        /* Base per-layer velocity from global base velocity. */
        struct vec2 vVel;
        vVel.fX = m_fBaseVelX * fSpeedMul * fZoomScale;
        vVel.fY = m_fBaseVelY * fSpeedMul * fZoomScale;
        m_aLayerVel[iLayer] = vVel;

        int iSize = m_aLayerSizes[iLayer];

        /* Layers without a size (e.g. planet layer) do not use streak geom. */
        if (iSize <= 0)
            continue;

        /* Geometry derived from star size for this layer (before fallback). */
        float fSize = (float)iSize;
        float fHalfSize = 0.5f * fSize;
        float fTargetLen = fSize * fGlobalLenFactor; /* target length */

        /* Smoothly lerp the length towards the target to avoid jumps.
         * Use the same acceleration/deceleration logic as the global length factor. */
        float fLen = m_aLayerLen[iLayer];
        bool bLayerAccelerating = (fTargetLen > fLen);
        float fLayerLerpFactor = bLayerAccelerating ? STARFIELD_STREAK_LERP_FACTOR_ACCEL : STARFIELD_STREAK_LERP_FACTOR_DECEL;
        float fLayerLerp = 1.0f - powf(1.0f - fLayerLerpFactor, fFrameMul);
        fLen += (fTargetLen - fLen) * fLayerLerp;

        /* Match render fallback exactly to avoid jitter.
         * Check smoothed length, not target, to prevent direction changes during lerp. */
        struct vec2 vDir = vGlobalDir;
        struct vec2 vRight = vGlobalRight;
        float fDirLenSq = vDir.fX * vDir.fX + vDir.fY * vDir.fY;
        bool bUseCardinalFallback = (fDirLenSq < 0.001f) || (fLen < 0.5f);
        if (bUseCardinalFallback)
        {
            vDir = vec2_make(1.0f, 0.0f);
            vRight = vec2_make(0.0f, 1.0f);
            fLen = 0.0f; /* Use smoothed length, not target */
        }

        float fDiagonalness = fabsf(vDir.fX * vDir.fY);
        float fDiagonalShift = fDiagonalness * 1.0f;

        /* Derived offsets shared with render. */
        float fBackDist = -fHalfSize;
        float fFrontDist = fHalfSize + fLen;

        struct vec2 vBackOffset = vec2_scale(vDir, fBackDist);
        struct vec2 vFrontOffset = vec2_scale(vDir, fFrontDist);
        struct vec2 vPerpOffset = vec2_scale(vRight, fHalfSize);

        /* Cached culling helpers (unscaled; scaled later per-layer). */
        float fRadius = fLen + fHalfSize;
        int iCullMargin = (int)fm_ceilf(fLen) + 2; // small bias to avoid pop at edges

        m_aLayerDir[iLayer] = vDir;
        m_aLayerRight[iLayer] = vRight;
        m_aLayerLen[iLayer] = fLen;
        m_aLayerHalfWidth[iLayer] = fHalfSize;
        m_aLayerRadius[iLayer] = (int)(fRadius + 1.0f);
        m_aLayerCullMargin[iLayer] = iCullMargin;
        /* Use dot rendering when length has lerped to essentially zero (more performant) */
        m_aLayerDrawAsDot[iLayer] = (fLen < (fSize * STARFIELD_DOT_RENDER_THRESHOLD));
        m_aLayerDiagShift[iLayer] = fDiagonalShift;
        m_aLayerBackOffset[iLayer] = vBackOffset;
        m_aLayerFrontOffset[iLayer] = vFrontOffset;
        m_aLayerPerpOffset[iLayer] = vPerpOffset;
    }

    /* Global flicker phase counter. */
    m_fFlickerFrame += fFrameMul;

    /* ---------------------------------------------------------------------
     * Rebuild universe state (stars + planets) for current camera position
     * --------------------------------------------------------------------- */
    starfield_populate_stars();
    starfield_populate_planets();
}

void starfield_render(void)
{
    /* ---------------------------------------------------------------------
     * Planets: sprite layer, independent of camera.
     * --------------------------------------------------------------------- */
    if (STARFIELD_NUM_PLANETS > 0)
    {
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        rdpq_mode_filter(FILTER_BILINEAR);
        float fScale = m_aLayerZoomScale[STARFIELD_PLANET_LAYER_INDEX];
        float fCenterX = (float)m_iScreenW * 0.5f;
        float fCenterY = (float)m_iScreenH * 0.5f;

        for (int iPlanet = 0; iPlanet < STARFIELD_NUM_PLANETS; ++iPlanet)
        {
            const starfield_planet_t *pPlanet = &m_aPlanets[iPlanet];

            if (!pPlanet->pSprite)
                continue;

            float fPosX = ((pPlanet->vPos.fX - fCenterX) * fScale) + fCenterX;
            float fPosY = ((pPlanet->vPos.fY - fCenterY) * fScale) + fCenterY;

            struct vec2i vMin;
            vMin.iX = (int)(fPosX + 0.5f);
            vMin.iY = (int)(fPosY + 0.5f);

            struct vec2i vMax;
            vMax.iX = vMin.iX + (int)fm_ceilf((float)pPlanet->pSprite->width * fScale);
            vMax.iY = vMin.iY + (int)fm_ceilf((float)pPlanet->pSprite->height * fScale);

            if (screen_cull_rect(&vMin, &vMax, m_iScreenW, m_iScreenH))
                continue;

            rdpq_sprite_blit(pPlanet->pSprite,
                             vMin.iX,
                             vMin.iY,
                             &(rdpq_blitparms_t){
                                 .scale_x = fScale,
                                 .scale_y = fScale,
                             });
        }
    }

    /* ---------------------------------------------------------------------
     * Stars
     * --------------------------------------------------------------------- */
    if (m_iStarCount > 0)
    {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

        int iCurrentColor = -1;
        float fScreenHalfW = (float)m_iScreenW * 0.5f;
        float fScreenHalfH = (float)m_iScreenH * 0.5f;

        /* Precompute layer offsets */
        float fLayerOffsetX[STARFIELD_NUM_LAYERS];
        float fLayerOffsetY[STARFIELD_NUM_LAYERS];
        for (int i = 0; i < STARFIELD_NUM_LAYERS; ++i)
        {
            float fScale = m_aLayerZoomScale[i];
            fLayerOffsetX[i] = fScreenHalfW * (1.0f - fScale);
            fLayerOffsetY[i] = fScreenHalfH * (1.0f - fScale);
        }

        /* Precompute global cull threshold */
        float fGlobalZoom = camera_get_zoom(&g_mainCamera);

        /* Cache screen bounds for inlined culling */
        int iScreenW = m_iScreenW;
        int iScreenH = m_iScreenH;

        for (int iStar = 0; iStar < m_iStarCount; ++iStar)
        {
            star_t *pStar = &m_aStars[iStar];
            int iLayer = pStar->iLayer;

            /* Cheap LOD: drop very small stars when zoomed out */
            /* Inlined check: avoid repeated conditional branch setup */
            int iSize = m_aLayerSizes[iLayer];
            if (iSize <= 0)
                continue;

            if (fGlobalZoom < 0.5f && iSize < 2)
                continue;
            if (fGlobalZoom < 0.3f && iSize < 3)
                continue;

            float fScale = m_aLayerZoomScale[iLayer];
            float fSizeScaled = (float)iSize * fScale;

            /* Optimized coordinate calculation using precomputed offsets */
            /* Original: fCenterX = ((vCenter.fX - fScreenHalfW) * fScale) + fScreenHalfW */
            /* Optimized: fCenterX = vCenter.fX * fScale + fLayerOffsetX[iLayer] */
            float fCenterX = pStar->vPos.fX * fScale + fLayerOffsetX[iLayer];
            float fCenterY = pStar->vPos.fY * fScale + fLayerOffsetY[iLayer];

            float fHalfSize = m_aLayerHalfWidth[iLayer] * fScale;
            bool bDrawAsDot = m_aLayerDrawAsDot[iLayer];
            int iCullMargin = (int)fm_ceilf((float)m_aLayerCullMargin[iLayer] * fScale);
            if (iCullMargin < 1)
                iCullMargin = 1;

            int iRectX = (int)fm_floorf(fCenterX - fHalfSize + 0.5f);
            int iRectY = (int)fm_floorf(fCenterY - fHalfSize + 0.5f);

            /* Inlined Screen-Space Culling */
            int iMinX, iMinY, iMaxX, iMaxY;
            if (bDrawAsDot)
            {
                iMinX = iRectX;
                iMinY = iRectY;
                int iSizeScaledInt = (int)fm_ceilf(fSizeScaled);
                iMaxX = iRectX + iSizeScaledInt;
                iMaxY = iRectY + iSizeScaledInt;
            }
            else
            {
                iMinX = iRectX - iCullMargin;
                iMinY = iRectY - iCullMargin;
                int iSizeScaledInt = (int)fm_ceilf(fSizeScaled);
                iMaxX = iRectX + iSizeScaledInt + iCullMargin;
                iMaxY = iRectY + iSizeScaledInt + iCullMargin;
            }

            /* Rect Culling: skip if completely outside screen [0,0] to [W,H] */
            if (iMaxX < 0 || iMinX >= iScreenW || iMaxY < 0 || iMinY >= iScreenH)
                continue;

            /* Color (with flicker effect) */
            int iFlickerFrame = (int)m_fFlickerFrame;
            enum eCGAColor eDrawColor = starfield_get_star_color(pStar, iFlickerFrame, iStar);

            if ((int)eDrawColor != iCurrentColor)
            {
                iCurrentColor = (int)eDrawColor;
                rdpq_set_prim_color(palette_get_cga_color(eDrawColor));
            }

            /* Draw as dot (rect) or streak (triangle quad) */
            if (bDrawAsDot)
            {
                int iSizeScaled = (int)fm_ceilf((float)iSize * fScale);
                rdpq_fill_rectangle(iRectX, iRectY, iRectX + iSizeScaled, iRectY + iSizeScaled);
            }
            else
            {
                /* Build triangle quad from snapped integer rect coordinates */
                float fScaleLayer = fScale; // use local var for register
                struct vec2 vBack = vec2_scale(m_aLayerBackOffset[iLayer], fScaleLayer);
                struct vec2 vFront = vec2_scale(m_aLayerFrontOffset[iLayer], fScaleLayer);
                struct vec2 vPerp = vec2_scale(m_aLayerPerpOffset[iLayer], fScaleLayer);

                float fDiagonalShift = m_aLayerDiagShift[iLayer] * fScaleLayer;

                float fLeft = (float)iRectX;
                float fTop = (float)iRectY;
                float fRight = fLeft + fSizeScaled;
                float fBottom = fTop + fSizeScaled;

                float fCenterX = (fLeft + fRight) * 0.5f - fDiagonalShift;
                float fCenterY = (fTop + fBottom) * 0.5f - fDiagonalShift;

                float v0x = fCenterX + vBack.fX - vPerp.fX;
                float v0y = fCenterY + vBack.fY - vPerp.fY;
                float v1x = fCenterX + vBack.fX + vPerp.fX;
                float v1y = fCenterY + vBack.fY + vPerp.fY;
                float v2x = fCenterX + vFront.fX - vPerp.fX;
                float v2y = fCenterY + vFront.fY - vPerp.fY;
                float v3x = fCenterX + vFront.fX + vPerp.fX;
                float v3y = fCenterY + vFront.fY + vPerp.fY;

                float t0[2] = {v0x, v0y};
                float t1[2] = {v1x, v1y};
                float t2[2] = {v2x, v2y};
                float t3[2] = {v3x, v3y};

                rdpq_triangle(&TRIFMT_FILL, t0, t1, t2);
                rdpq_triangle(&TRIFMT_FILL, t2, t1, t3);
            }
        }
    }
}
