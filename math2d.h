#pragma once

#include <math.h>

/* Simple 2D vector for world-space positions, velocities, etc. */
struct vec2
{
    float fX;
    float fY;
};

struct vec2i
{
    int iX;
    int iY;
};

/* ----- float vec2 helpers ----- */

static inline struct vec2 vec2_zero(void)
{
    struct vec2 v = {0.0f, 0.0f};
    return v;
}

static inline struct vec2 vec2_make(float _fX, float _fY)
{
    struct vec2 v = {_fX, _fY};
    return v;
}

static inline struct vec2 vec2_add(struct vec2 _vA, struct vec2 _vB)
{
    struct vec2 v = {_vA.fX + _vB.fX, _vA.fY + _vB.fY};
    return v;
}

static inline struct vec2 vec2_sub(struct vec2 _vA, struct vec2 _vB)
{
    struct vec2 v = {_vA.fX - _vB.fX, _vA.fY - _vB.fY};
    return v;
}

static inline struct vec2 vec2_scale(struct vec2 _v, float _fScale)
{
    struct vec2 v = {_v.fX * _fScale, _v.fY * _fScale};
    return v;
}

static inline float vec2_mag_sq(struct vec2 _v)
{
    return _v.fX * _v.fX + _v.fY * _v.fY;
}

static inline float vec2_mag(struct vec2 _v)
{
    return sqrtf(vec2_mag_sq(_v));
}

static inline float vec2_dot(struct vec2 _vA, struct vec2 _vB)
{
    return _vA.fX * _vB.fX + _vA.fY * _vB.fY;
}

static inline struct vec2 vec2_normalize(struct vec2 _v)
{
    float fM = vec2_mag(_v);
    if (fM > 1e-6f)
    {
        return vec2_scale(_v, 1.0f / fM);
    }
    return vec2_zero();
}

static inline float vec2_dist_sq(struct vec2 _vA, struct vec2 _vB)
{
    struct vec2 d = vec2_sub(_vA, _vB);
    return vec2_mag_sq(d);
}

static inline float vec2_dist(struct vec2 _vA, struct vec2 _vB)
{
    return sqrtf(vec2_dist_sq(_vA, _vB));
}

static inline struct vec2 vec2_mix(struct vec2 _vA, struct vec2 _vB, float _fT)
{
    struct vec2 v = {_vA.fX * (1.0f - _fT) + _vB.fX * _fT, _vA.fY * (1.0f - _fT) + _vB.fY * _fT};
    return v;
}

/* ----- int vec2i helpers (pure integer math) ----- */

static inline struct vec2i vec2i_zero(void)
{
    struct vec2i v = {0, 0};
    return v;
}

static inline struct vec2i vec2i_make(int _iX, int _iY)
{
    struct vec2i v = {_iX, _iY};
    return v;
}

static inline struct vec2i vec2i_add(struct vec2i _vA, struct vec2i _vB)
{
    struct vec2i v = {_vA.iX + _vB.iX, _vA.iY + _vB.iY};
    return v;
}

static inline struct vec2i vec2i_sub(struct vec2i _vA, struct vec2i _vB)
{
    struct vec2i v = {_vA.iX - _vB.iX, _vA.iY - _vB.iY};
    return v;
}

static inline struct vec2i vec2i_scale(struct vec2i _v, float _fScale)
{
    struct vec2i v = {_v.iX * _fScale, _v.iY * _fScale};
    return v;
}

/* ----- mixed vec2 / vec2i operations (result is float vec2) ----- */

static inline struct vec2 vec2_add_vec2i(struct vec2 _vA, struct vec2i _vB)
{
    struct vec2 v = {_vA.fX + (float)_vB.iX, _vA.fY + (float)_vB.iY};
    return v;
}

static inline struct vec2 vec2i_add_vec2(struct vec2i _vA, struct vec2 _vB)
{
    /* Commutative: reuse implementation. */
    return vec2_add_vec2i(_vB, _vA);
}

static inline struct vec2 vec2_sub_vec2i(struct vec2 _vA, struct vec2i _vB)
{
    struct vec2 v = {_vA.fX - (float)_vB.iX, _vA.fY - (float)_vB.iY};
    return v;
}

static inline struct vec2 vec2i_sub_vec2(struct vec2i _vA, struct vec2 _vB)
{
    struct vec2 v = {(float)_vA.iX - _vB.fX, (float)_vA.iY - _vB.fY};
    return v;
}

/* ----- utility helpers ----- */

#include "libdragon.h" /* FM_PI */

/* Wrap angle to [-PI, PI] range. */
static inline float angle_wrap_rad(float _fAngle)
{
    if (_fAngle > FM_PI)
        return _fAngle - 2.0f * FM_PI;
    else if (_fAngle < -FM_PI)
        return _fAngle + 2.0f * FM_PI;
    return _fAngle;
}

/* Wrap angle to [0, 2*PI) range. */
static inline float angle_wrap_rad_0_2pi(float _fAngle)
{
    float fTwoPi = 2.0f * FM_PI;
    /* Manual modulo: for game angles, simple loop is sufficient */
    if (_fAngle >= fTwoPi)
    {
        int iWraps = (int)(_fAngle / fTwoPi);
        _fAngle -= (float)iWraps * fTwoPi;
    }
    if (_fAngle < 0.0f)
    {
        int iWraps = (int)(-_fAngle / fTwoPi) + 1;
        _fAngle += (float)iWraps * fTwoPi;
    }
    return _fAngle;
}