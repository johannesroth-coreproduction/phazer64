#include "path_mover.h"
#include "camera.h"
#include "csv_helper.h"
#include "frame_time.h"
#include "game_objects/gp_state.h"
#include "libdragon.h"
#include "math2d.h"
#include "math_helper.h"
#include "path_helper.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_MOVER_MAX_PATHS 32

/* Default values */
#define PATH_MOVER_DEFAULT_SPEED 3.5f /* world units per frame (at 60fps), same units as NPC_ALIEN_MAX_SPEED */
#define PATH_MOVER_DEFAULT_SINUS_AMPLITUDE 10.0f
#define PATH_MOVER_DEFAULT_SINUS_FREQUENCY 1.0f

/* Path instance structure */
struct PathInstance
{
    bool bInUse;
    path_state_t eState;
    path_mode_t eMode;
    bool bLoop;

    char szPathName[64];
    struct vec2 vCalculatedPos; /* Current calculated position along path */

    struct vec2 *pWaypoints;
    uint16_t uPointCount;
    uint16_t uCurrentSegment;
    float fSegmentProgress; /* 0.0 to 1.0 */

    float fSpeed; /* Speed in world units per frame (at 60fps), same units as NPC/UFO velocity */

    float fSinusAmplitude;
    float fSinusFrequency;
    float fTotalDistanceTraveled;    /* Total distance traveled for distance-based sinus wave */
    struct vec2 vSinusPerpendicular; /* Cached perpendicular direction for smooth transitions */
};

/* Static array of path instances */
static PathInstance s_aPaths[PATH_MOVER_MAX_PATHS];
static bool s_bSystemInitialized = false;

/* Helper: Find free slot in path array */
static PathInstance *find_free_slot(void)
{
    for (uint16_t i = 0; i < PATH_MOVER_MAX_PATHS; ++i)
    {
        if (!s_aPaths[i].bInUse)
            return &s_aPaths[i];
    }
    return NULL;
}

/* Helper: Validate path pointer */
static bool is_valid_path(PathInstance *_pPath)
{
    if (!_pPath)
        return false;

    /* Check if pointer is within our array bounds */
    if (_pPath < s_aPaths || _pPath >= s_aPaths + PATH_MOVER_MAX_PATHS)
        return false;

    /* Check if it's actually in use */
    return _pPath->bInUse;
}

/* Helper: Calculate perpendicular vector from direction */
static inline struct vec2 calculate_perpendicular(struct vec2 _vDirection)
{
    struct vec2 vNormalized = vec2_normalize(_vDirection);
    return vec2_make(-vNormalized.fY, vNormalized.fX);
}

/* Helper: Calculate sinus offset for current position */
static inline struct vec2 calculate_sinus_offset(PathInstance *_pPath, struct vec2 _vPerpendicular)
{
    float fSinusValue = sinf(_pPath->fTotalDistanceTraveled * _pPath->fSinusFrequency * 2.0f * FM_PI);
    return vec2_scale(_vPerpendicular, fSinusValue * _pPath->fSinusAmplitude);
}

/* Helper: Get start and end points for a segment (handles loop segment) */
static inline void get_segment_points(PathInstance *_pPath, uint16_t _uSegment, struct vec2 *_pOutStart, struct vec2 *_pOutEnd, bool *_pOutIsLoopSegment)
{
    if (_uSegment >= _pPath->uPointCount - 1)
    {
        if (_pPath->bLoop)
        {
            /* Loop segment: from last point to first point */
            *_pOutStart = _pPath->pWaypoints[_pPath->uPointCount - 1];
            *_pOutEnd = _pPath->pWaypoints[0];
            if (_pOutIsLoopSegment)
                *_pOutIsLoopSegment = true;
        }
        else
        {
            /* Not looping: use last point for both */
            *_pOutStart = _pPath->pWaypoints[_pPath->uPointCount - 1];
            *_pOutEnd = _pPath->pWaypoints[_pPath->uPointCount - 1];
            if (_pOutIsLoopSegment)
                *_pOutIsLoopSegment = false;
        }
    }
    else
    {
        /* Normal segment */
        *_pOutStart = _pPath->pWaypoints[_uSegment];
        *_pOutEnd = _pPath->pWaypoints[_uSegment + 1];
        if (_pOutIsLoopSegment)
            *_pOutIsLoopSegment = false;
    }
}

void path_mover_init(void)
{
    if (s_bSystemInitialized)
        return;

    memset(s_aPaths, 0, sizeof(s_aPaths));
    s_bSystemInitialized = true;
}

void path_mover_free_all(void)
{
    for (uint16_t i = 0; i < PATH_MOVER_MAX_PATHS; ++i)
    {
        if (s_aPaths[i].bInUse)
            path_mover_free(&s_aPaths[i]);
    }
    s_bSystemInitialized = false;
}

PathInstance *path_mover_load(const char *_pPathName)
{
    if (!s_bSystemInitialized)
        path_mover_init();

    if (!_pPathName)
    {
        debugf("path_mover_load: Invalid path name (pathName=%p)\n", _pPathName);
        return NULL;
    }

    /* Load waypoints using path_helper */
    struct vec2 *pWaypoints = NULL;
    uint16_t uCount = 0;
    if (!path_helper_load_named_points("path", _pPathName, &pWaypoints, &uCount))
    {
        debugf("path_mover_load: Failed to load path '%s'\n", _pPathName);
        return NULL;
    }

    /* Find free slot */
    PathInstance *pPath = find_free_slot();
    if (!pPath)
    {
        debugf("path_mover_load: No free slots available (max %d paths), cannot load '%s'\n", PATH_MOVER_MAX_PATHS, _pPathName);
        free(pWaypoints);
        return NULL;
    }

    /* Initialize path */
    memset(pPath, 0, sizeof(*pPath));
    pPath->bInUse = true;
    pPath->eState = PATH_STATE_UNPLAYED;
    pPath->eMode = PATH_MODE_LINEAR;
    pPath->bLoop = false;
    pPath->pWaypoints = pWaypoints;
    pPath->uPointCount = uCount;
    pPath->uCurrentSegment = 0;
    pPath->fSegmentProgress = 0.0f;
    pPath->fSpeed = PATH_MOVER_DEFAULT_SPEED;
    pPath->fSinusAmplitude = PATH_MOVER_DEFAULT_SINUS_AMPLITUDE;
    pPath->fSinusFrequency = PATH_MOVER_DEFAULT_SINUS_FREQUENCY;
    pPath->fTotalDistanceTraveled = 0.0f;

    /* Initialize calculated position to first waypoint */
    if (uCount > 0)
    {
        pPath->vCalculatedPos = pWaypoints[0];
    }
    else
    {
        pPath->vCalculatedPos = vec2_zero();
    }

    /* Initialize perpendicular direction based on first segment */
    if (uCount >= 2)
    {
        struct vec2 vFirstDir = vec2_sub(pWaypoints[1], pWaypoints[0]);
        float fFirstDirLength = vec2_mag(vFirstDir);
        if (fFirstDirLength > 1e-6f)
        {
            pPath->vSinusPerpendicular = calculate_perpendicular(vFirstDir);
        }
        else
        {
            pPath->vSinusPerpendicular = vec2_make(0.0f, 1.0f); /* Default: up */
        }
    }
    else
    {
        pPath->vSinusPerpendicular = vec2_make(0.0f, 1.0f); /* Default: up */
    }

    if (!csv_helper_copy_string_safe(_pPathName, pPath->szPathName, sizeof(pPath->szPathName)))
    {
        free(pWaypoints);
        pPath->bInUse = false;
        return NULL;
    }

    return pPath;
}

void path_mover_start(PathInstance *_pPath)
{
    if (!is_valid_path(_pPath))
        return;

    _pPath->eState = PATH_STATE_PLAYING;
}

void path_mover_pause(PathInstance *_pPath)
{
    if (!is_valid_path(_pPath))
        return;

    /* Only pause if currently playing */
    if (_pPath->eState == PATH_STATE_PLAYING)
        _pPath->eState = PATH_STATE_PAUSED;
}

void path_mover_resume(PathInstance *_pPath)
{
    if (!is_valid_path(_pPath))
        return;

    /* Only resume if currently paused */
    if (_pPath->eState == PATH_STATE_PAUSED)
        _pPath->eState = PATH_STATE_PLAYING;
}

void path_mover_stop(PathInstance *_pPath)
{
    if (!is_valid_path(_pPath))
        return;

    _pPath->eState = PATH_STATE_UNPLAYED;
    _pPath->uCurrentSegment = 0;
    _pPath->fSegmentProgress = 0.0f;
    _pPath->fTotalDistanceTraveled = 0.0f;

    /* Reset calculated position to first waypoint */
    if (_pPath->uPointCount > 0)
    {
        _pPath->vCalculatedPos = _pPath->pWaypoints[0];
    }
}

void path_mover_free(PathInstance *_pPath)
{
    if (!is_valid_path(_pPath))
        return;

    if (_pPath->pWaypoints)
    {
        free(_pPath->pWaypoints);
        _pPath->pWaypoints = NULL;
    }

    _pPath->bInUse = false;
    memset(_pPath, 0, sizeof(*_pPath));
}

void path_mover_set_speed(PathInstance *_pPath, float _fSpeed)
{
    if (!is_valid_path(_pPath))
        return;

    _pPath->fSpeed = _fSpeed;
}

void path_mover_set_loop(PathInstance *_pPath, bool _bLoop)
{
    if (!is_valid_path(_pPath))
        return;

    _pPath->bLoop = _bLoop;
}

void path_mover_set_mode(PathInstance *_pPath, path_mode_t _eMode)
{
    if (!is_valid_path(_pPath))
        return;

    _pPath->eMode = _eMode;
}

void path_mover_set_sinus_params(PathInstance *_pPath, float _fAmplitude, float _fFrequency)
{
    if (!is_valid_path(_pPath))
        return;

    _pPath->fSinusAmplitude = _fAmplitude;
    _pPath->fSinusFrequency = _fFrequency;
}

float path_mover_get_speed(PathInstance *_pPath)
{
    if (!is_valid_path(_pPath))
        return 0.0f;

    return _pPath->fSpeed;
}

path_state_t path_mover_get_state(PathInstance *_pPath)
{
    if (!is_valid_path(_pPath))
        return PATH_STATE_UNPLAYED;

    return _pPath->eState;
}

/* Helper: Calculate current position along path (shared by get_current_pos and update_path) */
static inline struct vec2 calculate_path_position(PathInstance *_pPath)
{
    /* If path hasn't started or has no waypoints, return first waypoint */
    if (_pPath->uPointCount == 0)
        return vec2_zero();

    if (_pPath->uPointCount == 1)
    {
        return _pPath->pWaypoints[0];
    }

    /* Get current segment points */
    uint16_t uSegment = _pPath->uCurrentSegment;
    struct vec2 vStart, vEnd;
    bool bIsLoopSegment = false;
    get_segment_points(_pPath, uSegment, &vStart, &vEnd, &bIsLoopSegment);

    /* If not looping and at end, return final waypoint */
    if (!_pPath->bLoop && uSegment >= _pPath->uPointCount - 1)
    {
        return _pPath->pWaypoints[_pPath->uPointCount - 1];
    }

    /* Calculate linear position */
    struct vec2 vLinearPos = vec2_mix(vStart, vEnd, _pPath->fSegmentProgress);

    /* Apply sinus offset if in sinus_fly mode */
    if (_pPath->eMode == PATH_MODE_SINUS_FLY)
    {
        /* Calculate direction vector for current segment */
        struct vec2 vDirection = vec2_sub(vEnd, vStart);
        float fDirLength = vec2_mag(vDirection);
        if (fDirLength > 1e-6f)
        {
            /* Calculate perpendicular vector for current segment */
            struct vec2 vSegmentPerpendicular = calculate_perpendicular(vDirection);

            /* Smoothly blend perpendicular direction to avoid jumps at segment boundaries */
            /* Use a slow blend factor to transition smoothly over the entire segment */
            float fBlendFactor = clampf_01(_pPath->fSegmentProgress * 0.3f);
            _pPath->vSinusPerpendicular = vec2_normalize(vec2_mix(_pPath->vSinusPerpendicular, vSegmentPerpendicular, fBlendFactor));

            /* Calculate and apply sinus offset */
            struct vec2 vSinusOffset = calculate_sinus_offset(_pPath, _pPath->vSinusPerpendicular);
            vLinearPos = vec2_add(vLinearPos, vSinusOffset);
        }
    }

    return vLinearPos;
}

struct vec2 path_mover_get_current_pos(PathInstance *_pPath)
{
    if (!is_valid_path(_pPath))
        return vec2_zero();

    return _pPath->vCalculatedPos;
}

/* Update a single path instance */
static void update_path(PathInstance *_pPath)
{
    if (!_pPath || !_pPath->bInUse)
        return;

    /* Only update if playing */
    if (_pPath->eState != PATH_STATE_PLAYING)
        return;

    if (_pPath->uPointCount < 2)
        return;

    /* Get current segment */
    uint16_t uSegment = _pPath->uCurrentSegment;
    struct vec2 vStart, vEnd;
    bool bIsLoopSegment = false;

    /* Check if we're past the end (not looping) */
    if (uSegment >= _pPath->uPointCount - 1 && !_pPath->bLoop)
    {
        /* Not looping: finish - ensure final position is set */
        _pPath->eState = PATH_STATE_FINISHED;
        /* Set calculated position to final waypoint (stable landing, no sinus offset) */
        _pPath->vCalculatedPos = _pPath->pWaypoints[_pPath->uPointCount - 1];
        return;
    }

    /* Get segment points (handles loop segment) */
    get_segment_points(_pPath, uSegment, &vStart, &vEnd, &bIsLoopSegment);

    /* Calculate segment length */
    float fSegmentLength = vec2_dist(vStart, vEnd);
    if (fSegmentLength < 1e-6f)
    {
        /* Zero-length segment: skip to next */
        _pPath->uCurrentSegment++;
        _pPath->fSegmentProgress = 0.0f;
        return;
    }

    /* Calculate distance to move this frame */
    /* Speed is in world units per frame (at 60fps), multiply by frame_time_mul() to get actual movement */
    float fFrameMul = frame_time_mul();
    float fDistanceToMove = _pPath->fSpeed * fFrameMul;

    /* Update total distance traveled for distance-based sinus wave */
    _pPath->fTotalDistanceTraveled += fDistanceToMove;

    /* Calculate remaining distance in current segment */
    float fRemainingInSegment = fSegmentLength * (1.0f - _pPath->fSegmentProgress);

    /* Move along segment */
    if (fDistanceToMove >= fRemainingInSegment)
    {
        /* Reached end of segment */
        float fExtraDistance = fDistanceToMove - fRemainingInSegment;

        if (bIsLoopSegment)
        {
            /* Completed loop segment: reset to start (but keep fTotalDistanceTraveled for smooth sinus) */
            _pPath->uCurrentSegment = 0;
            _pPath->fSegmentProgress = 0.0f;
            /* Note: We do NOT reset fTotalDistanceTraveled - this ensures smooth sinus wave continuity */

            /* If we have more distance, continue to first segment */
            if (fExtraDistance > 0.0f && _pPath->uPointCount >= 2)
            {
                struct vec2 vNextStart, vNextEnd;
                get_segment_points(_pPath, 0, &vNextStart, &vNextEnd, NULL);
                float fNextSegmentLength = vec2_dist(vNextStart, vNextEnd);
                if (fNextSegmentLength > 1e-6f)
                {
                    _pPath->fSegmentProgress = clampf_01(fExtraDistance / fNextSegmentLength);
                }
            }
        }
        else
        {
            /* Normal segment: move to next */
            _pPath->uCurrentSegment++;
            _pPath->fSegmentProgress = 0.0f;

            /* If we have more distance, continue to next segment */
            if (fExtraDistance > 0.0f)
            {
                struct vec2 vNextStart, vNextEnd;
                get_segment_points(_pPath, _pPath->uCurrentSegment, &vNextStart, &vNextEnd, NULL);
                float fNextSegmentLength = vec2_dist(vNextStart, vNextEnd);
                if (fNextSegmentLength > 1e-6f)
                {
                    _pPath->fSegmentProgress = clampf_01(fExtraDistance / fNextSegmentLength);
                }
            }
        }

        /* Calculate and store position using calculate_path_position for smooth sinus transitions */
        _pPath->vCalculatedPos = calculate_path_position(_pPath);
    }
    else
    {
        /* Move within current segment */
        _pPath->fSegmentProgress = clampf_01(_pPath->fSegmentProgress + fDistanceToMove / fSegmentLength);

        /* Calculate and store position */
        _pPath->vCalculatedPos = calculate_path_position(_pPath);
    }
}

void path_mover_update(void)
{
    if (!s_bSystemInitialized)
        return;

    for (uint16_t i = 0; i < PATH_MOVER_MAX_PATHS; ++i)
    {
        if (s_aPaths[i].bInUse)
        {
            update_path(&s_aPaths[i]);
        }
    }
}

/* Helper: Clip line segment to screen bounds using math_helper line intersection */
static bool clip_line_to_screen(int *_pX1, int *_pY1, int *_pX2, int *_pY2)
{
    /* Convert integer screen coordinates to float vec2 for math_helper */
    struct vec2 vStart = vec2_make((float)*_pX1, (float)*_pY1);
    struct vec2 vEnd = vec2_make((float)*_pX2, (float)*_pY2);

    /* Screen rectangle bounds */
    struct vec2i vRectMin = vec2i_make(0, 0);
    struct vec2i vRectMax = vec2i_make(SCREEN_W, SCREEN_H);

    /* Use math_helper to check intersection and get exit point */
    struct vec2 vExitPoint;
    if (!math_helper_line_rect_intersection(vStart, vEnd, vRectMin, vRectMax, &vExitPoint))
    {
        return false; /* Line is completely outside */
    }

    /* Calculate entry point (t0) using Liang-Barsky algorithm */
    float x1 = vStart.fX;
    float y1 = vStart.fY;
    float x2 = vEnd.fX;
    float y2 = vEnd.fY;

    float t0 = 0.0f;
    float dx = x2 - x1;
    float dy = y2 - y1;

    /* Clip against each edge to find entry point (t0) */
    float p[4] = {-dx, dx, -dy, dy};
    float q[4] = {x1 - (float)vRectMin.iX, (float)vRectMax.iX - x1, y1 - (float)vRectMin.iY, (float)vRectMax.iY - y1};

    for (int i = 0; i < 4; ++i)
    {
        if (fabsf(p[i]) < 1e-6f)
        {
            /* Line is parallel to this edge */
            if (q[i] < 0.0f)
                return false; /* Line is outside */
        }
        else if (p[i] < 0.0f)
        {
            /* Only calculate t0 (entry point) since math_helper already gave us exit point */
            float r = q[i] / p[i];
            if (r > t0)
                t0 = r;
        }
    }

    /* Calculate clipped endpoints - entry point from t0, exit point from math_helper */
    *_pX1 = (int)(x1 + t0 * dx);
    *_pY1 = (int)(y1 + t0 * dy);
    *_pX2 = (int)vExitPoint.fX;
    *_pY2 = (int)vExitPoint.fY;

    return true;
}

/* Helper: Draw a line segment between two world points */
static void draw_debug_line(struct vec2 _vStart, struct vec2 _vEnd, color_t _uColor)
{
    /* Ensure we're in standard mode for triangle drawing */
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_set_prim_color(_uColor);

    struct vec2i vStartScreen, vEndScreen;
    camera_world_to_screen(&g_mainCamera, _vStart, &vStartScreen);
    camera_world_to_screen(&g_mainCamera, _vEnd, &vEndScreen);

    /* Check if both points are on-screen */
    bool bStartVisible = camera_is_screen_point_visible(&g_mainCamera, vStartScreen, 0.0f);
    bool bEndVisible = camera_is_screen_point_visible(&g_mainCamera, vEndScreen, 0.0f);

    if (bStartVisible && bEndVisible)
    {
        /* Both points on-screen: draw directly */
        int iDx = vEndScreen.iX - vStartScreen.iX;
        int iDy = vEndScreen.iY - vStartScreen.iY;
        float fLengthSq = (float)(iDx * iDx + iDy * iDy);

        if (fLengthSq < 4.0f)
            return;

        float fLength = sqrtf(fLengthSq);
        float fThickness = 0.5f;
        float fPerpX = -(float)iDy / fLength;
        float fPerpY = (float)iDx / fLength;

        float fX1 = (float)vStartScreen.iX + fPerpX * fThickness;
        float fY1 = (float)vStartScreen.iY + fPerpY * fThickness;
        float fX2 = (float)vStartScreen.iX - fPerpX * fThickness;
        float fY2 = (float)vStartScreen.iY - fPerpY * fThickness;
        float fX3 = (float)vEndScreen.iX - fPerpX * fThickness;
        float fY3 = (float)vEndScreen.iY - fPerpY * fThickness;
        float fX4 = (float)vEndScreen.iX + fPerpX * fThickness;
        float fY4 = (float)vEndScreen.iY + fPerpY * fThickness;

        float aTri1[3][2] = {{fX1, fY1}, {fX2, fY2}, {fX3, fY3}};
        float aTri2[3][2] = {{fX1, fY1}, {fX3, fY3}, {fX4, fY4}};
        rdpq_triangle(&TRIFMT_FILL, aTri1[0], aTri1[1], aTri1[2]);
        rdpq_triangle(&TRIFMT_FILL, aTri2[0], aTri2[1], aTri2[2]);
    }
    else
    {
        /* One or both points off-screen: clip to screen bounds */
        int iX1 = vStartScreen.iX;
        int iY1 = vStartScreen.iY;
        int iX2 = vEndScreen.iX;
        int iY2 = vEndScreen.iY;

        if (clip_line_to_screen(&iX1, &iY1, &iX2, &iY2))
        {
            int iDx = iX2 - iX1;
            int iDy = iY2 - iY1;
            float fLengthSq = (float)(iDx * iDx + iDy * iDy);

            if (fLengthSq < 4.0f)
                return;

            float fLength = sqrtf(fLengthSq);
            float fThickness = 0.5f;
            float fPerpX = -(float)iDy / fLength;
            float fPerpY = (float)iDx / fLength;

            float fX1 = (float)iX1 + fPerpX * fThickness;
            float fY1 = (float)iY1 + fPerpY * fThickness;
            float fX2 = (float)iX1 - fPerpX * fThickness;
            float fY2 = (float)iY1 - fPerpY * fThickness;
            float fX3 = (float)iX2 - fPerpX * fThickness;
            float fY3 = (float)iY2 - fPerpY * fThickness;
            float fX4 = (float)iX2 + fPerpX * fThickness;
            float fY4 = (float)iY2 + fPerpY * fThickness;

            float aTri1[3][2] = {{fX1, fY1}, {fX2, fY2}, {fX3, fY3}};
            float aTri2[3][2] = {{fX1, fY1}, {fX3, fY3}, {fX4, fY4}};
            rdpq_triangle(&TRIFMT_FILL, aTri1[0], aTri1[1], aTri1[2]);
            rdpq_triangle(&TRIFMT_FILL, aTri2[0], aTri2[1], aTri2[2]);
        }
    }
}

void path_mover_render_debug(void)
{
    if (!s_bSystemInitialized)
        return;

    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

    for (uint16_t i = 0; i < PATH_MOVER_MAX_PATHS; ++i)
    {
        if (!s_aPaths[i].bInUse)
            continue;

        PathInstance *pPath = &s_aPaths[i];

        if (pPath->uPointCount == 0)
            continue;

        /* Render waypoints as green rectangles (only if on-screen) */
        rdpq_set_mode_fill(RGBA32(0, 255, 0, 255)); /* Green */
        for (uint16_t j = 0; j < pPath->uPointCount; ++j)
        {
            if (camera_is_point_visible(&g_mainCamera, pPath->pWaypoints[j], 0.0f))
            {
                struct vec2i vScreen;
                camera_world_to_screen(&g_mainCamera, pPath->pWaypoints[j], &vScreen);
                rdpq_fill_rectangle(vScreen.iX - 2, vScreen.iY - 2, vScreen.iX + 2, vScreen.iY + 2);
            }
        }

        /* Render lines between points as white thin lines */
        if (pPath->uPointCount >= 2)
        {
            for (uint16_t j = 0; j < pPath->uPointCount - 1; ++j)
            {
                draw_debug_line(pPath->pWaypoints[j], pPath->pWaypoints[j + 1], RGBA32(255, 255, 255, 255));
            }

            /* If looping, draw line from last point to first */
            if (pPath->bLoop)
            {
                draw_debug_line(pPath->pWaypoints[pPath->uPointCount - 1], pPath->pWaypoints[0], RGBA32(255, 255, 255, 255));
            }
        }

        /* Render current position as red rectangle (only if on-screen) */
        struct vec2 vCurrentPos = path_mover_get_current_pos(pPath);
        if (camera_is_point_visible(&g_mainCamera, vCurrentPos, 0.0f))
        {
            struct vec2i vCurrentScreen;
            camera_world_to_screen(&g_mainCamera, vCurrentPos, &vCurrentScreen);
            rdpq_set_mode_fill(RGBA32(255, 0, 0, 255)); /* Red */
            rdpq_fill_rectangle(vCurrentScreen.iX - 3, vCurrentScreen.iY - 3, vCurrentScreen.iX + 3, vCurrentScreen.iY + 3);
        }
    }
}
