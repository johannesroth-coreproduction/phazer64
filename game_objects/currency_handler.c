#include "currency_handler.h"
#include "../audio.h"
#include "../camera.h"
#include "../csv_helper.h"
#include "../dialogue.h"
#include "../entity2d.h"
#include "../font_helper.h"
#include "../frame_time.h"
#include "../game_objects/gp_camera.h"
#include "../game_objects/gp_state.h"
#include "../game_objects/meteors.h"
#include "../game_objects/space_objects.h"
#include "../game_objects/ufo.h"
#include "../math2d.h"
#include "../minimap.h"
#include "../player_jnr.h"
#include "../player_surface.h"
#include "../resource_helper.h"
#include "../rng.h"
#include "../tilemap.h"
#include "../ui.h"
#include "libdragon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sprite - loaded once and reused */
static sprite_t *m_pCurrencySprite = NULL;
static bool m_bInitialized = false;

/* Sound - loaded once and reused */
static wav64_t *m_pCurrencyCollectSound = NULL;

/* Store current folder name (like tilemap does) */
static char m_szCurrentFolder[256] = {0};

/* Cached collection entry pointer for current folder (avoids repeated hash/search) */
static currency_collection_entry_t *m_pCachedCollectionEntry = NULL;

/* Total currency count for current folder (from CSV) */
static uint8_t m_uTotalCurrencyCount = 0;

/* Collected currency count for current folder (incremented when currency is collected or skipped during refresh) */
static uint8_t m_uCollectedCount = 0;

/* Currency instances */
#define MAX_CURRENCY 64
static CurrencyInstance m_aCurrency[MAX_CURRENCY];
static size_t m_iCurrencyCount = 0;

/* FNV-1a hash function for folder names (32-bit) */
static uint32_t hash_folder_name(const char *pFolder)
{
    if (!pFolder)
        return 0;

    uint32_t uHash = 2166136261u; /* FNV offset basis */
    const char *p = pFolder;

    while (*p)
    {
        uHash ^= (uint32_t)(unsigned char)(*p);
        uHash *= 16777619u; /* FNV prime */
        p++;
    }

    return uHash;
}

/* Get currency collection array from gp_state (for internal use) */
static currency_collection_entry_t *get_currency_collection_array(void)
{
    return gp_state_get_currency_collection_array();
}

/* Find or create collection entry for a folder */
static currency_collection_entry_t *find_or_create_collection_entry(const char *pFolder)
{
    if (!pFolder)
        return NULL;

    currency_collection_entry_t *pArray = get_currency_collection_array();
    if (!pArray)
        return NULL;

    uint32_t uHash = hash_folder_name(pFolder);

    /* Extract signature (first 4 chars) */
    char szSignature[4] = {0, 0, 0, 0};
    size_t uSigLen = strlen(pFolder);
    if (uSigLen > 4)
        uSigLen = 4;
    memcpy(szSignature, pFolder, uSigLen);

    /* Look for existing entry */
    for (size_t i = 0; i < MAX_CURRENCY_COLLECTION_FOLDERS; ++i)
    {
        currency_collection_entry_t *pEntry = &pArray[i];

        /* Empty slot? */
        if (pEntry->uFolderHash == 0)
        {
            /* Create new entry */
            pEntry->uFolderHash = uHash;
            memcpy(pEntry->szSignature, szSignature, 4);
            pEntry->uCollectedBits = 0;
            return pEntry;
        }

        /* Hash matches? */
        if (pEntry->uFolderHash == uHash)
        {
            /* Check signature for collision detection */
            if (memcmp(pEntry->szSignature, szSignature, 4) == 0)
            {
                /* Found matching entry */
                return pEntry;
            }
            else
            {
                /* Hash collision detected - signature mismatch */
                debugf("currency_handler: Hash collision detected for folder '%s' (hash: 0x%08X)\n", pFolder, (unsigned)uHash);
                /* Treat as uncollected (safe fallback) */
                return NULL;
            }
        }
    }

    /* Array full */
    debugf("currency_handler: Currency collection array full, cannot track folder '%s'\n", pFolder);
    return NULL;
}

/* Check if currency is collected */
static bool is_currency_collected(const char *pFolder, uint8_t uCurrencyId)
{
    if (!pFolder || uCurrencyId == 0 || uCurrencyId > MAX_CURRENCY_PER_FOLDER)
        return false;

    currency_collection_entry_t *pEntry = find_or_create_collection_entry(pFolder);
    if (!pEntry)
        return false;

    /* Bit N represents currency ID (N+1), so bit (uCurrencyId-1) */
    uint64_t uBit = (uint64_t)1 << (uCurrencyId - 1);
    return (pEntry->uCollectedBits & uBit) != 0;
}

/* Mark currency as collected */
static void mark_currency_collected(const char *pFolder, uint8_t uCurrencyId)
{
    if (!pFolder || uCurrencyId == 0 || uCurrencyId > MAX_CURRENCY_PER_FOLDER)
        return;

    /* Use cached entry if available (avoids hash/search overhead) */
    currency_collection_entry_t *pEntry = m_pCachedCollectionEntry;

    /* Fallback to lookup if cache is invalid (shouldn't happen in normal flow) */
    if (!pEntry)
    {
        pEntry = find_or_create_collection_entry(pFolder);
        if (!pEntry)
            return;
        m_pCachedCollectionEntry = pEntry; /* Cache for next time */
    }

    /* Bit N represents currency ID (N+1), so bit (uCurrencyId-1) */
    uint64_t uBit = (uint64_t)1 << (uCurrencyId - 1);
    pEntry->uCollectedBits |= uBit;
}

/* Initialize currency handler (loads sprites, called once at startup) */
void currency_handler_init(void)
{
    if (m_bInitialized)
        return;

    /* Load sprite once */
    if (!m_pCurrencySprite)
        m_pCurrencySprite = sprite_load("rom:/currency_00.sprite");

    /* Load currency collect sound */
    if (!m_pCurrencyCollectSound)
        m_pCurrencyCollectSound = wav64_load("rom:/currency_collect.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    currency_handler_reset();
    m_bInitialized = true;
}

/* Refresh currency handler (loads CSV data from folder, called during state switches) */
void currency_handler_refresh(const char *_pFolder, gp_state_t _eState)
{
    if (!m_bInitialized)
    {
        debugf("currency_handler_refresh: Handler not initialized, call currency_handler_init() first\n");
        return;
    }

    if (!_pFolder)
    {
        debugf("currency_handler_refresh: No folder provided\n");
        return;
    }

    /* Check if folder matches (like tilemap does) - skip reload if already loaded */
    if (m_szCurrentFolder[0] != '\0' && strcmp(m_szCurrentFolder, _pFolder) == 0)
    {
        /* Folder matches, currency already loaded - skip reload */
        return;
    }

    /* Store folder name */
    strncpy(m_szCurrentFolder, _pFolder, sizeof(m_szCurrentFolder) - 1);
    m_szCurrentFolder[sizeof(m_szCurrentFolder) - 1] = '\0';

    /* Cache collection entry for this folder (avoids repeated hash/search) */
    m_pCachedCollectionEntry = find_or_create_collection_entry(_pFolder);

    currency_handler_reset();

    /* Build path: rom:/<folder>/currency.csv */
    char szPath[256];
    snprintf(szPath, sizeof(szPath), "rom:/%s/currency.csv", _pFolder);

    /* Load file */
    char *pFileData = NULL;
    size_t uFileSize = 0;
    if (!csv_helper_load_file(szPath, &pFileData, &uFileSize))
    {
        /* File doesn't exist - that's okay, just return */
        debugf("currency_handler_refresh: currency.csv not found in %s (this is okay)\n", _pFolder);
        return;
    }

    /* Ensure collection entry exists for this folder */
    find_or_create_collection_entry(_pFolder);

    /* Parse CSV file line by line */
    char *pLineStart = pFileData;
    char *pLineEnd = NULL;
    uint8_t uCurrencyId = 0; /* Track currency ID (1-based) */

    while ((pLineEnd = strchr(pLineStart, '\n')) != NULL || (pLineStart < pFileData + uFileSize && *pLineStart != '\0'))
    {
        /* Extract line */
        size_t uLineLen = 0;
        if (pLineEnd)
        {
            uLineLen = (size_t)(pLineEnd - pLineStart);
        }
        else
        {
            uLineLen = strlen(pLineStart);
        }

        if (uLineLen == 0)
        {
            if (pLineEnd)
                pLineStart = pLineEnd + 1;
            else
                break;
            continue;
        }

        /* Copy line for tokenizing (strtok modifies the string) */
        char szLine[256];
        if (uLineLen >= sizeof(szLine))
            uLineLen = sizeof(szLine) - 1;
        memcpy(szLine, pLineStart, uLineLen);
        szLine[uLineLen] = '\0';
        csv_helper_strip_eol(szLine);

        /* Skip empty lines */
        if (szLine[0] == '\0')
        {
            if (pLineEnd)
                pLineStart = pLineEnd + 1;
            else
                break;
            continue;
        }

        /* Parse line: name,x,y or ,x,y (name is optional) */
        /* Parse optional name and x,y coordinates */
        char szName[64];
        struct vec2 vPos;
        if (!csv_helper_parse_optional_name_xy(szLine, szName, sizeof(szName), &vPos))
        {
            debugf("currency_handler_refresh: Failed to parse line: %s\n", szLine);
            if (pLineEnd)
                pLineStart = pLineEnd + 1;
            else
                break;
            continue;
        }

        /* Increment currency ID (1-based: 1, 2, 3...) */
        uCurrencyId++;

        /* Update total currency count (count all valid entries, regardless of collection status) */
        m_uTotalCurrencyCount = uCurrencyId;

        /* Check if this currency has already been collected */
        if (is_currency_collected(_pFolder, uCurrencyId))
        {
            /* Skip creating entity for already-collected currency */
            /* Increment collected count since this currency was already collected */
            m_uCollectedCount++;
            if (pLineEnd)
                pLineStart = pLineEnd + 1;
            else
                break;
            continue;
        }

        /* Check currency ID limit */
        if (uCurrencyId > MAX_CURRENCY_PER_FOLDER)
        {
            debugf("currency_handler_refresh: Currency ID %u exceeds max (%u), not tracked\n", (unsigned)uCurrencyId, (unsigned)MAX_CURRENCY_PER_FOLDER);
            if (pLineEnd)
                pLineStart = pLineEnd + 1;
            else
                break;
            continue;
        }

        /* Check if we're in SPACE mode - spawn meteors instead of currency entities */
        if (_eState == SPACE)
        {
            /* Spawn meteor for currency in SPACE mode */
            SpaceObject *pMeteor = space_objects_spawn_meteor(vPos);
            if (!pMeteor)
            {
                debugf("currency_handler_refresh: Failed to spawn meteor for currency ID %u\n", (unsigned)uCurrencyId);
                if (pLineEnd)
                    pLineStart = pLineEnd + 1;
                else
                    break;
                continue;
            }

            /* Get crystal sprite */
            sprite_t *pCrystalSprite = meteors_get_crystal_sprite();
            if (!pCrystalSprite)
            {
                debugf("currency_handler_refresh: Crystal sprite not loaded\n");
                if (pLineEnd)
                    pLineStart = pLineEnd + 1;
                else
                    break;
                continue;
            }

            /* Configure entity */
            uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
            uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;
            entity2d_init_from_sprite(&pMeteor->entity, vPos, pCrystalSprite, uFlags, uLayerMask);

            pMeteor->entity.fAngleRad = rngf(-FM_PI, FM_PI);
            pMeteor->entity.vVel = vec2_make(0.0f, 0.0f); /* Currency meteors don't move initially */
            pMeteor->entity.iCollisionRadius = 12;

            pMeteor->data.meteor.fRotationSpeed = rngf(-CURRENCY_METEOR_MAX_ROT_SPEED, CURRENCY_METEOR_MAX_ROT_SPEED);
            pMeteor->data.meteor.fTintFrames = 0.0f;
            pMeteor->data.meteor.iFramesAlive = 0;
            pMeteor->data.meteor.uCurrencyId = uCurrencyId;

            /* Set high hit points for currency meteors */
            pMeteor->iHitPoints = METEOR_CURRENCY_HITPOINTS;
            pMeteor->bSleeping = false;
        }
        else
        {
            /* Non-SPACE mode: create currency entity directly */
            /* Add currency instance */
            if (m_iCurrencyCount >= MAX_CURRENCY)
            {
                debugf("currency_handler_refresh: Currency array full\n");
                if (pLineEnd)
                    pLineStart = pLineEnd + 1;
                else
                    break;
                continue;
            }

            CurrencyInstance *pCurrency = &m_aCurrency[m_iCurrencyCount];

            if (!m_pCurrencySprite)
            {
                debugf("currency_handler_refresh: Currency sprite not loaded\n");
                if (pLineEnd)
                    pLineStart = pLineEnd + 1;
                else
                    break;
                continue;
            }

            /* Initialize entity from pre-loaded sprite */
            uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
            uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;

            entity2d_init_from_sprite(&pCurrency->entity, vPos, m_pCurrencySprite, uFlags, uLayerMask);

            /* Store currency ID for collection tracking */
            pCurrency->uCurrencyId = uCurrencyId;

            m_iCurrencyCount++;
        }

        /* Move to next line */
        if (pLineEnd)
            pLineStart = pLineEnd + 1;
        else
            break;
    }

    free(pFileData);
}

/* Reset currency handler (clears all currency instances) */
void currency_handler_reset(void)
{
    /* Reset total currency count and collected count for this folder */
    m_uTotalCurrencyCount = 0;
    m_uCollectedCount = 0;

    m_iCurrencyCount = 0;
    /* Note: We don't clear m_szCurrentFolder here - it persists like tilemap folder */
    /* Note: m_pCachedCollectionEntry persists across resets (same folder) */
    /* Note: m_uTotalCurrencyCount persists across resets (same folder) */
}

/* Check if all currency has been collected for the current folder */
bool currency_handler_is_all_collected(void)
{
    if (!m_bInitialized || m_szCurrentFolder[0] == '\0' || m_uTotalCurrencyCount == 0)
    {
        /* No currency to check, or folder not loaded - consider it "all collected" */
        return true;
    }

    /* Simple comparison: collected count equals total count */
    return m_uCollectedCount == m_uTotalCurrencyCount;
}

/* Spawn currency entity from destroyed meteor (called by meteor_apply_damage) */
void currency_handler_spawn_from_meteor(struct vec2 vPos, uint8_t uCurrencyId)
{
    if (!m_bInitialized || !m_pCurrencySprite)
        return;

    /* Find inactive slot or use next available */
    size_t iIndex = SIZE_MAX;
    for (size_t i = 0; i < m_iCurrencyCount; ++i)
    {
        if (!entity2d_is_active(&m_aCurrency[i].entity))
        {
            iIndex = i;
            break;
        }
    }

    /* If no inactive slot and array not full, use next slot */
    if (iIndex == SIZE_MAX)
    {
        if (m_iCurrencyCount >= MAX_CURRENCY)
        {
            debugf("currency_handler_spawn_from_meteor: Currency array full\n");
            return;
        }
        iIndex = m_iCurrencyCount;
        m_iCurrencyCount++;
    }

    CurrencyInstance *pCurrency = &m_aCurrency[iIndex];

    /* Initialize entity from pre-loaded sprite */
    uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
    uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;
    entity2d_init_from_sprite(&pCurrency->entity, vPos, m_pCurrencySprite, uFlags, uLayerMask);

    /* Store currency ID */
    pCurrency->uCurrencyId = uCurrencyId;
    pCurrency->entity.vVel = vec2_zero();
}

/* Free currency handler (frees sprites and clears currency instances) */
void currency_handler_free(void)
{
    currency_handler_reset();

    SAFE_FREE_SPRITE(m_pCurrencySprite);
    SAFE_CLOSE_WAV64(m_pCurrencyCollectSound);
    m_bInitialized = false;
}

/* Update currency handler (check collisions with player) */
void currency_handler_update(void)
{
    gp_state_t currentState = gp_state_get();

    const struct entity2D *pPlayerEntity = NULL;
    if (currentState == SURFACE)
    {
        /* Get player_surface entity for SURFACE mode */
        pPlayerEntity = player_surface_get_entity();
    }
    else if (currentState == JNR)
    {
        /* Get player_jnr entity for JNR mode */
        pPlayerEntity = player_jnr_get_entity();
    }
    else if (currentState == SPACE)
    {
        /* Get UFO entity for SPACE mode */
        pPlayerEntity = ufo_get_entity();
    }
    else
    {
        /* Only check collisions in SURFACE, JNR, or SPACE mode (not PLANET) */
        return;
    }

    if (!pPlayerEntity || !entity2d_is_collidable(pPlayerEntity))
        return;

    for (size_t i = 0; i < m_iCurrencyCount; ++i)
    {
        CurrencyInstance *pCurrency = &m_aCurrency[i];

        if (!entity2d_is_active(&pCurrency->entity))
            continue;

        if (!entity2d_is_collidable(&pCurrency->entity))
            continue;

        /* In SURFACE mode, check collision in distorted screen space to match visual representation */
        if (currentState == SURFACE && g_mainTilemap.bInitialized)
        {
            /* Convert both positions to distorted screen space */
            struct vec2i vCurrencyScreen, vPlayerScreen;
            if (!tilemap_world_to_screen_distorted(pCurrency->entity.vPos, &vCurrencyScreen))
                continue; /* currency not visible */
            if (!tilemap_world_to_screen_distorted(pPlayerEntity->vPos, &vPlayerScreen))
                continue; /* player not visible */

            /* Check collision in distorted screen space using shared helper */
            bool bIsColliding = entity2d_check_collision_circle_screen(vCurrencyScreen, pCurrency->entity.iCollisionRadius, vPlayerScreen, pPlayerEntity->iCollisionRadius);

            /* Update collision state and handle events */
            bool bWasColliding = pCurrency->entity.bWasColliding;
            pCurrency->entity.bWasColliding = bIsColliding;

            if (!bWasColliding && bIsColliding)
            {
                /* OnTriggerEnter: collision just started */
                entity2d_deactivate(&pCurrency->entity);

                /* Mark currency as collected */
                if (m_szCurrentFolder[0] != '\0' && pCurrency->uCurrencyId > 0)
                {
                    mark_currency_collected(m_szCurrentFolder, pCurrency->uCurrencyId);
                    /* Increment collected count */
                    m_uCollectedCount++;
                }

                /* Increase currency in gp_state */
                uint16_t uCurrentCurrency = gp_state_currency_get();
                gp_state_currency_set(uCurrentCurrency + 1);

                /* Play currency collect sound */
                if (m_pCurrencyCollectSound)
                    wav64_play(m_pCurrencyCollectSound, MIXER_CHANNEL_ITEMS);
            }
        }
        else
        {
            /* JNR or SPACE mode: use standard world-space collision detection */
            CollisionEvents events = entity2d_check_collision_and_update(&pCurrency->entity, pPlayerEntity);

            /* Handle collision events */
            if (events.bOnTriggerEnter)
            {
                /* Disable currency entity */
                entity2d_deactivate(&pCurrency->entity);

                /* Mark currency as collected */
                if (m_szCurrentFolder[0] != '\0' && pCurrency->uCurrencyId > 0)
                {
                    mark_currency_collected(m_szCurrentFolder, pCurrency->uCurrencyId);
                    /* Increment collected count */
                    m_uCollectedCount++;
                }

                /* Increase currency in gp_state */
                uint16_t uCurrentCurrency = gp_state_currency_get();
                gp_state_currency_set(uCurrentCurrency + 1);

                /* Play currency collect sound */
                if (m_pCurrencyCollectSound)
                    wav64_play(m_pCurrencyCollectSound, MIXER_CHANNEL_ITEMS);
            }
        }
    }
}

/* Render currency instances */
void currency_handler_render(void)
{
    gp_state_t currentState = gp_state_get();
    float fZoom = camera_get_zoom(&g_mainCamera);

    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);

    for (size_t i = 0; i < m_iCurrencyCount; ++i)
    {
        const CurrencyInstance *pCurrency = &m_aCurrency[i];
        const struct entity2D *pEnt = &pCurrency->entity;

        if (!entity2d_is_visible(pEnt) || !pEnt->pSprite)
            continue;

        if (currentState == SURFACE || currentState == PLANET)
        {
            /* In SURFACE/PLANET mode, render to intermediate surface using wrapped camera coordinates */
            /* Must use tilemap_world_to_surface instead of camera_world_to_screen to match tilemap rendering */

            /* Ensure both positions are in canonical wrapped space for consistent delta calculation */
            struct vec2 vCurrencyWrapped = pEnt->vPos;
            struct vec2 vCamWrapped = g_mainCamera.vPos;
            if (g_mainTilemap.bInitialized)
            {
                vCurrencyWrapped.fX = tilemap_wrap_world_x(vCurrencyWrapped.fX);
                vCamWrapped.fX = tilemap_wrap_world_x(vCamWrapped.fX);
            }

            /* Calculate wrapped delta to find shortest rendering path */
            struct vec2 vDelta = gp_camera_calc_wrapped_delta(vCamWrapped, vCurrencyWrapped);
            struct vec2 vAdjustedPos = vec2_add(vCamWrapped, vDelta);

            /* Check visibility using adjusted position with margin to account for sprite size */
            if (!gp_camera_is_point_visible_wrapped(&g_mainCamera, vAdjustedPos, (float)pEnt->vHalf.iX * 3.0f))
                continue; /* position not visible */

            /* Convert adjusted position from world to surface (undistorted intermediate buffer) */
            struct vec2i vSurfacePos;
            if (!tilemap_world_to_surface(vAdjustedPos, &vSurfacePos))
                continue; /* position outside surface bounds */

            /* Set filter based on zoom */
            if (fZoom != 1.0f)
                rdpq_mode_filter(FILTER_BILINEAR);
            else
                rdpq_mode_filter(FILTER_POINT);

            /* Render to surface - distortion will be applied when surface is composited to screen */
            rdpq_blitparms_t parms = {.cx = pEnt->vHalf.iX, .cy = pEnt->vHalf.iY, .scale_x = fZoom, .scale_y = fZoom};
            rdpq_sprite_blit(pEnt->pSprite, vSurfacePos.iX, vSurfacePos.iY, &parms);
        }
        else if (currentState == SPACE)
        {
            entity2d_render_simple(pEnt);
        }
        else
        {
            /* JNR mode: Use quantized rendering to prevent sub-pixel wobble against tilemap */
            entity2d_render_simple_quantized(pEnt);
        }
    }
}

/* Render currency UI (sprite and amount in lower right corner) */
void currency_handler_render_ui(void)
{
    if (!m_bInitialized || !m_pCurrencySprite)
        return;

    /* Skip UI rendering if minimap is active */
    if (minimap_is_active())
        return;

    uint16_t uCurrency = gp_state_currency_get();
    bool bBulletsUnlocked = gp_state_unlock_get(GP_UNLOCK_BULLETS_NORMAL);

    /* Render if player has bullets (normal) unlocked OR if currency > 0 */
    if (!bBulletsUnlocked && uCurrency == 0)
        return;

    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1); /* draw pixels with alpha >= 1 (colorkey style) */

    /* Get position for sprite in lower right corner */
    struct vec2i vSpritePos = ui_get_pos_bottom_right_sprite(m_pCurrencySprite);

    vSpritePos.iX -= 5;

    /* Render sprite */
    rdpq_sprite_blit(m_pCurrencySprite, vSpritePos.iX, vSpritePos.iY, NULL);

    /* Render currency amount next to sprite */
    char szCurrencyText[16];
    snprintf(szCurrencyText, sizeof(szCurrencyText), "%u", uCurrency);

    /* Position text to the left of the sprite */
    int iTextX = vSpritePos.iX - (int)font_helper_get_text_width(FONT_NORMAL, szCurrencyText) - 4; /* 4px spacing */
    int iTextY = vSpritePos.iY + UI_FONT_Y_OFFSET + 1;                                             /* Align with sprite vertically */

    rdpq_text_printf(NULL, FONT_NORMAL, iTextX, iTextY, "%s", szCurrencyText);
}
