#pragma once

#include "game_objects/gp_state.h"
#include "math2d.h"
#include <stdbool.h>
#include <stdint.h>

/* Save data structure (persisted via EEPROMFS) */
typedef struct
{
    /* Settings */
    int iOverscanPadding;       /* UI overscan border padding */
    bool bTargetLockToggleMode; /* true = toggle mode, false = hold mode */

    int iMusicVolume; /* Music volume (0-100) */
    int iSfxVolume;   /* SFX volume (0-100) */

    bool bPal60Enabled; /* PAL60 mode enabled (PAL systems only) */

    /* Analog stick calibration (min/max values for normalization) */
    int8_t iStickMinX;
    int8_t iStickMaxX;
    int8_t iStickMinY;
    int8_t iStickMaxY;

    /* Gameplay progression / world state from gp_state.* */
    gp_state_persist_t gp;
} SaveData;

/* Initialize save system - call on boot before loading data */
void save_init(void);

/* Load saved data from EEPROM - call on boot after save_init() */
void save_load(void);

/* Save current data to EEPROM - call when START button is pressed */
void save_write(void);

/* Check if a valid save exists */
bool save_exists(void);
bool save_progress_exists(void);

/* Getters for saved data */
int save_get_overscan_padding(void);
bool save_get_target_lock_toggle_mode(void);
int save_get_music_volume(void);
int save_get_sfx_volume(void);
bool save_get_pal60_enabled(void);
void save_get_stick_calibration(int8_t *min_x, int8_t *max_x, int8_t *min_y, int8_t *max_y);

/* Setters for saved data (updates in-memory data, call save_write() to persist) */
void save_set_overscan_padding(int _iPadding);
void save_set_target_lock_toggle_mode(bool _bToggleMode);
void save_set_music_volume(int _iVolume);
void save_set_sfx_volume(int _iVolume);
void save_set_pal60_enabled(bool _bEnabled);
void save_set_stick_calibration(int8_t min_x, int8_t max_x, int8_t min_y, int8_t max_y);

/*
    Sync all current game state to save data - call before save_write()
    (Settings/UI/audio + any other non-gp_state bits you track externally)
*/
void save_sync_settings(int _iOverscanPadding, bool _bTargetLockToggleMode, int _iMusicVolume, int _iSfxVolume, bool _bPal60Enabled);

/*
    gp_state integration helpers:

    - Call save_sync_gp_state_from_module() before save_write()
      to snapshot gp_state.* into the save blob.

    - Call save_apply_gp_state_to_module() after save_load()
      (once gp_state.* is ready) to apply loaded progress back into gp_state.*.
*/
void save_sync_gp_state(void);
void save_load_gp_state(void);

/* Wipe all save data (resets to defaults and clears EEPROM) */
void save_wipe(void);

/* Reset only the gp_state portion to defaults (preserves settings like volume, overscan, etc.) */
void save_reset_gp_state_to_defaults(void);
