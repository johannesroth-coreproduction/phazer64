#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Speakers supported by the dialogue system. */
typedef enum dialogue_speaker
{
    DIALOGUE_SPEAKER_BOY = 0,
    DIALOGUE_SPEAKER_RHINO,
    DIALOGUE_SPEAKER_ALIEN,
    DIALOGUE_SPEAKER_COUNT
} dialogue_speaker_t;

/* Position of the dialogue box: bottom (0) or top (1). Matches CSV position column. */
typedef enum dialogue_position
{
    DIALOGUE_POSITION_BOTTOM = 0,
    DIALOGUE_POSITION_TOP = 1
} dialogue_position_t;

/* Initialize dialogue system and preload dialogue UI/portrait sprites. */
bool dialogue_init(void);

/* Free dialogue resources. */
void dialogue_free(void);

/* Start a dialogue from a CSV file located in the current data folder. */
bool dialogue_start(const char *_pszCsvFilename);

/* Update state, typewriter, and handle skip/advance inputs (call once per frame). */
void dialogue_update(bool _bAdvancePressed, bool _bAdvanceDown);

/* Render the current dialogue box and text (call after world/UI rendering). */
void dialogue_render(void);

/* Returns true if a dialogue is currently active (use to pause other logic). */
bool dialogue_is_active(void);

/* Get current dialogue entry index (0-based, returns -1 if no dialogue active). */
int dialogue_get_current_entry_index(void);