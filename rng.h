#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Global Game Seed
 */
extern uint32_t g_uGameSeed;

/**
 * Initialize the global random number generator with a seed.
 */
void rng_init(uint32_t _uSeed);

/**
 * Get a random uint32_t.
 */
uint32_t rngu(void);

/**
 * Get a random integer in the range [min, max] (inclusive).
 */
int rngi(int _iMin, int _iMax);

/**
 * Get a random float in the range [min, max).
 */
float rngf(float _fMin, float _fMax);

/**
 * Check a probability (0.0 to 1.0). Returns true if the check passes.
 */
bool rngb(float _fChance);
