#include "string_helper.h"
#include <ctype.h>
#include <string.h>

bool string_helper_to_upper(char *_pStr, size_t _uSize)
{
    if (!_pStr || _uSize == 0)
        return false;

    size_t uLen = strlen(_pStr);
    size_t uCopyLen = (uLen < _uSize - 1) ? uLen : _uSize - 1;

    for (size_t i = 0; i < uCopyLen; i++)
    {
        _pStr[i] = (char)toupper((unsigned char)_pStr[i]);
    }
    _pStr[uCopyLen] = '\0';

    return true;
}

bool string_helper_nice_location_name(const char *_pSourceName, char *_pOutBuffer, size_t _uBufferSize)
{
    if (!_pSourceName || !_pOutBuffer || _uBufferSize == 0)
        return false;

    /* Copy source to output buffer */
    strncpy(_pOutBuffer, _pSourceName, _uBufferSize - 1);
    _pOutBuffer[_uBufferSize - 1] = '\0';

    /* Apply formatting: uppercase */
    return string_helper_to_upper(_pOutBuffer, _uBufferSize);
}
