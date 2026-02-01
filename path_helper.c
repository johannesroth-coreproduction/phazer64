#include "path_helper.h"
#include "csv_helper.h"
#include "game_objects/gp_state.h"
#include "libdragon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool path_helper_load_named_points(const char *_pFileName, const char *_pEntryName, struct vec2 **_ppOutPoints, uint16_t *_pOutCount)
{
    if (!_pFileName || !_pEntryName || !_ppOutPoints || !_pOutCount)
    {
        debugf("path_helper_load_named_points: Invalid parameters\n");
        return false;
    }

    *_ppOutPoints = NULL;
    *_pOutCount = 0;

    /* Get current folder */
    const char *pFolder = gp_state_get_current_folder();
    if (!pFolder)
    {
        debugf("path_helper_load_named_points: No current folder set, cannot load '%s'\n", _pFileName);
        return false;
    }

    /* Build path: rom:/<folder>/<filename>.csv */
    char szPath[256];
    snprintf(szPath, sizeof(szPath), "rom:/%s/%s.csv", pFolder, _pFileName);

    /* Load file */
    char *pFileData = NULL;
    size_t uFileSize = 0;
    if (!csv_helper_load_file(szPath, &pFileData, &uFileSize))
    {
        debugf("path_helper_load_named_points: Failed to load file '%s'\n", szPath);
        return false;
    }

    /* Find line starting with entry name */
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

        /* Quick name check with small buffer first (to avoid large stack allocation for non-matching lines) */
        char szNameCheck[256];
        size_t uNameCheckLen = (uLineLen < sizeof(szNameCheck) - 1) ? uLineLen : sizeof(szNameCheck) - 1;
        memcpy(szNameCheck, pLineStart, uNameCheckLen);
        szNameCheck[uNameCheckLen] = '\0';

        /* Skip empty lines */
        if (szNameCheck[0] == '\0' || szNameCheck[0] == '\r' || szNameCheck[0] == '\n')
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        /* Quick check: does line start with entry name? */
        size_t uEntryNameLen = strlen(_pEntryName);
        bool bNameMatches = false;
        if (uLineLen >= uEntryNameLen && memcmp(pLineStart, _pEntryName, uEntryNameLen) == 0)
        {
            /* Check if next character is comma (end of name token) */
            if (pLineStart[uEntryNameLen] == ',')
            {
                bNameMatches = true;
            }
        }

        /* Only allocate large buffer if name matches */
        if (!bNameMatches)
        {
            if (pLineEnd)
                pLineStart = pLineEnd + 1;
            else
                break;
            continue;
        }

        /* Name matches - now allocate full buffer for parsing */
        char szLine[4096];
        size_t uCopyLen = (uLineLen < sizeof(szLine) - 1) ? uLineLen : sizeof(szLine) - 1;
        memcpy(szLine, pLineStart, uCopyLen);
        szLine[uCopyLen] = '\0';

        csv_helper_strip_eol(szLine);

        /* Check if line starts with entry name */
        char szLineCopy[4096];
        if (!csv_helper_copy_line_for_tokenizing(szLine, szLineCopy, sizeof(szLineCopy)))
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        char *pToken = strtok(szLineCopy, ",");
        if (pToken && strcmp(pToken, _pEntryName) == 0)
        {
            bFound = true;

            /* Parse count */
            pToken = strtok(NULL, ",");
            if (!pToken)
            {
                free(pFileData);
                return false;
            }

            int iCount = 0;
            if (!csv_helper_parse_int(pToken, &iCount) || iCount <= 0)
            {
                free(pFileData);
                return false;
            }

            uint16_t uCount = (uint16_t)iCount;

            /* Allocate waypoint array */
            struct vec2 *pWaypoints = (struct vec2 *)malloc(sizeof(struct vec2) * uCount);
            if (!pWaypoints)
            {
                free(pFileData);
                return false;
            }

            /* Parse all points */
            bool bParseSuccess = true;
            for (uint16_t i = 0; i < uCount; ++i)
            {
                char *pTokenX = strtok(NULL, ",");
                char *pTokenY = strtok(NULL, ",");
                if (!pTokenX || !pTokenY || !csv_helper_parse_xy_from_tokens(pTokenX, pTokenY, &pWaypoints[i]))
                {
                    bParseSuccess = false;
                    break;
                }
            }

            if (!bParseSuccess)
            {
                debugf("path_helper_load_named_points: Failed to parse waypoint for entry '%s' in '%s'\n", _pEntryName, szPath);
                free(pWaypoints);
                free(pFileData);
                return false;
            }

            *_ppOutPoints = pWaypoints;
            *_pOutCount = uCount;
            free(pFileData);
            return true;
        }

        if (pLineEnd)
            pLineStart = pLineEnd + 1;
        else
            break;
    }

    free(pFileData);
    debugf("path_helper_load_named_points: Entry '%s' not found in '%s'\n", _pEntryName, szPath);
    return false;
}
