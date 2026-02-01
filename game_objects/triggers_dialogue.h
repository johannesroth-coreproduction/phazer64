#pragma once

#include "../triggers.h"

/* Initialize dialogue triggers from dialogue.csv in the current planet folder
 * _pPlanetFolder: Name of the planet folder (e.g., "terraria")
 * Returns true if successful, false on error */
bool triggers_dialogue_init(const char *_pPlanetFolder);

/* Free dialogue triggers */
void triggers_dialogue_free(void);

/* Update dialogue trigger collision checks and handle activation
 * SURFACE mode: Checks collision with player_surface
 * JNR mode: Checks collision with player_jnr
 * _bButtonAPressed: True if A button was pressed this frame */
void triggers_dialogue_update(bool _bButtonAPressed);

/* Get the data name of the currently selected trigger (original name for dialogue filename).
 * Returns NULL if no trigger is selected.
 */
const char *triggers_dialogue_get_selected_data_name(void);

/* Get selected trigger world position and half extents
 * Returns true if trigger is selected and valid, false otherwise
 * Outputs are written to _pOutCenter and _pOutHalfExtents if successful */
bool triggers_dialogue_get_selected_pos_and_size(struct vec2 *_pOutCenter, struct vec2i *_pOutHalfExtents);

/* Get the button sprite for rendering (btn_a_small_00)
 * Returns NULL if sprite is not loaded */
sprite_t *triggers_dialogue_get_button_sprite(void);

/* Render dialogue trigger UI (button prompt above selected trigger) */
void triggers_dialogue_render_ui(void);
