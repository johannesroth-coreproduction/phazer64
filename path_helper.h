#pragma once

#include "math2d.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Loads points from a named entry in a CSV file.
 * Format: name,count,x1,y1,x2,y2,...
 *
 * @param _pFileName Base filename (e.g., "path" or "race") - loads from rom:/<folder>/<filename>.csv
 * @param _pEntryName Name of the entry to find in the CSV
 * @param _ppOutPoints Output parameter for allocated array of points (caller must free)
 * @param _pOutCount Output parameter for number of points loaded
 * @return true if successful, false on error
 */
bool path_helper_load_named_points(const char *_pFileName, const char *_pEntryName, struct vec2 **_ppOutPoints, uint16_t *_pOutCount);
