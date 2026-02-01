#pragma once

#include <libdragon.h>

/**
 * @file resource_helper.h
 * @brief Helper macros for resource management and cache coherency
 */

/**
 * @brief Flush CPU cache for a memory buffer before RSP/RDP DMA access
 *
 * This macro ensures cache coherency by flushing CPU-written data to main RAM
 * before the RSP reads it via DMA. Only operates on cacheable memory (KSEG0).
 *
 * Use after CPU writes to dynamically allocated memory that will be read by
 * RSP/RDP (e.g., sprite data, tile maps, vertex buffers, lookup tables).
 *
 * @param _ptr Pointer to the buffer to flush
 * @param _size Size in bytes to flush
 *
 * Example:
 * @code
 *   int *data = malloc(sizeof(int) * count);
 *   // ... CPU writes to data ...
 *   CACHE_FLUSH_DATA(data, sizeof(int) * count);
 *   // ... now safe to DMA this data ...
 * @endcode
 */
#define CACHE_FLUSH_DATA(_ptr, _size)                                                                                                                                              \
    do                                                                                                                                                                             \
    {                                                                                                                                                                              \
        if (_ptr)                                                                                                                                                                  \
        {                                                                                                                                                                          \
            uintptr_t _addr = (uintptr_t)(_ptr);                                                                                                                                   \
            if (_addr >= 0x80000000UL && _addr < 0xa0000000UL)                                                                                                                     \
            {                                                                                                                                                                      \
                data_cache_hit_writeback_invalidate((_ptr), (_size));                                                                                                              \
            }                                                                                                                                                                      \
        }                                                                                                                                                                          \
    } while (0)

/**
 * @brief Safely free a sprite and set pointer to NULL
 *
 * This macro checks if the sprite pointer is not NULL before freeing it,
 * and sets the pointer to NULL after freeing to prevent double-free errors.
 *
 * @param ptr Pointer to sprite_t*
 */
#define SAFE_FREE_SPRITE(ptr)                                                                                                                                                      \
    do                                                                                                                                                                             \
    {                                                                                                                                                                              \
        if (ptr)                                                                                                                                                                   \
        {                                                                                                                                                                          \
            sprite_free(ptr);                                                                                                                                                      \
            (ptr) = NULL;                                                                                                                                                          \
        }                                                                                                                                                                          \
    } while (0)

/**
 * @brief Safely close a WAV64 file and set pointer to NULL
 *
 * This macro checks if the wav64 pointer is not NULL before closing it,
 * and sets the pointer to NULL after closing to prevent double-free errors.
 *
 * Note: wav64_close() automatically stops playback if the file is currently
 * playing, so there's no need to call mixer_ch_stop() beforehand.
 *
 * @param ptr Pointer to wav64_t*
 */
#define SAFE_CLOSE_WAV64(ptr)                                                                                                                                                      \
    do                                                                                                                                                                             \
    {                                                                                                                                                                              \
        if (ptr)                                                                                                                                                                   \
        {                                                                                                                                                                          \
            wav64_close(ptr);                                                                                                                                                      \
            (ptr) = NULL;                                                                                                                                                          \
        }                                                                                                                                                                          \
    } while (0)
