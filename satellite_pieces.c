#include "satellite_pieces.h"
#include "audio.h"
#include "camera.h"
#include "csv_helper.h"
#include "entity2d.h"
#include "frame_time.h"
#include "game_objects/gp_state.h"
#include "game_objects/tractor_beam.h"
#include "game_objects/ufo.h"
#include "libdragon.h"
#include "math2d.h"
#include "poi.h"
#include "rdpq.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "resource_helper.h"
#include "rng.h"
#include "ui.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sprite path defines */
#define SPRITE_PATH_CENTER "rom:/satellite_piece_center_00.sprite"
#define SPRITE_PATH_EAST "rom:/satellite_piece_east_00.sprite"
#define SPRITE_PATH_NORTH "rom:/satellite_piece_north_00.sprite"
#define SPRITE_PATH_WEST "rom:/satellite_piece_west_00.sprite"
#define SPRITE_PATH_SOUTH "rom:/satellite_piece_south_00.sprite"

#define SPRITE_PATH_EAST_MISSING "rom:/satellite_piece_east_missing_00.sprite"
#define SPRITE_PATH_NORTH_MISSING "rom:/satellite_piece_north_missing_00.sprite"
#define SPRITE_PATH_WEST_MISSING "rom:/satellite_piece_west_missing_00.sprite"
#define SPRITE_PATH_SOUTH_MISSING "rom:/satellite_piece_south_missing_00.sprite"

/* Sprite pointers */
static sprite_t *s_pSpriteCenter = NULL;
static sprite_t *s_pSpriteNorth = NULL;
static sprite_t *s_pSpriteEast = NULL;
static sprite_t *s_pSpriteSouth = NULL;
static sprite_t *s_pSpriteWest = NULL;

/* Missing sprite pointers (for uncollected pieces) */
static sprite_t *s_pSpriteNorthMissing = NULL;
static sprite_t *s_pSpriteEastMissing = NULL;
static sprite_t *s_pSpriteSouthMissing = NULL;
static sprite_t *s_pSpriteWestMissing = NULL;

/* Sound effect pointers */
static wav64_t *s_pSoundPieceCollect = NULL;
static wav64_t *s_pSoundPieceConnect = NULL;
static wav64_t *s_pSoundSatelliteRepaired = NULL;

/* Satellite repair POI position (loaded from space folder) */
static struct vec2 s_vSatelliteRepairPos = {0};

/* Track which pieces are snapped into place */
static bool s_aPiecesSnapped[PIECE_DIR_COUNT] = {false};

/* Individual distances from center piece to directional pieces (center-to-center) */
#define PIECE_DISTANCE_NORTH 48
#define PIECE_DISTANCE_EAST 64
#define PIECE_DISTANCE_SOUTH 48
#define PIECE_DISTANCE_WEST 64

/* Movement damping for pieces after impacts */
#define PIECE_VELOCITY_DAMPING 0.96f /* Similar to NPC decay - gradual slowdown */
#define PIECE_SLEEP_VEL_SQ 1e-6f
#define PIECE_ROT_SPEED 0.015f /* 50% of METEOR_MAX_ROT_SPEED */

/* Snapping thresholds for assemble mode */
#define PIECE_SNAP_DISTANCE 6.0f
#define PIECE_SNAP_ROTATION_DEG 15.0f
#define PIECE_SNAP_ROTATION_RAD (PIECE_SNAP_ROTATION_DEG * 3.14159265359f / 180.0f)

/* Individual slot offsets for fine-tuning (relative to center) */
#define SLOT_OFFSET_NORTH_X 0
#define SLOT_OFFSET_NORTH_Y -25
#define SLOT_OFFSET_EAST_X 29
#define SLOT_OFFSET_EAST_Y -1
#define SLOT_OFFSET_SOUTH_X 1
#define SLOT_OFFSET_SOUTH_Y 25
#define SLOT_OFFSET_WEST_X -28
#define SLOT_OFFSET_WEST_Y -1

/* Center piece collision bounce constants (matching space_objects) */
#define SATELLITE_CENTER_BOUNCE_FORCE 0.3f
#define SATELLITE_CENTER_BOUNCE_COOLDOWN_MS 250
#define ASSEMBLE_COLLISION_RADIUS_SCALE 0.8f

static int scale_assemble_collision_radius(int iRadius)
{
    int iScaled = (int)fm_floorf((float)iRadius * ASSEMBLE_COLLISION_RADIUS_SCALE + 0.5f);
    if (iScaled < 1)
        iScaled = 1;
    return iScaled;
}

/* Helper: Get unlock flag for a piece direction */
static uint16_t get_unlock_flag_for_direction(ePieceDirection _eDir)
{
    switch (_eDir)
    {
    case PIECE_DIR_SOUTH:
        return GP_UNLOCK_PIECE_A;
    case PIECE_DIR_WEST:
        return GP_UNLOCK_PIECE_B;
    case PIECE_DIR_EAST:
        return GP_UNLOCK_PIECE_C;
    case PIECE_DIR_NORTH:
        return GP_UNLOCK_PIECE_D;
    default:
        return 0;
    }
}

/* Helper: Get direction for an unlock flag */
static ePieceDirection get_direction_for_unlock_flag(uint16_t _uUnlockFlag)
{
    switch (_uUnlockFlag)
    {
    case GP_UNLOCK_PIECE_A:
        return PIECE_DIR_SOUTH;
    case GP_UNLOCK_PIECE_B:
        return PIECE_DIR_WEST;
    case GP_UNLOCK_PIECE_C:
        return PIECE_DIR_EAST;
    case GP_UNLOCK_PIECE_D:
        return PIECE_DIR_NORTH;
    default:
        return PIECE_DIR_COUNT; /* Invalid */
    }
}

/* Helper: Parse piece name */
static bool parse_piece_name(const char *_pName, ePieceDirection *_pOutDir, uint16_t *_pOutFlag)
{
    if (!_pName || !_pOutDir || !_pOutFlag)
        return false;

    if (strcmp(_pName, "piece_a") == 0)
    {
        *_pOutDir = PIECE_DIR_SOUTH;
        *_pOutFlag = GP_UNLOCK_PIECE_A;
        return true;
    }
    else if (strcmp(_pName, "piece_b") == 0)
    {
        *_pOutDir = PIECE_DIR_WEST;
        *_pOutFlag = GP_UNLOCK_PIECE_B;
        return true;
    }
    else if (strcmp(_pName, "piece_c") == 0)
    {
        *_pOutDir = PIECE_DIR_EAST;
        *_pOutFlag = GP_UNLOCK_PIECE_C;
        return true;
    }
    else if (strcmp(_pName, "piece_d") == 0)
    {
        *_pOutDir = PIECE_DIR_NORTH;
        *_pOutFlag = GP_UNLOCK_PIECE_D;
        return true;
    }

    return false;
}

/* Helper: Get sprite for a piece direction */
static sprite_t *get_sprite_for_direction(ePieceDirection _eDir)
{
    switch (_eDir)
    {
    case PIECE_DIR_NORTH:
        return s_pSpriteNorth;
    case PIECE_DIR_EAST:
        return s_pSpriteEast;
    case PIECE_DIR_SOUTH:
        return s_pSpriteSouth;
    case PIECE_DIR_WEST:
        return s_pSpriteWest;
    default:
        return NULL;
    }
}

/* Helper: Get missing sprite for a piece direction */
static sprite_t *get_missing_sprite_for_direction(ePieceDirection _eDir)
{
    switch (_eDir)
    {
    case PIECE_DIR_NORTH:
        return s_pSpriteNorthMissing;
    case PIECE_DIR_EAST:
        return s_pSpriteEastMissing;
    case PIECE_DIR_SOUTH:
        return s_pSpriteSouthMissing;
    case PIECE_DIR_WEST:
        return s_pSpriteWestMissing;
    default:
        return NULL;
    }
}

/* Helper: Get position offset for a piece direction (relative to center) */
static void get_position_offset_for_direction(ePieceDirection _eDir, int *_pOutX, int *_pOutY)
{
    switch (_eDir)
    {
    case PIECE_DIR_NORTH:
        *_pOutX = 0;
        *_pOutY = -PIECE_DISTANCE_NORTH;
        break;
    case PIECE_DIR_EAST:
        *_pOutX = PIECE_DISTANCE_EAST;
        *_pOutY = 0;
        break;
    case PIECE_DIR_SOUTH:
        *_pOutX = 0;
        *_pOutY = PIECE_DISTANCE_SOUTH;
        break;
    case PIECE_DIR_WEST:
        *_pOutX = -PIECE_DISTANCE_WEST;
        *_pOutY = 0;
        break;
    default:
        *_pOutX = 0;
        *_pOutY = 0;
        break;
    }
}

/* Helper: Get slot position for a piece direction (world space) */
static struct vec2 get_slot_position_for_direction(ePieceDirection _eDir)
{
    struct vec2 vPos = s_vSatelliteRepairPos;
    switch (_eDir)
    {
    case PIECE_DIR_NORTH:
        vPos.fX += (float)SLOT_OFFSET_NORTH_X;
        vPos.fY += (float)SLOT_OFFSET_NORTH_Y;
        break;
    case PIECE_DIR_EAST:
        vPos.fX += (float)SLOT_OFFSET_EAST_X;
        vPos.fY += (float)SLOT_OFFSET_EAST_Y;
        break;
    case PIECE_DIR_SOUTH:
        vPos.fX += (float)SLOT_OFFSET_SOUTH_X;
        vPos.fY += (float)SLOT_OFFSET_SOUTH_Y;
        break;
    case PIECE_DIR_WEST:
        vPos.fX += (float)SLOT_OFFSET_WEST_X;
        vPos.fY += (float)SLOT_OFFSET_WEST_Y;
        break;
    default:
        break;
    }
    return vPos;
}

/* Helper: Ensure sprites are loaded (lazy loading for UI) */
static void ensure_sprites_loaded(void)
{
    if (!s_pSpriteCenter)
    {
        s_pSpriteCenter = sprite_load(SPRITE_PATH_CENTER);
    }
    if (!s_pSpriteNorth)
    {
        s_pSpriteNorth = sprite_load(SPRITE_PATH_NORTH);
    }
    if (!s_pSpriteEast)
    {
        s_pSpriteEast = sprite_load(SPRITE_PATH_EAST);
    }
    if (!s_pSpriteSouth)
    {
        s_pSpriteSouth = sprite_load(SPRITE_PATH_SOUTH);
    }
    if (!s_pSpriteWest)
    {
        s_pSpriteWest = sprite_load(SPRITE_PATH_WEST);
    }
    if (!s_pSpriteNorthMissing)
    {
        s_pSpriteNorthMissing = sprite_load(SPRITE_PATH_NORTH_MISSING);
    }
    if (!s_pSpriteEastMissing)
    {
        s_pSpriteEastMissing = sprite_load(SPRITE_PATH_EAST_MISSING);
    }
    if (!s_pSpriteSouthMissing)
    {
        s_pSpriteSouthMissing = sprite_load(SPRITE_PATH_SOUTH_MISSING);
    }
    if (!s_pSpriteWestMissing)
    {
        s_pSpriteWestMissing = sprite_load(SPRITE_PATH_WEST_MISSING);
    }
}

void satellite_pieces_init(void)
{
    /* Load sprites (idempotent - safe to call multiple times) */
    ensure_sprites_loaded();

    /* Load sound effects */
    if (!s_pSoundPieceCollect)
        s_pSoundPieceCollect = wav64_load("rom:/piece_collect.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    if (!s_pSoundPieceConnect)
        s_pSoundPieceConnect = wav64_load("rom:/piece_connect.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    if (!s_pSoundSatelliteRepaired)
        s_pSoundSatelliteRepaired = wav64_load("rom:/satellite_repaired.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    /* Load satellite_repair POI from space folder */
    if (poi_load("satellite_repair", &s_vSatelliteRepairPos, "space"))
    {
    }
    else
    {
        debugf("satellite_pieces_init: Failed to load satellite_repair POI from space folder\n");
    }
}

void satellite_pieces_free(void)
{
    /* Don't free sprites - they need to persist for UI rendering even if init wasn't called */
    /* Sprites are kept loaded for the lifetime of the game */
    /* Sounds are also kept loaded for the lifetime of the game */
}

void satellite_pieces_render_ui(void)
{
    /* Ensure sprites are loaded (lazy loading for JNR mode where init may not be called) */
    ensure_sprites_loaded();

    if (!s_pSpriteCenter)
        return;

    int iBaseX = SCREEN_W / 2;
    int iBaseY = SCREEN_H / 3;

    /* Render directional pieces */
    for (ePieceDirection eDir = 0; eDir < PIECE_DIR_COUNT; eDir++)
    {
        int iOffsetX, iOffsetY;
        get_position_offset_for_direction(eDir, &iOffsetX, &iOffsetY);

        int iPieceCenterX = iBaseX + iOffsetX;
        int iPieceCenterY = iBaseY + iOffsetY;

        uint16_t uUnlockFlag = get_unlock_flag_for_direction(eDir);
        bool bIsUnlocked = gp_state_unlock_get(uUnlockFlag);

        /* Choose sprite based on unlock status */
        sprite_t *pSprite = bIsUnlocked ? get_sprite_for_direction(eDir) : get_missing_sprite_for_direction(eDir);
        if (!pSprite)
            continue;

        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        rdpq_mode_filter(FILTER_POINT);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX);

        rdpq_blitparms_t parms = {.cx = pSprite->width / 2, .cy = pSprite->height / 2};

        rdpq_sprite_blit(pSprite, iPieceCenterX, iPieceCenterY, &parms);
    }

    /* Render center piece last */
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_POINT);

    rdpq_blitparms_t parmsCenter = {.cx = s_pSpriteCenter->width / 2, .cy = s_pSpriteCenter->height / 2};

    rdpq_sprite_blit(s_pSpriteCenter, iBaseX, iBaseY, &parmsCenter);
}

void satellite_pieces_refresh(void)
{
    const char *pFolder = gp_state_get_current_folder();
    if (!pFolder)
        return;

    char szPath[256];
    snprintf(szPath, sizeof(szPath), "rom:/%s/piece.csv", pFolder);

    char *pFileData = NULL;
    size_t uFileSize = 0;
    if (!csv_helper_load_file(szPath, &pFileData, &uFileSize))
        return;

    char *pLineStart = pFileData;
    char *pLineEnd = NULL;

    while ((pLineEnd = strchr(pLineStart, '\n')) != NULL || (pLineStart[0] != '\0'))
    {
        size_t uLineLen = (pLineEnd) ? (size_t)(pLineEnd - pLineStart) : strlen(pLineStart);
        if (uLineLen == 0)
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        char szLine[512];
        size_t uCopyLen = (uLineLen < sizeof(szLine) - 1) ? uLineLen : sizeof(szLine) - 1;
        memcpy(szLine, pLineStart, uCopyLen);
        szLine[uCopyLen] = '\0';

        csv_helper_strip_eol(szLine);

        if (szLine[0] == '\0')
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        char szLineCopy[512];
        if (!csv_helper_copy_line_for_tokenizing(szLine, szLineCopy, sizeof(szLineCopy)))
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        char szName[64];
        struct vec2 vPos;

        char *pToken = strtok(szLineCopy, ",");
        if (!pToken)
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        if (!csv_helper_copy_string_safe(pToken, szName, sizeof(szName)))
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        char *pTokenX = strtok(NULL, ",");
        char *pTokenY = strtok(NULL, ",");
        if (!pTokenX || !pTokenY || !csv_helper_parse_xy_from_tokens(pTokenX, pTokenY, &vPos))
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        ePieceDirection eDir;
        uint16_t uUnlockFlag;
        if (!parse_piece_name(szName, &eDir, &uUnlockFlag))
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        if (gp_state_unlock_get(uUnlockFlag))
        {
            if (!pLineEnd)
                break;
            pLineStart = pLineEnd + 1;
            continue;
        }

        /* Create entity via space_objects (not in assemble mode) */
        satellite_pieces_create(uUnlockFlag, vPos, false);

        if (!pLineEnd)
            break;
        pLineStart = pLineEnd + 1;
    }

    free(pFileData);
}

/* Helper: Check if a piece with the given unlock flag already exists */
/* We iterate space_objects to check. This is slow but safe, and only called on create. */
static bool piece_already_exists(uint16_t _uUnlockFlag)
{
    int iMax = space_objects_get_max_count();
    for (int i = 0; i < iMax; i++)
    {
        SpaceObject *obj = space_objects_get_object(i);
        if (obj && obj->bAllocated && obj->type == SO_PIECE && obj->data.piece.uUnlockFlag == _uUnlockFlag)
        {
            return true;
        }
    }
    return false;
}

const struct entity2D *satellite_pieces_get_entity_by_unlock_flag(uint16_t _uUnlockFlag)
{
    /* Check if piece is already unlocked (shouldn't exist if unlocked) */
    if (gp_state_unlock_get(_uUnlockFlag))
    {
        return NULL;
    }

    /* Iterate space_objects to find the piece */
    int iMax = space_objects_get_max_count();
    for (int i = 0; i < iMax; i++)
    {
        SpaceObject *obj = space_objects_get_object(i);
        if (obj && obj->bAllocated && obj->type == SO_PIECE && obj->data.piece.uUnlockFlag == _uUnlockFlag)
        {
            /* Check if entity is active and loaded */
            if (entity2d_is_active(&obj->entity))
            {
                return &obj->entity;
            }
        }
    }
    return NULL;
}

bool satellite_pieces_create(uint16_t _uUnlockFlag, struct vec2 _vPos, bool _bAssembleMode)
{
    ePieceDirection eDir = get_direction_for_unlock_flag(_uUnlockFlag);

    if (piece_already_exists(_uUnlockFlag))
    {
        debugf("satellite_pieces_create: Piece (flag 0x%04X) already exists, skipping\n", _uUnlockFlag);
        return false;
    }

    if (gp_state_unlock_get(_uUnlockFlag))
    {
        debugf("satellite_pieces_create: Piece (flag 0x%04X) already unlocked, skipping\n", _uUnlockFlag);
        return false;
    }

    SpaceObject *pPiece = space_objects_spawn_piece(eDir, _uUnlockFlag, _vPos);
    if (!pPiece)
        return false;

    sprite_t *pSprite = get_sprite_for_direction(eDir);
    uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
    uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;
    entity2d_init_from_sprite(&pPiece->entity, _vPos, pSprite, uFlags, uLayerMask);

    pPiece->entity.fAngleRad = rngf(0.0f, 6.28318530718f);
    pPiece->data.piece.fRotationSpeed = rngb(0.5f) ? PIECE_ROT_SPEED : -PIECE_ROT_SPEED;
    pPiece->data.piece.bAssembleMode = _bAssembleMode;
    if (_bAssembleMode)
    {
        pPiece->entity.iCollisionRadius = scale_assemble_collision_radius(pPiece->entity.iCollisionRadius);
    }

    return true;
}

void satellite_piece_update_object(SpaceObject *pPiece)
{
    if (!pPiece || !entity2d_is_active(&pPiece->entity))
        return;

    if (pPiece->entity.bGrabbed && !tractor_beam_is_active())
    {
        /* Prevent stale grabbed state from disabling damping. */
        pPiece->entity.bGrabbed = false;
    }

    /* Check if piece is in assemble mode */
    if (pPiece->data.piece.bAssembleMode)
    {
        ePieceDirection eDir = (ePieceDirection)pPiece->data.piece.eDirection;

        /* Check if piece is already snapped */
        if (s_aPiecesSnapped[eDir])
        {
            /* Piece is snapped - lock position and skip all movement updates */
            struct vec2 vTargetPos = get_slot_position_for_direction(eDir);
            pPiece->entity.vPos = vTargetPos;
            pPiece->entity.fAngleRad = 0.0f;
            pPiece->entity.vVel = vec2_zero();
            pPiece->data.piece.fRotationSpeed = 0.0f;
            pPiece->bSleeping = true;
            return;
        }

        /* Not snapped yet - check for snapping */
        struct vec2 vTargetPos = get_slot_position_for_direction(eDir);
        float fDist = vec2_dist(pPiece->entity.vPos, vTargetPos);
        float fRotDiff = fabsf(angle_wrap_rad(pPiece->entity.fAngleRad - 0.0f));

        if (fDist <= PIECE_SNAP_DISTANCE && fRotDiff <= PIECE_SNAP_ROTATION_RAD)
        {
            /* Snap the piece into place */
            s_aPiecesSnapped[eDir] = true;
            pPiece->entity.vPos = vTargetPos;
            pPiece->entity.fAngleRad = 0.0f;
            pPiece->entity.vVel = vec2_zero();
            pPiece->data.piece.fRotationSpeed = 0.0f;
            pPiece->bSleeping = true;
            debugf("satellite_piece_update_object: Piece snapped into slot (direction %d)\n", eDir);

            /* Play piece connect sound */
            if (s_pSoundPieceConnect)
                wav64_play(s_pSoundPieceConnect, MIXER_CHANNEL_ITEMS);

            /* Check if all pieces are now snapped - play satellite repaired sound */
            bool bAllSnapped = true;
            for (ePieceDirection eCheckDir = 0; eCheckDir < PIECE_DIR_COUNT; eCheckDir++)
            {
                if (!s_aPiecesSnapped[eCheckDir])
                {
                    bAllSnapped = false;
                    break;
                }
            }
            if (bAllSnapped && s_pSoundSatelliteRepaired)
            {
                wav64_play(s_pSoundSatelliteRepaired, MIXER_CHANNEL_EXPLOSIONS);
            }

            return;
        }
    }

    float fFrameMul = frame_time_mul();

    if (!pPiece->entity.bGrabbed)
    {
        pPiece->entity.fAngleRad += pPiece->data.piece.fRotationSpeed * fFrameMul;
        pPiece->entity.fAngleRad = angle_wrap_rad(pPiece->entity.fAngleRad);
    }
    else
    {
        /* Follow UFO movement directly when grabbed (no damping/sleep). */
        pPiece->bSleeping = false;
        pPiece->data.piece.fRotationSpeed = 0.0f;
        pPiece->entity.vPos = vec2_add(pPiece->entity.vPos, vec2_scale(pPiece->entity.vVel, fFrameMul));
        return;
    }

    if (pPiece->bSleeping)
    {
        if (vec2_mag_sq(pPiece->entity.vVel) <= PIECE_SLEEP_VEL_SQ)
            return;
        pPiece->bSleeping = false;
    }

    /* Update position using velocity BEFORE damping */
    pPiece->entity.vPos = vec2_add(pPiece->entity.vPos, vec2_scale(pPiece->entity.vVel, fFrameMul));

    /* Apply damping to velocity for next frame */
    float fDamping = powf(PIECE_VELOCITY_DAMPING, fFrameMul);
    pPiece->entity.vVel = vec2_scale(pPiece->entity.vVel, fDamping);

    if (vec2_mag_sq(pPiece->entity.vVel) <= PIECE_SLEEP_VEL_SQ)
    {
        pPiece->entity.vVel = vec2_zero();
        pPiece->bSleeping = true;
    }
}

void satellite_piece_render_object(SpaceObject *pPiece, struct vec2i vScreen, float fZoom)
{
    if (!pPiece)
        return;

    const struct entity2D *pEnt = &pPiece->entity;
    if (!pEnt->pSprite)
        return;

    rdpq_mode_filter(FILTER_BILINEAR);

    rdpq_blitparms_t parms = {
        .cx = pEnt->vHalf.iX,
        .cy = pEnt->vHalf.iY,
        .scale_x = fZoom,
        .scale_y = fZoom,
        .theta = pEnt->fAngleRad,
    };
    rdpq_sprite_blit(pEnt->pSprite, vScreen.iX, vScreen.iY, &parms);
}

void satellite_piece_collect(SpaceObject *pPiece)
{
    if (!pPiece)
        return;

    gp_state_unlock_set(pPiece->data.piece.uUnlockFlag, true);

    /* Play collect sound */
    if (s_pSoundPieceCollect)
        wav64_play(s_pSoundPieceCollect, MIXER_CHANNEL_ITEMS);

    /* Deactivate and mark for delete */
    entity2d_deactivate(&pPiece->entity);
    pPiece->markForDelete = true;
    pPiece->entity.pSprite = NULL;

    /* Notify UFO to clear any lock on this entity */
    ufo_deselect_entity_lock_and_marker(&pPiece->entity);
}

bool satellite_pieces_bSatelliteRepaired(void)
{
    /* Check if all four pieces are snapped */
    for (ePieceDirection eDir = 0; eDir < PIECE_DIR_COUNT; eDir++)
    {
        if (!s_aPiecesSnapped[eDir])
        {
            return false;
        }
    }
    return true;
}

void satellite_pieces_spawn_assemble_pieces(void)
{
    struct vec2 vUfoPos = ufo_get_position();

    /* Spawn distance from UFO */
    const float fSpawnDistance = 80.0f;
    /* Initial velocity speed away from UFO */
    const float fVelocitySpeed = 2.0f;

    /* Spawn pieces a, b, c, d around the UFO */
    struct
    {
        uint16_t uUnlockFlag;
        ePieceDirection eDir;
        struct vec2 vOffset; /* Offset from UFO position */
    } aPieces[] = {
        {GP_UNLOCK_PIECE_A, PIECE_DIR_SOUTH, {0.0f, fSpawnDistance}}, /* Piece A - South */
        {GP_UNLOCK_PIECE_B, PIECE_DIR_WEST, {-fSpawnDistance, 0.0f}}, /* Piece B - West */
        {GP_UNLOCK_PIECE_C, PIECE_DIR_EAST, {fSpawnDistance, 0.0f}},  /* Piece C - East */
        {GP_UNLOCK_PIECE_D, PIECE_DIR_NORTH, {0.0f, -fSpawnDistance}} /* Piece D - North */
    };

    /* First, clear any existing pieces and snapped state (but preserve unlock flags!) */
    for (int i = 0; i < 4; i++)
    {
        /* Clear snapped state */
        s_aPiecesSnapped[aPieces[i].eDir] = false;

        /* Delete any existing piece objects */
        int iMax = space_objects_get_max_count();
        for (int j = 0; j < iMax; j++)
        {
            SpaceObject *pObj = space_objects_get_object(j);
            if (pObj && pObj->bAllocated && pObj->type == SO_PIECE && pObj->data.piece.uUnlockFlag == aPieces[i].uUnlockFlag)
            {
                entity2d_deactivate(&pObj->entity);
                pObj->markForDelete = true;
            }
        }
    }

    int iCreatedCount = 0;
    for (int i = 0; i < 4; i++)
    {
        /* Skip if piece already exists (shouldn't happen after clearing, but check anyway) */
        if (piece_already_exists(aPieces[i].uUnlockFlag))
        {
            continue;
        }

        /* Calculate spawn position */
        struct vec2 vSpawnPos = vec2_add(vUfoPos, aPieces[i].vOffset);

        /* Calculate velocity away from UFO (normalize offset and scale by speed) */
        struct vec2 vVelDir = vec2_normalize(aPieces[i].vOffset);
        struct vec2 vVelocity = vec2_scale(vVelDir, fVelocitySpeed);

        /* Create piece directly (bypass unlock check for assemble mode) */
        SpaceObject *pPiece = space_objects_spawn_piece(aPieces[i].eDir, aPieces[i].uUnlockFlag, vSpawnPos);
        if (pPiece)
        {
            sprite_t *pSprite = get_sprite_for_direction(aPieces[i].eDir);
            uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
            uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;
            entity2d_init_from_sprite(&pPiece->entity, vSpawnPos, pSprite, uFlags, uLayerMask);

            pPiece->entity.fAngleRad = rngf(0.0f, 6.28318530718f);
            pPiece->data.piece.fRotationSpeed = rngb(0.5f) ? PIECE_ROT_SPEED : -PIECE_ROT_SPEED;
            pPiece->data.piece.bAssembleMode = true;
            pPiece->entity.iCollisionRadius = scale_assemble_collision_radius(pPiece->entity.iCollisionRadius);

            /* Set velocity */
            pPiece->entity.vVel = vVelocity;
            pPiece->bSleeping = false; /* Ensure it's not sleeping so velocity applies */

            iCreatedCount++;
        }
    }
}

void satellite_pieces_render_satellite(void)
{
    /* Ensure sprites are loaded */
    ensure_sprites_loaded();

    /* Get camera zoom for scaling (once for all sprites) */
    float fZoom = camera_get_zoom(&g_mainCamera);

    /* Render directional pieces first (north, east, south, west) */
    for (ePieceDirection eDir = 0; eDir < PIECE_DIR_COUNT; eDir++)
    {
        /* Only render missing sprite if piece is NOT snapped */
        if (s_aPiecesSnapped[eDir])
            continue;

        sprite_t *pSprite = get_missing_sprite_for_direction(eDir);
        if (!pSprite)
            continue;

        /* Calculate slot position */
        struct vec2 vSlotPos = get_slot_position_for_direction(eDir);

        /* Convert world position to screen */
        struct vec2i vScreenPos;
        camera_world_to_screen(&g_mainCamera, vSlotPos, &vScreenPos);

        /* Render missing sprite at 50% alpha */
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        rdpq_mode_filter(FILTER_POINT);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(255, 255, 255, 128)); /* 50% alpha */

        rdpq_blitparms_t parms = {.cx = pSprite->width / 2, .cy = pSprite->height / 2, .scale_x = fZoom, .scale_y = fZoom};

        rdpq_sprite_blit(pSprite, vScreenPos.iX, vScreenPos.iY, &parms);
    }

    /* Render center piece last */
    struct vec2i vCenterScreenPos;
    camera_world_to_screen(&g_mainCamera, s_vSatelliteRepairPos, &vCenterScreenPos);

    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);

    rdpq_blitparms_t parmsCenter = {.cx = s_pSpriteCenter->width / 2, .cy = s_pSpriteCenter->height / 2, .scale_x = fZoom, .scale_y = fZoom};

    rdpq_sprite_blit(s_pSpriteCenter, vCenterScreenPos.iX, vCenterScreenPos.iY, &parmsCenter);
}

void satellite_pieces_check_center_collision(void)
{
    /* Check if POI position is valid (non-zero) */
    if (s_vSatelliteRepairPos.fX == 0.0f && s_vSatelliteRepairPos.fY == 0.0f)
        return;

    if (!s_pSpriteCenter)
        return;

    const struct entity2D *pUfo = ufo_get_entity();
    if (!pUfo || !entity2d_is_active(pUfo) || !entity2d_is_collidable(pUfo))
        return;

    /* Calculate collision radius for center piece (use minimum of width/height) */
    int iCenterRadius = (s_pSpriteCenter->width < s_pSpriteCenter->height) ? s_pSpriteCenter->width / 2 : s_pSpriteCenter->height / 2;
    float fCenterRadius = (float)iCenterRadius * ASSEMBLE_COLLISION_RADIUS_SCALE;

    /* Resolve collisions with assemble-mode pieces */
    int iMax = space_objects_get_max_count();
    for (int i = 0; i < iMax; i++)
    {
        SpaceObject *pPiece = space_objects_get_object(i);
        if (!pPiece || !pPiece->bAllocated || pPiece->type != SO_PIECE)
            continue;
        if (!pPiece->data.piece.bAssembleMode)
            continue;
        if (!entity2d_is_active(&pPiece->entity) || !entity2d_is_collidable(&pPiece->entity))
            continue;

        struct vec2 vDeltaPiece = vec2_sub(pPiece->entity.vPos, s_vSatelliteRepairPos);
        float fDistSqPiece = vec2_mag_sq(vDeltaPiece);
        float fRadSum = fCenterRadius + (float)pPiece->entity.iCollisionRadius;
        float fRadSumSq = fRadSum * fRadSum;
        if (fDistSqPiece >= fRadSumSq || fDistSqPiece <= 1e-6f)
            continue;

        float fDist = sqrtf(fDistSqPiece);
        struct vec2 vNormal = vec2_scale(vDeltaPiece, 1.0f / fDist);

        float fPenetration = fRadSum - fDist;
        struct vec2 vCorrection = vec2_scale(vNormal, fPenetration + 0.5f);
        pPiece->entity.vPos = vec2_add(pPiece->entity.vPos, vCorrection);

        /* Cancel piece velocity into center */
        float fVelDot = vec2_dot(pPiece->entity.vVel, vNormal);
        if (fVelDot < 0.0f)
        {
            pPiece->entity.vVel = vec2_sub(pPiece->entity.vVel, vec2_scale(vNormal, fVelDot));
        }
        pPiece->bSleeping = false;
    }

    /* Check circle-circle collision */
    struct vec2 vDelta = vec2_sub(s_vSatelliteRepairPos, pUfo->vPos);
    float fDistSq = vec2_mag_sq(vDelta);
    float fRadiusSum = fCenterRadius + (float)pUfo->iCollisionRadius;
    float fRadiusSumSq = fRadiusSum * fRadiusSum;

    if (fDistSq > fRadiusSumSq)
        return;

    SpaceObject centerObj = {0};
    centerObj.entity.vPos = s_vSatelliteRepairPos;
    centerObj.entity.iCollisionRadius = (int)fm_floorf(fCenterRadius + 0.5f);
    centerObj.entity.vVel = vec2_zero();

    CollisionEvents events = {0};
    events.bIsColliding = true;
    events.bOnTriggerEnter = true; /* Match previous behavior: bounce while colliding */

    space_objects_resolve_ufo_solid_collision(&centerObj, pUfo, events, true, SATELLITE_CENTER_BOUNCE_FORCE, SATELLITE_CENTER_BOUNCE_COOLDOWN_MS);
}
