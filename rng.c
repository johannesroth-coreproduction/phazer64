#include "rng.h"
#include "external/squirrel_noise5.h"
#include <limits.h>
#include <math.h>

uint32_t g_uGameSeed = 64;
static int m_iRngIndex = 0;

void rng_init(uint32_t _uSeed)
{
    g_uGameSeed = _uSeed;
    m_iRngIndex = 0;
}

uint32_t rngu(void)
{
    return sq5_get_1d_u32(m_iRngIndex++, g_uGameSeed);
}

int rngi(int _iMin, int _iMax)
{
    if (_iMin >= _iMax)
        return _iMin;
    uint32_t r = rngu();
    return _iMin + (r % (_iMax - _iMin + 1));
}

float rngf(float _fMin, float _fMax)
{
    if (_fMin >= _fMax)
        return _fMin;
    // Get 0..1 float
    float f = sq5_get_1d_zero_to_one(m_iRngIndex++, g_uGameSeed);
    return _fMin + f * (_fMax - _fMin);
}

bool rngb(float _fChance)
{
    if (_fChance <= 0.0f)
        return false;
    if (_fChance >= 1.0f)
        return true;
    return rngf(0.0f, 1.0f) < _fChance;
}
