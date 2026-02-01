#pragma once
#include "math2d.h"
#include <fmath.h>


/* Clamp a float value between min and max (inclusive). */
static inline float clampf(float _fValue, float _fMin, float _fMax)
{
    if (_fValue < _fMin)
        return _fMin;
    if (_fValue > _fMax)
        return _fMax;
    return _fValue;
}

static inline int round_to_int(float _fX)
{
    if (_fX >= 0.0f)
        return (int)fm_floorf(_fX + 0.5f);
    else
        return (int)fm_ceilf(_fX - 0.5f);
}

static inline float clampf_01(float _fX)
{
    if (_fX < 0.0f)
        return 0.0f;
    if (_fX > 1.0f)
        return 1.0f;
    return _fX;
}

/* Clamp an integer value between min and max (inclusive). */
static inline int clampi(int _iValue, int _iMin, int _iMax)
{
    if (_iValue < _iMin)
        return _iMin;
    if (_iValue > _iMax)
        return _iMax;
    return _iValue;
}

/* Line-rectangle intersection using Liang-Barsky algorithm
 * Finds the intersection point of a line segment with a rectangle border
 * _vStart: Start point of line (world coordinates)
 * _vEnd: End point of line (world coordinates)
 * _vRectMin: Top-left corner of rectangle (screen coordinates)
 * _vRectMax: Bottom-right corner of rectangle (screen coordinates)
 * _pOutIntersection: Output intersection point (screen coordinates)
 * Returns true if intersection found, false otherwise */
static inline bool math_helper_line_rect_intersection(struct vec2 _vStart, struct vec2 _vEnd, struct vec2i _vRectMin, struct vec2i _vRectMax, struct vec2 *_pOutIntersection)
{
    /* Convert world coordinates to screen for clipping */
    /* We need to work in screen space, so we'll use the line direction in world space
     * but clip against screen bounds. For now, assume we're working with screen coordinates
     * that have been converted from world space. */

    float x1 = _vStart.fX;
    float y1 = _vStart.fY;
    float x2 = _vEnd.fX;
    float y2 = _vEnd.fY;

    float t0 = 0.0f;
    float t1 = 1.0f;
    float dx = x2 - x1;
    float dy = y2 - y1;

    /* Clip against each edge */
    float p[4] = {-dx, dx, -dy, dy};
    float q[4] = {x1 - (float)_vRectMin.iX, (float)_vRectMax.iX - x1, y1 - (float)_vRectMin.iY, (float)_vRectMax.iY - y1};

    for (int i = 0; i < 4; ++i)
    {
        if (fabsf(p[i]) < 1e-6f)
        {
            /* Line is parallel to this edge */
            if (q[i] < 0.0f)
                return false; /* Line is outside */
        }
        else
        {
            float r = q[i] / p[i];
            if (p[i] < 0.0f)
            {
                if (r > t0)
                    t0 = r;
            }
            else
            {
                if (r < t1)
                    t1 = r;
            }
        }
    }

    if (t0 > t1)
        return false; /* Line is completely outside */

    /* Find the exit point (t1) which is where the line leaves the rectangle */
    /* For off-screen markers, we want the point where the line exits the screen */
    float tExit = t1;
    if (tExit < 0.0f || tExit > 1.0f)
        return false;

    /* Calculate intersection point */
    _pOutIntersection->fX = x1 + tExit * dx;
    _pOutIntersection->fY = y1 + tExit * dy;

    return true;
}