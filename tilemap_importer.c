#include "tilemap_importer.h"
#include "csv_helper.h"
#include "libdragon.h"
#include "n64sys.h"
#include "resource_helper.h"
#include "sprite_tools.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sparsity threshold: layers with fill ratio below this use sparse storage */
#define TILEMAP_SPARSE_THRESHOLD 0.2f

static int cmp_int_asc(const void *_pA, const void *_pB)
{
    const int a = *(const int *)_pA;
    const int b = *(const int *)_pB;
    return (a > b) - (a < b);
}

/* ---------- Sparse layer hash table functions ---------- */

/* Fast integer hash function for 2D coordinates */
static inline uint32_t hash_coord(uint16_t _uX, uint16_t _uY)
{
    uint32_t h = ((uint32_t)_uX * 73856093u) ^ ((uint32_t)_uY * 19349663u);
    return h;
}

/* Calculate next power of 2 >= n */
static inline uint16_t next_power_of_2(uint16_t _uN)
{
    if (_uN == 0)
        return 1;
    _uN--;
    _uN |= _uN >> 1;
    _uN |= _uN >> 2;
    _uN |= _uN >> 4;
    _uN |= _uN >> 8;
    _uN++;
    return _uN;
}

/* Initialize sparse layer hash table with given capacity */
static bool sparse_layer_init(sparse_layer_data_t *_pSparse, uint16_t _uTileCount)
{
    if (!_pSparse)
        return false;

    /* Handle empty layers (0 tiles) - create minimal structure */
    if (_uTileCount == 0)
    {
        _pSparse->pEntries = NULL;
        _pSparse->uCapacity = 0;
        _pSparse->uCount = 0;
        return true;
    }

    /* Calculate capacity: next power of 2 above tile_count * 1.5 (load factor ~0.67) */
    uint16_t uCapacity = next_power_of_2((_uTileCount * 3) / 2);
    if (uCapacity < 16)
        uCapacity = 16; /* Minimum capacity */

    _pSparse->pEntries = (sparse_tile_entry_t *)malloc(sizeof(sparse_tile_entry_t) * uCapacity);
    if (!_pSparse->pEntries)
    {
        debugf("Failed to allocate sparse layer hash table (%u entries)\n", (unsigned)uCapacity);
        return false;
    }

    /* Initialize all entries as empty */
    for (uint16_t i = 0; i < uCapacity; ++i)
    {
        _pSparse->pEntries[i].uX = SPARSE_ENTRY_EMPTY;
        _pSparse->pEntries[i].uY = SPARSE_ENTRY_EMPTY;
        _pSparse->pEntries[i].uTileId = TILEMAP_IMPORTER_EMPTY_TILE;
        _pSparse->pEntries[i].uPadding = 0;
    }

    _pSparse->uCapacity = uCapacity;
    _pSparse->uCount = 0;

    /* Flush cache for sparse layer data */
    CACHE_FLUSH_DATA(_pSparse->pEntries, sizeof(sparse_tile_entry_t) * uCapacity);

    return true;
}

/* Insert tile into sparse layer hash table (assumes capacity is sufficient) */
static bool sparse_layer_insert(sparse_layer_data_t *_pSparse, uint16_t _uX, uint16_t _uY, uint8_t _uTileId)
{
    if (!_pSparse || !_pSparse->pEntries || _pSparse->uCount >= _pSparse->uCapacity)
        return false;

    uint32_t uHash = hash_coord(_uX, _uY);
    uint16_t uIndex = (uint16_t)(uHash & (uint32_t)(_pSparse->uCapacity - 1));

    /* Linear probing to find empty slot */
    for (uint16_t i = 0; i < _pSparse->uCapacity; ++i)
    {
        sparse_tile_entry_t *pEntry = &_pSparse->pEntries[uIndex];

        if (pEntry->uX == SPARSE_ENTRY_EMPTY)
        {
            /* Found empty slot */
            pEntry->uX = _uX;
            pEntry->uY = _uY;
            pEntry->uTileId = _uTileId;
            _pSparse->uCount++;
            return true;
        }

        /* Check for duplicate (update instead of insert) */
        if (pEntry->uX == _uX && pEntry->uY == _uY)
        {
            pEntry->uTileId = _uTileId;
            return true;
        }

        /* Try next slot */
        uIndex = (uIndex + 1) & (_pSparse->uCapacity - 1);
    }

    debugf("Sparse layer hash table full (should not happen with proper capacity)\n");
    return false;
}

/* Lookup tile in sparse layer hash table - used by inline accessor in header */
uint8_t tilemap_layer_sparse_get(const sparse_layer_data_t *_pSparse, uint16_t _uX, uint16_t _uY)
{
    /* Empty layer fast path (0 tiles) */
    if (!_pSparse || !_pSparse->pEntries || _pSparse->uCapacity == 0)
        return TILEMAP_IMPORTER_EMPTY_TILE;

    uint32_t uHash = hash_coord(_uX, _uY);
    uint16_t uIndex = (uint16_t)(uHash & (uint32_t)(_pSparse->uCapacity - 1));

    /* Linear probing to find entry */
    for (uint16_t i = 0; i < _pSparse->uCapacity; ++i)
    {
        const sparse_tile_entry_t *pEntry = &_pSparse->pEntries[uIndex];

        if (pEntry->uX == SPARSE_ENTRY_EMPTY)
        {
            /* Hit empty slot - tile not found */
            return TILEMAP_IMPORTER_EMPTY_TILE;
        }

        if (pEntry->uX == _uX && pEntry->uY == _uY)
        {
            /* Found matching entry */
            return pEntry->uTileId;
        }

        /* Try next slot */
        uIndex = (uIndex + 1) & (_pSparse->uCapacity - 1);
    }

    return TILEMAP_IMPORTER_EMPTY_TILE;
}

/* Free sparse layer hash table */
static void sparse_layer_free(sparse_layer_data_t *_pSparse)
{
    if (!_pSparse)
        return;

    if (_pSparse->pEntries)
    {
        free(_pSparse->pEntries);
        _pSparse->pEntries = NULL;
    }

    _pSparse->uCapacity = 0;
    _pSparse->uCount = 0;
}

static void free_layer(tilemap_layer_t *_pLayer)
{
    if (!_pLayer)
        return;

    /* Free storage based on type */
    if (_pLayer->eStorage == TILEMAP_LAYER_STORAGE_DENSE)
    {
        if (_pLayer->pData)
        {
            free(_pLayer->pData);
            _pLayer->pData = NULL;
        }
        if (_pLayer->ppData)
        {
            free(_pLayer->ppData);
            _pLayer->ppData = NULL;
        }
    }
    else if (_pLayer->eStorage == TILEMAP_LAYER_STORAGE_SPARSE)
    {
        sparse_layer_free(&_pLayer->sparse);
    }
    /* TILEMAP_LAYER_STORAGE_SINGLE requires no freeing */

    _pLayer->uWidth = 0;
    _pLayer->uHeight = 0;
    _pLayer->uTileCount = 0;
}

/* ---------- tile_ids.csv ---------- */

/* Loads tile IDs and sorts them ascending (required for bsearch). */
static bool load_tile_ids_sorted(const char *_pMapFolder, int **_ppTileIds, uint16_t *_pTileCount)
{
    if (!_pMapFolder || !_ppTileIds || !_pTileCount)
        return false;

    *_ppTileIds = NULL;
    *_pTileCount = 0;

    char szPath[256];
    snprintf(szPath, sizeof(szPath), "rom:/%s/tile_ids.csv", _pMapFolder);

    char *pFileData = NULL;
    size_t uFileSize = 0;
    if (!csv_helper_load_file(szPath, &pFileData, &uFileSize))
    {
        debugf("Failed to read tile_ids.csv at %s\n", szPath);
        return false;
    }

    /* Count the number of tile IDs */
    uint16_t uTileCount = csv_helper_count_values(pFileData);
    if (uTileCount == 0 || uTileCount > TILEMAP_IMPORTER_MAX_TILES)
    {
        debugf("Invalid tile count: %u (max allowed: %u)\n", (unsigned)uTileCount, (unsigned)TILEMAP_IMPORTER_MAX_TILES);
        free(pFileData);
        return false;
    }

    int *pTileIds = (int *)malloc(sizeof(int) * uTileCount);
    if (!pTileIds)
    {
        debugf("Failed to allocate memory for tile IDs\n");
        free(pFileData);
        return false;
    }

    /* Parse the comma-separated values */
    char *pToken = strtok(pFileData, ",");
    uint16_t uIndex = 0;

    while (pToken && uIndex < uTileCount)
    {
        if (!csv_helper_parse_int(pToken, &pTileIds[uIndex]))
        {
            debugf("Failed to parse tile ID at index %u: '%s'\n", (unsigned)uIndex, pToken);
            free(pTileIds);
            free(pFileData);
            return false;
        }
        uIndex++;
        pToken = strtok(NULL, ",");
    }

    free(pFileData);
    pFileData = NULL;

    if (uIndex != uTileCount)
    {
        debugf("tile_ids.csv parse mismatch: got %u expected %u\n", (unsigned)uIndex, (unsigned)uTileCount);
        free(pTileIds);
        return false;
    }

    /* Sort for bsearch lookup */
    qsort(pTileIds, uTileCount, sizeof(int), cmp_int_asc);

    /* Flush cache after sorting */
    CACHE_FLUSH_DATA(pTileIds, sizeof(int) * uTileCount);

    *_ppTileIds = pTileIds;
    *_pTileCount = uTileCount;
    return true;
}

/* ---------- Sprite loading ---------- */

static bool load_tile_sprites(const char *_pMapFolder, const int *_pTileIds, uint16_t _uTileCount, sprite_t ***_pppSprites)
{
    if (!_pMapFolder || !_pTileIds || !_pppSprites)
        return false;

    *_pppSprites = NULL;

    sprite_t **ppSprites = (sprite_t **)malloc(sizeof(sprite_t *) * _uTileCount);
    if (!ppSprites)
    {
        debugf("Failed to allocate memory for sprite pointers\n");
        return false;
    }
    memset(ppSprites, 0, sizeof(sprite_t *) * _uTileCount);

    char szPath[256];

    for (uint16_t i = 0; i < _uTileCount; ++i)
    {
        snprintf(szPath, sizeof(szPath), "rom:/%s/%d.sprite", _pMapFolder, _pTileIds[i]);
        ppSprites[i] = sprite_load(szPath);

        if (!ppSprites[i])
        {
            debugf("Failed to load sprite for tile ID %d at %s\n", _pTileIds[i], szPath);
            for (uint16_t j = 0; j < _uTileCount; ++j)
            {
                SAFE_FREE_SPRITE(ppSprites[j]);
            }
            free(ppSprites);
            return false;
        }
    }

    *_pppSprites = ppSprites;
    return true;
}

/* ---------- CSV parsing ---------- */

static bool parse_csv_line(const char *_pLine, uint8_t *_pDstRow, uint16_t _uWidth, const int *_pTileIdsSorted, uint16_t _uTileCount)
{
    if (!_pLine || !_pDstRow || !_pTileIdsSorted)
        return false;

    char szLineCopy[4096];
    if (!csv_helper_copy_line_for_tokenizing(_pLine, szLineCopy, sizeof(szLineCopy)))
        return false;

    char *pToken = strtok(szLineCopy, ",");
    uint16_t uCol = 0;

    while (pToken && uCol < _uWidth)
    {
        int iTileId = 0;
        if (!csv_helper_parse_int(pToken, &iTileId))
        {
            debugf("Failed to parse tile ID in CSV: '%s'\n", pToken);
            return false;
        }

        if (iTileId == -1)
        {
            _pDstRow[uCol] = TILEMAP_IMPORTER_EMPTY_TILE;
        }
        else
        {
            const int *pFound = (const int *)bsearch(&iTileId, _pTileIdsSorted, _uTileCount, sizeof(int), cmp_int_asc);
            if (!pFound)
            {
                debugf("Tile ID %d not found in loaded tiles\n", iTileId);
                return false;
            }

            ptrdiff_t iIndex = (pFound - _pTileIdsSorted);
            if (iIndex < 0 || iIndex > 254)
            {
                debugf("Tile index out of range after bsearch: %ld\n", (long)iIndex);
                return false;
            }

            _pDstRow[uCol] = (uint8_t)iIndex;
        }

        uCol++;
        pToken = strtok(NULL, ",");
    }

    if (uCol != _uWidth)
    {
        debugf("CSV line has %u columns, expected %u\n", (unsigned)uCol, (unsigned)_uWidth);
        return false;
    }

    return true;
}

/* Loads one CSV layer with automatic sparse/dense storage selection. */
static bool load_csv_layer(const char *_pMapFolder, uint8_t _uLayerIndex, tilemap_layer_t *_pOutLayer, const int *_pTileIdsSorted, uint16_t _uTileCount)
{
    if (!_pMapFolder || !_pOutLayer || !_pTileIdsSorted)
        return false;

    memset(_pOutLayer, 0, sizeof(*_pOutLayer));

    char szPath[256];
    snprintf(szPath, sizeof(szPath), "rom:/%s/%s_%02d.csv", _pMapFolder, _pMapFolder, _uLayerIndex);

    FILE *pFile = fopen(szPath, "r");
    if (!pFile)
    {
        debugf("Failed to open CSV file %s\n", szPath);
        return false;
    }

    char szLine[4096];
    uint16_t uHeight = 0;
    uint16_t uWidth = 0;
    bool bFirstLine = true;

    /* First pass: determine dimensions */
    while (true)
    {
        bool bTruncated = false;
        if (!csv_helper_fgets_checked(szLine, sizeof(szLine), pFile, &bTruncated))
            break;

        if (bTruncated)
        {
            debugf("CSV line too long (buffer %u) in %s\n", (unsigned)sizeof(szLine), szPath);
            fclose(pFile);
            return false;
        }

        csv_helper_strip_eol(szLine);

        if (bFirstLine)
        {
            uWidth = csv_helper_count_values(szLine);
            bFirstLine = false;
        }
        else
        {
            uint16_t uLineWidth = csv_helper_count_values(szLine);
            if (uLineWidth != uWidth)
            {
                debugf("CSV line %u has inconsistent width: %u vs %u (%s)\n", (unsigned)(uHeight + 1), (unsigned)uLineWidth, (unsigned)uWidth, szPath);
                fclose(pFile);
                return false;
            }
        }

        uHeight++;
    }

    if (uWidth == 0 || uHeight == 0)
    {
        debugf("Empty or invalid CSV file: %s\n", szPath);
        fclose(pFile);
        return false;
    }

    /* Allocate temporary storage for parsing (always dense during load) */
    uint8_t **ppRows = (uint8_t **)malloc(sizeof(uint8_t *) * uHeight);
    if (!ppRows)
    {
        debugf("Failed to allocate memory for CSV layer row pointers\n");
        fclose(pFile);
        return false;
    }

    uint8_t *pData = (uint8_t *)malloc((size_t)uWidth * (size_t)uHeight);
    if (!pData)
    {
        debugf("Failed to allocate memory for CSV layer data (%ux%u)\n", (unsigned)uWidth, (unsigned)uHeight);
        free(ppRows);
        fclose(pFile);
        return false;
    }

    for (uint16_t y = 0; y < uHeight; ++y)
        ppRows[y] = &pData[(size_t)y * (size_t)uWidth];

    /* Second pass: parse and count non-empty tiles */
    rewind(pFile);

    uint16_t uRow = 0;
    uint16_t uNonEmptyCount = 0;

    /* Optimization: Check for uniformity during parsing */
    bool bAllSame = true;
    uint8_t uRefTileId = TILEMAP_IMPORTER_EMPTY_TILE;
    bool bRefTileSet = false;

    while (uRow < uHeight)
    {
        bool bTruncated = false;
        if (!csv_helper_fgets_checked(szLine, sizeof(szLine), pFile, &bTruncated))
            break;

        if (bTruncated)
        {
            debugf("CSV line too long (buffer %u) in %s\n", (unsigned)sizeof(szLine), szPath);
            free(pData);
            free(ppRows);
            fclose(pFile);
            return false;
        }

        csv_helper_strip_eol(szLine);

        if (!parse_csv_line(szLine, ppRows[uRow], uWidth, _pTileIdsSorted, _uTileCount))
        {
            debugf("Failed to parse CSV line %u in %s\n", (unsigned)uRow, szPath);
            free(pData);
            free(ppRows);
            fclose(pFile);
            return false;
        }

        /* Count non-empty tiles in this row and check uniformity */
        for (uint16_t x = 0; x < uWidth; ++x)
        {
            uint8_t uTileId = ppRows[uRow][x];

            if (uTileId != TILEMAP_IMPORTER_EMPTY_TILE)
                uNonEmptyCount++;

            /* Check uniformity (used later to detect single-tile filled layers) */
            if (!bRefTileSet)
            {
                uRefTileId = uTileId;
                bRefTileSet = true;
            }
            else
            {
                if (bAllSame && uTileId != uRefTileId)
                    bAllSame = false;
            }
        }

        uRow++;
    }

    fclose(pFile);

    if (uRow != uHeight)
    {
        debugf("CSV row count mismatch in %s: got %u expected %u\n", szPath, (unsigned)uRow, (unsigned)uHeight);
        free(pData);
        free(ppRows);
        return false;
    }

    /* Calculate fill ratio and decide storage type */
    uint32_t uTotalTiles = (uint32_t)uWidth * (uint32_t)uHeight;
    float fFillRatio = (float)uNonEmptyCount / (float)uTotalTiles;

    /* Check for single tile optimization */
    bool bIsSingle = false;
    uint8_t uSingleTileId = TILEMAP_IMPORTER_EMPTY_TILE;

    if (uNonEmptyCount == 0)
    {
        /* Completely empty layer */
        bIsSingle = true;
        uSingleTileId = TILEMAP_IMPORTER_EMPTY_TILE;
    }
    else if (uNonEmptyCount == uTotalTiles && bAllSame)
    {
        /* Completely filled layer with identical tiles */
        bIsSingle = true;
        uSingleTileId = uRefTileId;
    }

    bool bUseSparse = (fFillRatio < TILEMAP_SPARSE_THRESHOLD);

    /* Set layer dimensions */
    _pOutLayer->uWidth = uWidth;
    _pOutLayer->uHeight = uHeight;
    _pOutLayer->uTileCount = uNonEmptyCount;

    if (bIsSingle)
    {
        /* Use single storage */
        _pOutLayer->eStorage = TILEMAP_LAYER_STORAGE_SINGLE;
        _pOutLayer->uSingleTileId = uSingleTileId;

        /* Free temporary dense storage */
        free(pData);
        free(ppRows);
    }
    else if (bUseSparse)
    {
        /* Use sparse storage */
        _pOutLayer->eStorage = TILEMAP_LAYER_STORAGE_SPARSE;

        if (!sparse_layer_init(&_pOutLayer->sparse, uNonEmptyCount))
        {
            debugf("Failed to initialize sparse storage for layer %u\n", (unsigned)_uLayerIndex);
            free(pData);
            free(ppRows);
            return false;
        }

        /* Copy non-empty tiles to sparse hash table */
        for (uint16_t y = 0; y < uHeight; ++y)
        {
            for (uint16_t x = 0; x < uWidth; ++x)
            {
                uint8_t uTileId = ppRows[y][x];
                if (uTileId != TILEMAP_IMPORTER_EMPTY_TILE)
                {
                    if (!sparse_layer_insert(&_pOutLayer->sparse, x, y, uTileId))
                    {
                        debugf("Failed to insert tile into sparse layer at (%u, %u)\n", (unsigned)x, (unsigned)y);
                        sparse_layer_free(&_pOutLayer->sparse);
                        free(pData);
                        free(ppRows);
                        return false;
                    }
                }
            }
        }

        /* Flush cache for sparse layer data after all insertions */
        if (_pOutLayer->sparse.pEntries)
        {
            CACHE_FLUSH_DATA(_pOutLayer->sparse.pEntries, sizeof(sparse_tile_entry_t) * _pOutLayer->sparse.uCapacity);
        }

        /* Free temporary dense storage */
        free(pData);
        free(ppRows);
    }
    else
    {
        /* Use dense storage (keep the allocated arrays) */
        _pOutLayer->eStorage = TILEMAP_LAYER_STORAGE_DENSE;
        _pOutLayer->ppData = ppRows;
        _pOutLayer->pData = pData;

        /* Flush cache for dense layer tile data */
        if (pData)
        {
            CACHE_FLUSH_DATA(pData, (size_t)uWidth * (size_t)uHeight);
        }

        /* Flush cache for dense layer row pointers array */
        if (ppRows)
        {
            CACHE_FLUSH_DATA(ppRows, sizeof(uint8_t *) * uHeight);
        }
    }

    return true;
}

/* ---------- Atlas building helpers ---------- */

/* Validate tile index (returns false if invalid) */
static inline bool validate_tile_index(const tilemap_importer_t *_pImporter, uint8_t _uTileIndex)
{
    if (!_pImporter || !_pImporter->bInitialized)
        return false;
    if (_uTileIndex == TILEMAP_IMPORTER_EMPTY_TILE)
        return false;
    if (_uTileIndex >= _pImporter->uTileCount)
        return false;
    return true;
}

/* Tile frequency entry for sorting */
typedef struct
{
    uint8_t uTileId;
    uint32_t uFrequency;
} tile_frequency_t;

static int cmp_frequency_desc(const void *_pA, const void *_pB)
{
    const tile_frequency_t *pA = (const tile_frequency_t *)_pA;
    const tile_frequency_t *pB = (const tile_frequency_t *)_pB;
    if (pB->uFrequency > pA->uFrequency)
        return 1;
    if (pB->uFrequency < pA->uFrequency)
        return -1;
    return 0;
}

/* Build frequency histogram by scanning all layers */
static bool build_tile_frequency_histogram(const tilemap_importer_t *_pImporter, tile_frequency_t *_pFreq, uint16_t _uTileCount)
{
    if (!_pImporter || !_pFreq)
        return false;

    /* Initialize frequency array */
    for (uint16_t i = 0; i < _uTileCount; ++i)
    {
        _pFreq[i].uTileId = (uint8_t)i;
        _pFreq[i].uFrequency = 0;
    }

    /* Scan all layers */
    for (uint8_t uLayer = 0; uLayer < TILEMAP_IMPORTER_MAX_LAYERS; ++uLayer)
    {
        const tilemap_layer_t *pLayer = &_pImporter->aLayers[uLayer];
        if (!pLayer->ppData || pLayer->uWidth == 0 || pLayer->uHeight == 0)
            continue;

        for (uint16_t y = 0; y < pLayer->uHeight; ++y)
        {
            const uint8_t *pRow = pLayer->ppData[y];
            for (uint16_t x = 0; x < pLayer->uWidth; ++x)
            {
                uint8_t uTileId = pRow[x];
                if (uTileId != TILEMAP_IMPORTER_EMPTY_TILE && uTileId < _uTileCount)
                {
                    _pFreq[uTileId].uFrequency++;
                }
            }
        }
    }

    return true;
}

/* Convert a sprite pixel to RGBA16 format */
static uint16_t convert_pixel_to_rgba16(const surface_t *_pSrcSurface, tex_format_t _eFormat, const uint16_t *_pPalette, uint16_t _x, uint16_t _y)
{
    const uint8_t *pRow = (const uint8_t *)_pSrcSurface->buffer + (_y * _pSrcSurface->stride);

    if (_eFormat == FMT_RGBA16)
    {
        /* Read pixel directly - assume libdragon surfaces are already in correct format */
        const uint16_t *pPixel = (const uint16_t *)(pRow + (_x * 2));
        return *pPixel;
    }
    else if (_eFormat == FMT_RGBA32)
    {
        const uint32_t *pPixel = (const uint32_t *)(pRow + (_x * 4));
        uint32_t uRGBA32 = *pPixel;
        /* Convert RGBA32 (8888) to RGBA16 (5551)
         * On little-endian systems, when reading uint32_t from [R,G,B,A] in memory:
         * uRGBA32 = R | (G << 8) | (B << 16) | (A << 24)
         * So: r = bits 0-7, g = bits 8-15, b = bits 16-23, a = bits 24-31 */
        uint8_t r = (uint8_t)(uRGBA32 & 0xFF);
        uint8_t g = (uint8_t)((uRGBA32 >> 8) & 0xFF);
        uint8_t b = (uint8_t)((uRGBA32 >> 16) & 0xFF);
        uint8_t a = (uint8_t)((uRGBA32 >> 24) & 0xFF);
        uint16_t uRGBA16 = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a > 127 ? 1 : 0);
        return uRGBA16;
    }
    else if (_eFormat == FMT_CI4)
    {
        if (!_pPalette)
            return 0;
        const uint8_t *pByte = pRow + (_x / 2);
        /* CI4: 2 pixels per byte
         * Even x (0,2,4...): upper 4 bits
         * Odd x (1,3,5...): lower 4 bits */
        const uint8_t uIndex = (_x & 1) ? (*pByte & 0x0F) : ((*pByte >> 4) & 0x0F);
        /* Read palette entry directly */
        return _pPalette[uIndex];
    }
    else if (_eFormat == FMT_CI8)
    {
        if (!_pPalette)
            return 0;
        const uint8_t uIndex = pRow[_x];
        /* Read palette entry directly */
        return _pPalette[uIndex];
    }

    return 0; /* Unknown format = transparent */
}

/* Copy a 16x16 tile from sprite to atlas page at specified position */
static void copy_tile_to_atlas_page(surface_t *_pDstPage, uint8_t _uDstX, uint8_t _uDstY, sprite_t *_pSprite)
{
    if (!_pDstPage || !_pSprite)
        return;

    surface_t srcSurface = sprite_get_pixels(_pSprite);
    if (!srcSurface.buffer)
        return;

    /* Verify sprite dimensions match expected tile size */
    if (srcSurface.width < 16 || srcSurface.height < 16)
    {
        debugf("Warning: Sprite size (%ux%u) is smaller than tile size (16x16)\n", (unsigned)srcSurface.width, (unsigned)srcSurface.height);
        return;
    }

    tex_format_t eFormat = sprite_get_format(_pSprite);
    uint16_t *pPalette = NULL;
    if (eFormat == FMT_CI4 || eFormat == FMT_CI8)
    {
        pPalette = sprite_get_palette(_pSprite);
        if (!pPalette)
        {
            debugf("Warning: CI format sprite has no palette\n");
            return;
        }
    }

    /* Copy 16x16 tile (use top-left 16x16 region of sprite) */
    /* Always convert pixel-by-pixel to ensure correct format conversion */
    for (uint8_t y = 0; y < 16; ++y)
    {
        uint8_t *pDstRow = (uint8_t *)_pDstPage->buffer + ((_uDstY + y) * _pDstPage->stride);

        for (uint8_t x = 0; x < 16; ++x)
        {
            uint16_t uPixel = convert_pixel_to_rgba16(&srcSurface, eFormat, pPalette, x, y);
            /* Write pixel directly - libdragon handles byte order */
            uint16_t *pDstPixel = (uint16_t *)(pDstRow + ((_uDstX + x) * 2));
            *pDstPixel = uPixel;
        }
    }
}

/* Build atlas pages from sorted tile list */
static bool build_atlas_pages(tilemap_importer_t *_pImporter, const tile_frequency_t *_pSortedTiles, uint16_t _uTileCount)
{
    if (!_pImporter || !_pSortedTiles)
        return false;

    /* Calculate number of pages needed */
    uint16_t uPageCount = (_uTileCount + TILE_ATLAS_TILES_PER_PAGE - 1) / TILE_ATLAS_TILES_PER_PAGE;
    if (uPageCount > TILE_ATLAS_MAX_PAGES)
        uPageCount = TILE_ATLAS_MAX_PAGES;

    /* Allocate atlas pages */
    surface_t *pPages = (surface_t *)malloc(sizeof(surface_t) * uPageCount);
    if (!pPages)
    {
        debugf("Failed to allocate atlas pages\n");
        return false;
    }

    /* Allocate atlas entries lookup table */
    tile_atlas_entry_t *pEntries = (tile_atlas_entry_t *)malloc(sizeof(tile_atlas_entry_t) * _uTileCount);
    if (!pEntries)
    {
        debugf("Failed to allocate atlas entries\n");
        free(pPages);
        return false;
    }

    /* Initialize all entries to invalid */
    for (uint16_t i = 0; i < _uTileCount; ++i)
    {
        pEntries[i].uPageIndex = 255;
        pEntries[i].uU0 = 0;
        pEntries[i].uV0 = 0;
    }

    /* Allocate and build each page */
    for (uint16_t uPage = 0; uPage < uPageCount; ++uPage)
    {
        pPages[uPage] = surface_alloc(FMT_RGBA16, TILE_ATLAS_PAGE_WIDTH, TILE_ATLAS_PAGE_HEIGHT);
        if (!pPages[uPage].buffer)
        {
            debugf("Failed to allocate atlas page %u\n", (unsigned)uPage);
            /* Free already allocated pages */
            for (uint16_t i = 0; i < uPage; ++i)
            {
                surface_free(&pPages[i]);
            }
            free(pPages);
            free(pEntries);
            return false;
        }

        /* Clear the page to transparent (important: surface_alloc doesn't zero-initialize) */
        memset(pPages[uPage].buffer, 0, (size_t)pPages[uPage].stride * TILE_ATLAS_PAGE_HEIGHT);

        /* Pack up to 8 tiles into this page (4x2 grid) */
        for (uint8_t uTileInPage = 0; uTileInPage < TILE_ATLAS_TILES_PER_PAGE; ++uTileInPage)
        {
            uint16_t uGlobalTileIndex = uPage * TILE_ATLAS_TILES_PER_PAGE + uTileInPage;
            if (uGlobalTileIndex >= _uTileCount)
                break;

            uint8_t uTileId = _pSortedTiles[uGlobalTileIndex].uTileId;
            sprite_t *pSprite = _pImporter->ppTileSprites[uTileId];
            if (!pSprite)
                continue;

            /* Calculate position in page: 4 tiles per row, 2 rows */
            uint8_t uPageX = (uTileInPage % 4) * 16;
            uint8_t uPageY = (uTileInPage / 4) * 16;

            /* Copy tile to page */
            copy_tile_to_atlas_page(&pPages[uPage], uPageX, uPageY, pSprite);

            /* Store atlas entry */
            pEntries[uTileId].uPageIndex = (uint8_t)uPage;
            pEntries[uTileId].uU0 = uPageX;
            pEntries[uTileId].uV0 = uPageY;
        }

        /* CRITICAL: Flush cache immediately after building this page
         * This ensures all memset() and copy_tile_to_atlas_page() writes are visible to DMA.
         * Only call cache operations if address is in cacheable range (KSEG0: 0x80000000-0x9fffffff) */
        CACHE_FLUSH_DATA(pPages[uPage].buffer, (size_t)pPages[uPage].stride * TILE_ATLAS_PAGE_HEIGHT);
    }

    /* Flush cache for atlas entries lookup table */
    CACHE_FLUSH_DATA(pEntries, sizeof(tile_atlas_entry_t) * _uTileCount);

    _pImporter->pAtlasPages = pPages;
    _pImporter->uAtlasPageCount = uPageCount;
    _pImporter->pAtlasEntries = pEntries;

    return true;
}

/* ---------- Public API ---------- */

bool tilemap_importer_init(tilemap_importer_t *_pImporter, const char *_pMapFolder, tilemap_type_t _eType)
{
    if (!_pImporter || !_pMapFolder)
        return false;

    memset(_pImporter, 0, sizeof(*_pImporter));

    /* Determine layer count based on type */
    uint8_t uLayerCount = (_eType == TILEMAP_TYPE_JNR) ? TILEMAP_LAYER_COUNT_JNR : TILEMAP_LAYER_COUNT_SURFACE;
    _pImporter->uLayerCount = uLayerCount;
    _pImporter->eType = _eType;

    bool bOk = false;

    int *pTileIds = NULL;
    uint16_t uTileCount = 0;

    if (!load_tile_ids_sorted(_pMapFolder, &pTileIds, &uTileCount))
    {
        debugf("Failed to load tile IDs\n");
        goto fail;
    }

    sprite_t **ppSprites = NULL;
    if (!load_tile_sprites(_pMapFolder, pTileIds, uTileCount, &ppSprites))
    {
        debugf("Failed to load tile sprites\n");
        goto fail;
    }

    _pImporter->ppTileSprites = ppSprites;
    _pImporter->uTileCount = uTileCount;

    /* Flush cache for sprite pointer array */
    CACHE_FLUSH_DATA(ppSprites, sizeof(sprite_t *) * uTileCount);

    /* Calculate trimmed bounding boxes for all tile sprites */
    tile_trimmed_rect_t *pTrimmedRects = (tile_trimmed_rect_t *)malloc(sizeof(tile_trimmed_rect_t) * uTileCount);
    if (!pTrimmedRects)
    {
        debugf("Failed to allocate memory for trimmed rects\n");
        goto fail;
    }

    /* Initialize to zero before populating */
    memset(pTrimmedRects, 0, sizeof(tile_trimmed_rect_t) * uTileCount); // remove - overly aggressive fix?

    for (uint16_t i = 0; i < uTileCount; ++i)
    {
        struct vec2i vOffset = {0, 0};
        struct vec2i vSize = {0, 0};

        if (!sprite_tools_get_trimmed_rect(ppSprites[i], &vOffset, &vSize))
        {
            debugf("Failed to get trimmed rect for tile %u\n", (unsigned)i);
            /* Continue anyway, use default values (0,0) offset and size */
        }

        pTrimmedRects[i].vOffset = vOffset;
        pTrimmedRects[i].vSize = vSize;
    }

    _pImporter->pTileTrimmedRects = pTrimmedRects;

    /* Flush cache for trimmed rects array - AFTER assignment to ensure all writes are complete */
    CACHE_FLUSH_DATA(pTrimmedRects, sizeof(tile_trimmed_rect_t) * uTileCount);

    /* Load CSV layers (only load the required number of layers based on type) */
    for (uint8_t i = 0; i < uLayerCount; ++i)
    {
        tilemap_layer_t tLayer;
        if (!load_csv_layer(_pMapFolder, i, &tLayer, pTileIds, uTileCount))
        {
            debugf("Failed to load CSV layer %u\n", (unsigned)i);
            goto fail;
        }

        /* Verify consistent dimensions */
        if (i > 0)
        {
            if (tLayer.uWidth != _pImporter->aLayers[0].uWidth || tLayer.uHeight != _pImporter->aLayers[0].uHeight)
            {
                debugf("Layer %u dimensions (%ux%u) don't match layer 0 (%ux%u)\n",
                       (unsigned)i,
                       (unsigned)tLayer.uWidth,
                       (unsigned)tLayer.uHeight,
                       (unsigned)_pImporter->aLayers[0].uWidth,
                       (unsigned)_pImporter->aLayers[0].uHeight);
                free_layer(&tLayer);
                goto fail;
            }
        }

        _pImporter->aLayers[i] = tLayer;
    }

    /* Build frequency histogram and create atlas pages */
    tile_frequency_t *pFreq = (tile_frequency_t *)malloc(sizeof(tile_frequency_t) * uTileCount);
    if (!pFreq)
    {
        debugf("Failed to allocate frequency array\n");
        goto fail;
    }

    if (!build_tile_frequency_histogram(_pImporter, pFreq, uTileCount))
    {
        debugf("Failed to build frequency histogram\n");
        free(pFreq);
        goto fail;
    }

    /* Sort by frequency (descending) */
    qsort(pFreq, uTileCount, sizeof(tile_frequency_t), cmp_frequency_desc);

    /* Flush after qsort since it writes to memory that will be freed and potentially reused */
    CACHE_FLUSH_DATA(pFreq, sizeof(tile_frequency_t) * uTileCount);

    /* Build atlas pages */
    if (!build_atlas_pages(_pImporter, pFreq, uTileCount))
    {
        debugf("Failed to build atlas pages\n");
        free(pFreq);
        goto fail;
    }

    free(pFreq);
    pFreq = NULL;

    /* Free individual tile sprites - they're no longer needed after atlas creation */
    if (_pImporter->ppTileSprites)
    {
        for (uint16_t i = 0; i < _pImporter->uTileCount; ++i)
        {
            SAFE_FREE_SPRITE(_pImporter->ppTileSprites[i]);
        }
        free(_pImporter->ppTileSprites);
        _pImporter->ppTileSprites = NULL;
    }

    _pImporter->bInitialized = true;
    bOk = true;

fail:
    if (pTileIds)
    {
        free(pTileIds);
        pTileIds = NULL;
    }

    if (!bOk)
    {
        tilemap_importer_free(_pImporter);
        return false;
    }

    return true;
}

void tilemap_importer_free(tilemap_importer_t *_pImporter)
{
    if (!_pImporter)
        return;

    if (_pImporter->ppTileSprites)
    {
        for (uint16_t i = 0; i < _pImporter->uTileCount; ++i)
        {
            SAFE_FREE_SPRITE(_pImporter->ppTileSprites[i]);
        }
        free(_pImporter->ppTileSprites);
        _pImporter->ppTileSprites = NULL;
    }

    if (_pImporter->pTileTrimmedRects)
    {
        free(_pImporter->pTileTrimmedRects);
        _pImporter->pTileTrimmedRects = NULL;
    }

    if (_pImporter->pAtlasPages)
    {
        for (uint16_t i = 0; i < _pImporter->uAtlasPageCount; ++i)
        {
            surface_free(&_pImporter->pAtlasPages[i]);
        }
        free(_pImporter->pAtlasPages);
        _pImporter->pAtlasPages = NULL;
    }

    if (_pImporter->pAtlasEntries)
    {
        free(_pImporter->pAtlasEntries);
        _pImporter->pAtlasEntries = NULL;
    }

    for (uint8_t i = 0; i < TILEMAP_IMPORTER_MAX_LAYERS; ++i)
        free_layer(&_pImporter->aLayers[i]);

    _pImporter->uTileCount = 0;
    _pImporter->uAtlasPageCount = 0;
    _pImporter->bInitialized = false;
}

sprite_t *tilemap_importer_get_tile_sprite(const tilemap_importer_t *_pImporter, uint8_t _uTileIndex)
{
    if (!validate_tile_index(_pImporter, _uTileIndex))
        return NULL;

    return _pImporter->ppTileSprites[_uTileIndex];
}

const tilemap_layer_t *tilemap_importer_get_layer(const tilemap_importer_t *_pImporter, uint8_t _uLayerIndex)
{
    if (!_pImporter || !_pImporter->bInitialized || _uLayerIndex >= TILEMAP_IMPORTER_MAX_LAYERS)
        return NULL;

    return &_pImporter->aLayers[_uLayerIndex];
}

bool tilemap_importer_get_tile_trimmed_rect(const tilemap_importer_t *_pImporter, uint8_t _uTileIndex, struct vec2i *_pOutOffset, struct vec2i *_pOutSize)
{
    if (!_pOutOffset || !_pOutSize)
        return false;

    if (!validate_tile_index(_pImporter, _uTileIndex))
        return false;

    if (!_pImporter->pTileTrimmedRects)
        return false;

    *_pOutOffset = _pImporter->pTileTrimmedRects[_uTileIndex].vOffset;
    *_pOutSize = _pImporter->pTileTrimmedRects[_uTileIndex].vSize;

    return true;
}

bool tilemap_importer_get_atlas_entry(const tilemap_importer_t *_pImporter, uint8_t _uTileIndex, tile_atlas_entry_t *_pOutEntry)
{
    if (!_pOutEntry)
        return false;

    if (!validate_tile_index(_pImporter, _uTileIndex))
        return false;

    if (!_pImporter->pAtlasEntries)
        return false;

    *_pOutEntry = _pImporter->pAtlasEntries[_uTileIndex];
    return true;
}

const surface_t *tilemap_importer_get_atlas_page(const tilemap_importer_t *_pImporter, uint8_t _uPageIndex)
{
    if (!_pImporter || !_pImporter->bInitialized)
        return NULL;

    if (_uPageIndex >= _pImporter->uAtlasPageCount)
        return NULL;

    if (!_pImporter->pAtlasPages)
        return NULL;

    return &_pImporter->pAtlasPages[_uPageIndex];
}

void tilemap_importer_debug(const tilemap_importer_t *_pImporter)
{
    if (!_pImporter)
    {
        debugf("Tilemap Importer: NULL pointer\n");
        return;
    }

    debugf("Tilemap Importer: Initialized=%s, Tiles=%u\n", _pImporter->bInitialized ? "true" : "false", (unsigned)_pImporter->uTileCount);

    if (_pImporter->bInitialized)
    {
        uint32_t uTotalMemory = 0;

        for (uint8_t i = 0; i < TILEMAP_IMPORTER_MAX_LAYERS; ++i)
        {
            const tilemap_layer_t *pLayer = &_pImporter->aLayers[i];

            if (pLayer->uWidth == 0 || pLayer->uHeight == 0)
            {
                debugf("  Layer %u: No data\n", (unsigned)i);
                continue;
            }

            uint32_t uTotalTiles = (uint32_t)pLayer->uWidth * (uint32_t)pLayer->uHeight;
            float fFillPercent = (float)pLayer->uTileCount / (float)uTotalTiles * 100.0f;
            uint32_t uMemory = 0;

            if (pLayer->eStorage == TILEMAP_LAYER_STORAGE_DENSE)
            {
                uMemory = uTotalTiles + (uint32_t)pLayer->uHeight * sizeof(uint8_t *);
                debugf("  Layer %u: DENSE %ux%u (%u tiles, %.1f%% fill, %u bytes)\n",
                       (unsigned)i,
                       (unsigned)pLayer->uWidth,
                       (unsigned)pLayer->uHeight,
                       (unsigned)pLayer->uTileCount,
                       fFillPercent,
                       (unsigned)uMemory);
            }
            else if (pLayer->eStorage == TILEMAP_LAYER_STORAGE_SINGLE)
            {
                debugf("  Layer %u: SINGLE %ux%u (TileID: %u, 0 bytes)\n", (unsigned)i, (unsigned)pLayer->uWidth, (unsigned)pLayer->uHeight, (unsigned)pLayer->uSingleTileId);
            }
            else /* TILEMAP_LAYER_STORAGE_SPARSE */
            {
                uMemory = (uint32_t)pLayer->sparse.uCapacity * sizeof(sparse_tile_entry_t);
                uint32_t uDenseMemory = uTotalTiles;
                float fSavingsPercent = (float)(uDenseMemory - uMemory) / (float)uDenseMemory * 100.0f;
                debugf("  Layer %u: SPARSE %ux%u (%u tiles, %.1f%% fill, %u bytes, %.1f%% saved)\n",
                       (unsigned)i,
                       (unsigned)pLayer->uWidth,
                       (unsigned)pLayer->uHeight,
                       (unsigned)pLayer->uTileCount,
                       fFillPercent,
                       (unsigned)uMemory,
                       fSavingsPercent);
            }

            uTotalMemory += uMemory;
        }

        debugf("  Total layer memory: %u bytes\n", (unsigned)uTotalMemory);
    }
}
