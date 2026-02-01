#include "minimap_marker.h"
#include "audio.h"
#include "camera.h"
#include "dialogue.h"
#include "game_objects/planets.h"
#include "game_objects/ufo.h"
#include "libdragon.h"
#include "math_helper.h"
#include "minimap.h"
#include "poi.h"
#include "rdpq.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "resource_helper.h"
#include "satellite_pieces.h"
#include "sprite.h"
#include "ui.h"
#include <limits.h>
#include <math.h>
#include <string.h>

/* Marker storage */
static MinimapMarker s_aMarkers[MINIMAP_MARKER_MAX_COUNT];
static bool s_bInitialized = false;

minimap_marker_type_t minimap_marker_get_type(const struct entity2D *_pEntity)
{
    if (!_pEntity)
        return MARKER_TYPE_COUNT;

    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (s_aMarkers[i].bSlotInUse && &s_aMarkers[i].entity == _pEntity)
        {
            return s_aMarkers[i].eType;
        }
    }

    return MARKER_TYPE_COUNT;
}

/* Marker sprites */
static sprite_t *s_apMarkerSprites[MARKER_TYPE_COUNT] = {NULL};
static sprite_t *s_pLockOnSprite = NULL;

/* Sprite paths */
static const char *s_apMarkerSpritePaths[MARKER_TYPE_COUNT] = {
    "rom:/marker_rhino_00.sprite",
    "rom:/marker_piece_00.sprite",
    "rom:/marker_target_00.sprite",
    "rom:/marker_boy_00.sprite",
    "rom:/marker_pin_00.sprite",
    "rom:/marker_terra_00.sprite",
};

void minimap_marker_init(void)
{
    if (s_bInitialized)
        return;

    /* Initialize all markers */
    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        s_aMarkers[i].bSlotInUse = false;
        s_aMarkers[i].pName = NULL;
        s_aMarkers[i].eType = MARKER_RHINO;
        s_aMarkers[i].uUnlockFlag = 0;
        s_aMarkers[i].pPieceEntity = NULL;
        entity2d_deactivate(&s_aMarkers[i].entity);
    }

    /* Load marker sprites */
    for (int i = 0; i < MARKER_TYPE_COUNT; ++i)
    {
        if (!s_apMarkerSprites[i])
        {
            s_apMarkerSprites[i] = sprite_load(s_apMarkerSpritePaths[i]);
        }
    }

    /* Load lock-on sprite */
    if (!s_pLockOnSprite)
    {
        s_pLockOnSprite = sprite_load("rom:/marker_selected_00.sprite");
    }

    /* Always create marker_boy at UFO position */
    struct vec2 vUfoPos = ufo_get_position();
    sprite_t *pBoySprite = s_apMarkerSprites[MARKER_BOY];
    if (pBoySprite)
    {
        s_aMarkers[0].pName = "marker_boy";
        s_aMarkers[0].eType = MARKER_BOY;
        s_aMarkers[0].bSlotInUse = true;
        s_aMarkers[0].uUnlockFlag = 0;
        s_aMarkers[0].pPieceEntity = NULL;
        entity2d_init_from_sprite(&s_aMarkers[0].entity, vUfoPos, pBoySprite, ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE, ENTITY_LAYER_UI);
        s_aMarkers[0].entity.iCollisionRadius = MINIMAP_MARKER_SELECT_RADIUS;
    }

    /* Always create TERRA marker (position will be updated on minimap activation) */
    sprite_t *pTerraSprite = s_apMarkerSprites[MARKER_TERRA];
    if (pTerraSprite)
    {
        /* Find first available slot (skip slot 0 which is marker_boy) */
        for (uint16_t i = 1; i < MINIMAP_MARKER_MAX_COUNT; ++i)
        {
            if (!s_aMarkers[i].bSlotInUse)
            {
                s_aMarkers[i].pName = NULL; /* TERRA marker has no name */
                s_aMarkers[i].eType = MARKER_TERRA;
                s_aMarkers[i].bSlotInUse = true;
                s_aMarkers[i].uUnlockFlag = 0;
                s_aMarkers[i].pPieceEntity = NULL;
                /* Initialize at zero position - will be updated on minimap activation */
                entity2d_init_from_sprite(&s_aMarkers[i].entity, vec2_zero(), pTerraSprite, ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE, ENTITY_LAYER_UI);
                s_aMarkers[i].entity.iCollisionRadius = MINIMAP_MARKER_SELECT_RADIUS;
                break;
            }
        }
    }

    s_bInitialized = true;
}

/* Helper: Initialize marker entity2D based on type */
static bool minimap_marker_init_entity(MinimapMarker *_pMarker, struct vec2 _vPos, minimap_marker_type_t _eType)
{
    /* All markers now use sprites (including PIN) */
    sprite_t *pSprite = s_apMarkerSprites[_eType];
    if (!pSprite)
        return false;
    entity2d_init_from_sprite(&_pMarker->entity, _vPos, pSprite, ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE, ENTITY_LAYER_UI);

    /* Set collision radius for selection */
    _pMarker->entity.iCollisionRadius = MINIMAP_MARKER_SELECT_RADIUS;
    return true;
}

const struct entity2D *minimap_marker_set(const char *_pName, minimap_marker_type_t _eType)
{
    if (!_pName || _eType >= MARKER_TYPE_COUNT)
        return NULL;

    /* Check if marker with this name already exists - if so, don't add it again */
    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (s_aMarkers[i].bSlotInUse && s_aMarkers[i].pName && strcmp(s_aMarkers[i].pName, _pName) == 0)
        {
            /* Marker with this name already exists, return existing entity */
            return &s_aMarkers[i].entity;
        }
    }

    /* Find inactive slot */
    MinimapMarker *pMarker = NULL;
    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (!s_aMarkers[i].bSlotInUse)
        {
            pMarker = &s_aMarkers[i];
            break;
        }
    }

    if (!pMarker)
        return NULL; /* No free slots */

    /* Load POI position */
    struct vec2 vPos;
    if (!poi_load(_pName, &vPos, NULL))
        return NULL;

    /* Initialize marker */
    pMarker->pName = _pName;
    pMarker->eType = _eType;
    pMarker->bSlotInUse = true;
    pMarker->uUnlockFlag = 0; /* Named markers are not linked to pieces */
    pMarker->pPieceEntity = NULL;

    if (!minimap_marker_init_entity(pMarker, vPos, _eType))
        return NULL;

    return &pMarker->entity;
}

const struct entity2D *minimap_marker_set_at_pos(struct vec2 _vWorldPos, minimap_marker_type_t _eType)
{
    if (_eType >= MARKER_TYPE_COUNT)
        return NULL;

    /* Find inactive slot */
    MinimapMarker *pMarker = NULL;
    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (!s_aMarkers[i].bSlotInUse)
        {
            pMarker = &s_aMarkers[i];
            break;
        }
    }

    if (!pMarker)
        return NULL; /* No free slots */

    /* Initialize marker */
    pMarker->pName = NULL; /* Position-based markers have no name */
    pMarker->eType = _eType;
    pMarker->bSlotInUse = true;
    pMarker->uUnlockFlag = 0; /* Position-based markers are not linked to pieces */
    pMarker->pPieceEntity = NULL;

    if (!minimap_marker_init_entity(pMarker, _vWorldPos, _eType))
        return NULL;

    return &pMarker->entity;
}

const struct entity2D *minimap_marker_set_piece(uint16_t _uUnlockFlag)
{
    /* Check if marker for this piece already exists */
    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (s_aMarkers[i].bSlotInUse && s_aMarkers[i].eType == MARKER_PIECE && s_aMarkers[i].uUnlockFlag == _uUnlockFlag)
        {
            /* Marker for this piece already exists - always refresh the entity reference
             * to handle state transitions where space objects are recreated */
            const struct entity2D *pPieceEntity = satellite_pieces_get_entity_by_unlock_flag(_uUnlockFlag);
            if (pPieceEntity)
            {
                s_aMarkers[i].pPieceEntity = pPieceEntity;
                /* Update marker position to match piece position */
                entity2d_set_pos(&s_aMarkers[i].entity, pPieceEntity->vPos);
            }
            return &s_aMarkers[i].entity;
        }
    }

    /* Get piece entity by unlock flag */
    const struct entity2D *pPieceEntity = satellite_pieces_get_entity_by_unlock_flag(_uUnlockFlag);
    if (!pPieceEntity)
        return NULL; /* Piece doesn't exist yet */

    /* Find inactive slot */
    MinimapMarker *pMarker = NULL;
    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (!s_aMarkers[i].bSlotInUse)
        {
            pMarker = &s_aMarkers[i];
            break;
        }
    }

    if (!pMarker)
        return NULL; /* No free slots */

    /* Initialize marker linked to piece */
    pMarker->pName = NULL; /* Piece markers have no name */
    pMarker->eType = MARKER_PIECE;
    pMarker->bSlotInUse = true;
    pMarker->uUnlockFlag = _uUnlockFlag;  /* Link marker to piece */
    pMarker->pPieceEntity = pPieceEntity; /* Store direct reference for fast updates */

    if (!minimap_marker_init_entity(pMarker, pPieceEntity->vPos, MARKER_PIECE))
        return NULL;

    return &pMarker->entity;
}

void minimap_marker_clear(const char *_pName)
{
    if (!_pName)
        return;

    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (s_aMarkers[i].bSlotInUse && s_aMarkers[i].pName && strcmp(s_aMarkers[i].pName, _pName) == 0)
        {
            /* Notify UFO that this target is being destroyed */
            ufo_deselect_entity_lock_and_marker(&s_aMarkers[i].entity);

            /* Deactivate marker */
            s_aMarkers[i].bSlotInUse = false;
            s_aMarkers[i].pName = NULL;
            s_aMarkers[i].uUnlockFlag = 0;
            s_aMarkers[i].pPieceEntity = NULL;
            entity2d_deactivate(&s_aMarkers[i].entity);
            break;
        }
    }
}

void minimap_marker_update(bool _bPiecesOnly)
{
    /* Update all dynamic markers in a single pass */
    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (!s_aMarkers[i].bSlotInUse)
            continue;

        if (!_bPiecesOnly && s_aMarkers[i].eType == MARKER_BOY)
        {
            /* Only update marker_boy when minimap is active (not in pieces-only mode) */
            entity2d_set_pos(&s_aMarkers[i].entity, ufo_get_position());
        }
        else if (s_aMarkers[i].eType == MARKER_PIECE && s_aMarkers[i].pPieceEntity != NULL)
        {
            /* Update piece marker position directly from cached entity reference (fast path) */
            if (entity2d_is_active(s_aMarkers[i].pPieceEntity))
            {
                /* Update marker position to match piece position (fast path - no search) */
                entity2d_set_pos(&s_aMarkers[i].entity, s_aMarkers[i].pPieceEntity->vPos);
            }
            else
            {
                /* Entity became invalid - try to refresh reference once before giving up */
                const struct entity2D *pPieceEntity = satellite_pieces_get_entity_by_unlock_flag(s_aMarkers[i].uUnlockFlag);
                if (pPieceEntity && entity2d_is_active(pPieceEntity))
                {
                    /* Found it again - update reference and position */
                    s_aMarkers[i].pPieceEntity = pPieceEntity;
                    entity2d_set_pos(&s_aMarkers[i].entity, pPieceEntity->vPos);
                }
                else
                {
                    /* Piece no longer exists (collected or not spawned), deactivate marker */
                    ufo_deselect_entity_lock_and_marker(&s_aMarkers[i].entity);
                    s_aMarkers[i].bSlotInUse = false;
                    s_aMarkers[i].pName = NULL;
                    s_aMarkers[i].uUnlockFlag = 0;
                    s_aMarkers[i].pPieceEntity = NULL;
                    entity2d_deactivate(&s_aMarkers[i].entity);
                }
            }
        }
    }
}

void minimap_marker_update_terra(void)
{
    if (!s_bInitialized)
        return;

    /* Find terra marker */
    MinimapMarker *pTerraMarker = NULL;
    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (s_aMarkers[i].bSlotInUse && s_aMarkers[i].eType == MARKER_TERRA)
        {
            pTerraMarker = &s_aMarkers[i];
            break;
        }
    }

    if (!pTerraMarker)
        return; /* Terra marker should always exist (created in init) */

    /* Update terra marker position if available */
    struct vec2 vTerraPos;
    if (planets_get_terra_pos(&vTerraPos))
    {
        entity2d_set_pos(&pTerraMarker->entity, vTerraPos);
    }
}

/* Helper: Get padded screen bounds */
static void get_padded_screen_bounds(int *_pLeft, int *_pTop, int *_pRight, int *_pBottom)
{
    int iPadding = ui_get_overscan_padding();
    *_pLeft = iPadding + MINIMAP_MARKER_BORDER_PADDING;
    *_pTop = iPadding + MINIMAP_MARKER_BORDER_PADDING;
    *_pRight = SCREEN_W - iPadding - MINIMAP_MARKER_BORDER_PADDING;
    *_pBottom = SCREEN_H - iPadding - MINIMAP_MARKER_BORDER_PADDING;
}

/* Helper: Clamp screen position to nearest padded edge */
static void clamp_to_padded_edge(struct vec2i *_pScreenPos)
{
    int iLeft, iTop, iRight, iBottom;
    get_padded_screen_bounds(&iLeft, &iTop, &iRight, &iBottom);

    int iDistToLeft = _pScreenPos->iX - iLeft;
    int iDistToRight = iRight - _pScreenPos->iX;
    int iDistToTop = _pScreenPos->iY - iTop;
    int iDistToBottom = iBottom - _pScreenPos->iY;

    if (iDistToLeft < iDistToRight && iDistToLeft < iDistToTop && iDistToLeft < iDistToBottom)
        _pScreenPos->iX = iLeft;
    else if (iDistToRight < iDistToTop && iDistToRight < iDistToBottom)
        _pScreenPos->iX = iRight;
    else if (iDistToTop < iDistToBottom)
        _pScreenPos->iY = iTop;
    else
        _pScreenPos->iY = iBottom;
}

/* Helper: Calculate intersection of line with screen border */
static bool calculate_border_intersection(struct vec2 _vMarkerWorldPos, struct vec2 *_pOutIntersection)
{
    /* Get screen center in world space as starting point */
    struct vec2i vScreenCenter = {SCREEN_W / 2, SCREEN_H / 2};
    struct vec2 vScreenCenterWorld;
    camera_screen_to_world(&g_mainCamera, vScreenCenter, &vScreenCenterWorld);

    /* Get screen bounds with custom marker padding and overscan */
    int iLeft, iTop, iRight, iBottom;
    get_padded_screen_bounds(&iLeft, &iTop, &iRight, &iBottom);
    struct vec2i vRectMin = {iLeft, iTop};
    struct vec2i vRectMax = {iRight, iBottom};

    /* Convert world positions to screen space for intersection calculation */
    struct vec2i vStartScreen, vEndScreen;
    camera_world_to_screen(&g_mainCamera, vScreenCenterWorld, &vStartScreen);
    camera_world_to_screen(&g_mainCamera, _vMarkerWorldPos, &vEndScreen);

    struct vec2 vStartScreenF = {(float)vStartScreen.iX, (float)vStartScreen.iY};
    struct vec2 vEndScreenF = {(float)vEndScreen.iX, (float)vEndScreen.iY};

    struct vec2 vIntersection;
    if (math_helper_line_rect_intersection(vStartScreenF, vEndScreenF, vRectMin, vRectMax, &vIntersection))
    {
        /* Convert back to world space */
        camera_screen_to_world(&g_mainCamera, (struct vec2i){(int)vIntersection.fX, (int)vIntersection.fY}, _pOutIntersection);
        return true;
    }

    return false;
}

void minimap_marker_render(void)
{
    if (!s_bInitialized)
        return;

    /* Only render when minimap is active */
    if (!minimap_is_active())
        return;

    const struct entity2D *pUfoNextTarget = ufo_get_next_target();

    /* Set up RDP for sprite rendering */
    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_alphacompare(1);

    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (!s_aMarkers[i].bSlotInUse)
            continue;

        MinimapMarker *pMarker = &s_aMarkers[i];
        struct vec2 vMarkerPos = pMarker->entity.vPos;

        /* Convert marker position to screen space (quantized to prevent jitter) */
        struct vec2i vScreenPos;
        camera_world_to_screen_quantized(&g_mainCamera, vMarkerPos, &vScreenPos);

        /* Check if screen position is within visible bounds (with custom padding and overscan) */
        int iLeft, iTop, iRight, iBottom;
        get_padded_screen_bounds(&iLeft, &iTop, &iRight, &iBottom);
        bool bOnScreen = (vScreenPos.iX >= iLeft && vScreenPos.iX <= iRight && vScreenPos.iY >= iTop && vScreenPos.iY <= iBottom);

        /* TERRA marker: skip rendering if on-screen (only render for border-cases) */
        if (pMarker->eType == MARKER_TERRA && bOnScreen)
            continue;

        /* Render lock-on overlay if this marker is the UFO next target */
        if (pUfoNextTarget && pUfoNextTarget == &pMarker->entity && s_pLockOnSprite)
        {
            struct vec2i vLockOnScreen;
            if (bOnScreen)
            {
                vLockOnScreen = vScreenPos;
            }
            else
            {
                struct vec2 vBorderIntersection;
                if (calculate_border_intersection(vMarkerPos, &vBorderIntersection))
                {
                    camera_world_to_screen_quantized(&g_mainCamera, vBorderIntersection, &vLockOnScreen);
                    clamp_to_padded_edge(&vLockOnScreen);
                }
                else
                {
                    continue; /* Skip if no intersection */
                }
            }

            /* Render lock-on sprite centered on marker (no scaling) */
            rdpq_blitparms_t parms = {
                .cx = s_pLockOnSprite->width / 2,
                .cy = s_pLockOnSprite->height / 2,
            };
            rdpq_sprite_blit(s_pLockOnSprite, vLockOnScreen.iX, vLockOnScreen.iY, &parms);
        }

        if (bOnScreen)
        {
            /* On-screen: render sprite directly at screen position (no zoom applied) */
            sprite_t *pSprite = s_apMarkerSprites[pMarker->eType];
            if (pSprite)
            {
                rdpq_blitparms_t parms = {
                    .cx = pSprite->width / 2,
                    .cy = pSprite->height / 2,
                };
                rdpq_sprite_blit(pSprite, vScreenPos.iX, vScreenPos.iY, &parms);
            }
        }
        else
        {
            /* Off-screen: render at border intersection */
            struct vec2 vBorderIntersection;
            if (calculate_border_intersection(vMarkerPos, &vBorderIntersection))
            {
                /* Convert border intersection to screen space (quantized to prevent jitter) */
                struct vec2i vBorderScreen;
                camera_world_to_screen_quantized(&g_mainCamera, vBorderIntersection, &vBorderScreen);

                /* Clamp to padded screen bounds to ensure proper padding from edge */
                clamp_to_padded_edge(&vBorderScreen);

                /* Render marker sprite at border position (no scaling) */
                sprite_t *pSprite = s_apMarkerSprites[pMarker->eType];
                if (pSprite)
                {
                    rdpq_blitparms_t parms = {
                        .cx = pSprite->width / 2,
                        .cy = pSprite->height / 2,
                    };
                    rdpq_sprite_blit(pSprite, vBorderScreen.iX, vBorderScreen.iY, &parms);
                }
            }
        }
    }
}

const struct entity2D *minimap_marker_get_entity_by_name(const char *_pName)
{
    if (!_pName)
        return NULL;

    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (s_aMarkers[i].bSlotInUse && s_aMarkers[i].pName && strcmp(s_aMarkers[i].pName, _pName) == 0)
        {
            return &s_aMarkers[i].entity;
        }
    }

    return NULL;
}

const struct entity2D *minimap_marker_get_at_world_point(struct vec2 _vWorldPos)
{
    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (s_aMarkers[i].bSlotInUse && entity2d_is_active(&s_aMarkers[i].entity))
        {
            if (entity2d_check_point_collision(&s_aMarkers[i].entity, _vWorldPos))
            {
                return &s_aMarkers[i].entity;
            }
        }
    }

    return NULL;
}

const struct entity2D *minimap_marker_get_at_screen_point(struct vec2i _vScreenPos)
{
    /* Check all markers for collision in screen space */
    const struct entity2D *pClosestMarker = NULL;
    int iClosestDistSq = INT_MAX;

    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (!s_aMarkers[i].bSlotInUse || !entity2d_is_active(&s_aMarkers[i].entity))
            continue;

        /* Skip marker_boy, PIN, and TERRA markers - they're not selectable */
        if (s_aMarkers[i].eType == MARKER_BOY || s_aMarkers[i].eType == MARKER_PIN || s_aMarkers[i].eType == MARKER_TERRA)
            continue;

        /* Convert marker position to screen space (quantized for consistency) */
        struct vec2i vMarkerScreen;
        camera_world_to_screen_quantized(&g_mainCamera, s_aMarkers[i].entity.vPos, &vMarkerScreen);

        /* Check distance in screen space using shared helper */
        if (entity2d_check_collision_circle_screen(vMarkerScreen, s_aMarkers[i].entity.iCollisionRadius, _vScreenPos, 0))
        {
            /* Calculate distance squared for closest marker selection */
            int iDx = vMarkerScreen.iX - _vScreenPos.iX;
            int iDy = vMarkerScreen.iY - _vScreenPos.iY;
            int iDistSq = iDx * iDx + iDy * iDy;

            if (iDistSq < iClosestDistSq)
            {
                pClosestMarker = &s_aMarkers[i].entity;
                iClosestDistSq = iDistSq;
            }
        }
    }

    return pClosestMarker;
}

void minimap_marker_cleanup_stale_pin(void)
{
    const struct entity2D *pCurrentTarget = ufo_get_next_target();

    for (uint16_t i = 0; i < MINIMAP_MARKER_MAX_COUNT; ++i)
    {
        if (s_aMarkers[i].bSlotInUse && s_aMarkers[i].eType == MARKER_PIN)
        {
            /* If this PIN marker is not the current target, it's stale - remove it */
            if (pCurrentTarget != &s_aMarkers[i].entity)
            {
                /* Deactivate marker */
                s_aMarkers[i].bSlotInUse = false;
                s_aMarkers[i].pName = NULL;
                s_aMarkers[i].uUnlockFlag = 0;
                s_aMarkers[i].pPieceEntity = NULL;
                entity2d_deactivate(&s_aMarkers[i].entity);
            }
        }
    }
}
