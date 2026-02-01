#pragma once

#include "../tilemap.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    SPACE,
    PLANET,
    SURFACE,
    JNR
} gp_state_t;

extern gp_state_t gp_state_current;

typedef struct
{
    struct vec2 saved_position;
    char folder_name[64];
} layer_data_t;

typedef enum
{
    ACT_INTRO,
    ACT_INTRO_RACE,
    ACT_OPENING,
    ACT_MAIN,
    ACT_FINAL,
    ACT_COUNT
} gp_act_t;

/* Unlock flags stored as a bitmask (fast, compact, easy to extend) */
typedef enum
{
    GP_UNLOCK_BULLETS_NORMAL = (1u << 0),
    GP_UNLOCK_BULLETS_UPGRADED = (1u << 1),
    GP_UNLOCK_LASER = (1u << 2),
    GP_UNLOCK_BOMB = (1u << 3),
    GP_UNLOCK_TURBO = (1u << 4),
    GP_UNLOCK_TRACTOR_BEAM = (1u << 5),
    GP_UNLOCK_PIECE_A = (1u << 6),
    GP_UNLOCK_PIECE_B = (1u << 7),
    GP_UNLOCK_PIECE_C = (1u << 8),
    GP_UNLOCK_PIECE_D = (1u << 9),
    GP_UNLOCK_MINIMAP = (1u << 10),

    /* Keep this as the last flag! */
    GP_UNLOCK__LAST_FLAG = GP_UNLOCK_MINIMAP
} gp_unlock_flag_t;

/* Auto-computed mask: covers all bits from 0 to the highest flag bit.
 * Formula: (2 * last_flag) - 1 gives all bits up to and including last_flag.
 * Example: If last_flag = (1<<9), then mask = (1<<10) - 1 = 0x3FF (all 10 bits set) */
#define GP_UNLOCK_KNOWN_MASK ((GP_UNLOCK__LAST_FLAG << 1) - 1)

/* Compile-time check: Ensure GP_UNLOCK__LAST_FLAG is actually the highest bit.
 * This will fail if you add a new flag after GP_UNLOCK_PIECE_D but forget to update __LAST_FLAG. */
_Static_assert(GP_UNLOCK__LAST_FLAG == GP_UNLOCK_MINIMAP, "GP_UNLOCK__LAST_FLAG must be updated when adding new unlock flags!");

/* Currency collection tracking: hash-based system with collision detection */
#define MAX_CURRENCY_COLLECTION_FOLDERS 8 /* Reduced from 16 to fit in 4KB EEPROM (128 bytes instead of 256) */
#define MAX_CURRENCY_PER_FOLDER 64

typedef struct
{
    uint32_t uFolderHash;    /* 32-bit hash of folder name */
    char szSignature[4];     /* First 4 chars of folder name (collision detection) */
    uint64_t uCollectedBits; /* Bitfield: bit N = currency ID (N+1) collected */
} currency_collection_entry_t;

_Static_assert((sizeof(currency_collection_entry_t) % 8) == 0, "currency_collection_entry_t must be 8-byte aligned");

/* Persist snapshot copied to/from save system */
typedef struct
{
    layer_data_t aLayers[4];

    uint8_t uGpStateCurrent; /* gp_state_t */
    uint8_t uAct;            /* gp_act_t */

    uint16_t uUnlockFlags; /* gp_unlock_flag_t bitmask */
    uint16_t uCurrency;    /* currency amount (recommend uint16) */

    float fCurrentPosX;
    float fCurrentPosY;
    float fBestLapTime; /* Best lap time in seconds (0.0f if no best time set) */
    uint16_t uReserved; /* keep 8-byte alignment / future */

    /* Currency collection tracking: 8 folders × 16 bytes = 128 bytes */
    currency_collection_entry_t aCurrencyCollection[MAX_CURRENCY_COLLECTION_FOLDERS];
} gp_state_persist_t;

_Static_assert((sizeof(gp_state_persist_t) % 8) == 0, "gp_state_persist_t must be 8-byte aligned for EEPROM blocks");

/* Persist API (used by save.*) */
void gp_state_get_persist(gp_state_persist_t *_pOut);
void gp_state_set_persist(const gp_state_persist_t *_pIn);

/* Fast runtime accessors (used by game code) */
bool gp_state_unlock_get(uint16_t _uFlag);
void gp_state_unlock_set(uint16_t _uFlag, bool _bEnabled);

uint16_t gp_state_currency_get(void);
void gp_state_currency_set(uint16_t _uAmount);

gp_act_t gp_state_act_get(void);
void gp_state_act_set(gp_act_t _eAct);

struct vec2 gp_state_current_pos_get(void);
void gp_state_current_pos_set(struct vec2 _vPos);

gp_state_t gp_state_get(void);
void gp_state_set(gp_state_t _eState);

gp_state_t gp_state_get_previous(void);

/* Get best lap time in seconds (returns 0.0f if no best time set) */
float gp_state_get_best_lap_time(void);

/* Set best lap time in seconds */
void gp_state_set_best_lap_time(float _fBestLapTime);

/* Returns the current folder name for the current state (read-only). Returns NULL if not set. */
const char *gp_state_get_current_folder(void);

/* Returns true if player modes (UFO, player_surface, player_jnr) should accept input.
 * Returns false during state transitions (landing/launching/fading), when minimap is active,
 * or during cutscene mode. Dialogue and minimap systems continue to work independently. */
bool gp_state_accepts_input(void);

/* Get cutscene mode state. When true, gameplay input is blocked. */
bool gp_state_cutscene_get(void);

/* Set cutscene mode. When true, blocks gameplay input (ufo, weapons, tractor_beam, turbo, player_surface, player_jnr).
 * Dialogue and minimap systems continue to work independently. */
void gp_state_cutscene_set(bool _bActive);

/* Initialize gp_state system (loads UI sprites, etc.). Should be called during game initialization. */
void gp_state_init(void);

/* Initialize the scene/entities for the current gp_state.
 * Should be called after loading save data (gp_state_set_persist) to set up the world. */
void gp_state_init_scene(void);

/* Land: Space -> Planet -> Surface -> JNR. Does nothing if already at JNR. */
void gp_state_land(void);

/* Launch: JNR -> Surface -> Planet -> Space. Does nothing if already at Space. */
void gp_state_launch(void);

/* Update function to be called every frame to apply pending state changes after animations complete.
 * This function handles tilemap operations when the screen is fully dark during transitions. */
void gp_state_update(void);

/* Handle layer switching (c-up/c-down) input. Should be called before gp_state_update. */
void gp_update_handle_layer_switch(bool c_up, bool c_down);

/* Render UI elements for the current game state. Should be called during rendering. */
void gp_state_render_ui(void);

/* Snap camera and sync starfield for space transitions (prevents visual jumps when repositioning).
 * Should be called when repositioning the UFO in SPACE state to avoid camera/starfield snapping.
 * This ensures the camera and starfield are synchronized with the new UFO position. */
void gp_state_snap_space_transition(void);

/* Get direct access to currency collection array (for currency_handler internal use) */
currency_collection_entry_t *gp_state_get_currency_collection_array(void);
