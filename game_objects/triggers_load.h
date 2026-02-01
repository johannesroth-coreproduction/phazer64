#pragma once

#include "../triggers.h"

/* Initialize load triggers from load.csv in the current planet folder
 * _pPlanetFolder: Name of the planet folder (e.g., "terraria")
 * Returns true if successful, false on error */
bool triggers_load_init(const char *_pPlanetFolder);

/* Free load triggers */
void triggers_load_free(void);

/* Update load trigger collision checks
 * SURFACE mode: Checks collision with player_surface (triggers enter JNR areas via C_DOWN)
 * JNR mode: Checks collision with player_jnr (triggers exit to SURFACE via C_UP) */
void triggers_load_update(void);

/* Get the cached display name of the currently selected trigger (uppercase).
 * Returns NULL if no trigger is selected.
 */
const char *triggers_load_get_selected_display_name(void);

/* Get the data name of the currently selected trigger (original name for loading).
 * Returns NULL if no trigger is selected.
 */
const char *triggers_load_get_selected_data_name(void);

/* Get the trigger collection (for internal use by gp_state rendering) */
trigger_collection_t *triggers_load_get_collection(void);

/* Get selected trigger world position and half extents
 * Returns true if trigger is selected and valid, false otherwise
 * Outputs are written to _pOutCenter and _pOutHalfExtents if successful */
bool triggers_load_get_selected_pos_and_size(struct vec2 *_pOutCenter, struct vec2i *_pOutHalfExtents);
