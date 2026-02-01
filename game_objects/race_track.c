#include "race_track.h"
#include "../camera.h"
#include "../math_helper.h"
#include "../palette.h"
#include "../path_helper.h"
#include "../resource_helper.h"
#include "libdragon.h"
#include "rdpq.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "rdpq_tex.h"
#include "rdpq_tri.h"
#include "sprite.h"
#include "ufo.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Level of Detail (LOD) Settings */
#define RACE_TRACK_LOD_ZOOM_LOW 0.15f    /* Max zoom out threshold -- disables borders*/
#define RACE_TRACK_LOD_ZOOM_MED 0.40f    /* Moderate zoom threshold -- only reduces step size */
#define RACE_TRACK_LOD_STEP_LOW 4        /* Step size for max zoom out (must divide CHUNK_SIZE) */
#define RACE_TRACK_LOD_STEP_MED 2        /* Step size for moderate zoom */
#define RACE_TRACK_LOD_STEP_HIGH 1       /* Step size for normal view */
#define RACE_TRACK_LOD_BORDERS_LOW false /* Render borders at low zoom? */

/* Track instance */
static struct
{
    bool bInitialized;
    struct vec2 *pControlPoints; /* Original control points from CSV */
    uint16_t uControlPointCount;

    RaceTrackSample *pSamples; /* Resampled uniform points */
    uint16_t uSampleCount;
    float fTotalLength; /* Total track length L */
    float fStep;        /* Arc-length step used */
} m_track;

/* Spatial partitioning for optimization */
#define RACE_TRACK_CHUNK_SIZE 32

typedef struct
{
    uint16_t uStartIndex;
    uint16_t uEndIndex;
    float fMinX, fMaxX, fMinY, fMaxY;
} RaceTrackChunk;

static RaceTrackChunk *m_pChunks = NULL;
static uint16_t m_uChunkCount = 0;

/* Border textures */
static sprite_t *m_pBorderSprite = NULL;
static rdpq_texparms_t m_borderTexParms = {0};
static float m_fBorderTexHeight = 1.0f;

/* Road fill texture */
static sprite_t *m_pRoadSprite = NULL;
static rdpq_texparms_t m_roadTexParms = {0};
static float m_fRoadTexHeight = 1.0f;

/* Finish line texture */
static sprite_t *m_pFinishLineSprite = NULL;
static rdpq_texparms_t m_finishLineTexParms = {0};
static float m_fFinishLineTexWidth = 1.0f;

// FORWARD DECLARATIONS
static void compute_track_bounding_box(void);
static bool is_position_near_track(struct vec2 _vPos);
static void find_closest_point(struct vec2 _vPos, struct vec2 *_pOutClosest, struct vec2 *_pOutNormal, float *_pOutLateralDist, float *_pOutS);
static bool check_track_collision(struct vec2 _vUfoPos, struct vec2 *_pOutClosest, struct vec2 *_pOutNormal, float *_pOutPenetration);

/* Collision state */
static bool m_bCollisionEnabled = true;
static uint16_t m_uLastSegIndex = 0;
static bool m_bWasColliding = false; /* Track previous collision state for edge detection */

/* Bounding box for optimization */
static float m_fTrackMinX = 0.0f;
static float m_fTrackMaxX = 0.0f;
static float m_fTrackMinY = 0.0f;
static float m_fTrackMaxY = 0.0f;
static bool m_bBBoxValid = false;

/* Cached camera bounds for rendering optimization */
static struct
{
    float fCamLeft;
    float fCamRight;
    float fCamTop;
    float fCamBottom;
    bool bValid;
} m_cachedCameraBounds = {0};

/* Catmull-Rom for uniform parameterization */
static struct vec2 catmull_rom_evaluate_uniform(struct vec2 _vP0, struct vec2 _vP1, struct vec2 _vP2, struct vec2 _vP3, float _fT)
{
    float fT = _fT;
    float fT2 = fT * fT;
    float fT3 = fT2 * fT;

    /* Standard Catmull-Rom basis functions */
    float fB0 = -0.5f * fT3 + fT2 - 0.5f * fT;
    float fB1 = 1.5f * fT3 - 2.5f * fT2 + 1.0f;
    float fB2 = -1.5f * fT3 + 2.0f * fT2 + 0.5f * fT;
    float fB3 = 0.5f * fT3 - 0.5f * fT2;

    /* Evaluate spline */
    struct vec2 vResult = vec2_zero();
    vResult = vec2_add(vResult, vec2_scale(_vP0, fB0));
    vResult = vec2_add(vResult, vec2_scale(_vP1, fB1));
    vResult = vec2_add(vResult, vec2_scale(_vP2, fB2));
    vResult = vec2_add(vResult, vec2_scale(_vP3, fB3));

    return vResult;
}

/* Helper: Get control point with wrapping for loop */
static struct vec2 get_control_point_wrapped(int32_t _iIndex)
{
    if (m_track.uControlPointCount == 0)
        return vec2_zero();

    /* Wrap index */
    while (_iIndex < 0)
        _iIndex += (int32_t)m_track.uControlPointCount;

    /* Use modulo for positive wrapping */
    if (_iIndex >= (int32_t)m_track.uControlPointCount)
        _iIndex %= (int32_t)m_track.uControlPointCount;

    return m_track.pControlPoints[_iIndex];
}

/* Build oversampled polyline Q[] from control points using Catmull-Rom */
static bool build_oversampled_polyline(struct vec2 **_ppPolyline, uint16_t *_pPolylineCount)
{
    if (!_ppPolyline || !_pPolylineCount || m_track.uControlPointCount < 2)
        return false;

    /* Estimate total points needed (8-32 samples per segment) */
    uint16_t uSamplesPerSegment = 16;
    uint16_t uEstimatedCount = m_track.uControlPointCount * uSamplesPerSegment;
    if (uEstimatedCount < 64)
        uEstimatedCount = 64; /* Minimum for small tracks */

    struct vec2 *pPolyline = (struct vec2 *)malloc(sizeof(struct vec2) * uEstimatedCount);
    if (!pPolyline)
        return false;

    uint16_t uPolylineIndex = 0;

    /* For each control point, create a curve segment */
    for (uint16_t i = 0; i < m_track.uControlPointCount; ++i)
    {
        /* Get 4 control points for Catmull-Rom (P0, P1, P2, P3) */
        /* P1 and P2 are the segment endpoints, P0 and P3 are for smoothness */
        struct vec2 vP0 = get_control_point_wrapped((int32_t)i - 1);
        struct vec2 vP1 = get_control_point_wrapped((int32_t)i);
        struct vec2 vP2 = get_control_point_wrapped((int32_t)i + 1);
        struct vec2 vP3 = get_control_point_wrapped((int32_t)i + 2);

        /* Sample the curve segment */
        for (uint16_t j = 0; j < uSamplesPerSegment; ++j)
        {
            float fT = (float)j / (float)uSamplesPerSegment;

            /* Check if we need to reallocate */
            if (uPolylineIndex >= uEstimatedCount)
            {
                uint16_t uNewSize = uEstimatedCount * 2;
                struct vec2 *pNewPolyline = (struct vec2 *)realloc(pPolyline, sizeof(struct vec2) * uNewSize);
                if (!pNewPolyline)
                {
                    free(pPolyline);
                    return false;
                }
                pPolyline = pNewPolyline;
                uEstimatedCount = uNewSize;
            }

            pPolyline[uPolylineIndex] = catmull_rom_evaluate_uniform(vP0, vP1, vP2, vP3, fT);
            uPolylineIndex++;
        }
    }

    /* Add closing point (exactly the first control point) to complete the loop */
    if (uPolylineIndex >= uEstimatedCount)
    {
        uint16_t uNewSize = uEstimatedCount + 16;
        struct vec2 *pNewPolyline = (struct vec2 *)realloc(pPolyline, sizeof(struct vec2) * uNewSize);
        if (!pNewPolyline)
        {
            free(pPolyline);
            return false;
        }
        pPolyline = pNewPolyline;
        uEstimatedCount = uNewSize;
    }
    pPolyline[uPolylineIndex] = get_control_point_wrapped(0);
    uPolylineIndex++;

    *_ppPolyline = pPolyline;
    *_pPolylineCount = uPolylineIndex;
    return true;
}

/* Build arc-length table from polyline */
static bool build_arc_length_table(const struct vec2 *_pPolyline, uint16_t _uPolylineCount, float **_ppCumulative, float *_pTotalLength)
{
    if (!_pPolyline || _uPolylineCount == 0 || !_ppCumulative || !_pTotalLength)
        return false;

    float *pCumulative = (float *)malloc(sizeof(float) * _uPolylineCount);
    if (!pCumulative)
        return false;

    pCumulative[0] = 0.0f;
    float fTotal = 0.0f;

    for (uint16_t i = 1; i < _uPolylineCount; ++i)
    {
        float fDist = vec2_dist(_pPolyline[i - 1], _pPolyline[i]);
        fTotal += fDist;
        pCumulative[i] = fTotal;
    }

    *_ppCumulative = pCumulative;
    *_pTotalLength = fTotal;
    return true;
}

/* Resample uniformly by arc-length */
static bool resample_uniform(const struct vec2 *_pPolyline, uint16_t _uPolylineCount, const float *_pCumulative, float _fTotalLength, RaceTrackSample **_ppSamples,
                             uint16_t *_pSampleCount)
{
    if (!_pPolyline || _uPolylineCount == 0 || !_pCumulative || _fTotalLength <= 0.0f || !_ppSamples || !_pSampleCount)
        return false;

    /* Calculate number of samples needed */
    uint16_t uSampleCount = (uint16_t)(_fTotalLength / RACE_TRACK_STEP) + 1;
    if (uSampleCount < 2)
        uSampleCount = 2;

    RaceTrackSample *pSamples = (RaceTrackSample *)malloc(sizeof(RaceTrackSample) * uSampleCount);
    if (!pSamples)
        return false;

    uint16_t uSampleIndex = 0;

    /* Generate samples at uniform arc-length intervals */
    for (uint16_t i = 0; i < uSampleCount; ++i)
    {
        float fTargetS = (float)i * RACE_TRACK_STEP;
        if (fTargetS >= _fTotalLength)
        {
            /* Last sample: use final point */
            if (uSampleIndex < uSampleCount - 1)
            {
                fTargetS = _fTotalLength;
            }
            else
            {
                break;
            }
        }

        /* Find bracketing indices in cumulative array */
        uint16_t uLower = 0;
        uint16_t uUpper = _uPolylineCount - 1;

        /* Binary search for efficiency */
        while (uUpper - uLower > 1)
        {
            uint16_t uMid = (uLower + uUpper) / 2;
            if (_pCumulative[uMid] < fTargetS)
            {
                uLower = uMid;
            }
            else
            {
                uUpper = uMid;
            }
        }

        /* Interpolate position between Q[uLower] and Q[uUpper] */
        float fSegmentStart = _pCumulative[uLower];
        float fSegmentEnd = _pCumulative[uUpper];
        float fSegmentLength = fSegmentEnd - fSegmentStart;

        struct vec2 vPos;
        if (fSegmentLength < 1e-6f)
        {
            /* Degenerate segment: use lower point */
            vPos = _pPolyline[uLower];
        }
        else
        {
            float fT = (fTargetS - fSegmentStart) / fSegmentLength;
            vPos = vec2_mix(_pPolyline[uLower], _pPolyline[uUpper], fT);
        }

        pSamples[uSampleIndex].vPos = vPos;
        pSamples[uSampleIndex].fS = fTargetS;
        pSamples[uSampleIndex].vTangent = vec2_zero(); /* Will be computed later */
        pSamples[uSampleIndex].vNormal = vec2_zero();  /* Will be computed later */
        uSampleIndex++;
    }

    /* Adjust final sample to exactly match end */
    if (uSampleIndex > 0)
    {
        pSamples[uSampleIndex - 1].vPos = _pPolyline[_uPolylineCount - 1];
        pSamples[uSampleIndex - 1].fS = _fTotalLength;
    }

    *_ppSamples = pSamples;
    *_pSampleCount = uSampleIndex;
    return true;
}

/* Compute tangents and normals with smoothing */
static void compute_tangents_and_normals(RaceTrackSample *_pSamples, uint16_t _uSampleCount)
{
    if (!_pSamples || _uSampleCount < 2)
        return;

    /* First pass: compute raw tangents */
    for (uint16_t i = 0; i < _uSampleCount; ++i)
    {
        uint16_t uPrev = (i == 0) ? (_uSampleCount - 1) : (i - 1);
        uint16_t uNext = (i == _uSampleCount - 1) ? 0 : (i + 1);

        struct vec2 vDir = vec2_sub(_pSamples[uNext].vPos, _pSamples[uPrev].vPos);
        _pSamples[i].vTangent = vec2_normalize(vDir);
    }

    /* Second pass: smooth tangents by averaging with neighbors */
    for (uint16_t i = 0; i < _uSampleCount; ++i)
    {
        uint16_t uPrev = (i == 0) ? (_uSampleCount - 1) : (i - 1);
        uint16_t uNext = (i == _uSampleCount - 1) ? 0 : (i + 1);

        struct vec2 vSmoothed = vec2_add(_pSamples[uPrev].vTangent, _pSamples[i].vTangent);
        vSmoothed = vec2_add(vSmoothed, _pSamples[uNext].vTangent);
        vSmoothed = vec2_scale(vSmoothed, 1.0f / 3.0f);
        _pSamples[i].vTangent = vec2_normalize(vSmoothed);
    }

    /* Compute normals (perpendicular to tangent, consistent handedness) */
    for (uint16_t i = 0; i < _uSampleCount; ++i)
    {
        /* Perpendicular: rotate tangent 90 degrees counter-clockwise */
        _pSamples[i].vNormal = vec2_make(-_pSamples[i].vTangent.fY, _pSamples[i].vTangent.fX);
    }
}

/* Build spatial chunks for culling optimization */
static void build_track_chunks(void)
{
    if (!m_track.bInitialized || m_track.uSampleCount < 2)
        return;

    if (m_pChunks)
    {
        free(m_pChunks);
        m_pChunks = NULL;
    }

    /* Calculate number of chunks */
    /* We use ceil division to ensure all segments are covered */
    m_uChunkCount = (m_track.uSampleCount + RACE_TRACK_CHUNK_SIZE - 1) / RACE_TRACK_CHUNK_SIZE;

    m_pChunks = (RaceTrackChunk *)malloc(sizeof(RaceTrackChunk) * m_uChunkCount);
    if (!m_pChunks)
    {
        m_uChunkCount = 0;
        return;
    }

    float fHalfWidth = RACE_TRACK_WIDTH * 0.5f;

    /* Process each chunk */
    for (uint16_t i = 0; i < m_uChunkCount; ++i)
    {
        uint16_t uStart = i * RACE_TRACK_CHUNK_SIZE;
        uint16_t uEnd = uStart + RACE_TRACK_CHUNK_SIZE;

        /* Cap end index and handle loop wraparound for the last segment of the last chunk */
        if (uEnd > m_track.uSampleCount)
            uEnd = m_track.uSampleCount;

        m_pChunks[i].uStartIndex = uStart;
        m_pChunks[i].uEndIndex = uEnd;

        /* Initialize bounds with the first point in the chunk */
        /* Note: We need to include the "next" point for the last sample in the chunk because it forms a segment */
        struct vec2 vFirst = m_track.pSamples[uStart].vPos;
        float fMinX = vFirst.fX;
        float fMaxX = vFirst.fX;
        float fMinY = vFirst.fY;
        float fMaxY = vFirst.fY;

        /* Iterate through all SEGMENTS in this chunk */
        /* A chunk from uStart to uEnd controls segments starting at uStart...uEnd-1 */
        for (uint16_t j = uStart; j < uEnd; ++j)
        {
            /* Current point */
            struct vec2 vP = m_track.pSamples[j].vPos;
            if (vP.fX < fMinX)
                fMinX = vP.fX;
            if (vP.fX > fMaxX)
                fMaxX = vP.fX;
            if (vP.fY < fMinY)
                fMinY = vP.fY;
            if (vP.fY > fMaxY)
                fMaxY = vP.fY;

            /* Next point (segment end) */
            uint16_t uNext = (j == m_track.uSampleCount - 1) ? 0 : (j + 1);
            struct vec2 vNext = m_track.pSamples[uNext].vPos;
            if (vNext.fX < fMinX)
                fMinX = vNext.fX;
            if (vNext.fX > fMaxX)
                fMaxX = vNext.fX;
            if (vNext.fY < fMinY)
                fMinY = vNext.fY;
            if (vNext.fY > fMaxY)
                fMaxY = vNext.fY;
        }

        /* Expand by track half-width */
        m_pChunks[i].fMinX = fMinX - fHalfWidth;
        m_pChunks[i].fMaxX = fMaxX + fHalfWidth;
        m_pChunks[i].fMinY = fMinY - fHalfWidth;
        m_pChunks[i].fMaxY = fMaxY + fHalfWidth;
    }
}

void race_track_init(const char *_pRaceName)
{
    race_track_free();

    if (!_pRaceName)
    {
        debugf("race_track_init: Invalid race name\n");
        return;
    }

    /* Load control points from race.csv in current folder */
    struct vec2 *pControlPoints = NULL;
    uint16_t uControlPointCount = 0;
    if (!path_helper_load_named_points("race", _pRaceName, &pControlPoints, &uControlPointCount))
    {
        debugf("race_track_init: Failed to load race '%s' from race.csv\n", _pRaceName);
        return;
    }

    if (uControlPointCount < 2)
    {
        debugf("race_track_init: Need at least 2 control points, got %d\n", uControlPointCount);
        free(pControlPoints);
        return;
    }

    m_track.pControlPoints = pControlPoints;
    m_track.uControlPointCount = uControlPointCount;

    /* Build oversampled polyline */
    struct vec2 *pPolyline = NULL;
    uint16_t uPolylineCount = 0;
    if (!build_oversampled_polyline(&pPolyline, &uPolylineCount))
    {
        debugf("race_track_init: Failed to build oversampled polyline\n");
        free(pControlPoints);
        return;
    }

    /* Build arc-length table */
    float *pCumulative = NULL;
    float fTotalLength = 0.0f;
    if (!build_arc_length_table(pPolyline, uPolylineCount, &pCumulative, &fTotalLength))
    {
        debugf("race_track_init: Failed to build arc-length table\n");
        free(pPolyline);
        free(pControlPoints);
        return;
    }

    /* Resample uniformly */
    RaceTrackSample *pSamples = NULL;
    uint16_t uSampleCount = 0;
    if (!resample_uniform(pPolyline, uPolylineCount, pCumulative, fTotalLength, &pSamples, &uSampleCount))
    {
        debugf("race_track_init: Failed to resample uniformly\n");
        free(pCumulative);
        free(pPolyline);
        free(pControlPoints);
        return;
    }

    /* Check for duplicate end point (loop closure) and remove it if present */
    /* This ensures tangents are computed correctly across the loop seam */
    if (uSampleCount > 1)
    {
        struct vec2 vDiff = vec2_sub(pSamples[0].vPos, pSamples[uSampleCount - 1].vPos);
        if (vec2_mag_sq(vDiff) < 1.0f)
        {
            uSampleCount--;
        }
    }

    /* Compute tangents and normals */
    compute_tangents_and_normals(pSamples, uSampleCount);

    /* Store results */
    m_track.pSamples = pSamples;
    m_track.uSampleCount = uSampleCount;
    m_track.fTotalLength = fTotalLength;
    m_track.fStep = RACE_TRACK_STEP;
    m_track.bInitialized = true;

    /* Clean up temporary arrays */
    free(pCumulative);
    free(pPolyline);

    /* Load border texture */
    m_pBorderSprite = sprite_load("rom:/race_border_00.sprite");
    if (m_pBorderSprite)
    {
        m_fBorderTexHeight = (float)m_pBorderSprite->height;

        m_borderTexParms = (rdpq_texparms_t){
            .s = {.repeats = REPEAT_INFINITE, .mirror = MIRROR_NONE},
            .t = {.repeats = 1.0f, .mirror = MIRROR_NONE},
        };
    }

    /* Load road fill texture */
    m_pRoadSprite = sprite_load("rom:/race_track_00.sprite");
    if (m_pRoadSprite)
    {
        m_fRoadTexHeight = (float)m_pRoadSprite->height;

        m_roadTexParms = (rdpq_texparms_t){
            .s = {.repeats = REPEAT_INFINITE, .mirror = MIRROR_NONE},
            .t = {.repeats = 1.0f, .mirror = MIRROR_NONE},
        };
    }

    /* Load finish line texture */
    m_pFinishLineSprite = sprite_load("rom:/race_finish_line_00.sprite");
    if (m_pFinishLineSprite)
    {
        m_fFinishLineTexWidth = (float)m_pFinishLineSprite->width;
        /* Setup texture parameters for S-axis repeating */
        m_finishLineTexParms = (rdpq_texparms_t){
            .s = {.repeats = REPEAT_INFINITE, .mirror = MIRROR_NONE},
            .t = {.repeats = 1.0f, .mirror = MIRROR_NONE},
        };
    }

    /* Compute bounding box for collision optimization */
    compute_track_bounding_box();

    /* Build spatial chunks for culling optimization */
    build_track_chunks();
}

void race_track_free(void)
{
    if (m_track.pControlPoints)
    {
        free(m_track.pControlPoints);
        m_track.pControlPoints = NULL;
    }

    if (m_track.pSamples)
    {
        free(m_track.pSamples);
        m_track.pSamples = NULL;
    }

    if (m_pChunks)
    {
        free(m_pChunks);
        m_pChunks = NULL;
        m_uChunkCount = 0;
    }

    SAFE_FREE_SPRITE(m_pBorderSprite);
    SAFE_FREE_SPRITE(m_pRoadSprite);
    SAFE_FREE_SPRITE(m_pFinishLineSprite);

    memset(&m_track, 0, sizeof(m_track));
    m_bBBoxValid = false;
    m_bCollisionEnabled = true;
    m_uLastSegIndex = 0;
    m_bWasColliding = false;
    m_cachedCameraBounds.bValid = false;
}

bool race_track_is_initialized(void)
{
    return m_track.bInitialized;
}

uint16_t race_track_get_sample_count(void)
{
    return m_track.uSampleCount;
}

float race_track_get_total_length(void)
{
    return m_track.fTotalLength;
}

const RaceTrackSample *race_track_get_samples(void)
{
    return m_track.pSamples;
}

/* Compute track bounding box for collision optimization */
static void compute_track_bounding_box(void)
{
    if (!m_track.bInitialized || m_track.uSampleCount == 0)
    {
        m_bBBoxValid = false;
        return;
    }

    /* Initialize with first sample */
    m_fTrackMinX = m_track.pSamples[0].vPos.fX;
    m_fTrackMaxX = m_track.pSamples[0].vPos.fX;
    m_fTrackMinY = m_track.pSamples[0].vPos.fY;
    m_fTrackMaxY = m_track.pSamples[0].vPos.fY;

    /* Find min/max across all samples */
    for (uint16_t i = 1; i < m_track.uSampleCount; ++i)
    {
        if (m_track.pSamples[i].vPos.fX < m_fTrackMinX)
            m_fTrackMinX = m_track.pSamples[i].vPos.fX;
        if (m_track.pSamples[i].vPos.fX > m_fTrackMaxX)
            m_fTrackMaxX = m_track.pSamples[i].vPos.fX;
        if (m_track.pSamples[i].vPos.fY < m_fTrackMinY)
            m_fTrackMinY = m_track.pSamples[i].vPos.fY;
        if (m_track.pSamples[i].vPos.fY > m_fTrackMaxY)
            m_fTrackMaxY = m_track.pSamples[i].vPos.fY;
    }

    /* Expand by collision half-width + margin */
    float fExpand = RACE_TRACK_HALF_COLLIDE + RACE_TRACK_BBOX_MARGIN;
    m_fTrackMinX -= fExpand;
    m_fTrackMaxX += fExpand;
    m_fTrackMinY -= fExpand;
    m_fTrackMaxY += fExpand;

    m_bBBoxValid = true;
}

/* Check if position is near track (bounding box optimization) */
static bool is_position_near_track(struct vec2 _vPos)
{
    if (!m_bBBoxValid)
        return false;

    return (_vPos.fX >= m_fTrackMinX && _vPos.fX <= m_fTrackMaxX && _vPos.fY >= m_fTrackMinY && _vPos.fY <= m_fTrackMaxY);
}

/* Helper: Test a segment and update best result if closer */
static void test_segment(uint16_t _uSegIndex, struct vec2 _vPos, float *_pBestDistSq, uint16_t *_pBestSegIndex, float *_pBestT, struct vec2 *_pBestClosest)
{
    uint16_t uNext = (_uSegIndex == m_track.uSampleCount - 1) ? 0 : (_uSegIndex + 1);
    struct vec2 vSegStart = m_track.pSamples[_uSegIndex].vPos;
    struct vec2 vSegEnd = m_track.pSamples[uNext].vPos;
    struct vec2 vSegDir = vec2_sub(vSegEnd, vSegStart);
    float fSegLenSq = vec2_mag_sq(vSegDir);

    struct vec2 vClosest;
    float fT;

    if (fSegLenSq < 1e-6f)
    {
        /* Degenerate segment, use start point */
        vClosest = vSegStart;
        fT = 0.0f;
    }
    else
    {
        /* Project position onto segment */
        struct vec2 vToStart = vec2_sub(_vPos, vSegStart);
        fT = vec2_dot(vToStart, vSegDir) / fSegLenSq;
        fT = (fT < 0.0f) ? 0.0f : ((fT > 1.0f) ? 1.0f : fT);
        vClosest = vec2_mix(vSegStart, vSegEnd, fT);
    }

    struct vec2 vDelta = vec2_sub(_vPos, vClosest);
    float fDistSq = vec2_mag_sq(vDelta);

    if (fDistSq < *_pBestDistSq)
    {
        *_pBestDistSq = fDistSq;
        *_pBestSegIndex = _uSegIndex;
        *_pBestT = fT;
        *_pBestClosest = vClosest;
    }
}

/* Find closest point on track polyline to given position */
static void find_closest_point(struct vec2 _vPos, struct vec2 *_pOutClosest, struct vec2 *_pOutNormal, float *_pOutLateralDist, float *_pOutS)
{
    if (!_pOutClosest || !_pOutNormal || !_pOutLateralDist || !_pOutS || !m_track.bInitialized || m_track.uSampleCount < 2)
    {
        if (_pOutClosest)
            *_pOutClosest = vec2_zero();
        if (_pOutNormal)
            *_pOutNormal = vec2_make(1.0f, 0.0f);
        if (_pOutLateralDist)
            *_pOutLateralDist = 0.0f;
        if (_pOutS)
            *_pOutS = 0.0f;
        return;
    }

    float fBestDistSq = 1e10f;
    uint16_t uBestSegIndex = 0;
    float fBestT = 0.0f;
    struct vec2 vBestClosest = vec2_zero();
    bool bFoundInWindow = false;

    /* First, try searching in window around last known segment (with wrapping) */
    if (m_uLastSegIndex < m_track.uSampleCount)
    {
        /* Search window segments, handling wrap-around */
        for (int16_t iOffset = -(int16_t)RACE_TRACK_SEARCH_WINDOW; iOffset <= (int16_t)RACE_TRACK_SEARCH_WINDOW; ++iOffset)
        {
            int16_t iSegIndex = (int16_t)m_uLastSegIndex + iOffset;

            /* Wrap index to valid range */
            while (iSegIndex < 0)
                iSegIndex += (int16_t)m_track.uSampleCount;
            while (iSegIndex >= (int16_t)m_track.uSampleCount)
                iSegIndex -= (int16_t)m_track.uSampleCount;

            test_segment((uint16_t)iSegIndex, _vPos, &fBestDistSq, &uBestSegIndex, &fBestT, &vBestClosest);
            bFoundInWindow = true;
        }
    }

    /* If not found in window, do full search (optimized with chunks if available) */
    if (!bFoundInWindow)
    {
        debugf("race_track: Full search triggered (lastSegIndex=%d, sampleCount=%d)\n", m_uLastSegIndex, m_track.uSampleCount);

        if (m_pChunks && m_uChunkCount > 0)
        {
            /* Optimized search: check chunks first */
            /* float fExpand = RACE_TRACK_WIDTH * 0.5f + RACE_TRACK_CHUNK_SIZE * RACE_TRACK_STEP * 0.5f; */ /* Conservative search radius unused for now */

            for (uint16_t c = 0; c < m_uChunkCount; ++c)
            {
                const RaceTrackChunk *pChunk = &m_pChunks[c];

                /* Check if point is near chunk (AABB check with margin) */
                /* We use a looser check here to be safe */
                if (_vPos.fX >= pChunk->fMinX - 50.0f && _vPos.fX <= pChunk->fMaxX + 50.0f && _vPos.fY >= pChunk->fMinY - 50.0f && _vPos.fY <= pChunk->fMaxY + 50.0f)
                {
                    /* Search inside this chunk */
                    for (uint16_t i = pChunk->uStartIndex; i < pChunk->uEndIndex; ++i)
                    {
                        test_segment(i, _vPos, &fBestDistSq, &uBestSegIndex, &fBestT, &vBestClosest);
                    }
                }
            }
        }
        else
        {
            /* Fallback: linear search all segments */
            for (uint16_t i = 0; i < m_track.uSampleCount; ++i)
            {
                test_segment(i, _vPos, &fBestDistSq, &uBestSegIndex, &fBestT, &vBestClosest);
            }
        }
    }

    /* Update cached segment index */
    m_uLastSegIndex = uBestSegIndex;

    /* Interpolate normal between segment endpoints */
    uint16_t uNext = (uBestSegIndex == m_track.uSampleCount - 1) ? 0 : (uBestSegIndex + 1);
    struct vec2 vNormal = vec2_mix(m_track.pSamples[uBestSegIndex].vNormal, m_track.pSamples[uNext].vNormal, fBestT);
    vNormal = vec2_normalize(vNormal);

    /* Calculate signed lateral distance */
    struct vec2 vToPos = vec2_sub(_vPos, vBestClosest);
    float fLateralDist = vec2_dot(vToPos, vNormal);

    /* Calculate progress coordinate (interpolate S values) */
    float fS = m_track.pSamples[uBestSegIndex].fS;
    float fSDelta = m_track.pSamples[uNext].fS - m_track.pSamples[uBestSegIndex].fS;
    if (fSDelta < 0.0f)
        fSDelta += m_track.fTotalLength; /* Handle wrap (last segment to first) */
    fS += fSDelta * fBestT;
    if (fS >= m_track.fTotalLength)
        fS -= m_track.fTotalLength; /* Wrap S coordinate */

    *_pOutClosest = vBestClosest;
    *_pOutNormal = vNormal;
    *_pOutLateralDist = fLateralDist;
    *_pOutS = fS;
}

/* Check if UFO is colliding with track boundary */
static bool check_track_collision(struct vec2 _vUfoPos, struct vec2 *_pOutClosest, struct vec2 *_pOutNormal, float *_pOutPenetration)
{
    if (!_pOutClosest || !_pOutNormal || !_pOutPenetration || !m_track.bInitialized)
        return false;

    struct vec2 vClosest, vNormal;
    float fLateralDist, fS;
    find_closest_point(_vUfoPos, &vClosest, &vNormal, &fLateralDist, &fS);

    /* Check if off-track */
    float fAbsLateralDist = (fLateralDist < 0.0f) ? -fLateralDist : fLateralDist;
    if (fAbsLateralDist <= RACE_TRACK_HALF_COLLIDE)
        return false; /* On track, no collision */

    /* Collision detected */
    *_pOutClosest = vClosest;
    *_pOutNormal = vNormal;
    *_pOutPenetration = fAbsLateralDist - RACE_TRACK_HALF_COLLIDE;
    return true;
}

/* Public API */
void race_track_set_collision_enabled(bool _bEnabled)
{
    m_bCollisionEnabled = _bEnabled;
    /* Reset cached segment index when disabling collision to prevent stale data */
    if (!_bEnabled)
    {
        m_uLastSegIndex = 0;
        m_bWasColliding = false;
    }
}

bool race_track_is_collision_enabled(void)
{
    return m_bCollisionEnabled;
}

void race_track_update(void)
{
    /* Check collision if enabled and track is initialized */
    if (!m_bCollisionEnabled || !m_track.bInitialized)
    {
        m_bWasColliding = false;
        return;
    }

    /* Get UFO position */
    struct vec2 vUfoPos = ufo_get_position();

    /* Fast bounding box check first */
    if (!is_position_near_track(vUfoPos))
    {
        m_bWasColliding = false;
        return; /* UFO is far from track, skip expensive collision check */
    }

    /* Check collision */
    struct vec2 vClosest, vNormal;
    float fPenetration;
    bool bIsColliding = check_track_collision(vUfoPos, &vClosest, &vNormal, &fPenetration);

    if (bIsColliding)
    {
        /* Calculate position correction (push back to boundary) */
        float fSign = (vec2_dot(vec2_sub(vUfoPos, vClosest), vNormal) < 0.0f) ? -1.0f : 1.0f;
        struct vec2 vCorrection = vec2_scale(vNormal, -fSign * (fPenetration + RACE_TRACK_COLLISION_EPSILON));
        ufo_set_position(vec2_add(vUfoPos, vCorrection));

        /* Reflect velocity off wall (billiard ball style) */
        /* Formula: v' = v - 2 * dot(v, n) * n */
        /* This reverses the component along the normal while keeping tangential component */
        struct vec2 vUfoVel = ufo_get_velocity();
        float fVelDot = vec2_dot(vUfoVel, vNormal);
        struct vec2 vReflected = vec2_sub(vUfoVel, vec2_scale(vNormal, 2.0f * fVelDot));
        ufo_set_velocity(vReflected);

        /* Apply bounce effect */
        ufo_apply_bounce_effect(RACE_TRACK_BOUNCE_COOLDOWN_MS);
    }

    m_bWasColliding = bIsColliding;
}

/* Update cached camera bounds (call once per frame before rendering) */
static void update_cached_camera_bounds(void)
{
    float fZoom = camera_get_zoom(&g_mainCamera);
    float fHalfX = (float)g_mainCamera.vHalf.iX / fZoom;
    float fHalfY = (float)g_mainCamera.vHalf.iY / fZoom;

    m_cachedCameraBounds.fCamLeft = g_mainCamera.vPos.fX - fHalfX;
    m_cachedCameraBounds.fCamRight = g_mainCamera.vPos.fX + fHalfX;
    m_cachedCameraBounds.fCamTop = g_mainCamera.vPos.fY - fHalfY;
    m_cachedCameraBounds.fCamBottom = g_mainCamera.vPos.fY + fHalfY;
    m_cachedCameraBounds.bValid = true;
}

/* Fast visibility check using cached camera bounds */
static inline bool camera_rect_visible_cached(float _fMinX, float _fMinY, float _fMaxX, float _fMaxY)
{
    if (!m_cachedCameraBounds.bValid)
        return true; /* Fallback: render if cache invalid */

    /* Early exit tests (most common case: off-screen) */
    if (_fMaxX < m_cachedCameraBounds.fCamLeft)
        return false;
    if (_fMinX > m_cachedCameraBounds.fCamRight)
        return false;
    if (_fMaxY < m_cachedCameraBounds.fCamTop)
        return false;
    if (_fMinY > m_cachedCameraBounds.fCamBottom)
        return false;

    return true;
}

/* Compute bounding box of 4 vec2 vertices (optimized for performance) */
static inline void compute_quad_bounds(const struct vec2 *_pV0, const struct vec2 *_pV1, const struct vec2 *_pV2, const struct vec2 *_pV3, float *_pMinX, float *_pMaxX,
                                       float *_pMinY, float *_pMaxY)
{
    /* Initialize with first vertex */
    float fMinX = _pV0->fX;
    float fMaxX = _pV0->fX;
    float fMinY = _pV0->fY;
    float fMaxY = _pV0->fY;

    /* Expand bounds with remaining vertices (unrolled for performance) */
    if (_pV1->fX < fMinX)
        fMinX = _pV1->fX;
    if (_pV1->fX > fMaxX)
        fMaxX = _pV1->fX;
    if (_pV1->fY < fMinY)
        fMinY = _pV1->fY;
    if (_pV1->fY > fMaxY)
        fMaxY = _pV1->fY;

    if (_pV2->fX < fMinX)
        fMinX = _pV2->fX;
    if (_pV2->fX > fMaxX)
        fMaxX = _pV2->fX;
    if (_pV2->fY < fMinY)
        fMinY = _pV2->fY;
    if (_pV2->fY > fMaxY)
        fMaxY = _pV2->fY;

    if (_pV3->fX < fMinX)
        fMinX = _pV3->fX;
    if (_pV3->fX > fMaxX)
        fMaxX = _pV3->fX;
    if (_pV3->fY < fMinY)
        fMinY = _pV3->fY;
    if (_pV3->fY > fMaxY)
        fMaxY = _pV3->fY;

    *_pMinX = fMinX;
    *_pMaxX = fMaxX;
    *_pMinY = fMinY;
    *_pMaxY = fMaxY;
}

/* Screen-space culling: check if quad is completely off-screen */
static inline bool screen_cull_quad(const struct vec2i *_pV0, const struct vec2i *_pV1, const struct vec2i *_pV2, const struct vec2i *_pV3)
{
    int iMinX = _pV0->iX;
    int iMaxX = _pV0->iX;
    int iMinY = _pV0->iY;
    int iMaxY = _pV0->iY;

    /* Find bounding box of all 4 vertices */
    if (_pV1->iX < iMinX)
        iMinX = _pV1->iX;
    if (_pV1->iX > iMaxX)
        iMaxX = _pV1->iX;
    if (_pV1->iY < iMinY)
        iMinY = _pV1->iY;
    if (_pV1->iY > iMaxY)
        iMaxY = _pV1->iY;

    if (_pV2->iX < iMinX)
        iMinX = _pV2->iX;
    if (_pV2->iX > iMaxX)
        iMaxX = _pV2->iX;
    if (_pV2->iY < iMinY)
        iMinY = _pV2->iY;
    if (_pV2->iY > iMaxY)
        iMaxY = _pV2->iY;

    if (_pV3->iX < iMinX)
        iMinX = _pV3->iX;
    if (_pV3->iX > iMaxX)
        iMaxX = _pV3->iX;
    if (_pV3->iY < iMinY)
        iMinY = _pV3->iY;
    if (_pV3->iY > iMaxY)
        iMaxY = _pV3->iY;

    /* Check if completely off-screen */
    int iScreenW = g_mainCamera.vHalf.iX * 2;
    int iScreenH = g_mainCamera.vHalf.iY * 2;

    if (iMaxX < 0 || iMaxY < 0 || iMinX >= iScreenW || iMinY >= iScreenH)
        return true; /* Culled */

    return false; /* Visible */
}

/* Fast world-to-screen using precalculated camera values */
static inline void fast_world_to_screen(float _fBaseX, float _fBaseY, float _fZoom, struct vec2 _vWorld, struct vec2i *_pOutScreen)
{
    float fScreenX = _fBaseX + _vWorld.fX * _fZoom;
    float fScreenY = _fBaseY + _vWorld.fY * _fZoom;
    _pOutScreen->iX = (int)fm_floorf(fScreenX);
    _pOutScreen->iY = (int)fm_floorf(fScreenY);
}

/* Render road fill (textured) */
static void render_road_fill(uint16_t _uStep)
{
    if (m_track.uSampleCount < 2 || !m_pRoadSprite)
        return;

    /* Ensure step is valid and divides chunk size if using chunks */
    if (_uStep < 1)
        _uStep = 1;

    float fHalfWidth = RACE_TRACK_WIDTH * 0.5f;
    float fBorderThick = RACE_TRACK_BORDER_THICK;
    float fInnerWidth = fHalfWidth - fBorderThick;

    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY_CONST);
    rdpq_mode_dithering(DITHER_NOISE_SQUARE);

    /* Set alpha to 0.5 (128/255) */
    rdpq_set_fog_color(RGBA32(0, 0, 0, 128));
    rdpq_mode_alphacompare(255);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);

    /* Upload road texture */
    rdpq_sprite_upload(TILE0, m_pRoadSprite, &m_roadTexParms);

    /* Precalculate camera transform values */
    float fZoom = camera_get_zoom(&g_mainCamera);
    float fBaseX = (float)g_mainCamera.vHalf.iX - g_mainCamera.vPos.fX * fZoom;
    float fBaseY = (float)g_mainCamera.vHalf.iY - g_mainCamera.vPos.fY * fZoom;

    /* Build triangle strip for road fill */
    if (m_pChunks && m_uChunkCount > 0)
    {
        /* Optimized chunk-based rendering */
        for (uint16_t c = 0; c < m_uChunkCount; ++c)
        {
            const RaceTrackChunk *pChunk = &m_pChunks[c];

            /* Check visibility of chunk bounding box */
            if (!camera_rect_visible_cached(pChunk->fMinX, pChunk->fMinY, pChunk->fMaxX, pChunk->fMaxY))
                continue;

            /* Pre-calculate first sample's screen coordinates to prime the loop cache */
            uint16_t uFirst = pChunk->uStartIndex;
            const RaceTrackSample *pFirstSample = &m_track.pSamples[uFirst];
            struct vec2 vFirstLeftInner = vec2_add(pFirstSample->vPos, vec2_scale(pFirstSample->vNormal, fInnerWidth));
            struct vec2 vFirstRightInner = vec2_sub(pFirstSample->vPos, vec2_scale(pFirstSample->vNormal, fInnerWidth));

            struct vec2i vLeftInnerScreen, vRightInnerScreen;
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vFirstLeftInner, &vLeftInnerScreen);
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vFirstRightInner, &vRightInnerScreen);

            /* Render samples in this chunk with LOD step */
            for (uint16_t i = pChunk->uStartIndex; i < pChunk->uEndIndex; i += _uStep)
            {
                uint16_t uNext = i + _uStep;
                /* Handle loop wrapping: if next point exceeds total count, snap to start point */
                if (uNext >= m_track.uSampleCount)
                    uNext = 0;

                const RaceTrackSample *pNextSample = &m_track.pSamples[uNext];

                /* Compute inner edges for NEXT sample */
                struct vec2 vNextLeftInner = vec2_add(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fInnerWidth));
                struct vec2 vNextRightInner = vec2_sub(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fInnerWidth));

                /* Convert to screen coordinates using fast transform */
                struct vec2i vNextLeftInnerScreen, vNextRightInnerScreen;
                fast_world_to_screen(fBaseX, fBaseY, fZoom, vNextLeftInner, &vNextLeftInnerScreen);
                fast_world_to_screen(fBaseX, fBaseY, fZoom, vNextRightInner, &vNextRightInnerScreen);

                /* Screen-space culling: skip if quad is completely off-screen */
                /* For textured quads, we can't easily rely on the screen_cull_quad logic across triangle strips effectively in this loop structure
                   without more complex state tracking. However, we can perform a quick bounding box check on the segment. */

                int iMinX = vLeftInnerScreen.iX;
                if (vRightInnerScreen.iX < iMinX)
                    iMinX = vRightInnerScreen.iX;
                if (vNextLeftInnerScreen.iX < iMinX)
                    iMinX = vNextLeftInnerScreen.iX;
                if (vNextRightInnerScreen.iX < iMinX)
                    iMinX = vNextRightInnerScreen.iX;

                int iMaxX = vLeftInnerScreen.iX;
                if (vRightInnerScreen.iX > iMaxX)
                    iMaxX = vRightInnerScreen.iX;
                if (vNextLeftInnerScreen.iX > iMaxX)
                    iMaxX = vNextLeftInnerScreen.iX;
                if (vNextRightInnerScreen.iX > iMaxX)
                    iMaxX = vNextRightInnerScreen.iX;

                int iMinY = vLeftInnerScreen.iY;
                if (vRightInnerScreen.iY < iMinY)
                    iMinY = vRightInnerScreen.iY;
                if (vNextLeftInnerScreen.iY < iMinY)
                    iMinY = vNextLeftInnerScreen.iY;
                if (vNextRightInnerScreen.iY < iMinY)
                    iMinY = vNextRightInnerScreen.iY;

                int iMaxY = vLeftInnerScreen.iY;
                if (vRightInnerScreen.iY > iMaxY)
                    iMaxY = vRightInnerScreen.iY;
                if (vNextLeftInnerScreen.iY > iMaxY)
                    iMaxY = vNextLeftInnerScreen.iY;
                if (vNextRightInnerScreen.iY > iMaxY)
                    iMaxY = vNextRightInnerScreen.iY;

                int iScreenW = g_mainCamera.vHalf.iX * 2;
                int iScreenH = g_mainCamera.vHalf.iY * 2;

                if (!(iMaxX < 0 || iMaxY < 0 || iMinX >= iScreenW || iMinY >= iScreenH))
                {
                    /* Use constant S coordinates since texture is uniform in each column */
                    const float fS = 0.0f;
                    float fT0 = 0.0f;                    /* Left edge */
                    float fT1 = m_fRoadTexHeight - 1.0f; /* Right edge */

                    /* Build textured quad using 5-element vertex arrays [x, y, s, t, w] */
                    float v0[5] = {(float)vLeftInnerScreen.iX, (float)vLeftInnerScreen.iY, fS, fT0, 1.0f};
                    float v1[5] = {(float)vRightInnerScreen.iX, (float)vRightInnerScreen.iY, fS, fT1, 1.0f};
                    float v2[5] = {(float)vNextLeftInnerScreen.iX, (float)vNextLeftInnerScreen.iY, fS, fT0, 1.0f};
                    float v3[5] = {(float)vNextRightInnerScreen.iX, (float)vNextRightInnerScreen.iY, fS, fT1, 1.0f};

                    /* Render two triangles forming the textured quad */
                    rdpq_triangle(&TRIFMT_TEX, v0, v2, v1);
                    rdpq_triangle(&TRIFMT_TEX, v1, v2, v3);
                }

                /* Shift "Next" to "Current" for next iteration */
                vLeftInnerScreen = vNextLeftInnerScreen;
                vRightInnerScreen = vNextRightInnerScreen;
            }
        }
    }
    else
    {
        /* Fallback: iterate all samples */
        for (uint16_t i = 0; i < m_track.uSampleCount; i += _uStep)
        {
            uint16_t uNext = i + _uStep;
            /* Handle loop wrapping: if next point exceeds total count, snap to start point */
            if (uNext >= m_track.uSampleCount)
                uNext = 0;

            const RaceTrackSample *pSample = &m_track.pSamples[i];
            const RaceTrackSample *pNextSample = &m_track.pSamples[uNext];

            /* Compute inner edges */
            struct vec2 vLeftInner = vec2_add(pSample->vPos, vec2_scale(pSample->vNormal, fInnerWidth));
            struct vec2 vRightInner = vec2_sub(pSample->vPos, vec2_scale(pSample->vNormal, fInnerWidth));
            struct vec2 vNextLeftInner = vec2_add(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fInnerWidth));
            struct vec2 vNextRightInner = vec2_sub(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fInnerWidth));

            /* Quick visibility check using cached bounds (still useful for individual quads) */
            float fMinX, fMaxX, fMinY, fMaxY;
            compute_quad_bounds(&vLeftInner, &vRightInner, &vNextLeftInner, &vNextRightInner, &fMinX, &fMaxX, &fMinY, &fMaxY);

            if (!camera_rect_visible_cached(fMinX, fMinY, fMaxX, fMaxY))
                continue;

            /* Convert to screen coordinates using fast transform */
            struct vec2i vLeftInnerScreen, vRightInnerScreen, vNextLeftInnerScreen, vNextRightInnerScreen;
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vLeftInner, &vLeftInnerScreen);
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vRightInner, &vRightInnerScreen);
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vNextLeftInner, &vNextLeftInnerScreen);
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vNextRightInner, &vNextRightInnerScreen);

            /* Screen-space culling: skip if quad is completely off-screen */
            if (screen_cull_quad(&vLeftInnerScreen, &vRightInnerScreen, &vNextLeftInnerScreen, &vNextRightInnerScreen))
                continue;

            /* Use constant S coordinates since texture is uniform in each column */
            const float fS = 0.0f;
            float fT0 = 0.0f;                    /* Left edge */
            float fT1 = m_fRoadTexHeight - 1.0f; /* Right edge */

            /* Build textured quad using 5-element vertex arrays [x, y, s, t, w] */
            float v0[5] = {(float)vLeftInnerScreen.iX, (float)vLeftInnerScreen.iY, fS, fT0, 1.0f};
            float v1[5] = {(float)vRightInnerScreen.iX, (float)vRightInnerScreen.iY, fS, fT1, 1.0f};
            float v2[5] = {(float)vNextLeftInnerScreen.iX, (float)vNextLeftInnerScreen.iY, fS, fT0, 1.0f};
            float v3[5] = {(float)vNextRightInnerScreen.iX, (float)vNextRightInnerScreen.iY, fS, fT1, 1.0f};

            /* Render two triangles forming the textured quad */
            rdpq_triangle(&TRIFMT_TEX, v0, v2, v1);
            rdpq_triangle(&TRIFMT_TEX, v1, v2, v3);
        }
    }
}

/* Render border strip (left or right) */
static void render_border_strip(bool _bLeft, uint16_t _uStep)
{
    /* Don't render borders if collision is disabled */
    if (!m_bCollisionEnabled)
        return;

    if (m_track.uSampleCount < 2 || !m_pBorderSprite)
        return;

    /* Ensure step is valid */
    if (_uStep < 1)
        _uStep = 1;

    float fHalfWidth = RACE_TRACK_WIDTH * 0.5f;
    float fBorderThick = RACE_TRACK_BORDER_THICK;
    float fInnerWidth = fHalfWidth - fBorderThick;

    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    /* Upload border texture */
    rdpq_sprite_upload(TILE0, m_pBorderSprite, &m_borderTexParms);

    /* Precalculate camera transform values */
    float fZoom = camera_get_zoom(&g_mainCamera);
    float fBaseX = (float)g_mainCamera.vHalf.iX - g_mainCamera.vPos.fX * fZoom;
    float fBaseY = (float)g_mainCamera.vHalf.iY - g_mainCamera.vPos.fY * fZoom;

    /* Build triangle strip for border */
    if (m_pChunks && m_uChunkCount > 0)
    {
        /* Optimized chunk-based rendering */
        for (uint16_t c = 0; c < m_uChunkCount; ++c)
        {
            const RaceTrackChunk *pChunk = &m_pChunks[c];

            /* Check visibility of chunk bounding box */
            if (!camera_rect_visible_cached(pChunk->fMinX, pChunk->fMinY, pChunk->fMaxX, pChunk->fMaxY))
                continue;

            /* Pre-calculate first sample's screen coordinates to prime the loop cache */
            uint16_t uFirst = pChunk->uStartIndex;
            const RaceTrackSample *pFirstSample = &m_track.pSamples[uFirst];
            struct vec2 vFirstInner, vFirstOuter;
            if (_bLeft)
            {
                vFirstInner = vec2_add(pFirstSample->vPos, vec2_scale(pFirstSample->vNormal, fInnerWidth));
                vFirstOuter = vec2_add(pFirstSample->vPos, vec2_scale(pFirstSample->vNormal, fHalfWidth));
            }
            else
            {
                vFirstInner = vec2_sub(pFirstSample->vPos, vec2_scale(pFirstSample->vNormal, fInnerWidth));
                vFirstOuter = vec2_sub(pFirstSample->vPos, vec2_scale(pFirstSample->vNormal, fHalfWidth));
            }
            struct vec2i vInnerScreen, vOuterScreen;
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vFirstInner, &vInnerScreen);
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vFirstOuter, &vOuterScreen);

            /* Render samples in this chunk with LOD step */
            for (uint16_t i = pChunk->uStartIndex; i < pChunk->uEndIndex; i += _uStep)
            {
                uint16_t uNext = i + _uStep;
                /* Handle loop wrapping: if next point exceeds total count, snap to start point */
                if (uNext >= m_track.uSampleCount)
                    uNext = 0;

                const RaceTrackSample *pNextSample = &m_track.pSamples[uNext];

                /* Compute edges */
                struct vec2 vNextInner, vNextOuter;

                if (_bLeft)
                {
                    vNextInner = vec2_add(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fInnerWidth));
                    vNextOuter = vec2_add(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fHalfWidth));
                }
                else
                {
                    vNextInner = vec2_sub(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fInnerWidth));
                    vNextOuter = vec2_sub(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fHalfWidth));
                }

                /* Convert to screen coordinates using fast transform */
                struct vec2i vNextInnerScreen, vNextOuterScreen;
                fast_world_to_screen(fBaseX, fBaseY, fZoom, vNextInner, &vNextInnerScreen);
                fast_world_to_screen(fBaseX, fBaseY, fZoom, vNextOuter, &vNextOuterScreen);

                /* Screen-space culling: skip if quad is completely off-screen */
                /* Inline unrolled bounding box check */
                int iMinX = vInnerScreen.iX;
                if (vOuterScreen.iX < iMinX)
                    iMinX = vOuterScreen.iX;
                if (vNextInnerScreen.iX < iMinX)
                    iMinX = vNextInnerScreen.iX;
                if (vNextOuterScreen.iX < iMinX)
                    iMinX = vNextOuterScreen.iX;

                int iMaxX = vInnerScreen.iX;
                if (vOuterScreen.iX > iMaxX)
                    iMaxX = vOuterScreen.iX;
                if (vNextInnerScreen.iX > iMaxX)
                    iMaxX = vNextInnerScreen.iX;
                if (vNextOuterScreen.iX > iMaxX)
                    iMaxX = vNextOuterScreen.iX;

                int iMinY = vInnerScreen.iY;
                if (vOuterScreen.iY < iMinY)
                    iMinY = vOuterScreen.iY;
                if (vNextInnerScreen.iY < iMinY)
                    iMinY = vNextInnerScreen.iY;
                if (vNextOuterScreen.iY < iMinY)
                    iMinY = vNextOuterScreen.iY;

                int iMaxY = vInnerScreen.iY;
                if (vOuterScreen.iY > iMaxY)
                    iMaxY = vOuterScreen.iY;
                if (vNextInnerScreen.iY > iMaxY)
                    iMaxY = vNextInnerScreen.iY;
                if (vNextOuterScreen.iY > iMaxY)
                    iMaxY = vNextOuterScreen.iY;

                int iScreenW = g_mainCamera.vHalf.iX * 2;
                int iScreenH = g_mainCamera.vHalf.iY * 2;

                if (!(iMaxX < 0 || iMaxY < 0 || iMinX >= iScreenW || iMinY >= iScreenH))
                {
                    /* Use constant S coordinates since texture is uniform in each column */
                    const float fS = 0.0f;
                    float fT0 = 0.0f;                      /* Inner edge */
                    float fT1 = m_fBorderTexHeight - 1.0f; /* Outer edge */

                    /* Build textured quad using 5-element vertex arrays [x, y, s, t, w] */
                    float v0[5] = {(float)vInnerScreen.iX, (float)vInnerScreen.iY, fS, fT0, 1.0f};
                    float v1[5] = {(float)vOuterScreen.iX, (float)vOuterScreen.iY, fS, fT1, 1.0f};
                    float v2[5] = {(float)vNextInnerScreen.iX, (float)vNextInnerScreen.iY, fS, fT0, 1.0f};
                    float v3[5] = {(float)vNextOuterScreen.iX, (float)vNextOuterScreen.iY, fS, fT1, 1.0f};

                    /* Render two triangles forming the textured quad */
                    rdpq_triangle(&TRIFMT_TEX, v0, v2, v1);
                    rdpq_triangle(&TRIFMT_TEX, v1, v2, v3);
                }

                /* Shift "Next" to "Current" */
                vInnerScreen = vNextInnerScreen;
                vOuterScreen = vNextOuterScreen;
            }
        }
    }
    else
    {
        /* Fallback: iterate all samples */
        for (uint16_t i = 0; i < m_track.uSampleCount; i += _uStep)
        {
            uint16_t uNext = i + _uStep;
            /* Handle loop wrapping: if next point exceeds total count, snap to start point */
            if (uNext >= m_track.uSampleCount)
                uNext = 0;

            const RaceTrackSample *pSample = &m_track.pSamples[i];
            const RaceTrackSample *pNextSample = &m_track.pSamples[uNext];

            /* Compute edges */
            struct vec2 vInner, vOuter, vNextInner, vNextOuter;

            if (_bLeft)
            {
                vInner = vec2_add(pSample->vPos, vec2_scale(pSample->vNormal, fInnerWidth));
                vOuter = vec2_add(pSample->vPos, vec2_scale(pSample->vNormal, fHalfWidth));
                vNextInner = vec2_add(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fInnerWidth));
                vNextOuter = vec2_add(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fHalfWidth));
            }
            else
            {
                vInner = vec2_sub(pSample->vPos, vec2_scale(pSample->vNormal, fInnerWidth));
                vOuter = vec2_sub(pSample->vPos, vec2_scale(pSample->vNormal, fHalfWidth));
                vNextInner = vec2_sub(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fInnerWidth));
                vNextOuter = vec2_sub(pNextSample->vPos, vec2_scale(pNextSample->vNormal, fHalfWidth));
            }

            /* Quick visibility check using cached bounds */
            float fMinX, fMaxX, fMinY, fMaxY;
            compute_quad_bounds(&vInner, &vOuter, &vNextInner, &vNextOuter, &fMinX, &fMaxX, &fMinY, &fMaxY);

            if (!camera_rect_visible_cached(fMinX, fMinY, fMaxX, fMaxY))
                continue;

            /* Convert to screen coordinates using fast transform */
            struct vec2i vInnerScreen, vOuterScreen, vNextInnerScreen, vNextOuterScreen;
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vInner, &vInnerScreen);
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vOuter, &vOuterScreen);
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vNextInner, &vNextInnerScreen);
            fast_world_to_screen(fBaseX, fBaseY, fZoom, vNextOuter, &vNextOuterScreen);

            /* Screen-space culling: skip if quad is completely off-screen */
            if (screen_cull_quad(&vInnerScreen, &vOuterScreen, &vNextInnerScreen, &vNextOuterScreen))
                continue;

            /* Use constant S coordinates since texture is uniform in each column */
            const float fS = 0.0f;
            float fT0 = 0.0f;                      /* Inner edge */
            float fT1 = m_fBorderTexHeight - 1.0f; /* Outer edge */

            /* Build textured quad using 5-element vertex arrays [x, y, s, t, w] */
            float v0[5] = {(float)vInnerScreen.iX, (float)vInnerScreen.iY, fS, fT0, 1.0f};
            float v1[5] = {(float)vOuterScreen.iX, (float)vOuterScreen.iY, fS, fT1, 1.0f};
            float v2[5] = {(float)vNextInnerScreen.iX, (float)vNextInnerScreen.iY, fS, fT0, 1.0f};
            float v3[5] = {(float)vNextOuterScreen.iX, (float)vNextOuterScreen.iY, fS, fT1, 1.0f};

            /* Render two triangles forming the textured quad */
            rdpq_triangle(&TRIFMT_TEX, v0, v2, v1);
            rdpq_triangle(&TRIFMT_TEX, v1, v2, v3);
        }
    }
}

/* Render finish/start line stripe */
static void render_finish_line(void)
{
    if (m_track.uSampleCount < 2 || !m_pFinishLineSprite)
        return;

    /* Get the first sample (start/finish line position) */
    const RaceTrackSample *pSample = &m_track.pSamples[0];

    float fHalfWidth = RACE_TRACK_WIDTH * 0.4f;

    /* Compute left and right edges of the finish line */
    struct vec2 vLeft = vec2_add(pSample->vPos, vec2_scale(pSample->vNormal, fHalfWidth));
    struct vec2 vRight = vec2_sub(pSample->vPos, vec2_scale(pSample->vNormal, fHalfWidth));

    /* Extend slightly along the tangent to make the line visible */
    float fLineThickness = 8.0f; /* Thickness of the finish line */
    struct vec2 vOffset = vec2_scale(pSample->vTangent, fLineThickness * 0.5f);

    struct vec2 vLeftStart = vec2_sub(vLeft, vOffset);
    struct vec2 vLeftEnd = vec2_add(vLeft, vOffset);
    struct vec2 vRightStart = vec2_sub(vRight, vOffset);
    struct vec2 vRightEnd = vec2_add(vRight, vOffset);

    /* Set up rendering modes for full brightness (no multiply blend or fog) */
    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    /* Upload finish line texture */
    rdpq_sprite_upload(TILE0, m_pFinishLineSprite, &m_finishLineTexParms);

    /* Precalculate camera transform values */
    float fZoom = camera_get_zoom(&g_mainCamera);
    float fBaseX = (float)g_mainCamera.vHalf.iX - g_mainCamera.vPos.fX * fZoom;
    float fBaseY = (float)g_mainCamera.vHalf.iY - g_mainCamera.vPos.fY * fZoom;

    /* Calculate texture coordinates for repeated pattern along the width */
    /* S coordinate varies from 0 to number of repeats (for texture wrapping) */
    float fTrackWidth = RACE_TRACK_WIDTH;
    float fRepeats = fTrackWidth / m_fFinishLineTexWidth * 5.0f;
    float fS0 = 0.0f;
    float fS1 = fRepeats; /* Number of texture repeats across track width */
    float fT0 = 0.0f;
    float fT1 = (float)m_pFinishLineSprite->height - 1.0f;

    /* Convert to screen coordinates using fast transform */
    struct vec2i vLeftStartScreen, vLeftEndScreen, vRightStartScreen, vRightEndScreen;
    fast_world_to_screen(fBaseX, fBaseY, fZoom, vLeftStart, &vLeftStartScreen);
    fast_world_to_screen(fBaseX, fBaseY, fZoom, vLeftEnd, &vLeftEndScreen);
    fast_world_to_screen(fBaseX, fBaseY, fZoom, vRightStart, &vRightStartScreen);
    fast_world_to_screen(fBaseX, fBaseY, fZoom, vRightEnd, &vRightEndScreen);

    /* Screen-space culling: skip if quad is completely off-screen */
    if (screen_cull_quad(&vLeftStartScreen, &vRightStartScreen, &vLeftEndScreen, &vRightEndScreen))
        return;

    /* Build textured quad using 5-element vertex arrays [x, y, s, t, w] */
    float v0[5] = {(float)vLeftStartScreen.iX, (float)vLeftStartScreen.iY, fS0, fT0, 1.0f};
    float v1[5] = {(float)vRightStartScreen.iX, (float)vRightStartScreen.iY, fS1, fT0, 1.0f};
    float v2[5] = {(float)vLeftEndScreen.iX, (float)vLeftEndScreen.iY, fS0, fT1, 1.0f};
    float v3[5] = {(float)vRightEndScreen.iX, (float)vRightEndScreen.iY, fS1, fT1, 1.0f};

    /* Render two triangles forming the textured quad */
    rdpq_triangle(&TRIFMT_TEX, v0, v2, v1);
    rdpq_triangle(&TRIFMT_TEX, v1, v2, v3);
}

float race_track_get_progress_for_position(struct vec2 _vPos)
{
    if (!m_track.bInitialized || m_track.uSampleCount < 2)
        return 0.0f;

    struct vec2 vClosest, vNormal;
    float fLateralDist, fS;
    find_closest_point(_vPos, &vClosest, &vNormal, &fLateralDist, &fS);
    return fS;
}

/* Internal helper: Find sample indices and interpolation factor for a given s value */
static bool find_sample_indices_for_s(float _fS, uint16_t *_pOutLower, uint16_t *_pOutUpper, float *_pOutT)
{
    if (!_pOutLower || !_pOutUpper || !_pOutT || !m_track.bInitialized || m_track.uSampleCount < 2)
        return false;

    /* Wrap s to valid range [0, L) */
    float fS = _fS;
    while (fS < 0.0f)
        fS += m_track.fTotalLength;
    while (fS >= m_track.fTotalLength)
        fS -= m_track.fTotalLength;

    /* Find the sample index that contains this s value */
    /* Binary search for efficiency */
    uint16_t uLower = 0;
    uint16_t uUpper = m_track.uSampleCount - 1;

    while (uUpper - uLower > 1)
    {
        uint16_t uMid = (uLower + uUpper) / 2;
        if (m_track.pSamples[uMid].fS < fS)
        {
            uLower = uMid;
        }
        else
        {
            uUpper = uMid;
        }
    }

    /* Handle wrap case: if uUpper is 0 and fS is near the end, use last segment */
    if (uUpper == 0 && fS > m_track.pSamples[m_track.uSampleCount - 1].fS)
    {
        uLower = m_track.uSampleCount - 1;
        uUpper = 0;
    }

    /* Calculate interpolation factor */
    const RaceTrackSample *pLower = &m_track.pSamples[uLower];
    const RaceTrackSample *pUpper = &m_track.pSamples[uUpper];

    float fSLower = pLower->fS;
    float fSUpper = pUpper->fS;
    if (fSUpper < fSLower)
        fSUpper += m_track.fTotalLength; /* Handle wrap */

    float fSegmentLength = fSUpper - fSLower;
    float fT = (fSegmentLength > 1e-6f) ? ((fS - fSLower) / fSegmentLength) : 0.0f;
    fT = clampf_01(fT);

    *_pOutLower = uLower;
    *_pOutUpper = uUpper;
    *_pOutT = fT;
    return true;
}

bool race_track_get_position_for_progress(float _fS, struct vec2 *_pOutPos, struct vec2 *_pOutTangent)
{
    if (!_pOutPos || !_pOutTangent)
        return false;

    uint16_t uLower, uUpper;
    float fT;
    if (!find_sample_indices_for_s(_fS, &uLower, &uUpper, &fT))
        return false;

    /* Interpolate position and tangent between samples */
    const RaceTrackSample *pLower = &m_track.pSamples[uLower];
    const RaceTrackSample *pUpper = &m_track.pSamples[uUpper];

    *_pOutPos = vec2_mix(pLower->vPos, pUpper->vPos, fT);
    *_pOutTangent = vec2_normalize(vec2_mix(pLower->vTangent, pUpper->vTangent, fT));

    return true;
}

bool race_track_get_position_for_progress_with_normal(float _fS, struct vec2 *_pOutPos, struct vec2 *_pOutTangent, struct vec2 *_pOutNormal)
{
    if (!_pOutPos || !_pOutTangent || !_pOutNormal)
        return false;

    uint16_t uLower, uUpper;
    float fT;
    if (!find_sample_indices_for_s(_fS, &uLower, &uUpper, &fT))
        return false;

    /* Interpolate position, tangent, and normal between samples */
    const RaceTrackSample *pLower = &m_track.pSamples[uLower];
    const RaceTrackSample *pUpper = &m_track.pSamples[uUpper];

    *_pOutPos = vec2_mix(pLower->vPos, pUpper->vPos, fT);
    *_pOutTangent = vec2_normalize(vec2_mix(pLower->vTangent, pUpper->vTangent, fT));
    *_pOutNormal = vec2_normalize(vec2_mix(pLower->vNormal, pUpper->vNormal, fT));

    return true;
}

void race_track_render(void)
{
    if (!m_track.bInitialized || m_track.uSampleCount < 2)
        return;

    /* Cache camera bounds once per frame (must be done before early exit check) */
    update_cached_camera_bounds();

    /* Get camera zoom level for LOD optimization */
    float fZoom = camera_get_zoom(&g_mainCamera);

    /* Determine LOD step and border visibility */
    uint16_t uStep = RACE_TRACK_LOD_STEP_HIGH;
    bool bRenderBorders = true;

    if (fZoom < RACE_TRACK_LOD_ZOOM_LOW)
    {
        /* Extreme zoom out: aggressive optimization */
        uStep = RACE_TRACK_LOD_STEP_LOW;
        bRenderBorders = RACE_TRACK_LOD_BORDERS_LOW;
    }
    else if (fZoom < RACE_TRACK_LOD_ZOOM_MED)
    {
        /* Moderate zoom out: moderate optimization */
        uStep = RACE_TRACK_LOD_STEP_MED;
        bRenderBorders = true;
    }

    /* Early exit: check if entire track is off-screen */
    if (m_bBBoxValid)
    {
        if (!camera_rect_visible_cached(m_fTrackMinX, m_fTrackMinY, m_fTrackMaxX, m_fTrackMaxY))
            return; /* Entire track is off-screen, skip all rendering */
    }

    /* Render finish/start line */
    render_finish_line();

    /* Render road fill */
    render_road_fill(uStep);

    /* Render borders (skip if disabled by LOD or collision is disabled) */
    if (bRenderBorders && m_bCollisionEnabled)
    {
        render_border_strip(true, uStep);  /* Left border */
        render_border_strip(false, uStep); /* Right border */
    }
}
