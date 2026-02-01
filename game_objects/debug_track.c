#include "debug_track.h"

#include "../camera.h"
#include "../palette.h"
#include "item_turbo.h"
#include "libdragon.h"
#include "obstacle_bounce.h"
#include "sprite.h"
#include <stddef.h>

/* Track width scaling factor - multiply all track widths by this value */
#ifndef TRACK_WIDTH_SCALE
#define TRACK_WIDTH_SCALE 0.65f
#endif

/* Center line visual pattern: stripe length in track-space units (world units). */
#ifndef TRACK_CENTER_STRIPE_LENGTH
#define TRACK_CENTER_STRIPE_LENGTH 40.0f
#endif

/* Width scale for the center line (fraction of the base fHalfWidth). */
#ifndef TRACK_CENTER_WIDTH_SCALE
#define TRACK_CENTER_WIDTH_SCALE 0.5f
#endif

/* Max distance from camera at which we still render center stripes (world units).
 * Farther track sections stay solid to save RDP work. */
#ifndef TRACK_CENTER_STRIPE_MAX_DIST
#define TRACK_CENTER_STRIPE_MAX_DIST 240.0f
#endif

#define TRACK_CENTER_STRIPE_MAX_DIST_SQ (TRACK_CENTER_STRIPE_MAX_DIST * TRACK_CENTER_STRIPE_MAX_DIST)

/* Colors for the three layers and the striped center line. */
#define TRACK_COLOR_BORDER palette_get_cga_color(CGA_BLUE)
#define TRACK_COLOR_MIDDLE palette_get_cga_color(CGA_LIGHT_BLUE)
#define TRACK_COLOR_CENTER_STRIPE_A palette_get_cga_color(CGA_LIGHT_CYAN)
#define TRACK_COLOR_CENTER_STRIPE_B palette_get_cga_color(CGA_CYAN)

typedef struct DebugTrackSegment
{
    struct vec2 p0;
    struct vec2 p1;
    float fHalfWidth;
} DebugTrackSegment;

/* Longer, more interesting race track with figure-8 style intersections */
/* Track forms a loop that crosses itself, creating an interesting racing challenge */
static const DebugTrackSegment m_aSegments[] = {
    /* Start at center, going right */
    {{0.0f, 0.0f}, {1000.0f, 0.0f}, 90.0f},            /* Start straight right */
    {{1000.0f, 0.0f}, {2000.0f, 300.0f}, 95.0f},       /* Curve up-right */
    {{2000.0f, 300.0f}, {2400.0f, 800.0f}, 100.0f},    /* Continue curve */
    {{2400.0f, 800.0f}, {2400.0f, 1400.0f}, 105.0f},   /* Vertical straight up */
    {{2400.0f, 1400.0f}, {2200.0f, 2000.0f}, 110.0f},  /* Top-right curve */
    {{2200.0f, 2000.0f}, {1600.0f, 2400.0f}, 110.0f},  /* Top curve */
    {{1600.0f, 2400.0f}, {800.0f, 2400.0f}, 105.0f},   /* Top straight */
    {{800.0f, 2400.0f}, {0.0f, 2200.0f}, 100.0f},      /* Top-left curve */
    {{0.0f, 2200.0f}, {-800.0f, 1800.0f}, 95.0f},      /* Left curve */
    {{-800.0f, 1800.0f}, {-1200.0f, 1200.0f}, 90.0f},  /* Continue left */
    {{-1200.0f, 1200.0f}, {-1200.0f, 600.0f}, 85.0f},  /* Vertical down */
    {{-1200.0f, 600.0f}, {-800.0f, 0.0f}, 85.0f},      /* Bottom-left curve */
    {{-800.0f, 0.0f}, {-400.0f, -600.0f}, 90.0f},      /* Continue down-left */
    {{-400.0f, -600.0f}, {0.0f, -1000.0f}, 95.0f},     /* Bottom curve */
    {{0.0f, -1000.0f}, {600.0f, -1200.0f}, 100.0f},    /* Bottom-right curve */
    {{600.0f, -1200.0f}, {1400.0f, -1000.0f}, 105.0f}, /* Continue bottom */
    {{1400.0f, -1000.0f}, {2000.0f, -600.0f}, 110.0f}, /* Bottom curve up */
    {{2000.0f, -600.0f}, {2200.0f, 0.0f}, 110.0f},     /* Right side up */
    {{2200.0f, 0.0f}, {2000.0f, 600.0f}, 105.0f},      /* Right curve */
    {{2000.0f, 600.0f}, {1600.0f, 1000.0f}, 100.0f},   /* Inner curve */
    {{1600.0f, 1000.0f}, {1000.0f, 1200.0f}, 95.0f},   /* Continue inner */
    {{1000.0f, 1200.0f}, {0.0f, 1000.0f}, 90.0f},      /* Top inner curve */
    {{0.0f, 1000.0f}, {-600.0f, 600.0f}, 85.0f},       /* Left inner curve */
    {{-600.0f, 600.0f}, {-400.0f, 0.0f}, 85.0f},       /* Bottom inner curve */
    {{-400.0f, 0.0f}, {0.0f, 0.0f}, 90.0f},            /* Return to start */
};
#define DEBUG_TRACK_SEGMENT_COUNT (sizeof(m_aSegments) / sizeof(m_aSegments[0]))
static const size_t m_iSegmentCount = DEBUG_TRACK_SEGMENT_COUNT;

/* Per-segment length and accumulated distance for continuous pattern along the loop. */
static float m_aSegmentLength[DEBUG_TRACK_SEGMENT_COUNT];
static float m_aSegmentAccum[DEBUG_TRACK_SEGMENT_COUNT];
static float m_fTrackTotalLength;

/* Item positions distributed along the longer track layout */
static const struct vec2 m_aItemPositions[] = {
    {500.0f, 0.0f},      /* Start area */
    {1500.0f, 150.0f},   /* Right curve */
    {2200.0f, 550.0f},   /* Top-right */
    {2400.0f, 1100.0f},  /* Vertical up */
    {2300.0f, 1700.0f},  /* Top-right curve */
    {1900.0f, 2200.0f},  /* Top */
    {1200.0f, 2400.0f},  /* Top straight */
    {400.0f, 2300.0f},   /* Top-left */
    {-400.0f, 2000.0f},  /* Left curve */
    {-1000.0f, 1500.0f}, /* Left side */
    {-1200.0f, 900.0f},  /* Vertical down */
    {-1000.0f, 300.0f},  /* Bottom-left */
    {-600.0f, -300.0f},  /* Bottom curve */
    {0.0f, -1100.0f},    /* Bottom */
    {1000.0f, -1100.0f}, /* Bottom-right */
    {1700.0f, -800.0f},  /* Bottom curve up */
    {2100.0f, -300.0f},  /* Right side */
    {1800.0f, 800.0f},   /* Inner curve */
    {800.0f, 1100.0f},   /* Top inner */
    {-200.0f, 800.0f},   /* Left inner */
    {-500.0f, 300.0f},   /* Bottom inner */
};
static const size_t m_iItemCount = sizeof(m_aItemPositions) / sizeof(m_aItemPositions[0]);

static bool camera_rect_visible(float _fMinX, float _fMinY, float _fMaxX, float _fMaxY)
{
    float fZoom = camera_get_zoom(&g_mainCamera);
    float fHalfX = (float)g_mainCamera.vHalf.iX / fZoom;
    float fHalfY = (float)g_mainCamera.vHalf.iY / fZoom;

    float fCamLeft = g_mainCamera.vPos.fX - fHalfX;
    float fCamRight = g_mainCamera.vPos.fX + fHalfX;
    float fCamTop = g_mainCamera.vPos.fY - fHalfY;
    float fCamBottom = g_mainCamera.vPos.fY + fHalfY;

    if (_fMaxX < fCamLeft)
        return false;
    if (_fMinX > fCamRight)
        return false;
    if (_fMaxY < fCamTop)
        return false;
    if (_fMinY > fCamBottom)
        return false;

    return true;
}

/* Calculate corner points for a segment */
static void get_segment_corners(const DebugTrackSegment *_pSeg, float _fScale, struct vec2 _aOutCorners[4])
{
    struct vec2 vDelta = vec2_sub(_pSeg->p1, _pSeg->p0);
    float fLenSq = vec2_mag_sq(vDelta);
    if (fLenSq <= 1e-6f) /* 1e-3 squared = 1e-6 */
    {
        /* Degenerate segment - return zero corners */
        for (int i = 0; i < 4; ++i)
            _aOutCorners[i] = vec2_zero();
        return;
    }

    float fLen = sqrtf(fLenSq);
    struct vec2 vDir = vec2_scale(vDelta, 1.0f / fLen);
    struct vec2 vLeft = vec2_make(-vDir.fY, vDir.fX);
    struct vec2 vOffset = vec2_scale(vLeft, _pSeg->fHalfWidth * _fScale * TRACK_WIDTH_SCALE);

    _aOutCorners[0] = vec2_add(_pSeg->p0, vOffset); /* Left at p0 */
    _aOutCorners[1] = vec2_sub(_pSeg->p0, vOffset); /* Right at p0 */
    _aOutCorners[2] = vec2_add(_pSeg->p1, vOffset); /* Left at p1 */
    _aOutCorners[3] = vec2_sub(_pSeg->p1, vOffset); /* Right at p1 */
}

static void render_segment_quad(const DebugTrackSegment *_pSeg, float _fScale, color_t _uColor)
{
    /* Early visibility culling: quick bounding box check using segment endpoints and width */
    float fHalfWidthScaled = _pSeg->fHalfWidth * _fScale * TRACK_WIDTH_SCALE;
    float fMinX = (_pSeg->p0.fX < _pSeg->p1.fX) ? _pSeg->p0.fX : _pSeg->p1.fX;
    float fMaxX = (_pSeg->p0.fX > _pSeg->p1.fX) ? _pSeg->p0.fX : _pSeg->p1.fX;
    float fMinY = (_pSeg->p0.fY < _pSeg->p1.fY) ? _pSeg->p0.fY : _pSeg->p1.fY;
    float fMaxY = (_pSeg->p0.fY > _pSeg->p1.fY) ? _pSeg->p0.fY : _pSeg->p1.fY;

    /* Expand by half width to account for track width */
    fMinX -= fHalfWidthScaled;
    fMaxX += fHalfWidthScaled;
    fMinY -= fHalfWidthScaled;
    fMaxY += fHalfWidthScaled;

    if (!camera_rect_visible(fMinX, fMinY, fMaxX, fMaxY))
        return;

    struct vec2 aCorners[4];
    get_segment_corners(_pSeg, _fScale, aCorners);

    /* Check if degenerate */
    if (vec2_mag_sq(aCorners[0]) < 1e-6f && vec2_mag_sq(aCorners[1]) < 1e-6f)
        return;

    /* Recalculate precise bounding box from corners */
    fMinX = aCorners[0].fX;
    fMaxX = aCorners[0].fX;
    fMinY = aCorners[0].fY;
    fMaxY = aCorners[0].fY;

    for (int i = 1; i < 4; ++i)
    {
        if (aCorners[i].fX < fMinX)
            fMinX = aCorners[i].fX;
        else if (aCorners[i].fX > fMaxX)
            fMaxX = aCorners[i].fX;
        if (aCorners[i].fY < fMinY)
            fMinY = aCorners[i].fY;
        else if (aCorners[i].fY > fMaxY)
            fMaxY = aCorners[i].fY;
    }

    float aScreen[4][2];
    for (int i = 0; i < 4; ++i)
    {
        struct vec2i vScreen;
        camera_world_to_screen(&g_mainCamera, aCorners[i], &vScreen);
        aScreen[i][0] = (float)vScreen.iX;
        aScreen[i][1] = (float)vScreen.iY;
    }

    rdpq_set_prim_color(_uColor);
    rdpq_triangle(&TRIFMT_FILL, aScreen[0], aScreen[1], aScreen[2]);
    rdpq_triangle(&TRIFMT_FILL, aScreen[2], aScreen[1], aScreen[3]);
}

/* Render connection triangles between segments to fill gaps at junctions */
static void render_segment_connection(const DebugTrackSegment *_pSegPrev, const DebugTrackSegment *_pSegNext, float _fScale, color_t _uColor)
{
    if (_pSegPrev == NULL || _pSegNext == NULL)
        return;

    /* Verify segments actually connect (p1 of prev should equal p0 of next) */
    struct vec2 vJunctionPrev = _pSegPrev->p1;
    struct vec2 vJunctionNext = _pSegNext->p0;
    float fJunctionDistSq = vec2_dist_sq(vJunctionPrev, vJunctionNext);
    if (fJunctionDistSq > 1.0f) /* 1.0 squared = 1.0, allow small floating point error */
    {
        /* Segments don't connect - skip connection rendering */
        return;
    }

    /* Calculate only the corners we need at the junction */
    struct vec2 vDeltaPrev = vec2_sub(_pSegPrev->p1, _pSegPrev->p0);
    struct vec2 vDeltaNext = vec2_sub(_pSegNext->p1, _pSegNext->p0);
    float fLenPrevSq = vec2_mag_sq(vDeltaPrev);
    float fLenNextSq = vec2_mag_sq(vDeltaNext);

    struct vec2 vPrevLeft, vPrevRight, vNextLeft, vNextRight;

    if (fLenPrevSq > 1e-6f)
    {
        float fLenPrev = sqrtf(fLenPrevSq);
        struct vec2 vDirPrev = vec2_scale(vDeltaPrev, 1.0f / fLenPrev);
        struct vec2 vLeftPrev = vec2_make(-vDirPrev.fY, vDirPrev.fX);
        struct vec2 vOffsetPrev = vec2_scale(vLeftPrev, _pSegPrev->fHalfWidth * _fScale * TRACK_WIDTH_SCALE);
        vPrevLeft = vec2_add(_pSegPrev->p1, vOffsetPrev);
        vPrevRight = vec2_sub(_pSegPrev->p1, vOffsetPrev);
    }
    else
    {
        vPrevLeft = vPrevRight = _pSegPrev->p1;
    }

    if (fLenNextSq > 1e-6f)
    {
        float fLenNext = sqrtf(fLenNextSq);
        struct vec2 vDirNext = vec2_scale(vDeltaNext, 1.0f / fLenNext);
        struct vec2 vLeftNext = vec2_make(-vDirNext.fY, vDirNext.fX);
        struct vec2 vOffsetNext = vec2_scale(vLeftNext, _pSegNext->fHalfWidth * _fScale * TRACK_WIDTH_SCALE);
        vNextLeft = vec2_add(_pSegNext->p0, vOffsetNext);
        vNextRight = vec2_sub(_pSegNext->p0, vOffsetNext);
    }
    else
    {
        vNextLeft = vNextRight = _pSegNext->p0;
    }

    /* Calculate bounding box for visibility check */
    float fMinX = vPrevLeft.fX;
    float fMaxX = vPrevLeft.fX;
    float fMinY = vPrevLeft.fY;
    float fMaxY = vPrevLeft.fY;

    /* Optimize bounding box calculation */
    if (vPrevRight.fX < fMinX)
        fMinX = vPrevRight.fX;
    else if (vPrevRight.fX > fMaxX)
        fMaxX = vPrevRight.fX;
    if (vPrevRight.fY < fMinY)
        fMinY = vPrevRight.fY;
    else if (vPrevRight.fY > fMaxY)
        fMaxY = vPrevRight.fY;

    if (vNextLeft.fX < fMinX)
        fMinX = vNextLeft.fX;
    else if (vNextLeft.fX > fMaxX)
        fMaxX = vNextLeft.fX;
    if (vNextLeft.fY < fMinY)
        fMinY = vNextLeft.fY;
    else if (vNextLeft.fY > fMaxY)
        fMaxY = vNextLeft.fY;

    if (vNextRight.fX < fMinX)
        fMinX = vNextRight.fX;
    else if (vNextRight.fX > fMaxX)
        fMaxX = vNextRight.fX;
    if (vNextRight.fY < fMinY)
        fMinY = vNextRight.fY;
    else if (vNextRight.fY > fMaxY)
        fMaxY = vNextRight.fY;

    if (!camera_rect_visible(fMinX, fMinY, fMaxX, fMaxY))
        return;

    /* Convert to screen coordinates */
    struct vec2i vScreenPrevLeft, vScreenPrevRight, vScreenNextLeft, vScreenNextRight;
    camera_world_to_screen(&g_mainCamera, vPrevLeft, &vScreenPrevLeft);
    camera_world_to_screen(&g_mainCamera, vPrevRight, &vScreenPrevRight);
    camera_world_to_screen(&g_mainCamera, vNextLeft, &vScreenNextLeft);
    camera_world_to_screen(&g_mainCamera, vNextRight, &vScreenNextRight);

    float fPrevLeftScreen[2] = {(float)vScreenPrevLeft.iX, (float)vScreenPrevLeft.iY};
    float fPrevRightScreen[2] = {(float)vScreenPrevRight.iX, (float)vScreenPrevRight.iY};
    float fNextLeftScreen[2] = {(float)vScreenNextLeft.iX, (float)vScreenNextLeft.iY};
    float fNextRightScreen[2] = {(float)vScreenNextRight.iX, (float)vScreenNextRight.iY};

    rdpq_set_prim_color(_uColor);

    /* Render two triangles to form a quadrilateral connecting the corners:
       - Triangle 1: prevLeft -> nextLeft -> prevRight (covers left side and center)
       - Triangle 2: prevRight -> nextLeft -> nextRight (covers right side) */
    rdpq_triangle(&TRIFMT_FILL, fPrevLeftScreen, fNextLeftScreen, fPrevRightScreen);
    rdpq_triangle(&TRIFMT_FILL, fPrevRightScreen, fNextLeftScreen, fNextRightScreen);
}

/* Render center-line stripes for a single segment.
 * Stripes are defined in track-space along the whole loop, so the pattern
 * continues seamlessly across segments.
 *
 * Optimizations:
 *  - Per-stripe distance check: only render stripes within TRACK_CENTER_STRIPE_MAX_DIST of camera.
 *  - Per-stripe camera-rect check: skip stripes whose quad is fully off-screen.
 */
static void render_center_stripes_for_segment(size_t _iSeg)
{
    const DebugTrackSegment *pSeg = &m_aSegments[_iSeg];

    float fSegLen = m_aSegmentLength[_iSeg];
    if (fSegLen <= 1e-3f)
        return;

    /* Quick segment-level culling (as before). */
    float fHalfWidthScaled = pSeg->fHalfWidth * TRACK_CENTER_WIDTH_SCALE * TRACK_WIDTH_SCALE;

    float fMinX = (pSeg->p0.fX < pSeg->p1.fX) ? pSeg->p0.fX : pSeg->p1.fX;
    float fMaxX = (pSeg->p0.fX > pSeg->p1.fX) ? pSeg->p0.fX : pSeg->p1.fX;
    float fMinY = (pSeg->p0.fY < pSeg->p1.fY) ? pSeg->p0.fY : pSeg->p1.fY;
    float fMaxY = (pSeg->p0.fY > pSeg->p1.fY) ? pSeg->p0.fY : pSeg->p1.fY;

    fMinX -= fHalfWidthScaled;
    fMaxX += fHalfWidthScaled;
    fMinY -= fHalfWidthScaled;
    fMaxY += fHalfWidthScaled;

    if (!camera_rect_visible(fMinX, fMinY, fMaxX, fMaxY))
        return;

    /* Direction and lateral offset for this segment. */
    struct vec2 vDelta = vec2_sub(pSeg->p1, pSeg->p0);
    float fLenSq = vec2_mag_sq(vDelta);
    if (fLenSq <= 1e-6f)
        return;

    float fLenInv = 1.0f / sqrtf(fLenSq);
    struct vec2 vDir = vec2_scale(vDelta, fLenInv);
    struct vec2 vLeft = vec2_make(-vDir.fY, vDir.fX);
    struct vec2 vOffset = vec2_scale(vLeft, fHalfWidthScaled);

    /* Global distance range covered by this segment. */
    float fSegStart = m_aSegmentAccum[_iSeg];
    float fSegEnd = fSegStart + fSegLen;

    /* Find a starting stripe boundary slightly before or at fSegStart. */
    float fFirstStripeIndex = fm_floorf(fSegStart / TRACK_CENTER_STRIPE_LENGTH);
    float fStripeStart = fFirstStripeIndex * TRACK_CENTER_STRIPE_LENGTH;
    if (fStripeStart > fSegStart)
        fStripeStart -= TRACK_CENTER_STRIPE_LENGTH;

    const struct vec2 vCamPos = g_mainCamera.vPos;

    /* Iterate over stripes intersecting this segment. */
    for (float fStripeS0 = fStripeStart; fStripeS0 < fSegEnd; fStripeS0 += TRACK_CENTER_STRIPE_LENGTH)
    {
        float fStripeS1 = fStripeS0 + TRACK_CENTER_STRIPE_LENGTH;

        /* Clamp stripe range to this segment. */
        float fClampedS0 = (fStripeS0 < fSegStart) ? fSegStart : fStripeS0;
        float fClampedS1 = (fStripeS1 > fSegEnd) ? fSegEnd : fStripeS1;

        if (fClampedS1 <= fClampedS0)
            continue;

        /* Local distances along the segment (0..fSegLen). */
        float fLocal0 = fClampedS0 - fSegStart;
        float fLocal1 = fClampedS1 - fSegStart;

        struct vec2 vCenter0 = vec2_add(pSeg->p0, vec2_scale(vDir, fLocal0));
        struct vec2 vCenter1 = vec2_add(pSeg->p0, vec2_scale(vDir, fLocal1));

        /* --- RCP optimization #1: distance-based culling per stripe --- */
        struct vec2 vMid = vec2_scale(vec2_add(vCenter0, vCenter1), 0.5f);
        struct vec2 vToCam = vec2_sub(vMid, vCamPos);
        float fZoom = camera_get_zoom(&g_mainCamera);
        float fMaxDistSq = TRACK_CENTER_STRIPE_MAX_DIST_SQ / (fZoom * fZoom);
        float fDistSq = vec2_mag_sq(vToCam);
        if (fDistSq > fMaxDistSq)
        {
            /* Stripe too far from camera – just keep the base track fill. */
            continue;
        }

        struct vec2 v0L = vec2_add(vCenter0, vOffset);
        struct vec2 v0R = vec2_sub(vCenter0, vOffset);
        struct vec2 v1L = vec2_add(vCenter1, vOffset);
        struct vec2 v1R = vec2_sub(vCenter1, vOffset);

        /* --- RCP optimization #2: per-stripe screen-rect culling --- */
        float fStripeMinX = v0L.fX;
        float fStripeMaxX = v0L.fX;
        float fStripeMinY = v0L.fY;
        float fStripeMaxY = v0L.fY;

        if (v0R.fX < fStripeMinX)
            fStripeMinX = v0R.fX;
        else if (v0R.fX > fStripeMaxX)
            fStripeMaxX = v0R.fX;
        if (v0R.fY < fStripeMinY)
            fStripeMinY = v0R.fY;
        else if (v0R.fY > fStripeMaxY)
            fStripeMaxY = v0R.fY;

        if (v1L.fX < fStripeMinX)
            fStripeMinX = v1L.fX;
        else if (v1L.fX > fStripeMaxX)
            fStripeMaxX = v1L.fX;
        if (v1L.fY < fStripeMinY)
            fStripeMinY = v1L.fY;
        else if (v1L.fY > fStripeMaxY)
            fStripeMaxY = v1L.fY;

        if (v1R.fX < fStripeMinX)
            fStripeMinX = v1R.fX;
        else if (v1R.fX > fStripeMaxX)
            fStripeMaxX = v1R.fX;
        if (v1R.fY < fStripeMinY)
            fStripeMinY = v1R.fY;
        else if (v1R.fY > fStripeMaxY)
            fStripeMaxY = v1R.fY;

        if (!camera_rect_visible(fStripeMinX, fStripeMinY, fStripeMaxX, fStripeMaxY))
            continue;

        /* Project and draw. */
        struct vec2i v0LScreen, v0RScreen, v1LScreen, v1RScreen;
        camera_world_to_screen(&g_mainCamera, v0L, &v0LScreen);
        camera_world_to_screen(&g_mainCamera, v0R, &v0RScreen);
        camera_world_to_screen(&g_mainCamera, v1L, &v1LScreen);
        camera_world_to_screen(&g_mainCamera, v1R, &v1RScreen);

        float a0L[2] = {(float)v0LScreen.iX, (float)v0LScreen.iY};
        float a0R[2] = {(float)v0RScreen.iX, (float)v0RScreen.iY};
        float a1L[2] = {(float)v1LScreen.iX, (float)v1LScreen.iY};
        float a1R[2] = {(float)v1RScreen.iX, (float)v1RScreen.iY};

        int iStripeIndex = (int)fm_floorf(fStripeS0 / TRACK_CENTER_STRIPE_LENGTH);
        bool bStripeEven = (iStripeIndex & 1) == 0;
        color_t uColor = bStripeEven ? TRACK_COLOR_CENTER_STRIPE_A : TRACK_COLOR_CENTER_STRIPE_B;

        rdpq_set_prim_color(uColor);
        rdpq_triangle(&TRIFMT_FILL, a0L, a0R, a1L);
        rdpq_triangle(&TRIFMT_FILL, a1L, a0R, a1R);
    }
}

void debug_track_init(void)
{

    /* Precompute segment lengths and accumulated distances for continuous patterning. */
    float fAccum = 0.0f;
    for (size_t i = 0; i < m_iSegmentCount; ++i)
    {
        float fLenSq = vec2_dist_sq(m_aSegments[i].p0, m_aSegments[i].p1);
        float fLen = (fLenSq > 1e-6f) ? sqrtf(fLenSq) : 0.0f;
        m_aSegmentLength[i] = fLen;
        m_aSegmentAccum[i] = fAccum;
        fAccum += fLen;
    }
    m_fTrackTotalLength = fAccum;

    /* Create game objects at item positions */
    /* Alternate between turbo and bounce for variety */
    for (size_t i = 0; i < m_iItemCount; ++i)
    {
        if (i % 2 == 0)
        {
            item_turbo_add(m_aItemPositions[i]);
        }
        else
        {
            obstacle_bounce_add(m_aItemPositions[i]);
        }
    }
}

void debug_track_free(void)
{
    /* No dynamic allocation to free for the track itself.
     * Items spawned by init are managed/freed by item_turbo and obstacle_bounce modules.
     */
}

void debug_track_render(void)
{
    if (m_iSegmentCount == 0)
        return;

    float fZoom = camera_get_zoom(&g_mainCamera);
    bool bRenderConnections = (fZoom >= 0.2f);
    bool bRenderCenterStripes = (fZoom >= 0.5f);

    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

    /* Render outer layer (track border) */
    for (size_t i = 0; i < m_iSegmentCount; ++i)
        render_segment_quad(&m_aSegments[i], 1.2f, TRACK_COLOR_BORDER);

    /* Render connection triangles for outer layer */
    if (bRenderConnections)
    {
        for (size_t i = 0; i < m_iSegmentCount; ++i)
        {
            size_t iPrev = (i == 0) ? (m_iSegmentCount - 1) : (i - 1);
            render_segment_connection(&m_aSegments[iPrev], &m_aSegments[i], 1.2f, TRACK_COLOR_BORDER);
        }
    }

    /* Render middle layer (track surface) */
    for (size_t i = 0; i < m_iSegmentCount; ++i)
        render_segment_quad(&m_aSegments[i], 1.0f, TRACK_COLOR_MIDDLE);

    /* Render connection triangles for middle layer */
    if (bRenderConnections)
    {
        for (size_t i = 0; i < m_iSegmentCount; ++i)
        {
            size_t iPrev = (i == 0) ? (m_iSegmentCount - 1) : (i - 1);
            render_segment_connection(&m_aSegments[iPrev], &m_aSegments[i], 1.0f, TRACK_COLOR_MIDDLE);
        }
    }

    /* Render inner layer (track center line) as alternating stripes along the loop. */
    if (bRenderCenterStripes)
    {
        for (size_t i = 0; i < m_iSegmentCount; ++i)
            render_center_stripes_for_segment(i);
    }

    /* Items are now rendered by interactable system */
}
