#include "csv_helper.h"
#include "math2d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void csv_helper_strip_eol(char *_pLine)
{
    if (!_pLine)
        return;

    char *p;
    p = strchr(_pLine, '\n');
    if (p)
        *p = '\0';
    p = strchr(_pLine, '\r');
    if (p)
        *p = '\0';
}

bool csv_helper_fgets_checked(char *_pBuf, size_t _uBufSize, FILE *_pFile, bool *_pbTruncated)
{
    if (_pbTruncated)
        *_pbTruncated = false;

    if (!fgets(_pBuf, (int)_uBufSize, _pFile))
        return false;

    /* If buffer contains no '\n' and we're not at EOF, the line did not fit. */
    if (_pbTruncated)
    {
        if (!strchr(_pBuf, '\n') && !feof(_pFile))
            *_pbTruncated = true;
    }

    return true;
}

uint16_t csv_helper_count_values(const char *_pLine)
{
    if (!_pLine || !_pLine[0])
        return 0;

    uint16_t uCount = 1; /* At least one value if string is non-empty */
    const char *pChar = _pLine;

    while (*pChar)
    {
        if (*pChar == ',')
            uCount++;
        pChar++;
    }

    return uCount;
}

/* Skip leading whitespace from a token string (shared helper) */
static const char *skip_whitespace(const char *_pToken)
{
    if (!_pToken)
        return NULL;

    while (*_pToken == ' ' || *_pToken == '\t' || *_pToken == '\r' || *_pToken == '\n')
        _pToken++;

    return _pToken;
}

bool csv_helper_parse_int(const char *_pToken, int *_pValue)
{
    if (!_pToken || !_pValue)
        return false;

    /* Skip leading whitespace */
    _pToken = skip_whitespace(_pToken);
    if (!_pToken)
        return false;

    /* Parse integer */
    char *pEnd = NULL;
    *_pValue = (int)strtol(_pToken, &pEnd, 10);

    /* Check if parsing succeeded */
    return (pEnd != _pToken);
}

bool csv_helper_parse_float(const char *_pToken, float *_pValue)
{
    if (!_pToken || !_pValue)
        return false;

    /* Skip leading whitespace */
    _pToken = skip_whitespace(_pToken);
    if (!_pToken)
        return false;

    /* Parse float */
    char *pEnd = NULL;
    *_pValue = strtof(_pToken, &pEnd);

    /* Check if parsing succeeded */
    return (pEnd != _pToken);
}

bool csv_helper_parse_xy_from_tokens(const char *_pTokenX, const char *_pTokenY, struct vec2 *_pOutVec)
{
    if (!_pTokenX || !_pTokenY || !_pOutVec)
        return false;

    /* Initialize output to zero */
    *_pOutVec = vec2_zero();

    /* Parse X coordinate */
    float fX = 0.0f;
    if (!csv_helper_parse_float(_pTokenX, &fX))
        return false;

    /* Parse Y coordinate */
    float fY = 0.0f;
    if (!csv_helper_parse_float(_pTokenY, &fY))
        return false;

    *_pOutVec = vec2_make(fX, fY);
    return true;
}

bool csv_helper_load_file(const char *_pPath, char **_ppOutData, size_t *_pOutSize)
{
    if (!_pPath || !_ppOutData || !_pOutSize)
        return false;

    *_ppOutData = NULL;
    *_pOutSize = 0;

    FILE *pFile = fopen(_pPath, "rb");
    if (!pFile)
        return false;

    if (fseek(pFile, 0, SEEK_END) != 0)
    {
        fclose(pFile);
        return false;
    }

    long iSize = ftell(pFile);
    if (iSize <= 0)
    {
        fclose(pFile);
        return false;
    }

    if (fseek(pFile, 0, SEEK_SET) != 0)
    {
        fclose(pFile);
        return false;
    }

    char *pData = (char *)malloc((size_t)iSize + 1);
    if (!pData)
    {
        fclose(pFile);
        return false;
    }

    size_t uRead = fread(pData, 1, (size_t)iSize, pFile);
    fclose(pFile);

    if (uRead != (size_t)iSize)
    {
        free(pData);
        return false;
    }

    pData[uRead] = '\0';
    *_ppOutData = pData;
    *_pOutSize = uRead;
    return true;
}

bool csv_helper_get_dimensions(const char *_pPath, uint16_t *_pWidth, uint16_t *_pHeight, size_t _uLineBufferSize)
{
    if (!_pPath || !_pWidth || !_pHeight || _uLineBufferSize == 0)
        return false;

    *_pWidth = 0;
    *_pHeight = 0;

    FILE *pFile = fopen(_pPath, "r");
    if (!pFile)
        return false;

    char *pLineBuf = (char *)malloc(_uLineBufferSize);
    if (!pLineBuf)
    {
        fclose(pFile);
        return false;
    }

    uint16_t uHeight = 0;
    uint16_t uWidth = 0;
    bool bFirstLine = true;
    bool bSuccess = true;

    /* Read through file to determine dimensions */
    while (true)
    {
        bool bTruncated = false;
        if (!csv_helper_fgets_checked(pLineBuf, _uLineBufferSize, pFile, &bTruncated))
            break;

        if (bTruncated)
        {
            bSuccess = false;
            break;
        }

        csv_helper_strip_eol(pLineBuf);

        if (bFirstLine)
        {
            uWidth = csv_helper_count_values(pLineBuf);
            bFirstLine = false;
        }
        else
        {
            uint16_t uLineWidth = csv_helper_count_values(pLineBuf);
            if (uLineWidth != uWidth)
            {
                bSuccess = false;
                break;
            }
        }

        uHeight++;
    }

    free(pLineBuf);
    fclose(pFile);

    if (!bSuccess || uWidth == 0 || uHeight == 0)
        return false;

    *_pWidth = uWidth;
    *_pHeight = uHeight;
    return true;
}

char *csv_helper_parse_name(char *_pLine, char *_pOutName, size_t _uNameSize)
{
    if (!_pLine || !_pOutName)
        return NULL;

    char *pToken = strtok(_pLine, ",");
    if (!pToken)
        return NULL;

    if (!csv_helper_copy_string_safe(pToken, _pOutName, _uNameSize))
        return NULL;

    return pToken;
}

bool csv_helper_parse_name_xy(char *_pLine, char *_pOutName, size_t _uNameSize, struct vec2 *_pOutPos)
{
    if (!_pLine || !_pOutName || !_pOutPos)
        return false;

    /* Parse name (first token) */
    if (!csv_helper_parse_name(_pLine, _pOutName, _uNameSize))
        return false;

    /* Parse x,y coordinates */
    if (!csv_helper_parse_xy_from_tokens(strtok(NULL, ","), strtok(NULL, ","), _pOutPos))
        return false;

    return true;
}

bool csv_helper_parse_optional_name_xy(char *_pLine, char *_pOutName, size_t _uNameSize, struct vec2 *_pOutPos)
{
    if (!_pLine || !_pOutName || !_pOutPos)
        return false;

    /* Check if line starts with comma (no name) */
    if (_pLine[0] == ',')
    {
        /* No name - set to empty string */
        _pOutName[0] = '\0';

        /* Skip the first comma and parse x,y directly */
        char *pToken = strtok(_pLine, ",");
        if (!pToken) /* This should be empty, skip it */
            pToken = strtok(NULL, ",");

        if (!pToken)
            return false;

        if (!csv_helper_parse_xy_from_tokens(pToken, strtok(NULL, ","), _pOutPos))
            return false;
    }
    else
    {
        /* Parse name,x,y using common helper */
        if (!csv_helper_parse_name_xy(_pLine, _pOutName, _uNameSize, _pOutPos))
            return false;
    }

    return true;
}

bool csv_helper_copy_string_safe(const char *_pSrc, char *_pDst, size_t _uDstSize)
{
    if (!_pSrc || !_pDst || _uDstSize == 0)
        return false;

    size_t uSrcLen = strlen(_pSrc);
    size_t uCopyLen = (uSrcLen < _uDstSize - 1) ? uSrcLen : _uDstSize - 1;
    memcpy(_pDst, _pSrc, uCopyLen);
    _pDst[uCopyLen] = '\0';

    return true;
}

bool csv_helper_copy_line_for_tokenizing(const char *_pLine, char *_pOutBuf, size_t _uBufSize)
{
    if (!_pLine || !_pOutBuf || _uBufSize == 0)
        return false;

    strncpy(_pOutBuf, _pLine, _uBufSize - 1);
    _pOutBuf[_uBufSize - 1] = '\0';

    return true;
}

bool csv_helper_load_spawn_position(const char *_pFolderName, struct vec2 *_pOutPos)
{
    if (!_pFolderName || !_pOutPos)
        return false;

    /* Default to zero if file not found */
    _pOutPos->fX = 0.0f;
    _pOutPos->fY = 0.0f;

    /* Build path: "rom:/<folder>/logic.csv" */
    char szPath[256];
    snprintf(szPath, sizeof(szPath), "rom:/%s/logic.csv", _pFolderName);

    FILE *pFile = fopen(szPath, "r");
    if (!pFile)
        return false;

    char szLine[256];
    bool bTruncated = false;
    bool bSuccess = false;

    if (csv_helper_fgets_checked(szLine, sizeof(szLine), pFile, &bTruncated) && !bTruncated)
    {
        csv_helper_strip_eol(szLine);

        /* Parse format: "spawn,x,y" - skip first token, then parse x,y */
        char *pToken = strtok(szLine, ",");
        if (pToken && strcmp(pToken, "spawn") == 0)
        {
            if (csv_helper_parse_xy_from_tokens(strtok(NULL, ","), strtok(NULL, ","), _pOutPos))
            {
                bSuccess = true;
            }
        }
    }

    fclose(pFile);
    return bSuccess;
}