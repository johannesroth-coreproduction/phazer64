#include "poi.h"
#include "csv_helper.h"
#include "game_objects/gp_state.h"
#include "libdragon.h"
#include "math2d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool poi_load(const char *_pPointName, struct vec2 *_pOutPos, const char *_pFolderName)
{
    if (!_pPointName || !_pOutPos)
    {
        debugf("poi_load: Invalid parameters (pointName=%p, outPos=%p)\n", _pPointName, _pOutPos);
        return false;
    }

    /* Initialize output to zero */
    *_pOutPos = vec2_zero();

    /* Get folder - use provided folder name or fall back to current folder */
    const char *pFolder = _pFolderName;
    if (!pFolder)
    {
        pFolder = gp_state_get_current_folder();
        if (!pFolder)
        {
            debugf("poi_load: No folder specified and no current folder set, cannot load point '%s'\n", _pPointName);
            return false;
        }
    }

    /* Build path: rom:/<folder>/point.csv */
    char szPath[256];
    snprintf(szPath, sizeof(szPath), "rom:/%s/point.csv", pFolder);

    /* Load file */
    char *pFileData = NULL;
    size_t uFileSize = 0;
    if (!csv_helper_load_file(szPath, &pFileData, &uFileSize))
    {
        debugf("poi_load: Failed to load point file '%s' (point '%s')\n", szPath, _pPointName);
        return false;
    }

    /* Find line starting with point name */
    char *pLineStart = pFileData;
    char *pLineEnd = NULL;
    bool bFound = false;

    while ((pLineEnd = strchr(pLineStart, '\n')) != NULL || (pLineStart[0] != '\0' && !bFound))
    {
        /* Extract line */
        size_t uLineLen = (pLineEnd) ? (size_t)(pLineEnd - pLineStart) : strlen(pLineStart);
        if (uLineLen == 0)
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        char szLine[512];
        size_t uCopyLen = (uLineLen < sizeof(szLine) - 1) ? uLineLen : sizeof(szLine) - 1;
        memcpy(szLine, pLineStart, uCopyLen);
        szLine[uCopyLen] = '\0';

        csv_helper_strip_eol(szLine);

        /* Skip empty lines */
        if (szLine[0] == '\0')
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        /* Check if line starts with point name */
        char szLineCopy[512];
        if (!csv_helper_copy_line_for_tokenizing(szLine, szLineCopy, sizeof(szLineCopy)))
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        char *pToken = strtok(szLineCopy, ",");
        if (pToken && strcmp(pToken, _pPointName) == 0)
        {
            bFound = true;

            /* Parse x,y coordinates */
            char *pTokenX = strtok(NULL, ",");
            char *pTokenY = strtok(NULL, ",");
            if (!pTokenX || !pTokenY || !csv_helper_parse_xy_from_tokens(pTokenX, pTokenY, _pOutPos))
            {
                debugf("poi_load: Failed to parse coordinates for point '%s' in '%s'\n", _pPointName, szPath);
                free(pFileData);
                return false;
            }

            free(pFileData);
            return true;
        }

        if (pLineEnd)
            pLineStart = pLineEnd + 1;
        else
            break;
    }

    free(pFileData);
    debugf("poi_load: Point '%s' not found in '%s'\n", _pPointName, szPath);
    return false;
}
