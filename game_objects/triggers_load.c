#include "triggers_load.h"
#include "../csv_helper.h"
#include "../player_jnr.h"
#include "../player_surface.h"
#include "../string_helper.h"
#include "../triggers.h"
#include "gp_state.h"
#include "libdragon.h"
#include <ctype.h>
#include <string.h>

static trigger_collection_t m_loadTriggers;

bool triggers_load_init(const char *_pPlanetFolder)
{
    if (!_pPlanetFolder)
        return false;

    /* Free existing triggers if any */
    triggers_load_free();

    /* Initialize collection */
    trigger_collection_init(&m_loadTriggers);

    /* Build CSV path: rom:/<planet_folder>/load.csv */
    char szCsvPath[256];
    snprintf(szCsvPath, sizeof(szCsvPath), "rom:/%s/load.csv", _pPlanetFolder);

    /* Load triggers from CSV */
    if (!trigger_collection_load_from_csv(szCsvPath, TRIGGER_SHAPE_RECT, TRIGGER_TYPE_LOAD, &m_loadTriggers))
    {
        /* It's okay if the file doesn't exist - not all planets have load triggers */
        debugf("No load triggers found in %s (file may not exist)\n", szCsvPath);
        return true; /* Return true anyway - this is not an error */
    }

    /* Set display names for all loaded triggers using centralized formatting */
    for (size_t i = 0; i < m_loadTriggers.uCount; ++i)
    {
        trigger_t *pTrigger = &m_loadTriggers.pTriggers[i];
        /* Format display name using centralized nice location name function */
        string_helper_nice_location_name(pTrigger->szName, pTrigger->szDisplayName, sizeof(pTrigger->szDisplayName));
    }

    return true;
}

void triggers_load_free(void)
{
    trigger_collection_free(&m_loadTriggers);
}

void triggers_load_update(void)
{
    /* Get player position and collision box based on current game state */
    struct vec2 vPlayerPos;
    struct vec2 vPlayerHalfExtents;

    gp_state_t currentState = gp_state_get();

    if (currentState == SURFACE)
    {
        /* In SURFACE mode, check collision with player_surface */
        vPlayerPos = player_surface_get_position();
        vPlayerHalfExtents = player_surface_get_collision_half_extents();
    }
    else if (currentState == JNR)
    {
        /* In JNR mode, check collision with player_jnr */
        vPlayerPos = player_jnr_get_position();
        vPlayerHalfExtents = player_jnr_get_collision_half_extents();
    }
    else
    {
        /* Not in SURFACE or JNR mode, no collision checks needed */
        return;
    }

    /* Update trigger collision state using box collision */
    trigger_collection_update_with_box(&m_loadTriggers, vPlayerPos, vPlayerHalfExtents);
}

/* Get the display name of the currently selected trigger (uppercase).
 * Returns NULL if no trigger is selected.
 */
const char *triggers_load_get_selected_display_name(void)
{
    return trigger_collection_get_selected_display_name(&m_loadTriggers);
}

/* Get the data name of the currently selected trigger (original name for loading).
 * Returns NULL if no trigger is selected.
 */
const char *triggers_load_get_selected_data_name(void)
{
    return trigger_collection_get_selected_data_name(&m_loadTriggers);
}

/* Get the trigger collection (for internal use by gp_state rendering) */
trigger_collection_t *triggers_load_get_collection(void)
{
    return &m_loadTriggers;
}

/* Get selected trigger world position and half extents */
bool triggers_load_get_selected_pos_and_size(struct vec2 *_pOutCenter, struct vec2i *_pOutHalfExtents)
{
    if (!_pOutCenter || !_pOutHalfExtents)
        return false;

    if (!trigger_collection_get_selected_center(&m_loadTriggers, _pOutCenter))
        return false;

    const trigger_t *pSelected = trigger_collection_get_selected(&m_loadTriggers);
    if (!pSelected || pSelected->eShape != TRIGGER_SHAPE_RECT)
        return false;

    /* Calculate half extents from rect size */
    _pOutHalfExtents->iX = (int)(pSelected->shapeData.rect.fWidth / 2.0f);
    _pOutHalfExtents->iY = (int)(pSelected->shapeData.rect.fHeight / 2.0f);

    return true;
}
