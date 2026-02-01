#include "sprite_tools.h"
#include "libdragon.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/**
 * @brief Check if a pixel at (x, y) is fully transparent (supports RGBA16, RGBA32, CI4, CI8)
 *
 * @param _pSurface     Surface to check
 * @param _eFormat      Texture format
 * @param _pPalette     Palette data (for CI4/CI8 formats, NULL for RGBA formats)
 * @param _x            X coordinate
 * @param _y            Y coordinate
 * @return              true if pixel is fully transparent, false otherwise
 */
static inline bool is_pixel_at_transparent(const surface_t *_pSurface, tex_format_t _eFormat, const uint16_t *_pPalette, uint16_t _x, uint16_t _y)
{
    const uint8_t *pRow = (const uint8_t *)_pSurface->buffer + (_y * _pSurface->stride);

    if (_eFormat == FMT_CI4)
    {
        /* CI4: 2 pixels per byte
         * Even x (0,2,4...): upper 4 bits
         * Odd x (1,3,5...): lower 4 bits */
        if (!_pPalette)
            return true;
        const uint8_t *pByte = pRow + (_x / 2);
        const uint8_t uIndex = (_x & 1) ? (*pByte & 0x0F) : ((*pByte >> 4) & 0x0F);
        const uint16_t uPaletteEntry = _pPalette[uIndex];
        return (uPaletteEntry & 0x0001) == 0; /* Alpha bit in RGBA16 palette */
    }
    else if (_eFormat == FMT_CI8)
    {
        /* CI8: 1 pixel per byte, direct 8-bit index */
        if (!_pPalette)
            return true;
        const uint8_t uIndex = pRow[_x];
        const uint16_t uPaletteEntry = _pPalette[uIndex];
        return (uPaletteEntry & 0x0001) == 0; /* Alpha bit in RGBA16 palette */
    }
    else if (_eFormat == FMT_RGBA16)
    {
        /* RGBA16: RGBA 5551, alpha is bit 0 */
        const uint16_t *pPixel = (const uint16_t *)(pRow + (_x * 2));
        return (*pPixel & 0x0001) == 0;
    }
    else if (_eFormat == FMT_RGBA32)
    {
        /* RGBA32: RGBA 8888, alpha is byte 3 */
        const uint32_t *pPixel = (const uint32_t *)(pRow + (_x * 4));
        return ((*pPixel >> 24) & 0xFF) == 0;
    }

    return true; /* Unknown format = assume transparent */
}

/**
 * @brief Generic function to find first/last non-transparent row
 *
 * @param _pSurface     Surface to scan
 * @param _eFormat      Texture format
 * @param _pPalette     Palette data (for CI4/CI8, NULL for RGBA)
 * @param _bFromTop     true to scan top-to-bottom, false to scan bottom-to-top
 * @return              Row index of first/last non-transparent row, or -1 if all transparent
 */
static int find_non_transparent_row(const surface_t *_pSurface, tex_format_t _eFormat, const uint16_t *_pPalette, bool _bFromTop)
{
    const uint16_t uWidth = _pSurface->width;
    const uint16_t uHeight = _pSurface->height;

    if (_bFromTop)
    {
        for (uint16_t y = 0; y < uHeight; ++y)
        {
            for (uint16_t x = 0; x < uWidth; ++x)
            {
                if (!is_pixel_at_transparent(_pSurface, _eFormat, _pPalette, x, y))
                {
                    return (int)y;
                }
            }
        }
    }
    else
    {
        for (int y = (int)uHeight - 1; y >= 0; --y)
        {
            for (uint16_t x = 0; x < uWidth; ++x)
            {
                if (!is_pixel_at_transparent(_pSurface, _eFormat, _pPalette, x, (uint16_t)y))
                {
                    return y;
                }
            }
        }
    }

    return -1; // All transparent
}

/**
 * @brief Generic function to find first/last non-transparent column
 *
 * @param _pSurface     Surface to scan
 * @param _eFormat      Texture format
 * @param _pPalette     Palette data (for CI4/CI8, NULL for RGBA)
 * @param _iTop         Top row bound (inclusive)
 * @param _iBottom      Bottom row bound (inclusive)
 * @param _bFromLeft    true to scan left-to-right, false to scan right-to-left
 * @return              Column index of first/last non-transparent column, or -1 if all transparent
 */
static int find_non_transparent_column(const surface_t *_pSurface, tex_format_t _eFormat, const uint16_t *_pPalette, int _iTop, int _iBottom, bool _bFromLeft)
{
    const uint16_t uWidth = _pSurface->width;

    if (_bFromLeft)
    {
        for (uint16_t x = 0; x < uWidth; ++x)
        {
            for (int y = _iTop; y <= _iBottom; ++y)
            {
                if (!is_pixel_at_transparent(_pSurface, _eFormat, _pPalette, x, (uint16_t)y))
                {
                    return (int)x;
                }
            }
        }
    }
    else
    {
        for (int x = (int)uWidth - 1; x >= 0; --x)
        {
            for (int y = _iTop; y <= _iBottom; ++y)
            {
                if (!is_pixel_at_transparent(_pSurface, _eFormat, _pPalette, (uint16_t)x, (uint16_t)y))
                {
                    return x;
                }
            }
        }
    }

    return -1; // All transparent
}

bool sprite_tools_get_trimmed_rect(sprite_t *_pSprite, struct vec2i *_pOutOffset, struct vec2i *_pOutSize)
{
    if (!_pSprite || !_pOutOffset || !_pOutSize)
    {
        debugf("sprite_tools_get_trimmed_rect: NULL parameter (sprite=%p, offset=%p, size=%p)\n", (void *)_pSprite, (void *)_pOutOffset, (void *)_pOutSize);
        return false;
    }

    /* Get the sprite's pixel surface */
    surface_t surface = sprite_get_pixels(_pSprite);
    if (!surface.buffer)
    {
        debugf("sprite_tools_get_trimmed_rect: surface.buffer is NULL (sprite width=%u, height=%u)\n", (unsigned)_pSprite->width, (unsigned)_pSprite->height);
        return false;
    }

    /* Check if format is supported */
    tex_format_t eFormat = sprite_get_format(_pSprite);
    if (eFormat != FMT_RGBA16 && eFormat != FMT_RGBA32 && eFormat != FMT_CI4 && eFormat != FMT_CI8)
    {
        const char *pFormatName = tex_format_name(eFormat);
        debugf("sprite_tools_get_trimmed_rect: unsupported format %u (%s) - expected FMT_RGBA16, FMT_RGBA32, FMT_CI4, or FMT_CI8, sprite width=%u, height=%u\n",
               (unsigned)eFormat,
               pFormatName ? pFormatName : "unknown",
               (unsigned)_pSprite->width,
               (unsigned)_pSprite->height);
        return false;
    }

    /* Get palette for CI4/CI8 formats */
    uint16_t *pPalette = NULL;
    if (eFormat == FMT_CI4 || eFormat == FMT_CI8)
    {
        pPalette = sprite_get_palette(_pSprite);
        if (!pPalette)
        {
            debugf("sprite_tools_get_trimmed_rect: CI format sprite has no palette (format=%u)\n", (unsigned)eFormat);
            return false;
        }
    }

    /*const char *pFormatName = tex_format_name(eFormat);
    debugf("sprite_tools_get_trimmed_rect: sprite format=%u (%s), width=%u, height=%u, stride=%u, buffer=%p, palette=%p\n",
           (unsigned)eFormat,
           pFormatName ? pFormatName : "unknown",
           (unsigned)surface.width,
           (unsigned)surface.height,
           (unsigned)surface.stride,
           (void *)surface.buffer,
           (void *)pPalette);*/

    /* Find top and bottom non-transparent rows */
    int iTop = find_non_transparent_row(&surface, eFormat, pPalette, true);
    int iBottom = find_non_transparent_row(&surface, eFormat, pPalette, false);

    /* If entire sprite is transparent, return empty rect at origin */
    if (iTop < 0 || iBottom < 0 || iTop > iBottom)
    {
        debugf("sprite_tools_get_trimmed_rect: sprite is fully transparent (top=%d, bottom=%d)\n", iTop, iBottom);
        _pOutOffset->iX = 0;
        _pOutOffset->iY = 0;
        _pOutSize->iX = 0;
        _pOutSize->iY = 0;
        return true;
    }

    /* Find left and right non-transparent columns (within the vertical bounds) */
    int iLeft = find_non_transparent_column(&surface, eFormat, pPalette, iTop, iBottom, true);
    int iRight = find_non_transparent_column(&surface, eFormat, pPalette, iTop, iBottom, false);

    /* If no non-transparent pixels found in columns, return empty rect */
    if (iLeft < 0 || iRight < 0 || iLeft > iRight)
    {
        debugf("sprite_tools_get_trimmed_rect: no non-transparent columns found (left=%d, right=%d, top=%d, bottom=%d)\n", iLeft, iRight, iTop, iBottom);
        _pOutOffset->iX = 0;
        _pOutOffset->iY = 0;
        _pOutSize->iX = 0;
        _pOutSize->iY = 0;
        return true;
    }

    /* Calculate trimmed rectangle */
    _pOutOffset->iX = iLeft;
    _pOutOffset->iY = iTop;
    _pOutSize->iX = iRight - iLeft + 1;
    _pOutSize->iY = iBottom - iTop + 1;

    /*debugf("sprite_tools_get_trimmed_rect: SUCCESS - trimmed rect offset=(%d,%d) size=(%d,%d) from original (%u,%u)\n",
           _pOutOffset->iX,
           _pOutOffset->iY,
           _pOutSize->iX,
           _pOutSize->iY,
           (unsigned)surface.width,
           (unsigned)surface.height);
*/
    return true;
}
