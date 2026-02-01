//-----------------------------------------------------------------------------------------------
// squirrel_noise5.c
//-----------------------------------------------------------------------------------------------

#include "squirrel_noise5.h"
#include <limits.h>

/* Enforce the same assumption the original header typically has: 32-bit int. */
_Static_assert(sizeof(int) == 4, "SquirrelNoise5 expects 32-bit 'int'.");
_Static_assert(UINT_MAX == 0xFFFFFFFFu, "SquirrelNoise5 expects 32-bit 'unsigned int'.");

/* SQ5 constants (from original SquirrelNoise5) */
#define SQ5_BIT_NOISE1 (0xd2a80a3fu)
#define SQ5_BIT_NOISE2 (0xa884f197u)
#define SQ5_BIT_NOISE3 (0x6C736F4Bu)
#define SQ5_BIT_NOISE4 (0xB79F3ABBu)
#define SQ5_BIT_NOISE5 (0x1b56c4f5u)

/* Internal: core hash. Defined modulo-2^32 because we use uint32_t. */
static inline uint32_t sq5_squirrel_noise5_u32(int _iPositionX, uint32_t _uSeed)
{
    uint32_t uMangledBits = (uint32_t)_iPositionX;

    uMangledBits *= SQ5_BIT_NOISE1;
    uMangledBits += _uSeed;
    uMangledBits ^= (uMangledBits >> 9);
    uMangledBits += SQ5_BIT_NOISE2;
    uMangledBits ^= (uMangledBits >> 11);
    uMangledBits *= SQ5_BIT_NOISE3;
    uMangledBits ^= (uMangledBits >> 13);
    uMangledBits += SQ5_BIT_NOISE4;
    uMangledBits ^= (uMangledBits >> 15);
    uMangledBits *= SQ5_BIT_NOISE5;
    uMangledBits ^= (uMangledBits >> 17);

    return uMangledBits;
}

/*------------------------------------------------------------------------------------------------*/
uint32_t sq5_get_1d_u32(int _iIndex, uint32_t _uSeed)
{
    return sq5_squirrel_noise5_u32(_iIndex, _uSeed);
}

/*------------------------------------------------------------------------------------------------*/
/* UB-mirroring note:
   This intentionally matches the C++ expression:
       indexX + (PRIME_NUMBER * indexY)
   using signed int arithmetic (and thus preserves signed overflow UB). */
uint32_t sq5_get_2d_u32(int _iX, int _iY, uint32_t _uSeed)
{
    const int iPrime = 198491317; /* signed int, like C++ constexpr int */

    /* This multiplication/addition can overflow signed int => UB (intentional mirror). */
    int iPos = _iX + (iPrime * _iY);

    return sq5_squirrel_noise5_u32(iPos, _uSeed);
}

/*------------------------------------------------------------------------------------------------*/
uint32_t sq5_get_3d_u32(int _iX, int _iY, int _iZ, uint32_t _uSeed)
{
    const int iPrime1 = 198491317;
    const int iPrime2 = 6542989;

    /* Potential signed overflow UB (intentional mirror). */
    int iPos = _iX + (iPrime1 * _iY) + (iPrime2 * _iZ);

    return sq5_squirrel_noise5_u32(iPos, _uSeed);
}

/*------------------------------------------------------------------------------------------------*/
uint32_t sq5_get_4d_u32(int _iX, int _iY, int _iZ, int _iT, uint32_t _uSeed)
{
    const int iPrime1 = 198491317;
    const int iPrime2 = 6542989;
    const int iPrime3 = 357239;

    /* Potential signed overflow UB (intentional mirror). */
    int iPos = _iX + (iPrime1 * _iY) + (iPrime2 * _iZ) + (iPrime3 * _iT);

    return sq5_squirrel_noise5_u32(iPos, _uSeed);
}

/*------------------------------------------------------------------------------------------------*/
float sq5_get_1d_zero_to_one(int _iIndex, uint32_t _uSeed)
{
    const double dOneOverMaxUint = (1.0 / 4294967295.0);
    return (float)(dOneOverMaxUint * (double)sq5_get_1d_u32(_iIndex, _uSeed));
}

/*------------------------------------------------------------------------------------------------*/
float sq5_get_2d_zero_to_one(int _iX, int _iY, uint32_t _uSeed)
{
    const double dOneOverMaxUint = (1.0 / 4294967295.0);
    return (float)(dOneOverMaxUint * (double)sq5_get_2d_u32(_iX, _iY, _uSeed));
}

/*------------------------------------------------------------------------------------------------*/
float sq5_get_3d_zero_to_one(int _iX, int _iY, int _iZ, uint32_t _uSeed)
{
    const double dOneOverMaxUint = (1.0 / 4294967295.0);
    return (float)(dOneOverMaxUint * (double)sq5_get_3d_u32(_iX, _iY, _iZ, _uSeed));
}

/*------------------------------------------------------------------------------------------------*/
float sq5_get_4d_zero_to_one(int _iX, int _iY, int _iZ, int _iT, uint32_t _uSeed)
{
    const double dOneOverMaxUint = (1.0 / 4294967295.0);
    return (float)(dOneOverMaxUint * (double)sq5_get_4d_u32(_iX, _iY, _iZ, _iT, _uSeed));
}

/*------------------------------------------------------------------------------------------------*/
float sq5_get_1d_neg_one_to_one(int _iIndex, uint32_t _uSeed)
{
    const double dOneOverMaxInt = (1.0 / 2147483647.0);

    /* Match C++: (int)SquirrelNoise5(...) then scale */
    int i = (int)sq5_get_1d_u32(_iIndex, _uSeed);

    return (float)(dOneOverMaxInt * (double)i);
}

/*------------------------------------------------------------------------------------------------*/
float sq5_get_2d_neg_one_to_one(int _iX, int _iY, uint32_t _uSeed)
{
    const double dOneOverMaxInt = (1.0 / 2147483647.0);
    int i = (int)sq5_get_2d_u32(_iX, _iY, _uSeed);
    return (float)(dOneOverMaxInt * (double)i);
}

/*------------------------------------------------------------------------------------------------*/
float sq5_get_3d_neg_one_to_one(int _iX, int _iY, int _iZ, uint32_t _uSeed)
{
    const double dOneOverMaxInt = (1.0 / 2147483647.0);
    int i = (int)sq5_get_3d_u32(_iX, _iY, _iZ, _uSeed);
    return (float)(dOneOverMaxInt * (double)i);
}

/*------------------------------------------------------------------------------------------------*/
float sq5_get_4d_neg_one_to_one(int _iX, int _iY, int _iZ, int _iT, uint32_t _uSeed)
{
    const double dOneOverMaxInt = (1.0 / 2147483647.0);
    int i = (int)sq5_get_4d_u32(_iX, _iY, _iZ, _iT, _uSeed);
    return (float)(dOneOverMaxInt * (double)i);
}
