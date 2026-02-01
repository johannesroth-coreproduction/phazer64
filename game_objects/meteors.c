#include "meteors.h"
#include "../anim_effects.h"
#include "../audio.h"
#include "../camera.h"
#include "../csv_helper.h"
#include "../entity2d.h"
#include "../frame_time.h"
#include "../math2d.h"
#include "../minimap.h"
#include "../resource_helper.h"
#include "../rng.h"
#include "item_turbo.h"
#include "libdragon.h"
#include "rdpq_mode.h"
#include "space_objects.h"
#include "ufo.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Meteors settings */
#define METEOR_MAX_ROT_SPEED 0.05f    /* radians per frame */
#define METEOR_MAX_SPEED 0.0f         /* max linear speed */
#define METEOR_TINT_FRAMES 3          /* frames to tint after taking damage */
#define METEOR_TURBO_DROP_CHANCE 0.5f /* chance (0.0-1.0) that a destroyed meteor drops a turbo item */
#define METEOR_HITPOINTS 5

static sprite_t *m_pMeteorSprite = NULL;
static sprite_t *m_pMeteorCrystalSprite = NULL;

static float randf_symmetric(float _fMax)
{
    return rngf(-_fMax, _fMax);
}

void meteors_free(void)
{
    /* Free sprites if loaded */
    SAFE_FREE_SPRITE(m_pMeteorSprite);
    SAFE_FREE_SPRITE(m_pMeteorCrystalSprite);

    /* Also clear meteors from space_objects to prevent stale sprite pointers */
    /* Note: space_objects_free() handles general cleanup, but we do this here to explicitly
       nullify the sprite pointer since we are freeing the sprite resource itself. */
    int iMax = space_objects_get_max_count();
    for (int i = 0; i < iMax; i++)
    {
        SpaceObject *obj = space_objects_get_object(i);
        if (obj && obj->bAllocated && obj->type == SO_METEOR)
        {
            /* Instead of just marking for delete, immediately deactivate and clear pointer */
            obj->entity.uFlags &= ~ENTITY_FLAG_ACTIVE;
            obj->markForDelete = true;
            /* Important: Clear the entity's sprite pointer so it doesn't reference freed memory */
            obj->entity.pSprite = NULL;
        }
    }
}

void meteors_init(void)
{
    /* Clean up any existing state first */
    meteors_free();

    if (!m_pMeteorSprite)
        m_pMeteorSprite = sprite_load("rom:/meteor_00.sprite");

    if (!m_pMeteorCrystalSprite)
        m_pMeteorCrystalSprite = sprite_load("rom:/meteor_crystal_00.sprite");

    int iTotalRequested = 0;
    int iTotalSpawned = 0;

    /* Try to parse CSV file for meteor spawn data */
    FILE *pFile = fopen("rom:/space/meteors.csv", "r");
    if (pFile)
    {
        char szLine[512];
        while (true)
        {
            bool bTruncated = false;
            if (!csv_helper_fgets_checked(szLine, sizeof(szLine), pFile, &bTruncated))
                break;

            if (bTruncated)
                break;

            csv_helper_strip_eol(szLine);

            /* Skip empty lines */
            if (!szLine[0])
                continue;

            /* Parse line: amount,x,y,width,height */
            char szLineCopy[512];
            if (!csv_helper_copy_line_for_tokenizing(szLine, szLineCopy, sizeof(szLineCopy)))
                continue;

            /* Parse amount (first token) */
            char *pToken = strtok(szLineCopy, ",");
            int iAmount = 0;
            if (!pToken || !csv_helper_parse_int(pToken, &iAmount))
                continue;

            /* Track total requested amount */
            iTotalRequested += iAmount;

            /* Parse position (x,y) and size (width,height) as vec2 tuples */
            struct vec2 vPos, vSize;
            if (!csv_helper_parse_xy_from_tokens(strtok(NULL, ","), strtok(NULL, ","), &vPos))
                continue;
            if (!csv_helper_parse_xy_from_tokens(strtok(NULL, ","), strtok(NULL, ","), &vSize))
                continue;

            /* Spawn amount meteors distributed in the rectangle [x, x+width] x [y, y+height] */
            for (int i = 0; i < iAmount; ++i)
            {
                struct vec2 vSpawnPos = vec2_make(vPos.fX + rngf(0.0f, vSize.fX), vPos.fY + rngf(0.0f, vSize.fY));
                SpaceObject *pMeteor = space_objects_spawn_meteor(vSpawnPos);

                if (pMeteor)
                {
                    /* Configure entity */
                    uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
                    uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;
                    entity2d_init_from_sprite(&pMeteor->entity, vSpawnPos, m_pMeteorSprite, uFlags, uLayerMask);

                    pMeteor->entity.fAngleRad = rngf(-FM_PI, FM_PI);
                    pMeteor->entity.vVel = vec2_make(randf_symmetric(METEOR_MAX_SPEED), randf_symmetric(METEOR_MAX_SPEED));
                    pMeteor->entity.iCollisionRadius = 12;

                    /* Configure meteor data */
                    pMeteor->data.meteor.fRotationSpeed = randf_symmetric(METEOR_MAX_ROT_SPEED);
                    pMeteor->data.meteor.fTintFrames = 0.0f;
                    pMeteor->data.meteor.iFramesAlive = 0;

                    pMeteor->iHitPoints = METEOR_HITPOINTS;
                    pMeteor->bSleeping = false;

                    iTotalSpawned++;
                }
            }
        }
        fclose(pFile);

        if (iTotalSpawned != iTotalRequested)
        {
            debugf("meteors_init: Failed to spawn all requested meteors. Requested: %d, Spawned: %d.\n", iTotalRequested, iTotalSpawned);
        }
    }
}

void meteor_apply_damage(SpaceObject *pMeteor, int iDamage, struct vec2 vImpactDir)
{
    if (!pMeteor || !entity2d_is_active(&pMeteor->entity))
        return;

    /* Currency meteor special handling */
    if (pMeteor->data.meteor.uCurrencyId > 0)
    {
        /* Always apply impact force (even if damage is 0) */
        if (vec2_mag_sq(vImpactDir) > 1e-6f)
        {
            /* Apply impact force using same logic as space_objects */
            struct vec2 vNormalized = vec2_normalize(vImpactDir);
            float fImpactStrength = vec2_mag(vImpactDir);
            struct vec2 vImpulse = vec2_scale(vNormalized, fImpactStrength);
            pMeteor->entity.vVel = vec2_add(pMeteor->entity.vVel, vImpulse);
        }

        /* Normal bullets (damage = 1) don't damage currency meteors */
        /* This means only upgraded bullets (3), laser, or bomb can damage them */
        if (iDamage == 1) /* BULLET_DAMAGE_NORMAL */
        {
            iDamage = 0;
        }

        /* Only apply tint and reduce hit points if actual damage was applied */
        if (iDamage > 0)
        {
            pMeteor->data.meteor.fTintFrames = (float)METEOR_TINT_FRAMES;
            pMeteor->iHitPoints -= iDamage;
        }
    }
    else
    {
        /* Normal meteor behavior */
        pMeteor->data.meteor.fTintFrames = (float)METEOR_TINT_FRAMES;
        pMeteor->iHitPoints -= iDamage;
    }

    if (pMeteor->iHitPoints <= 0)
    {
        /* Check if this is a currency meteor - spawn currency before destroying */
        if (pMeteor->data.meteor.uCurrencyId > 0)
        {
            /* Spawn currency entity at meteor position */
            extern void currency_handler_spawn_from_meteor(struct vec2 vPos, uint8_t uCurrencyId);
            currency_handler_spawn_from_meteor(pMeteor->entity.vPos, pMeteor->data.meteor.uCurrencyId);
        }

        /* Disable meteor and play explosion */
        /* Instead of just deactivating, we mark for delete so space_objects can reclaim it */
        entity2d_deactivate(&pMeteor->entity);
        pMeteor->markForDelete = true;
        pMeteor->entity.pSprite = NULL;

        /* Notify UFO to clear any lock on this entity */
        ufo_deselect_entity_lock_and_marker(&pMeteor->entity);

        space_objects_play_explosion(pMeteor->entity.vPos);

        // /* Chance to drop turbo item at meteor position */
        // if (rngb(METEOR_TURBO_DROP_CHANCE))
        // {
        //     item_turbo_spawn(pMeteor->entity.vPos);
        // }
    }
}

sprite_t *meteors_get_crystal_sprite(void)
{
    return m_pMeteorCrystalSprite;
}

void meteor_render_object(SpaceObject *pMeteor, struct vec2i vScreen, float fZoom)
{
    /* Mode setting removed - handled by space_objects_render batching */
    // rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    // rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    const struct entity2D *pEnt = &pMeteor->entity;
    if (!pEnt->pSprite)
        return;

    rdpq_blitparms_t parms = {
        .cx = pEnt->vHalf.iX,
        .cy = pEnt->vHalf.iY,
        .scale_x = fZoom,
        .scale_y = fZoom,
        .theta = pEnt->fAngleRad,
    };

    /* Tint on hit to show feedback: modulate texture with prim color */
    /* Optimized: only set color if tinted, otherwise assume default white (set in batch start) */
    if (pMeteor->data.meteor.fTintFrames > 0.0f)
    {
        rdpq_set_prim_color(RGBA32(255, 100, 100, 255));

        /* Render */
        rdpq_sprite_blit(pEnt->pSprite, vScreen.iX, vScreen.iY, &parms);

        /* Restore default white for next object in batch */
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    }
    else
    {
        /* Render (color is already white) */
        rdpq_sprite_blit(pEnt->pSprite, vScreen.iX, vScreen.iY, &parms);
    }
}
