#pragma once

#include "math2d.h"
#include <stdbool.h>

/**
 * Loads a point of interest (POI) from point.csv in the specified folder (or current folder if NULL).
 * Searches for a line starting with the specified name and returns its x,y coordinates.
 * @param _pPointName The name of the point to find (e.g., "green_alien_leave")
 * @param _pOutPos Output parameter for the parsed position
 * @param _pFolderName Optional folder name (e.g., "space"). If NULL, uses current folder from gp_state.
 * @return true if the point was found and parsed successfully, false otherwise
 */
bool poi_load(const char *_pPointName, struct vec2 *_pOutPos, const char *_pFolderName);
