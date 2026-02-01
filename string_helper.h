#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Safe string copy macro - copies string and ensures null termination
 * Usage: STRING_COPY(dest, src)
 * Expands to safe strncpy with automatic null termination */
#define STRING_COPY(dest, src)                                                                                                                                                     \
    do                                                                                                                                                                             \
    {                                                                                                                                                                              \
        strncpy((dest), (src), sizeof(dest) - 1);                                                                                                                                  \
        (dest)[sizeof(dest) - 1] = '\0';                                                                                                                                           \
    } while (0)

/* Convert a string to uppercase in-place
 * _pStr: String to convert (modified in-place)
 * _uSize: Size of the buffer
 * Returns true if successful, false on error */
bool string_helper_to_upper(char *_pStr, size_t _uSize);

/* Format a location/folder name nicely for display (uppercase)
 * _pSourceName: Source folder/location name
 * _pOutBuffer: Output buffer for formatted name
 * _uBufferSize: Size of output buffer
 * Returns true if successful, false on error
 *
 * This is the single point for location name formatting across the game.
 * Currently: converts to uppercase. Future: could add underscores to spaces, etc. */
bool string_helper_nice_location_name(const char *_pSourceName, char *_pOutBuffer, size_t _uBufferSize);
