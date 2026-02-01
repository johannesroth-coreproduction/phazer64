#pragma once

#include "math2d.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/**
 * Strips end-of-line characters (\n and \r) from a string in-place.
 * @param _pLine The string to strip (modified in-place)
 */
void csv_helper_strip_eol(char *_pLine);

/**
 * Safe fgets wrapper that detects if a line was truncated.
 * @param _pBuf Buffer to read into
 * @param _uBufSize Size of the buffer
 * @param _pFile File to read from
 * @param _pbTruncated Optional output parameter set to true if line was truncated
 * @return true if a line was read, false on EOF or error
 */
bool csv_helper_fgets_checked(char *_pBuf, size_t _uBufSize, FILE *_pFile, bool *_pbTruncated);

/**
 * Counts the number of comma-separated values in a CSV line.
 * @param _pLine The CSV line to count
 * @return Number of comma-separated values (at least 1 if line is non-empty, 0 if empty)
 */
uint16_t csv_helper_count_values(const char *_pLine);

/**
 * Parses a single integer from a CSV token, skipping leading whitespace.
 * @param _pToken The token string to parse
 * @param _pValue Output parameter for the parsed integer
 * @return true if parsing succeeded, false otherwise
 */
bool csv_helper_parse_int(const char *_pToken, int *_pValue);

/**
 * Parses a single float from a CSV token, skipping leading whitespace.
 * @param _pToken The token string to parse
 * @param _pValue Output parameter for the parsed float
 * @return true if parsing succeeded, false otherwise
 */
bool csv_helper_parse_float(const char *_pToken, float *_pValue);

/**
 * Parses x,y coordinates from two token strings.
 * This is the preferred method as it's fast, doesn't modify strings, and works with existing strtok state.
 * @param _pTokenX The X coordinate token string
 * @param _pTokenY The Y coordinate token string
 * @param _pOutVec Output parameter for the parsed vec2
 * @return true if parsing succeeded, false otherwise
 */
bool csv_helper_parse_xy_from_tokens(const char *_pTokenX, const char *_pTokenY, struct vec2 *_pOutVec);

/**
 * Loads an entire file into memory as a null-terminated string.
 * The caller is responsible for freeing the returned buffer with free().
 * @param _pPath Path to the file to load
 * @param _ppOutData Output parameter for the allocated buffer (must be freed by caller)
 * @param _pOutSize Output parameter for the size of the data read
 * @return true if successful, false on error
 */
bool csv_helper_load_file(const char *_pPath, char **_ppOutData, size_t *_pOutSize);

/**
 * Gets the dimensions (width and height) of a CSV file by reading through it.
 * Validates that all lines have consistent width.
 * @param _pPath Path to the CSV file
 * @param _pWidth Output parameter for the number of columns (width)
 * @param _pHeight Output parameter for the number of rows (height)
 * @param _uLineBufferSize Size of internal line buffer (should be large enough for longest line)
 * @return true if successful, false on error (file not found, inconsistent widths, etc.)
 */
bool csv_helper_get_dimensions(const char *_pPath, uint16_t *_pWidth, uint16_t *_pHeight, size_t _uLineBufferSize);

/**
 * Parse name from CSV line (first token).
 * _pLine: Line to parse (will be modified by strtok)
 * _pOutName: Output buffer for name
 * _uNameSize: Size of output buffer
 * Returns pointer to the name token, or NULL on error
 */
char *csv_helper_parse_name(char *_pLine, char *_pOutName, size_t _uNameSize);

/**
 * Parse name,x,y from CSV line (common helper for planets and triggers).
 * _pLine: Line to parse (will be modified by strtok)
 * _pOutName: Output buffer for name
 * _uNameSize: Size of output buffer
 * _pOutPos: Output position
 * Returns true if successful, false on error
 */
bool csv_helper_parse_name_xy(char *_pLine, char *_pOutName, size_t _uNameSize, struct vec2 *_pOutPos);

/**
 * Parse optional name,x,y from CSV line (handles both "name,x,y" and ",x,y" formats).
 * _pLine: Line to parse (will be modified by strtok)
 * _pOutName: Output buffer for name (will be empty string if line starts with comma)
 * _uNameSize: Size of output buffer
 * _pOutPos: Output position
 * Returns true if successful, false on error
 */
bool csv_helper_parse_optional_name_xy(char *_pLine, char *_pOutName, size_t _uNameSize, struct vec2 *_pOutPos);

/**
 * Safely copies a string to a destination buffer with null termination.
 * This is a common pattern used throughout CSV parsing code.
 * @param _pSrc Source string to copy
 * @param _pDst Destination buffer
 * @param _uDstSize Size of destination buffer
 * @return true if successful, false on error
 */
bool csv_helper_copy_string_safe(const char *_pSrc, char *_pDst, size_t _uDstSize);

/**
 * Copies a line into a buffer for tokenizing (strtok-safe copy).
 * This pattern is repeated in multiple CSV parsing functions.
 * @param _pLine Source line to copy
 * @param _pOutBuf Output buffer
 * @param _uBufSize Size of output buffer
 * @return true if successful, false on error
 */
bool csv_helper_copy_line_for_tokenizing(const char *_pLine, char *_pOutBuf, size_t _uBufSize);

/**
 * Loads spawn position from logic.csv file in the specified folder.
 * Parses the "spawn,x,y" entry from the first line of the file.
 * @param _pFolderName Folder name (e.g., "space", "cave", etc.)
 * @param _pOutPos Output parameter for the parsed spawn position (defaults to 0,0 if not found)
 * @return true if spawn position was successfully loaded, false otherwise
 */
bool csv_helper_load_spawn_position(const char *_pFolderName, struct vec2 *_pOutPos);