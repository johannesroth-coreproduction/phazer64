#include "triggers_dialogue.h"
#include "../csv_helper.h"
#include "../dialogue.h"
#include "../player_jnr.h"
#include "../player_surface.h"
#include "../triggers.h"
#include "../ui.h"
#include "gp_state.h"
#include "libdragon.h"
#include <string.h>

static trigger_collection_t m_dialogueTriggers;
static sprite_t *m_pBtnASmallSprite = NULL;

bool triggers_dialogue_init(const char *_pPlanetFolder)
{
    if (!_pPlanetFolder)
        return false;

    /* Free existing triggers if any */
    triggers_dialogue_free();

    /* Initialize collection */
    trigger_collection_init(&m_dialogueTriggers);

    /* Build CSV path: rom:/<planet_folder>/dialogue.csv */
    char szCsvPath[256];
    snprintf(szCsvPath, sizeof(szCsvPath), "rom:/%s/dialogue.csv", _pPlanetFolder);

    /* Load triggers from CSV */
    if (!trigger_collection_load_from_csv(szCsvPath, TRIGGER_SHAPE_RECT, TRIGGER_TYPE_DIALOGUE, &m_dialogueTriggers))
    {
        /* It's okay if the file doesn't exist - not all planets have dialogue triggers */
        debugf("No dialogue triggers found in %s (file may not exist)\n", szCsvPath);
        return true; /* Return true anyway - this is not an error */
    }

    /* Load button sprite if not already loaded */
    if (!m_pBtnASmallSprite)
    {
        m_pBtnASmallSprite = sprite_load("rom:/btn_a_small_00.sprite");
        if (!m_pBtnASmallSprite)
        {
            debugf("Failed to load btn_a_small_00.sprite\n");
        }
    }

    return true;
}

void triggers_dialogue_free(void)
{
    trigger_collection_free(&m_dialogueTriggers);
    /* Don't free sprite - it's kept loaded for the lifetime of the game */
}

void triggers_dialogue_update(bool _bButtonAPressed)
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
    trigger_collection_update_with_box(&m_dialogueTriggers, vPlayerPos, vPlayerHalfExtents);

    /* Check for dialogue trigger activation (only if dialogue is not active) */
    if (!dialogue_is_active() && _bButtonAPressed)
    {
        const char *pDialogueName = triggers_dialogue_get_selected_data_name();
        if (pDialogueName != NULL)
        {
            dialogue_start(pDialogueName);
        }
    }
}

/* Get the data name of the currently selected trigger (original name for dialogue filename).
 * Returns NULL if no trigger is selected.
 */
const char *triggers_dialogue_get_selected_data_name(void)
{
    return trigger_collection_get_selected_data_name(&m_dialogueTriggers);
}

/* Get selected trigger world position and half extents */
bool triggers_dialogue_get_selected_pos_and_size(struct vec2 *_pOutCenter, struct vec2i *_pOutHalfExtents)
{
    if (!_pOutCenter || !_pOutHalfExtents)
        return false;

    if (!trigger_collection_get_selected_center(&m_dialogueTriggers, _pOutCenter))
        return false;

    const trigger_t *pSelected = trigger_collection_get_selected(&m_dialogueTriggers);
    if (!pSelected || pSelected->eShape != TRIGGER_SHAPE_RECT)
        return false;

    /* Calculate half extents from rect size */
    _pOutHalfExtents->iX = (int)(pSelected->shapeData.rect.fWidth / 2.0f);
    _pOutHalfExtents->iY = (int)(pSelected->shapeData.rect.fHeight / 2.0f);

    return true;
}

/* Get the button sprite for rendering (btn_a_small_00)
 * Returns NULL if sprite is not loaded */
sprite_t *triggers_dialogue_get_button_sprite(void)
{
    return m_pBtnASmallSprite;
}

/* Render dialogue trigger UI (button prompt above selected trigger) */
void triggers_dialogue_render_ui(void)
{
    const char *pDialogueName = triggers_dialogue_get_selected_data_name();
    if (pDialogueName)
    {
        struct vec2 vDialogueCenter;
        struct vec2i vDialogueHalfExtents;
        sprite_t *pBtnASmall = triggers_dialogue_get_button_sprite();
        if (triggers_dialogue_get_selected_pos_and_size(&vDialogueCenter, &vDialogueHalfExtents) && pBtnASmall)
        {
            ui_render_button_above_world_pos(vDialogueCenter, vDialogueHalfExtents, pBtnASmall, 1.0f);
        }
    }
}
