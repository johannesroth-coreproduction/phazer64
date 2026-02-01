#include "save.h"
#include "csv_helper.h"
#include "eepromfs.h"
#include "libdragon.h"
#include "stick_normalizer.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
    EEPROMFS notes (per libdragon docs):
    - Files always exist at the size specified during eepfs_init.
    - "Erasing" means writing the whole file as zeroes.
    - eepfs_verify_signature validates only the filesystem layout, not the file contents.
*/

/* EEPROM file path for save data (keep stable across builds) */
#define SAVE_FILE_NAME "/save.dat"

/* Save blob header with CRC32 checksum for data integrity */
#define SAVE_BLOB_MAGIC (0x53415645u) /* 'S''A''V''E' */
#define SAVE_BLOB_VERSION (5u)        /* Bumped from 4: added currency collection array */

/* In-memory save data */
static SaveData s_saveData = {
    .iOverscanPadding = 0,
    .bTargetLockToggleMode = false,

    .iMusicVolume = 100,
    .iSfxVolume = 100,

    .bPal60Enabled = false,

    .iStickMinX = -STICK_NORMALIZED_MAX,
    .iStickMaxX = STICK_NORMALIZED_MAX,
    .iStickMinY = -STICK_NORMALIZED_MAX,
    .iStickMaxY = STICK_NORMALIZED_MAX,

    .gp =
        {
            .aLayers =
                {
                    [SPACE] = {.saved_position = {.fX = 0.0f, .fY = 0.0f}, .folder_name = "space"},
                    [PLANET] = {.saved_position = {.fX = 0.0f, .fY = 0.0f}, .folder_name = ""},
                    [SURFACE] = {.saved_position = {.fX = 0.0f, .fY = 0.0f}, .folder_name = ""},
                    [JNR] = {.saved_position = {.fX = 0.0f, .fY = 0.0f}, .folder_name = ""},
                },
            .uGpStateCurrent = (uint8_t)SPACE,
            .uAct = (uint8_t)ACT_INTRO,
            .uUnlockFlags = 0,
            .uCurrency = 0,
            .uReserved = 0,
            .fCurrentPosX = 0.0f,
            .fCurrentPosY = 0.0f,
        },
};

/* Flag to track if save system is initialized */
static bool s_bInitialized = false;

typedef struct
{
    uint32_t uMagic;
    uint16_t uVersion;
    /* Compiler will add 2 bytes of padding here to align SaveData to 8-byte boundary */
    SaveData data;
    uint32_t uChecksum; /* CRC32 of data field (protects against bit rot/corruption) */
    uint32_t uReserved; /* padding to ensure total struct size is a multiple of 8 bytes */
} SaveBlob;

_Static_assert((sizeof(SaveBlob) % 8) == 0, "SaveBlob size must be a multiple of 8 bytes for EEPROM blocks");

/* Simple CRC32 implementation for save data integrity checking */
static uint32_t calculate_crc32(const void *pData, size_t uSize)
{
    static const uint32_t CRC32_POLY = 0xEDB88320u;

    uint32_t uCrc = 0xFFFFFFFFu;
    const uint8_t *pBytes = (const uint8_t *)pData;

    for (size_t i = 0; i < uSize; i++)
    {
        uCrc ^= pBytes[i];
        for (int j = 0; j < 8; j++)
        {
            uCrc = (uCrc >> 1) ^ (CRC32_POLY & -(uCrc & 1));
        }
    }

    return ~uCrc;
}

/* Calculate CRC32 checksum for save data integrity */
static uint32_t calculate_checksum(const SaveData *pData)
{
    return calculate_crc32(pData, sizeof(SaveData));
}

/* Helper: defaults for gp payload */
static void gp_reset_to_defaults(gp_state_persist_t *_pGp)
{
    if (!_pGp)
    {
        return;
    }

    memset(_pGp, 0, sizeof(*_pGp));
    /* Note: memset zeros everything, but we explicitly set fields below for clarity/documentation */

    _pGp->uGpStateCurrent = (uint8_t)SPACE;
    _pGp->uAct = (uint8_t)ACT_INTRO;
    _pGp->uUnlockFlags = 0;
    _pGp->uCurrency = 0;
    _pGp->fBestLapTime = 0.0f;
    _pGp->uReserved = 0;

    /* Currency collection array: all entries zeroed (no collected currency)
     * Array size: 8 folders × 16 bytes = 128 bytes */
    memset(_pGp->aCurrencyCollection, 0, sizeof(_pGp->aCurrencyCollection));

    /* Load spawn position from space folder CSV as default starting position */
    struct vec2 vSpaceSpawn = {0.0f, 0.0f};
    if (csv_helper_load_spawn_position("space", &vSpaceSpawn))
    {
        _pGp->fCurrentPosX = vSpaceSpawn.fX;
        _pGp->fCurrentPosY = vSpaceSpawn.fY;
    }
    else
    {
        /* Fallback to origin if spawn position not found */
        _pGp->fCurrentPosX = 0.0f;
        _pGp->fCurrentPosY = 0.0f;
    }

    /* Set SPACE folder name and saved position from spawn */
    strncpy(_pGp->aLayers[SPACE].folder_name, "space", sizeof(_pGp->aLayers[SPACE].folder_name) - 1);
    _pGp->aLayers[SPACE].folder_name[sizeof(_pGp->aLayers[SPACE].folder_name) - 1] = '\0';
    _pGp->aLayers[SPACE].saved_position = vSpaceSpawn;
}

/* Helper function to reset save data to default values */
static void reset_to_defaults(void)
{
    s_saveData.iOverscanPadding = 0;
    s_saveData.bTargetLockToggleMode = false;

    s_saveData.iMusicVolume = 100;
    s_saveData.iSfxVolume = 100;

    s_saveData.bPal60Enabled = false;

    s_saveData.iStickMinX = -STICK_NORMALIZED_MAX;
    s_saveData.iStickMaxX = STICK_NORMALIZED_MAX;
    s_saveData.iStickMinY = -STICK_NORMALIZED_MAX;
    s_saveData.iStickMaxY = STICK_NORMALIZED_MAX;

    gp_reset_to_defaults(&s_saveData.gp);
}

/* Helper function to initialize EEPROM filesystem */
static bool init_eeprom_filesystem(void)
{
    static const eepfs_entry_t entries[] = {
        {SAVE_FILE_NAME, sizeof(SaveBlob)},
    };

    const int iResult = eepfs_init(entries, 1);
    if (iResult != EEPFS_ESUCCESS)
    {
        /* If EEPROM does not have enough space for the configured layout, eepfs_init will fail. */
        debugf("EEPROMFS init failed (%d)\n", iResult);
        return false;
    }

    return true;
}

/* Helper function to ensure save system is initialized */
static bool ensure_initialized(void)
{
    if (!s_bInitialized)
    {
        save_init();
    }
    return s_bInitialized;
}

/* Validation helpers */
static bool is_valid_folder_name(const char *_pName, size_t _uMaxLen)
{
    if (!_pName || _uMaxLen == 0)
    {
        return false;
    }

    /* Must be NUL-terminated within the fixed buffer */
    if (!memchr(_pName, '\0', _uMaxLen))
    {
        return false;
    }

    /* Allow empty string */
    for (size_t i = 0; i < _uMaxLen; i++)
    {
        const unsigned char c = (unsigned char)_pName[i];
        if (c == '\0')
        {
            break;
        }

        /* Conservative: printable ASCII only (space to ~). Adjust if you need UTF-8 later. */
        if (c < 32 || c > 126)
        {
            return false;
        }
    }

    return true;
}

static bool is_valid_gp_persist(const gp_state_persist_t *_pGp)
{
    if (!_pGp)
    {
        return false;
    }

    if (_pGp->uGpStateCurrent >= 4u)
    {
        return false;
    }

    if (_pGp->uAct >= (uint8_t)ACT_COUNT)
    {
        return false;
    }

    /* Require unlock flags to only use known bits (forces version bump when adding more) */
    if ((_pGp->uUnlockFlags & (uint16_t)~GP_UNLOCK_KNOWN_MASK) != 0)
    {
        return false;
    }

    /* NaN checks */
    if (_pGp->fCurrentPosX != _pGp->fCurrentPosX)
        return false;
    if (_pGp->fCurrentPosY != _pGp->fCurrentPosY)
        return false;
    if (_pGp->fBestLapTime != _pGp->fBestLapTime)
        return false;

    for (int i = 0; i < 4; i++)
    {
        const layer_data_t *pL = &_pGp->aLayers[i];

        if (pL->saved_position.fX != pL->saved_position.fX)
            return false;
        if (pL->saved_position.fY != pL->saved_position.fY)
            return false;

        if (!is_valid_folder_name(pL->folder_name, sizeof(pL->folder_name)))
            return false;
    }

    return true;
}

/* Helper function to validate save payload */
static bool is_valid_save_data(const SaveData *pData)
{
    if (!pData)
    {
        return false;
    }

    /* Basic range checks */
    if (pData->iMusicVolume < 0 || pData->iMusicVolume > 100)
        return false;
    if (pData->iSfxVolume < 0 || pData->iSfxVolume > 100)
        return false;
    if (pData->iOverscanPadding < -64 || pData->iOverscanPadding > 64)
        return false;

    /* Stick calibration validation */
    if (pData->iStickMaxX < STICK_CALIBRATION_MIN_THRESHOLD || pData->iStickMaxX > STICK_CALIBRATION_MAX_RANGE)
        return false;
    if (pData->iStickMinX > -STICK_CALIBRATION_MIN_THRESHOLD || pData->iStickMinX < -STICK_CALIBRATION_MAX_RANGE)
        return false;
    if (pData->iStickMaxY < STICK_CALIBRATION_MIN_THRESHOLD || pData->iStickMaxY > STICK_CALIBRATION_MAX_RANGE)
        return false;
    if (pData->iStickMinY > -STICK_CALIBRATION_MIN_THRESHOLD || pData->iStickMinY < -STICK_CALIBRATION_MAX_RANGE)
        return false;

    if (!is_valid_gp_persist(&pData->gp))
        return false;

    return true;
}

static bool is_valid_blob(const SaveBlob *pBlob)
{
    if (!pBlob)
    {
        return false;
    }

    if (pBlob->uMagic != SAVE_BLOB_MAGIC)
    {
        return false;
    }

    if (pBlob->uVersion != SAVE_BLOB_VERSION)
    {
        return false;
    }

    /* Verify checksum before validating data contents */
    const uint32_t uExpectedChecksum = calculate_checksum(&pBlob->data);
    if (pBlob->uChecksum != uExpectedChecksum)
    {
        debugf("Save checksum mismatch\n");
        return false;
    }

    return is_valid_save_data(&pBlob->data);
}

/*
    Read-only existence check:
    - validates EEPROMFS signature (layout)
    - reads blob
    - validates blob payload
*/
static bool save_peek_is_valid(void)
{
    if (!ensure_initialized())
    {
        return false;
    }

#ifndef SKIP_EEPROM_INTEGRITY_CHECK
    if (!eepfs_verify_signature())
    {
        return false;
    }
#endif

    SaveBlob blob;
    if (eepfs_read(SAVE_FILE_NAME, &blob, sizeof(blob)) != EEPFS_ESUCCESS)
    {
        return false;
    }

    return is_valid_blob(&blob);
}

/* Wipe EEPROMFS and seed a valid default blob to prevent "all-zero loads" */
static void wipe_and_seed_defaults(void)
{
    reset_to_defaults();

    if (!ensure_initialized())
    {
        return;
    }

    eepfs_wipe();
    debugf("EEPROMFS wiped - seeding defaults\n");

    /* Persist defaults immediately so next boot can't read zeros */
    save_write();
}

void save_init(void)
{
    /* Check if EEPROM is present first */
    const eeprom_type_t eepromType = eeprom_present();
    if (eepromType == EEPROM_NONE)
    {
        /* No EEPROM present - save system will not work */
        debugf("EEPROM not present\n");
        return;
    }

    if (init_eeprom_filesystem())
    {
        s_bInitialized = true;
    }
    else
    {
        debugf("EEPROMFS initialization failed - save system disabled\n");
    }
}

void save_load(void)
{
    if (!ensure_initialized())
    {
        debugf("EEPROM not available, keeping defaults\n");
        gp_reset_to_defaults(&s_saveData.gp);
        return;
    }

#ifndef SKIP_EEPROM_INTEGRITY_CHECK
    if (!eepfs_verify_signature())
    {
        debugf("EEPROMFS signature mismatch - wiping and reseeding defaults\n");
        wipe_and_seed_defaults();
        return;
    }
#endif

    SaveBlob blob;
    const int iResult = eepfs_read(SAVE_FILE_NAME, &blob, sizeof(blob));
    if (iResult != EEPFS_ESUCCESS)
    {
        debugf("EEPROMFS read failed (%d) - reseeding defaults (no wipe)\n", iResult);
        reset_to_defaults();
        save_write();
        return;
    }

    if (!is_valid_blob(&blob))
    {
        debugf("Save blob invalid (magic/version/data) - wiping and reseeding defaults\n");
        wipe_and_seed_defaults();
        return;
    }

    s_saveData = blob.data;
    debugf("Loaded save data (v%u)\n", (unsigned)blob.uVersion);
}

bool save_exists(void)
{
    return save_peek_is_valid();
}

bool save_progress_exists(void)
{
    if (!save_exists())
    {
        return false;
    }

    if (s_saveData.gp.uAct != (uint8_t)ACT_INTRO)
        return true;

    return false;
}

void save_write(void)
{
    if (!ensure_initialized())
    {
        return;
    }

    SaveBlob blob;
    memset(&blob, 0, sizeof(blob));
    blob.uMagic = SAVE_BLOB_MAGIC;
    blob.uVersion = SAVE_BLOB_VERSION;
    blob.data = s_saveData;
    blob.uChecksum = calculate_checksum(&blob.data);

    const int iResult = eepfs_write(SAVE_FILE_NAME, &blob, sizeof(blob));
    if (iResult == EEPFS_ESUCCESS)
    {
        debugf("Saved save data (v%u)\n", (unsigned)blob.uVersion);
    }
    else
    {
        debugf("Failed to save save data (%d)\n", iResult);
    }
}

/* gp_state integration */
void save_sync_gp_state(void)
{
    /* Snapshot gp_state.* into the in-memory save struct */
    gp_state_get_persist(&s_saveData.gp);
}

void save_load_gp_state(void)
{
    /* Apply loaded gp snapshot back into gp_state.* */
    gp_state_set_persist(&s_saveData.gp);
}

/* Getters */
int save_get_overscan_padding(void)
{
    return s_saveData.iOverscanPadding;
}

bool save_get_target_lock_toggle_mode(void)
{
    return s_saveData.bTargetLockToggleMode;
}

int save_get_music_volume(void)
{
    return s_saveData.iMusicVolume;
}

int save_get_sfx_volume(void)
{
    return s_saveData.iSfxVolume;
}

bool save_get_pal60_enabled(void)
{
    return s_saveData.bPal60Enabled;
}

/* Setters */
void save_set_overscan_padding(int _iPadding)
{
    s_saveData.iOverscanPadding = _iPadding;
}

void save_set_target_lock_toggle_mode(bool _bToggleMode)
{
    s_saveData.bTargetLockToggleMode = _bToggleMode;
}

void save_set_music_volume(int _iVolume)
{
    s_saveData.iMusicVolume = _iVolume;
}

void save_set_sfx_volume(int _iVolume)
{
    s_saveData.iSfxVolume = _iVolume;
}

void save_set_pal60_enabled(bool _bEnabled)
{
    s_saveData.bPal60Enabled = _bEnabled;
}

void save_sync_settings(int _iOverscanPadding, bool _bTargetLockToggleMode, int _iMusicVolume, int _iSfxVolume, bool _bPal60Enabled)
{
    s_saveData.iOverscanPadding = _iOverscanPadding;
    s_saveData.bTargetLockToggleMode = _bTargetLockToggleMode;
    s_saveData.iMusicVolume = _iMusicVolume;
    s_saveData.iSfxVolume = _iSfxVolume;
    s_saveData.bPal60Enabled = _bPal60Enabled;
}

void save_get_stick_calibration(int8_t *min_x, int8_t *max_x, int8_t *min_y, int8_t *max_y)
{
    if (min_x)
        *min_x = s_saveData.iStickMinX;
    if (max_x)
        *max_x = s_saveData.iStickMaxX;
    if (min_y)
        *min_y = s_saveData.iStickMinY;
    if (max_y)
        *max_y = s_saveData.iStickMaxY;
}

void save_set_stick_calibration(int8_t min_x, int8_t max_x, int8_t min_y, int8_t max_y)
{
    s_saveData.iStickMinX = min_x;
    s_saveData.iStickMaxX = max_x;
    s_saveData.iStickMinY = min_y;
    s_saveData.iStickMaxY = max_y;
}

void save_wipe(void)
{
    debugf("Wiping save data (EEPROMFS)\n");
    wipe_and_seed_defaults();
}

/* Reset only the gp_state portion to defaults (preserves settings like volume, overscan, etc.) */
void save_reset_gp_state_to_defaults(void)
{
    gp_reset_to_defaults(&s_saveData.gp);
}
