#include "triggers.h"
#include "csv_helper.h"
#include "libdragon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRIGGERS 64

void trigger_collection_init(trigger_collection_t *_pCollection)
{
    if (!_pCollection)
        return;

    memset(_pCollection, 0, sizeof(*_pCollection));
    _pCollection->uCapacity = MAX_TRIGGERS;
    _pCollection->pTriggers = (trigger_t *)malloc(sizeof(trigger_t) * _pCollection->uCapacity);
    if (!_pCollection->pTriggers)
    {
        debugf("Failed to allocate memory for triggers\n");
        _pCollection->uCapacity = 0;
    }
}

void trigger_collection_free(trigger_collection_t *_pCollection)
{
    if (!_pCollection)
        return;

    if (_pCollection->pTriggers)
    {
        free(_pCollection->pTriggers);
        _pCollection->pTriggers = NULL;
    }
    _pCollection->uCount = 0;
    _pCollection->uCapacity = 0;
    _pCollection->pSelected = NULL;
}

/* Parse a circle trigger line: name,x,y,radius */
static bool parse_circle_trigger_line(const char *_pLine, char *_pOutName, size_t _uNameSize, struct vec2 *_pOutPos, float *_pOutRadius)
{
    if (!_pLine || !_pOutName || !_pOutPos || !_pOutRadius)
        return false;

    char szLineCopy[512];
    if (!csv_helper_copy_line_for_tokenizing(_pLine, szLineCopy, sizeof(szLineCopy)))
        return false;

    /* Parse name (first token) */
    if (!csv_helper_parse_name(szLineCopy, _pOutName, _uNameSize))
        return false;

    /* Parse x,y coordinates */
    if (!csv_helper_parse_xy_from_tokens(strtok(NULL, ","), strtok(NULL, ","), _pOutPos))
        return false;

    /* Parse radius (fourth token) */
    char *pToken = strtok(NULL, ",");
    if (!pToken)
        return false;
    if (!csv_helper_parse_float(pToken, _pOutRadius))
        return false;

    return true;
}

/* Parse a rect trigger line: name,x,y,width,height */
static bool parse_rect_trigger_line(const char *_pLine, char *_pOutName, size_t _uNameSize, struct vec2 *_pOutTopLeft, struct vec2 *_pOutSize)
{
    if (!_pLine || !_pOutName || !_pOutTopLeft || !_pOutSize)
        return false;

    char szLineCopy[512];
    if (!csv_helper_copy_line_for_tokenizing(_pLine, szLineCopy, sizeof(szLineCopy)))
        return false;

    /* Parse name (first token) */
    if (!csv_helper_parse_name(szLineCopy, _pOutName, _uNameSize))
        return false;

    /* Parse x,y (top-left corner) */
    if (!csv_helper_parse_xy_from_tokens(strtok(NULL, ","), strtok(NULL, ","), _pOutTopLeft))
        return false;

    /* Parse width,height */
    char *pToken = strtok(NULL, ",");
    if (!pToken)
        return false;
    float fWidth;
    if (!csv_helper_parse_float(pToken, &fWidth))
        return false;

    pToken = strtok(NULL, ",");
    if (!pToken)
        return false;
    float fHeight;
    if (!csv_helper_parse_float(pToken, &fHeight))
        return false;

    _pOutSize->fX = fWidth;
    _pOutSize->fY = fHeight;

    return true;
}

bool trigger_collection_load_from_csv(const char *_pCsvPath, trigger_shape_t _eShape, trigger_type_t _eType, trigger_collection_t *_pCollection)
{
    if (!_pCsvPath || !_pCollection)
        return false;

    if (!_pCollection->pTriggers || _pCollection->uCapacity == 0)
    {
        debugf("Trigger collection not initialized\n");
        return false;
    }

    FILE *pFile = fopen(_pCsvPath, "r");
    if (!pFile)
    {
        debugf("Failed to open trigger CSV file: %s\n", _pCsvPath);
        return false;
    }

    char szLine[512];
    size_t uLineNum = 0;

    /* Read and parse each line */
    while (true)
    {
        bool bTruncated = false;
        if (!csv_helper_fgets_checked(szLine, sizeof(szLine), pFile, &bTruncated))
            break;

        if (bTruncated)
        {
            debugf("CSV line too long (buffer %u) in %s at line %u\n", (unsigned)sizeof(szLine), _pCsvPath, (unsigned)uLineNum);
            continue;
        }

        csv_helper_strip_eol(szLine);

        /* Skip empty lines */
        if (szLine[0] == '\0')
            continue;

        uLineNum++;

        /* Check capacity */
        if (_pCollection->uCount >= _pCollection->uCapacity)
        {
            debugf("Trigger array full, skipping remaining triggers\n");
            break;
        }

        /* Parse line based on shape */
        trigger_t *pTrigger = &_pCollection->pTriggers[_pCollection->uCount];
        memset(pTrigger, 0, sizeof(*pTrigger));

        bool bParseSuccess = false;
        if (_eShape == TRIGGER_SHAPE_CIRCLE)
        {
            char szName[64];
            struct vec2 vPos;
            float fRadius;

            if (parse_circle_trigger_line(szLine, szName, sizeof(szName), &vPos, &fRadius))
            {
                if (!csv_helper_copy_string_safe(szName, pTrigger->szName, sizeof(pTrigger->szName)))
                    continue;
                /* Display name will be set by caller if needed */
                pTrigger->szDisplayName[0] = '\0';

                pTrigger->eShape = TRIGGER_SHAPE_CIRCLE;
                pTrigger->eType = _eType;
                pTrigger->vPos = vPos; /* center position */
                pTrigger->shapeData.circle.fRadius = fRadius;
                pTrigger->bActive = true;
                bParseSuccess = true;
            }
        }
        else if (_eShape == TRIGGER_SHAPE_RECT)
        {
            char szName[64];
            struct vec2 vTopLeft;
            struct vec2 vSize;

            if (parse_rect_trigger_line(szLine, szName, sizeof(szName), &vTopLeft, &vSize))
            {
                if (!csv_helper_copy_string_safe(szName, pTrigger->szName, sizeof(pTrigger->szName)))
                    continue;
                /* Display name will be set by caller if needed */
                pTrigger->szDisplayName[0] = '\0';

                pTrigger->eShape = TRIGGER_SHAPE_RECT;
                pTrigger->eType = _eType;
                pTrigger->vPos = vTopLeft; /* top-left position */
                pTrigger->shapeData.rect.fX = vTopLeft.fX;
                pTrigger->shapeData.rect.fY = vTopLeft.fY;
                pTrigger->shapeData.rect.fWidth = vSize.fX;
                pTrigger->shapeData.rect.fHeight = vSize.fY;
                pTrigger->bActive = true;
                bParseSuccess = true;
            }
        }

        if (!bParseSuccess)
        {
            debugf("Failed to parse trigger line %u: %s\n", (unsigned)uLineNum, szLine);
            continue;
        }

        _pCollection->uCount++;
    }

    fclose(pFile);
    debugf("Loaded %u triggers from %s\n", (unsigned)_pCollection->uCount, _pCsvPath);
    return true;
}

/* Handle trigger enter/exit events (common logic)
 * _pCollection: Trigger collection
 * _pTrigger: Trigger that changed state
 * _bIsColliding: Current collision state
 * Returns true if selection changed */
static bool handle_trigger_events(trigger_collection_t *_pCollection, trigger_t *_pTrigger, bool _bIsColliding)
{
    bool bChanged = false;

    if (!_pTrigger->bWasColliding && _bIsColliding)
    {
        /* OnTriggerEnter */
        _pCollection->pSelected = _pTrigger;
        debugf("Entered trigger: %s\n", _pTrigger->szName);
        bChanged = true;
    }
    else if (_pTrigger->bWasColliding && !_bIsColliding)
    {
        /* OnTriggerExit */
        if (_pCollection->pSelected == _pTrigger)
        {
            _pCollection->pSelected = NULL;
            debugf("Exited trigger: %s\n", _pTrigger->szName);
            bChanged = true;
        }
    }

    _pTrigger->bWasColliding = _bIsColliding;
    return bChanged;
}

bool trigger_collection_update_with_entity(trigger_collection_t *_pCollection, const struct entity2D *_pEntity)
{
    if (!_pCollection || !_pEntity || !entity2d_is_active(_pEntity) || !entity2d_is_collidable(_pEntity))
        return false;

    bool bChanged = false;

    for (size_t i = 0; i < _pCollection->uCount; ++i)
    {
        trigger_t *pTrigger = &_pCollection->pTriggers[i];

        if (!pTrigger->bActive)
            continue;

        /* Check collision based on trigger shape */
        bool bIsColliding = false;
        if (pTrigger->eShape == TRIGGER_SHAPE_CIRCLE)
        {
            /* Create a temporary entity for the trigger to use circle-circle collision */
            struct entity2D tempTriggerEntity;
            tempTriggerEntity.vPos = pTrigger->vPos;
            tempTriggerEntity.iCollisionRadius = (int)pTrigger->shapeData.circle.fRadius;
            bIsColliding = entity2d_check_collision_circle(_pEntity, &tempTriggerEntity);
        }
        else if (pTrigger->eShape == TRIGGER_SHAPE_RECT)
        {
            /* Use circle-rect collision */
            struct vec2 vRectTopLeft = vec2_make(pTrigger->shapeData.rect.fX, pTrigger->shapeData.rect.fY);
            struct vec2 vRectSize = vec2_make(pTrigger->shapeData.rect.fWidth, pTrigger->shapeData.rect.fHeight);
            bIsColliding = entity2d_check_collision_circle_rect(_pEntity, vRectTopLeft, vRectSize);
        }

        /* Handle enter/exit events */
        if (handle_trigger_events(_pCollection, pTrigger, bIsColliding))
            bChanged = true;
    }

    return bChanged;
}

const trigger_t *trigger_collection_get_selected(const trigger_collection_t *_pCollection)
{
    if (!_pCollection)
        return NULL;
    return _pCollection->pSelected;
}

const char *trigger_collection_get_selected_display_name(const trigger_collection_t *_pCollection)
{
    const trigger_t *pSelected = trigger_collection_get_selected(_pCollection);
    if (pSelected)
        return pSelected->szDisplayName[0] != '\0' ? pSelected->szDisplayName : pSelected->szName;
    return NULL;
}

const char *trigger_collection_get_selected_data_name(const trigger_collection_t *_pCollection)
{
    const trigger_t *pSelected = trigger_collection_get_selected(_pCollection);
    if (pSelected)
        return pSelected->szName;
    return NULL;
}

bool trigger_collection_get_selected_center(const trigger_collection_t *_pCollection, struct vec2 *_pOutCenter)
{
    if (!_pCollection || !_pOutCenter)
        return false;

    const trigger_t *pSelected = trigger_collection_get_selected(_pCollection);
    if (!pSelected)
        return false;

    if (pSelected->eShape == TRIGGER_SHAPE_CIRCLE)
    {
        /* For circles, vPos is already the center */
        *_pOutCenter = pSelected->vPos;
    }
    else if (pSelected->eShape == TRIGGER_SHAPE_RECT)
    {
        /* For rects, vPos is top-left, calculate center */
        float fCenterX = pSelected->vPos.fX + pSelected->shapeData.rect.fWidth / 2.0f;
        float fCenterY = pSelected->vPos.fY + pSelected->shapeData.rect.fHeight / 2.0f;
        *_pOutCenter = vec2_make(fCenterX, fCenterY);
    }
    else
    {
        return false;
    }

    return true;
}

/* Update trigger collision state with a position and collision box (for entities that use box collision)
 * _pCollection: Trigger collection to update
 * _vPos: Position of the entity (center)
 * _vHalfExtents: Half extents of the collision box
 * Returns true if a trigger was entered/exited */
bool trigger_collection_update_with_box(trigger_collection_t *_pCollection, struct vec2 _vPos, struct vec2 _vHalfExtents)
{
    if (!_pCollection)
        return false;

    bool bChanged = false;

    for (size_t i = 0; i < _pCollection->uCount; ++i)
    {
        trigger_t *pTrigger = &_pCollection->pTriggers[i];

        if (!pTrigger->bActive)
            continue;

        /* Check collision based on trigger shape */
        bool bIsColliding = false;
        if (pTrigger->eShape == TRIGGER_SHAPE_CIRCLE)
        {
            /* Create a temporary entity for circle collision */
            struct entity2D tempEntity;
            tempEntity.vPos = _vPos;
            tempEntity.iCollisionRadius = (int)((_vHalfExtents.fX > _vHalfExtents.fY) ? _vHalfExtents.fX : _vHalfExtents.fY);

            struct entity2D tempTriggerEntity;
            tempTriggerEntity.vPos = pTrigger->vPos;
            tempTriggerEntity.iCollisionRadius = (int)pTrigger->shapeData.circle.fRadius;
            bIsColliding = entity2d_check_collision_circle(&tempEntity, &tempTriggerEntity);
        }
        else if (pTrigger->eShape == TRIGGER_SHAPE_RECT)
        {
            /* Check box-rect collision */
            struct vec2 vRectTopLeft = vec2_make(pTrigger->shapeData.rect.fX, pTrigger->shapeData.rect.fY);
            struct vec2 vRectSize = vec2_make(pTrigger->shapeData.rect.fWidth, pTrigger->shapeData.rect.fHeight);

            /* Convert box center + half extents to top-left + size */
            struct vec2 vBoxTopLeft = vec2_sub(_vPos, _vHalfExtents);
            struct vec2 vBoxSize = vec2_scale(_vHalfExtents, 2.0f);

            bIsColliding = entity2d_check_collision_rect_rect(vBoxTopLeft, vBoxSize, vRectTopLeft, vRectSize);
        }

        /* Handle enter/exit events */
        if (handle_trigger_events(_pCollection, pTrigger, bIsColliding))
            bChanged = true;
    }

    return bChanged;
}
