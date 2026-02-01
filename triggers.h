#pragma once

#include "entity2d.h"
#include "math2d.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Trigger shape types */
typedef enum
{
    TRIGGER_SHAPE_CIRCLE,
    TRIGGER_SHAPE_RECT
} trigger_shape_t;

/* Trigger type types */
typedef enum
{
    TRIGGER_TYPE_LOAD,     /* Loads a folder/tilemap (context determines: SPACE→planet, SURFACE→JNR) */
    TRIGGER_TYPE_DIALOGUE /* Future: dialogue triggers */
} trigger_type_t;

/* Trigger structure */
typedef struct
{
    char szName[64];        /* Original name from CSV (for data loading) */
    char szDisplayName[64]; /* Formatted name (for UI display, e.g., uppercase) */
    trigger_shape_t eShape;
    trigger_type_t eType;
    union
    {
        struct
        {
            float fRadius;
        } circle;
        struct
        {
            float fX, fY, fWidth, fHeight; /* top-left corner + size */
        } rect;
    } shapeData;
    struct vec2 vPos;   /* center for circle, top-left for rect */
    bool bActive;       /* whether trigger is currently active */
    bool bWasColliding; /* for enter/exit detection */
} trigger_t;

/* Trigger collection */
typedef struct
{
    trigger_t *pTriggers;
    size_t uCount;
    size_t uCapacity;
    trigger_t *pSelected; /* Currently selected trigger (via trigger enter) */
} trigger_collection_t;

/* Initialize a trigger collection */
void trigger_collection_init(trigger_collection_t *_pCollection);

/* Free a trigger collection */
void trigger_collection_free(trigger_collection_t *_pCollection);

/* Load triggers from CSV file
 * _pCsvPath: Path to CSV file
 * _eShape: Shape type for all triggers in this file
 * _eType: Type for all triggers in this file
 * _pCollection: Collection to add triggers to
 * Returns true if successful, false on error */
bool trigger_collection_load_from_csv(const char *_pCsvPath, trigger_shape_t _eShape, trigger_type_t _eType, trigger_collection_t *_pCollection);

/* Update trigger collision state with an entity
 * _pCollection: Trigger collection to update
 * _pEntity: Entity to check collision against
 * Returns true if a trigger was entered/exited */
bool trigger_collection_update_with_entity(trigger_collection_t *_pCollection, const struct entity2D *_pEntity);

/* Get the currently selected trigger (via trigger enter)
 * Returns NULL if no trigger is selected */
const trigger_t *trigger_collection_get_selected(const trigger_collection_t *_pCollection);

/* Get the display name of the currently selected trigger (formatted for UI)
 * Returns NULL if no trigger is selected */
const char *trigger_collection_get_selected_display_name(const trigger_collection_t *_pCollection);

/* Get the data name of the currently selected trigger (original name for loading)
 * Returns NULL if no trigger is selected */
const char *trigger_collection_get_selected_data_name(const trigger_collection_t *_pCollection);

/* Get the center position of the currently selected trigger
 * For circles: returns the center position
 * For rects: returns the center of the rectangle
 * Returns true if a trigger is selected and position was set, false otherwise */
bool trigger_collection_get_selected_center(const trigger_collection_t *_pCollection, struct vec2 *_pOutCenter);

/* Update trigger collision state with a position and collision box (for entities that use box collision)
 * _pCollection: Trigger collection to update
 * _vPos: Position of the entity (center)
 * _vHalfExtents: Half extents of the collision box
 * Returns true if a trigger was entered/exited */
bool trigger_collection_update_with_box(trigger_collection_t *_pCollection, struct vec2 _vPos, struct vec2 _vHalfExtents);
