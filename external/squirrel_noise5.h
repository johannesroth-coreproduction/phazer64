//-----------------------------------------------------------------------------------------------
// squirrel_noise5.h
//
// SquirrelNoise5 - Squirrel's Raw Noise utilities (version 5)
// Original code from http://eiserloh.net/noise/SquirrelNoise5.hpp
// by Squirrel Eiserloh
//
// Original code: CC-BY-3.0 US (Attribution in source code comments is sufficient.)
// https://creativecommons.org/licenses/by/3.0/us/
//
// IMPORTANT (UB-mirroring port):
// - The original C++ Get2d/Get3d/Get4d fold coordinates using signed int arithmetic.
// - Signed overflow is undefined behavior in both C and C++.
// - This C port intentionally performs the same signed int folding (and thus preserves UB).
// - Therefore results are only "identical" to your C++ build when compiled under equivalent
//   compiler + flags + optimization assumptions.
//
//-----------------------------------------------------------------------------------------------

#pragma once

#include <stdint.h>

/* Raw pseudorandom noise functions (random-access / deterministic *if no UB is triggered*). */
uint32_t sq5_get_1d_u32(int _iIndex, uint32_t _uSeed);
uint32_t sq5_get_2d_u32(int _iX, int _iY, uint32_t _uSeed);
uint32_t sq5_get_3d_u32(int _iX, int _iY, int _iZ, uint32_t _uSeed);
uint32_t sq5_get_4d_u32(int _iX, int _iY, int _iZ, int _iT, uint32_t _uSeed);

/* Mapped to floats in [0,1]. */
float sq5_get_1d_zero_to_one(int _iIndex, uint32_t _uSeed);
float sq5_get_2d_zero_to_one(int _iX, int _iY, uint32_t _uSeed);
float sq5_get_3d_zero_to_one(int _iX, int _iY, int _iZ, uint32_t _uSeed);
float sq5_get_4d_zero_to_one(int _iX, int _iY, int _iZ, int _iT, uint32_t _uSeed);

/* Mapped to floats in [-1,1]. */
float sq5_get_1d_neg_one_to_one(int _iIndex, uint32_t _uSeed);
float sq5_get_2d_neg_one_to_one(int _iX, int _iY, uint32_t _uSeed);
float sq5_get_3d_neg_one_to_one(int _iX, int _iY, int _iZ, uint32_t _uSeed);
float sq5_get_4d_neg_one_to_one(int _iX, int _iY, int _iZ, int _iT, uint32_t _uSeed);

/* Convenience macros for seed=0, matching the C++ default argument behavior. */
#define SQ5_GET_1D_U32(_iIndex) sq5_get_1d_u32((_iIndex), 0u)
#define SQ5_GET_2D_U32(_iX, _iY) sq5_get_2d_u32((_iX), (_iY), 0u)
#define SQ5_GET_3D_U32(_iX, _iY, _iZ) sq5_get_3d_u32((_iX), (_iY), (_iZ), 0u)
#define SQ5_GET_4D_U32(_iX, _iY, _iZ, _iT) sq5_get_4d_u32((_iX), (_iY), (_iZ), (_iT), 0u)