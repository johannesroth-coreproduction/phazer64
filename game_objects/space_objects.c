#include "space_objects.h"
#include "../anim_effects.h"
#include "../audio.h"
#include "../camera.h"
#include "../frame_time.h"
#include "../math2d.h"
#include "../minimap.h"
#include "../rng.h"
#include "../satellite_pieces.h"
#include "libdragon.h"
#include "meteors.h"
#include "npc_alien.h"
#include "npc_handler.h"
#include "tractor_beam.h"
#include "ufo.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static audio_sound_group_t m_soundGroupExplosions;
static bool m_bSoundGroupInitialized = false;
static wav64_t *m_explosionSounds[3] = {NULL, NULL, NULL};

#define MAX_SPACE_OBJECTS 512

/* Spatial Hash Settings */
#define SPACE_GRID_CELL 48.0f /* Optimized: 48 matches object sizes better (meteors ~24px, UFO ~16px) */
#define SPACE_GRID_BUCKETS 2048
#define SPACE_GRID_BUCKET_MASK (SPACE_GRID_BUCKETS - 1)

/* Helper macro for iterating over objects in a grid range */
#define SPACE_GRID_LOOP(min_x, max_x, min_y, max_y, obj_name)                                                                                                                      \
    for (int _cx = (min_x); _cx <= (max_x); _cx++)                                                                                                                                 \
        for (int _cy = (min_y); _cy <= (max_y); _cy++)                                                                                                                             \
            for (int _j = s_gridHead[space_hash_cell(_cx, _cy)]; _j != -1; _j = s_objects[_j].next_in_cell)                                                                        \
                for (SpaceObject *obj_name = &s_objects[_j]; obj_name && obj_name->bAllocated && !obj_name->markForDelete && entity2d_is_active(&obj_name->entity); obj_name = NULL)

static SpaceObject s_objects[MAX_SPACE_OBJECTS];
static int s_gridHead[SPACE_GRID_BUCKETS];
static int s_aliveCount = 0;
static uint16_t s_renderStamp[MAX_SPACE_OBJECTS];
static uint16_t s_renderStampCounter = 1;

#define SO_BOUNCE_FORCE_UFO 0.3f
#define SO_BOUNCE_FORCE_OBJECT 1.0f
#define SO_BOUNCE_COOLDOWN_MS 250
#define METEOR_BOUNCE_COOLDOWN_MS 1000
#define METEOR_UFO_SEPARATION_MARGIN 0.5f /* margin added to separation to prevent flickering */

#define METEOR_SLEEP_COOLDOWN_FRAMES 30
#define METEOR_CURRENCY_VELOCITY_DAMPING 0.96f
#define METEOR_CURRENCY_SLEEP_VEL_SQ 1e-6f

#define METEOR_MINIMAP_RENDER_INTERVAL 5

/* Helper: Apply impact force to an object's velocity */
static void apply_impact_force(SpaceObject *obj, struct vec2 vImpactDir)
{
    if (!obj || vec2_mag_sq(vImpactDir) <= 1e-6f)
        return;

    /* Normalize impact direction and apply impact strength */
    struct vec2 vNormalized = vec2_normalize(vImpactDir);
    float fImpactStrength = vec2_mag(vImpactDir);
    struct vec2 vImpulse = vec2_scale(vNormalized, fImpactStrength);

    /* Add impulse to object velocity */
    obj->entity.vVel = vec2_add(obj->entity.vVel, vImpulse);
}

/* Helper: Hash function */
static inline int space_hash_cell(int cellX, int cellY)
{
    uint32_t uX = (uint32_t)cellX;
    uint32_t uY = (uint32_t)cellY;
    uint32_t h = (uX * 73856093u) ^ (uY * 19349663u);
    return (int)(h & SPACE_GRID_BUCKET_MASK);
}

/* Helper: Calculate grid bounds */
static inline void space_calc_grid_bounds(float minX, float maxX, float minY, float maxY, int *pMinCellX, int *pMaxCellX, int *pMinCellY, int *pMaxCellY)
{
    *pMinCellX = (int)fm_floorf(minX / SPACE_GRID_CELL);
    *pMaxCellX = (int)fm_floorf(maxX / SPACE_GRID_CELL);
    *pMinCellY = (int)fm_floorf(minY / SPACE_GRID_CELL);
    *pMaxCellY = (int)fm_floorf(maxY / SPACE_GRID_CELL);
}

/* Helper: Calculate camera bounds with optional margin */
static inline void space_calc_camera_bounds(const struct camera2D *pCamera, float fMargin, float *pCamLeft, float *pCamRight, float *pCamTop, float *pCamBottom)
{
    float fZoom = camera_get_zoom(pCamera);
    float fInvZoom = 1.0f / fZoom;
    float fHalfX = (float)pCamera->vHalf.iX * fInvZoom + fMargin;
    float fHalfY = (float)pCamera->vHalf.iY * fInvZoom + fMargin;
    *pCamLeft = pCamera->vPos.fX - fHalfX;
    *pCamRight = pCamera->vPos.fX + fHalfX;
    *pCamTop = pCamera->vPos.fY - fHalfY;
    *pCamBottom = pCamera->vPos.fY + fHalfY;
}

/* Helper: Calculate grid bounds based on camera view and margin */
static inline void space_calc_camera_grid_bounds(const struct camera2D *pCamera, float fMargin, int *pMinX, int *pMaxX, int *pMinY, int *pMaxY)
{
    float fZoom = camera_get_zoom(pCamera);
    float fInvZoom = 1.0f / fZoom;
    float fCamHalfX = (float)pCamera->vHalf.iX * fInvZoom;
    float fCamHalfY = (float)pCamera->vHalf.iY * fInvZoom;

    float fCamLeft = pCamera->vPos.fX - fCamHalfX - fMargin;
    float fCamRight = pCamera->vPos.fX + fCamHalfX + fMargin;
    float fCamTop = pCamera->vPos.fY - fCamHalfY - fMargin;
    float fCamBottom = pCamera->vPos.fY + fCamHalfY + fMargin;

    space_calc_grid_bounds(fCamLeft, fCamRight, fCamTop, fCamBottom, pMinX, pMaxX, pMinY, pMaxY);
}

void space_objects_init(void)
{
    /* Clear and reset the space objects array */
    space_objects_clear();

    /* Initialize subsystems resources */
    meteors_init();
    satellite_pieces_init();
    npc_handler_init();

    if (!m_bSoundGroupInitialized)
    {
        const char *explosion_sounds[] = {"rom:/explode_00.wav64", "rom:/explode_01.wav64", "rom:/explode_02.wav64"};
        audio_sound_group_init(&m_soundGroupExplosions, explosion_sounds, MIXER_CHANNEL_EXPLOSIONS, m_explosionSounds);
        m_bSoundGroupInitialized = true;
    }
}

void space_objects_play_explosion(struct vec2 vPos)
{
    anim_effects_play(ANIM_EFFECT_EXPLOSION, vPos);
    if (m_bSoundGroupInitialized)
    {
        audio_sound_group_play_random(&m_soundGroupExplosions, false);
    }
}

void space_objects_clear(void)
{
    /* Clear all objects and reset spatial hash, but keep memory allocated */
    memset(s_objects, 0, sizeof(s_objects));
    s_aliveCount = 0;
    memset(s_gridHead, 0xFF, sizeof(s_gridHead));
}

void space_objects_free(void)
{
    /* Destroy individual objects first to free per-instance resources */
    for (int i = 0; i < MAX_SPACE_OBJECTS; i++)
    {
        if (s_objects[i].bAllocated && s_objects[i].type == SO_NPC)
        {
            /* NPC might have cleanup needs (sprites, paths) */
            npc_alien_destroy((NpcAlienInstance *)&s_objects[i]);
        }
    }

    /* Free subsystem resources */
    meteors_free();
    satellite_pieces_free();

    /* Reset NPC handler pointers (since the objects they pointed to are gone) */
    npc_handler_init();

    /* Free explosion sound group */
    if (m_bSoundGroupInitialized)
    {
        audio_sound_group_free(&m_soundGroupExplosions);
        m_bSoundGroupInitialized = false;
    }

    /* Clear the object pool */
    space_objects_clear();
}

static SpaceObject *alloc_object(SpaceObjectType type)
{
    for (int i = 0; i < MAX_SPACE_OBJECTS; i++)
    {
        if (!s_objects[i].bAllocated)
        {
            memset(&s_objects[i], 0, sizeof(SpaceObject));
            s_objects[i].bAllocated = true;
            s_objects[i].type = type;
            s_objects[i].entity.uLayerMask = ENTITY_LAYER_GAMEPLAY;
            s_objects[i].entity.uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
            s_aliveCount++;
            return &s_objects[i];
        }
    }
    return NULL;
}

SpaceObject *space_objects_spawn_meteor(struct vec2 pos)
{
    SpaceObject *obj = alloc_object(SO_METEOR);
    if (!obj)
        return NULL;

    obj->entity.vPos = pos;
    /* Meteor specific init will be done by caller or we can move it here if we want strict coupling.
       The plan says "Update meteors_init to use space_objects_spawn_meteor", so the caller (meteors.c)
       will populate the rest (sprites, velocity, etc).
    */
    return obj;
}

SpaceObject *space_objects_spawn_npc(int type)
{
    SpaceObject *obj = alloc_object(SO_NPC);
    if (!obj)
        return NULL;
    obj->data.npc.type = type;
    return obj;
}

SpaceObject *space_objects_spawn_piece(int direction, uint16_t unlock_flag, struct vec2 pos)
{
    SpaceObject *obj = alloc_object(SO_PIECE);
    if (!obj)
        return NULL;
    obj->entity.vPos = pos;
    obj->data.piece.eDirection = direction;
    obj->data.piece.uUnlockFlag = unlock_flag;
    obj->data.piece.bAssembleMode = false; /* Default to false, can be overridden by caller */
    return obj;
}

static void meteor_reflect_velocity(struct vec2 *pVel, struct vec2 vNormal)
{
    float fLenSq = vec2_mag_sq(vNormal);
    if (fLenSq <= 1e-6f)
        return;

    float fInvLen = 1.0f / sqrtf(fLenSq);
    struct vec2 vN = vec2_scale(vNormal, fInvLen);
    float fDot = vec2_dot(*pVel, vN);
    *pVel = vec2_sub(*pVel, vec2_scale(vN, 2.0f * fDot));
}

static void resolve_collision(SpaceObject *a, SpaceObject *b)
{
    /* Elastic bounce */
    struct vec2 vDelta = vec2_sub(b->entity.vPos, a->entity.vPos);
    float fDistSq = vec2_mag_sq(vDelta);
    float fRadSum = (float)(a->entity.iCollisionRadius + b->entity.iCollisionRadius);
    float fRadSumSq = fRadSum * fRadSum;

    if (fDistSq >= fRadSumSq || fDistSq <= 1e-6f)
        return;

    float fDist = sqrtf(fDistSq);
    float fInvDist = 1.0f / fDist;
    struct vec2 vNormal = vec2_scale(vDelta, fInvDist);

    /* Separate overlap equally. */
    float fPenetration = fRadSum - fDist;
    struct vec2 vCorrection = vec2_scale(vNormal, fPenetration * 0.5f);
    a->entity.vPos = vec2_sub(a->entity.vPos, vCorrection);
    b->entity.vPos = vec2_add(b->entity.vPos, vCorrection);

    /* Reflect velocities */
    float fDotA = vec2_dot(a->entity.vVel, vNormal);
    float fDotB = vec2_dot(b->entity.vVel, vNormal);
    struct vec2 vProjA = vec2_scale(vNormal, fDotA);
    struct vec2 vProjB = vec2_scale(vNormal, fDotB);

    a->entity.vVel = vec2_add(vec2_sub(a->entity.vVel, vProjA), vProjB);
    b->entity.vVel = vec2_add(vec2_sub(b->entity.vVel, vProjB), vProjA);

    a->bSleeping = false;
    b->bSleeping = false;
}

static void check_ufo_meteor_collisions(const struct entity2D *pUfo, int iMinCellX, int iMaxCellX, int iMinCellY, int iMaxCellY)
{
    struct vec2 vTotalCorrection = vec2_zero();
    struct vec2 vCollisionNormal = vec2_zero();
    bool bHadBounce = false;
    bool bIsCollidingAny = false;

    SPACE_GRID_LOOP(iMinCellX, iMaxCellX, iMinCellY, iMaxCellY, obj)
    {
        if (obj->type != SO_METEOR)
            continue;

        CollisionEvents events = entity2d_check_collision_and_update(&obj->entity, pUfo);
        if (!events.bIsColliding)
            continue;

        obj->bCollisionEventUfo = true;
        bIsCollidingAny = true;

        /* Calculate collision data (entity2d_check_collision_circle already did this, but we need the values) */
        struct vec2 vDelta = vec2_sub(obj->entity.vPos, pUfo->vPos);
        float fDistSq = vec2_mag_sq(vDelta);
        if (fDistSq <= 1e-6f)
            continue;

        float fDist = sqrtf(fDistSq);
        struct vec2 vNormal = vec2_scale(vDelta, 1.0f / fDist);
        float fRadSum = (float)(obj->entity.iCollisionRadius + pUfo->iCollisionRadius);
        float fPenetration = fRadSum - fDist;

        /* Accumulate correction (use maximum penetration for multiple meteors) */
        float fTotalSeparation = fPenetration + METEOR_UFO_SEPARATION_MARGIN;
        struct vec2 vCorrection = vec2_scale(vNormal, -fTotalSeparation);
        if (vec2_mag_sq(vTotalCorrection) < vec2_mag_sq(vCorrection))
        {
            vTotalCorrection = vCorrection;
            vCollisionNormal = vNormal;
        }

        /* On first contact: apply bounce effect (only once per frame) */
        if (events.bOnTriggerEnter && !bHadBounce)
        {
            bHadBounce = true;
            struct vec2 vUfoVel = vec2_scale(ufo_get_velocity(), -0.5f);
            ufo_set_velocity(vUfoVel);
            meteor_reflect_velocity(&obj->entity.vVel, vNormal);
            obj->bSleeping = false;
        }
    }

    /* Apply accumulated corrections */
    if (bIsCollidingAny)
    {
        ufo_set_position(vec2_add(pUfo->vPos, vTotalCorrection));

        /* Clamp velocity toward meteor (skip on first contact since bounce already handled it) */
        if (!bHadBounce)
        {
            struct vec2 vUfoVel = ufo_get_velocity();
            float fVelDot = vec2_dot(vUfoVel, vCollisionNormal);
            if (fVelDot < 0.0f)
            {
                vUfoVel = vec2_sub(vUfoVel, vec2_scale(vCollisionNormal, fVelDot));
                ufo_set_velocity(vUfoVel);
            }
        }

        /* Reduce thrust effectiveness while colliding to prevent velocity buildup */
        ufo_apply_bounce_effect(METEOR_BOUNCE_COOLDOWN_MS);
    }
}

void space_objects_resolve_ufo_solid_collision(SpaceObject *obj, const struct entity2D *pUfo, CollisionEvents events, bool bPushUfo, float fUfoBounceForce,
                                               int iUfoBounceCooldownMs)
{
    if (!events.bIsColliding)
        return;

    /* Calculate collision normal */
    struct vec2 vDelta = vec2_sub(obj->entity.vPos, pUfo->vPos);
    float fDistSq = vec2_mag_sq(vDelta);
    if (fDistSq <= 1e-6f)
        return;

    float fDist = sqrtf(fDistSq);
    struct vec2 vNormal = vec2_scale(vDelta, 1.0f / fDist);

    /* Push object/UFO out every frame to prevent clipping */
    float fRadiusSum = (float)(obj->entity.iCollisionRadius + pUfo->iCollisionRadius);
    float fPenetration = fRadiusSum - fDist;
    if (fPenetration > 0.0f)
    {
        float fSeparation = fPenetration + 0.5f;
        if (bPushUfo)
        {
            struct vec2 vCorrection = vec2_scale(vNormal, -fSeparation);
            ufo_set_position(vec2_add(pUfo->vPos, vCorrection));

            /* Cancel UFO velocity into object */
            struct vec2 vUfoVel = ufo_get_velocity();
            float fVelDot = vec2_dot(vUfoVel, vNormal);
            if (fVelDot > 0.0f)
            {
                vUfoVel = vec2_sub(vUfoVel, vec2_scale(vNormal, fVelDot));
                ufo_set_velocity(vUfoVel);
            }
        }
        else
        {
            struct vec2 vCorrection = vec2_scale(vNormal, fSeparation);
            obj->entity.vPos = vec2_add(obj->entity.vPos, vCorrection);

            /* Cancel object velocity into UFO */
            float fVelDot = vec2_dot(obj->entity.vVel, vNormal);
            if (fVelDot < 0.0f)
            {
                obj->entity.vVel = vec2_sub(obj->entity.vVel, vec2_scale(vNormal, fVelDot));
            }
        }
    }

    /* Object bounce vs UFO */
    if (events.bOnTriggerEnter)
    {
        /* Apply bounce to UFO */
        struct vec2 vBounceForce = vec2_scale(vNormal, -fUfoBounceForce);
        ufo_set_velocity(vBounceForce);
        ufo_apply_bounce_effect(iUfoBounceCooldownMs);

        if (!bPushUfo)
        {
            /* Objects get pushed away */
            obj->entity.vVel = vec2_add(obj->entity.vVel, vec2_scale(vNormal, SO_BOUNCE_FORCE_OBJECT));
            obj->bSleeping = false;
        }
    }
}

static void check_ufo_collision(SpaceObject *obj)
{
    const struct entity2D *pUfo = ufo_get_entity();
    if (!pUfo || !entity2d_is_active(pUfo) || !entity2d_is_collidable(pUfo))
        return;

    if (obj->type == SO_METEOR)
        return;

    CollisionEvents events = entity2d_check_collision_and_update(&obj->entity, pUfo);
    if (events.bIsColliding)
    {
        obj->bCollisionEventUfo = true;

        if (obj->type == SO_PIECE)
        {
            /* Skip collection for pieces in assemble mode (they can still collide physically) */
            if (obj->data.piece.bAssembleMode)
            {
                space_objects_resolve_ufo_solid_collision(obj, pUfo, events, false, SO_BOUNCE_FORCE_UFO, SO_BOUNCE_COOLDOWN_MS);
            }
            else if (events.bOnTriggerEnter)
            {
                satellite_piece_collect(obj);
            }
        }
        else
        {
            space_objects_resolve_ufo_solid_collision(obj, pUfo, events, false, SO_BOUNCE_FORCE_UFO, SO_BOUNCE_COOLDOWN_MS);
        }
    }
}

void space_objects_update(void)
{
    float fFrameMul = frame_time_mul();
    bool bMinimapActive = minimap_is_active();

    /* Reset Grid - Always reset to ensure it's empty if we skip filling it */
    memset(s_gridHead, 0xFF, sizeof(s_gridHead));

    /* Update & Fill Grid */
    for (int i = 0; i < MAX_SPACE_OBJECTS; i++)
    {
        SpaceObject *obj = &s_objects[i];
        if (obj->markForDelete)
        {
            obj->bAllocated = false;
            s_aliveCount--;
            continue;
        }

        if (!obj->bAllocated || !entity2d_is_active(&obj->entity))
            continue;

        /* Clear event flag after update loop read it? No, set it false here so next frame reads it correctly?
           Actually, we need to clear it BEFORE setting it in collision pass.
           Since collision pass is after update loop, clearing here is correct for the NEXT frame cycle.
           Cycle:
           1. Update Loop (Read flag from prev frame, Clear flag)
           2. Collision Loop (Set flag for next frame)
        */
        bool bWasColliding = obj->bCollisionEventUfo;
        obj->bCollisionEventUfo = false;

        /* Update logic */
        switch (obj->type)
        {
        case SO_NPC:
            /* Restore flag momentarily for update call to read */
            obj->bCollisionEventUfo = bWasColliding;
            npc_alien_update_object(obj);
            obj->bCollisionEventUfo = false; /* Clear it again */
            break;
        case SO_PIECE:
            satellite_piece_update_object(obj);
            break;
        case SO_METEOR:
            if (bMinimapActive)
                break;

            if (obj->entity.bGrabbed && !tractor_beam_is_active())
            {
                /* Prevent stale grabbed state */
                obj->entity.bGrabbed = false;
            }

            /* Basic physics */
            if (obj->entity.bGrabbed)
            {
                /* Wake up if grabbed */
                obj->bSleeping = false;
                obj->data.meteor.iFramesAlive = 0;
                obj->data.meteor.fRotationSpeed = 0.0f;
            }
            else
            {
                obj->entity.fAngleRad += obj->data.meteor.fRotationSpeed * fFrameMul;
                obj->entity.fAngleRad = angle_wrap_rad(obj->entity.fAngleRad);
            }

            /* Tint decay should be time-based, not render-based */
            if (obj->data.meteor.fTintFrames > 0.0f)
            {
                obj->data.meteor.fTintFrames -= fFrameMul;
                if (obj->data.meteor.fTintFrames < 0.0f)
                    obj->data.meteor.fTintFrames = 0.0f;
            }

            /* Position update - always happens unless sleeping (original logic) */
            /* Tractor beam might override position later in tractor_beam_update,
               but originally meteors updated pos here too. */
            if (!obj->bSleeping)
            {
                obj->entity.vPos = vec2_add(obj->entity.vPos, vec2_scale(obj->entity.vVel, fFrameMul));
            }

            if (obj->data.meteor.uCurrencyId > 0 && !obj->entity.bGrabbed && !obj->bSleeping)
            {
                float fDamping = powf(METEOR_CURRENCY_VELOCITY_DAMPING, fFrameMul);
                obj->entity.vVel = vec2_scale(obj->entity.vVel, fDamping);
                if (vec2_mag_sq(obj->entity.vVel) <= METEOR_CURRENCY_SLEEP_VEL_SQ)
                {
                    obj->entity.vVel = vec2_zero();
                    obj->bSleeping = true;
                }
            }

            /* Sleeping logic */
            if (obj->data.meteor.iFramesAlive < METEOR_SLEEP_COOLDOWN_FRAMES)
                obj->data.meteor.iFramesAlive++;

            if (!obj->entity.bGrabbed && obj->data.meteor.iFramesAlive >= METEOR_SLEEP_COOLDOWN_FRAMES)
            {
                float fVelMagSq = vec2_mag_sq(obj->entity.vVel);
                if (fVelMagSq < 1e-6f)
                    obj->bSleeping = true;
            }
            break;
        }

        /* Insert into grid only if minimap is NOT active */
        if (!bMinimapActive)
        {
            int cellX = (int)fm_floorf(obj->entity.vPos.fX / SPACE_GRID_CELL);
            int cellY = (int)fm_floorf(obj->entity.vPos.fY / SPACE_GRID_CELL);
            int bucket = space_hash_cell(cellX, cellY);

            obj->next_in_cell = s_gridHead[bucket];
            s_gridHead[bucket] = i;
        }
    }

    /* Collision Pass - Skip entirely if minimap is active */
    if (bMinimapActive)
    {
        return;
    }

    for (int i = 0; i < MAX_SPACE_OBJECTS; i++)
    {
        SpaceObject *a = &s_objects[i];
        if (!a->bAllocated || !entity2d_is_active(&a->entity) || !entity2d_is_collidable(&a->entity))
            continue;

        int cellX = (int)fm_floorf(a->entity.vPos.fX / SPACE_GRID_CELL);
        int cellY = (int)fm_floorf(a->entity.vPos.fY / SPACE_GRID_CELL);

        /* Check neighbors using optimized macro */
        SPACE_GRID_LOOP(cellX - 1, cellX + 1, cellY - 1, cellY + 1, b)
        {
            if (b <= a)
                continue; /* Avoid duplicates and self */
            if (!entity2d_is_collidable(&b->entity))
                continue;
            if (a->bSleeping && b->bSleeping)
                continue;

            resolve_collision(a, b);
        }
    }

    /* UFO Collision Pass - Optimization: Only check objects in UFO's vicinity */
    const struct entity2D *pUfo = ufo_get_entity();
    if (pUfo && entity2d_is_active(pUfo) && entity2d_is_collidable(pUfo))
    {
        /* Calculate grid bounds for UFO collision area */
        int iMinCellX, iMaxCellX, iMinCellY, iMaxCellY;
        space_calc_grid_bounds(pUfo->vPos.fX - pUfo->iCollisionRadius,
                               pUfo->vPos.fX + pUfo->iCollisionRadius,
                               pUfo->vPos.fY - pUfo->iCollisionRadius,
                               pUfo->vPos.fY + pUfo->iCollisionRadius,
                               &iMinCellX,
                               &iMaxCellX,
                               &iMinCellY,
                               &iMaxCellY);

        /* Expand bounds by 1 cell to catch edge cases, but only if object is large enough to span cells */
        /* For small objects (radius < cell_size/4), we don't need full expansion */
        float fObjectDiameter = (float)(pUfo->iCollisionRadius * 2);
        if (fObjectDiameter >= SPACE_GRID_CELL * 0.5f)
        {
            /* Large object: expand by 1 cell */
            iMinCellX--;
            iMaxCellX++;
            iMinCellY--;
            iMaxCellY++;
        }
        /* Small objects: bounds already calculated correctly, no expansion needed */

        check_ufo_meteor_collisions(pUfo, iMinCellX, iMaxCellX, iMinCellY, iMaxCellY);

        SPACE_GRID_LOOP(iMinCellX, iMaxCellX, iMinCellY, iMaxCellY, obj)
        {
            /* Only check if allocated and active (collidable check is inside check_ufo_collision for now, or redundant) */
            if (obj->type != SO_METEOR)
            {
                check_ufo_collision(obj);
            }
        }
    }

    /* Check collision with satellite center piece */
    satellite_pieces_check_center_collision();
}

/* Helper: Render a single object if visible */
static inline void render_single_object(SpaceObject *obj, float fBaseX, float fBaseY, float fZoom, float fCamLeft, float fCamRight, float fCamTop, float fCamBottom,
                                        int *iLastRenderType, bool bMinimapActive, int iIndex)
{
    if (!obj->bAllocated || !entity2d_is_active(&obj->entity) || !entity2d_is_visible(&obj->entity))
        return;

    /* Minimap culling optimization for meteors */
    if (bMinimapActive && obj->type == SO_METEOR && (iIndex % METEOR_MINIMAP_RENDER_INTERVAL != 0) && obj->data.meteor.uCurrencyId == 0)
        return;

    const struct entity2D *pEnt = &obj->entity;
    if (!pEnt->pSprite)
        return;

    /* Viewport Culling */
    float fEntLeft = pEnt->vPos.fX - (float)pEnt->vHalf.iX;
    float fEntRight = pEnt->vPos.fX + (float)pEnt->vHalf.iX;
    float fEntTop = pEnt->vPos.fY - (float)pEnt->vHalf.iY;
    float fEntBottom = pEnt->vPos.fY + (float)pEnt->vHalf.iY;

    if (fEntRight < fCamLeft || fEntLeft > fCamRight || fEntBottom < fCamTop || fEntTop > fCamBottom)
        return;

    /* Screen Position */
    struct vec2i vScreen;
    vScreen.iX = (int)fm_floorf(fBaseX + pEnt->vPos.fX * fZoom);
    vScreen.iY = (int)fm_floorf(fBaseY + pEnt->vPos.fY * fZoom);

    /* Render Dispatch */
    if (obj->type == SO_METEOR)
    {
        if (*iLastRenderType != 0)
        {
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_filter(FILTER_BILINEAR);
            rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            *iLastRenderType = 0;
        }
        meteor_render_object(obj, vScreen, fZoom);
    }
    else
    {
        /* NPC and PIECE handle their own state */
        if (obj->type == SO_NPC)
            npc_alien_render_object(obj, vScreen, fZoom);
        else if (obj->type == SO_PIECE)
            satellite_piece_render_object(obj, vScreen, fZoom);

        *iLastRenderType = 1;
    }
}

void space_objects_render(void)
{
    bool bMinimapActive = minimap_is_active();
    const struct camera2D *pCamera = &g_mainCamera;
    float fZoom = camera_get_zoom(pCamera);
    float fInvZoom = 1.0f / fZoom;
    float fCamHalfX = (float)pCamera->vHalf.iX * fInvZoom;
    float fCamHalfY = (float)pCamera->vHalf.iY * fInvZoom;
    float fCamLeft = pCamera->vPos.fX - fCamHalfX;
    float fCamRight = pCamera->vPos.fX + fCamHalfX;
    float fCamTop = pCamera->vPos.fY - fCamHalfY;
    float fCamBottom = pCamera->vPos.fY + fCamHalfY;
    float fBaseX = (float)pCamera->vHalf.iX - pCamera->vPos.fX * fZoom;
    float fBaseY = (float)pCamera->vHalf.iY - pCamera->vPos.fY * fZoom;
    int iLastRenderType = -1; /* -1: None, 0: Meteor, 1: Other */

    if (!bMinimapActive)
    {
        s_renderStampCounter++;
        if (s_renderStampCounter == 0)
        {
            memset(s_renderStamp, 0, sizeof(s_renderStamp));
            s_renderStampCounter = 1;
        }

        /* Calculate grid bounds using helper with one cell margin */
        int iMinCellX, iMaxCellX, iMinCellY, iMaxCellY;
        space_calc_camera_grid_bounds(pCamera, SPACE_GRID_CELL, &iMinCellX, &iMaxCellX, &iMinCellY, &iMaxCellY);

        SPACE_GRID_LOOP(iMinCellX, iMaxCellX, iMinCellY, iMaxCellY, obj)
        {
            int j = (int)(obj - s_objects); /* Calculate index from pointer */
            if (s_renderStamp[j] == s_renderStampCounter)
                continue;
            s_renderStamp[j] = s_renderStampCounter;

            render_single_object(obj, fBaseX, fBaseY, fZoom, fCamLeft, fCamRight, fCamTop, fCamBottom, &iLastRenderType, false, j);
        }
        return;
    }

    for (int i = 0; i < MAX_SPACE_OBJECTS; i++)
    {
        SpaceObject *obj = &s_objects[i];
        render_single_object(obj, fBaseX, fBaseY, fZoom, fCamLeft, fCamRight, fCamTop, fCamBottom, &iLastRenderType, true, i);
    }
}

/* API Helpers */
int space_objects_get_active_count(void)
{
    return s_aliveCount;
}

SpaceObject *space_objects_get_object(int index)
{
    if (index < 0 || index >= MAX_SPACE_OBJECTS)
        return NULL;
    return &s_objects[index];
}

int space_objects_get_max_count(void)
{
    return MAX_SPACE_OBJECTS;
}

void space_object_apply_damage(SpaceObject *obj, int iDamage, struct vec2 vImpactDir)
{
    if (!obj || !obj->bAllocated || !entity2d_is_active(&obj->entity))
        return;

    switch (obj->type)
    {
    case SO_METEOR:
        meteor_apply_damage(obj, iDamage, vImpactDir);
        if (obj->data.meteor.uCurrencyId > 0 && vec2_mag_sq(vImpactDir) > 1e-6f)
        {
            obj->bSleeping = false;
            obj->data.meteor.iFramesAlive = 0;
        }
        break;
    case SO_NPC:
        /* NPCs cannot be destroyed - show shield effect and apply impact force */
        {
            NpcData *pData = &obj->data.npc;
            uint32_t uCurrentMs = get_ticks_ms();
            pData->uShieldEndMs = uCurrentMs + 300; /* 300ms shield duration (matches NPC_ALIEN_SHIELD_DURATION_MS) */

            /* Apply impact force to NPC velocity */
            apply_impact_force(obj, vImpactDir);
        }
        break;
    case SO_PIECE:
        /* When hit, make pieces fly away with impact force */
        apply_impact_force(obj, vImpactDir);

        /* Wake up the piece so it starts moving */
        if (vec2_mag_sq(vImpactDir) > 1e-6f)
        {
            obj->bSleeping = false;
        }
        break;
    }
}

/* Queries - adapted from meteors.c */
const struct entity2D *space_objects_get_closest_entity_on_screen(struct vec2 vFrom, const struct camera2D *pCamera, float fActivationMargin)
{
    if (s_aliveCount == 0 || !pCamera)
        return NULL;

    /* Optimized Grid Search */
    float fBestDistSq = 0.0f;
    const SpaceObject *pBest = NULL;

    int iMinCellX, iMaxCellX, iMinCellY, iMaxCellY;
    space_calc_camera_grid_bounds(pCamera, fActivationMargin, &iMinCellX, &iMaxCellX, &iMinCellY, &iMaxCellY);

    /* Precalculate camera bounds for fast visibility check */
    float fCamLeft, fCamRight, fCamTop, fCamBottom;
    space_calc_camera_bounds(pCamera, fActivationMargin, &fCamLeft, &fCamRight, &fCamTop, &fCamBottom);

    SPACE_GRID_LOOP(iMinCellX, iMaxCellX, iMinCellY, iMaxCellY, obj)
    {
        /* Fast visibility check (grid cells are rough approximations) */
        if (obj->entity.vPos.fX < fCamLeft || obj->entity.vPos.fX > fCamRight || obj->entity.vPos.fY < fCamTop || obj->entity.vPos.fY > fCamBottom)
            continue;

        struct vec2 vDelta = vec2_sub(obj->entity.vPos, vFrom);
        float fDistSq = vec2_mag_sq(vDelta);

        if (!pBest || fDistSq < fBestDistSq)
        {
            fBestDistSq = fDistSq;
            pBest = obj;
        }
    }
    return pBest ? &pBest->entity : NULL;
}

const struct entity2D *space_objects_get_closest_entity_in_viewcone(struct vec2 vFrom, float fFacingAngleRad, const struct camera2D *pCamera, float fViewconeHalfAngleRad,
                                                                    float fActivationMargin)
{
    if (s_aliveCount == 0 || !pCamera)
        return NULL;

    if (minimap_is_active())
        return NULL;

    /* --- Optimized Grid Search --- */

    float fBestDistSq = 0.0f;
    const SpaceObject *pBest = NULL;

    /* Precompute Facing Vector & Dot Product Thresholds */
    float fFaceX = fm_sinf(fFacingAngleRad);
    float fFaceY = -fm_cosf(fFacingAngleRad);
    struct vec2 vFacing = {fFaceX, fFaceY};

    float fCosHalfAngle = cosf(fViewconeHalfAngleRad);
    float fCosHalfAngleSq = fCosHalfAngle * fCosHalfAngle;

    int iMinCellX, iMaxCellX, iMinCellY, iMaxCellY;
    space_calc_camera_grid_bounds(pCamera, fActivationMargin, &iMinCellX, &iMaxCellX, &iMinCellY, &iMaxCellY);

    /* Precalculate camera bounds for fast visibility check */
    float fCamLeft, fCamRight, fCamTop, fCamBottom;
    space_calc_camera_bounds(pCamera, fActivationMargin, &fCamLeft, &fCamRight, &fCamTop, &fCamBottom);

    SPACE_GRID_LOOP(iMinCellX, iMaxCellX, iMinCellY, iMaxCellY, obj)
    {
        /* Fast visibility check (grid cells are rough approximations) */
        if (obj->entity.vPos.fX < fCamLeft || obj->entity.vPos.fX > fCamRight || obj->entity.vPos.fY < fCamTop || obj->entity.vPos.fY > fCamBottom)
            continue;

        /* Viewcone Math (Dot Product) */
        struct vec2 vDelta = vec2_sub(obj->entity.vPos, vFrom);
        float fDistSq = vec2_mag_sq(vDelta);
        if (fDistSq <= 1e-6f)
            continue;

        float fDot = vec2_dot(vDelta, vFacing);

        /* 1. Behind check */
        if (fDot < 0.0f)
            continue;

        /* 2. Angle check (compare squares) */
        if ((fDot * fDot) < (fDistSq * fCosHalfAngleSq))
            continue;

        /* Found valid candidate */
        if (!pBest || fDistSq < fBestDistSq)
        {
            fBestDistSq = fDistSq;
            pBest = obj;
        }
    }

    return pBest ? &pBest->entity : NULL;
}

void space_objects_damage_in_radius(struct vec2 vCenter, float fRadius, int iDamage, struct vec2 vImpactDir)
{
    float fRadiusSq = fRadius * fRadius;

    /* Using grid for efficiency */
    int minX, maxX, minY, maxY;
    space_calc_grid_bounds(vCenter.fX - fRadius, vCenter.fX + fRadius, vCenter.fY - fRadius, vCenter.fY + fRadius, &minX, &maxX, &minY, &maxY);

    SPACE_GRID_LOOP(minX, maxX, minY, maxY, obj)
    {
        struct vec2 vDelta = vec2_sub(obj->entity.vPos, vCenter);
        if (vec2_mag_sq(vDelta) <= fRadiusSq)
        {
            /* For radius damage, calculate direction from center to target */
            struct vec2 vImpactToTarget = vec2_normalize(vDelta);
            /* Use the magnitude of the impact direction vector (which contains the impact strength) */
            float fImpactStrength = vec2_mag(vImpactDir);
            struct vec2 vFinalImpact = vec2_scale(vImpactToTarget, fImpactStrength);
            space_object_apply_damage(obj, iDamage, vFinalImpact);
        }
    }
}

bool space_objects_check_bullet_collision(const struct entity2D *pBullet, int iDamage)
{
    int cellX = (int)fm_floorf(pBullet->vPos.fX / SPACE_GRID_CELL);
    int cellY = (int)fm_floorf(pBullet->vPos.fY / SPACE_GRID_CELL);

    /* Don't normalize velocity until we actually have a collision - this avoids expensive sqrtf() calls */
    SPACE_GRID_LOOP(cellX - 1, cellX + 1, cellY - 1, cellY + 1, obj)
    {
        if (entity2d_check_collision_circle(pBullet, &obj->entity))
        {
            /* Only normalize velocity when we actually have a collision */
            struct vec2 vBulletDir = vec2_normalize(pBullet->vVel);
            struct vec2 vImpactDir = vec2_scale(vBulletDir, IMPACT_STRENGTH_BULLET);
            space_object_apply_damage(obj, iDamage, vImpactDir);
            return true;
        }
    }
    return false;
}

bool space_objects_check_laser_collision(struct vec2 vStart, struct vec2 vEnd, struct vec2 *pOutHitPoint, SpaceObject **ppOutTarget)
{
    if (!pOutHitPoint || !ppOutTarget)
        return false;

    if (s_aliveCount == 0)
        return false;

    bool bFoundHit = false;
    float fClosestDistSq = 0.0f;
    struct vec2 vClosestHit = vEnd;
    SpaceObject *pClosestTarget = NULL;

    /* Calculate bounding box of line segment for spatial grid optimization */
    float fMinX = (vStart.fX < vEnd.fX) ? vStart.fX : vEnd.fX;
    float fMaxX = (vStart.fX > vEnd.fX) ? vStart.fX : vEnd.fX;
    float fMinY = (vStart.fY < vEnd.fY) ? vStart.fY : vEnd.fY;
    float fMaxY = (vStart.fY > vEnd.fY) ? vStart.fY : vEnd.fY;

    /* Expand by max radius (assuming 16 for safety, meteors are 12) */
    float fMaxRadius = 16.0f;
    fMinX -= fMaxRadius;
    fMaxX += fMaxRadius;
    fMinY -= fMaxRadius;
    fMaxY += fMaxRadius;

    /* Calculate grid cells covered by the bounding box */
    int iMinCellX, iMaxCellX, iMinCellY, iMaxCellY;
    space_calc_grid_bounds(fMinX, fMaxX, fMinY, fMaxY, &iMinCellX, &iMaxCellX, &iMinCellY, &iMaxCellY);

    /* Check all objects in the grid cells covered by the line */
    SPACE_GRID_LOOP(iMinCellX, iMaxCellX, iMinCellY, iMaxCellY, obj)
    {
        /* Line vs Circle intersection check (inline to avoid dependency on meteor function) */
        struct vec2 vLine = vec2_sub(vEnd, vStart);
        float fLineLenSq = vec2_mag_sq(vLine);
        bool bIntersect = false;
        struct vec2 vHit = vStart;

        if (fLineLenSq <= 1e-6f)
        {
            if (vec2_dist_sq(vStart, obj->entity.vPos) <= (float)(obj->entity.iCollisionRadius * obj->entity.iCollisionRadius))
            {
                bIntersect = true;
                vHit = vStart;
            }
        }
        else
        {
            float fLineLen = sqrtf(fLineLenSq);
            struct vec2 vLineDir = vec2_scale(vLine, 1.0f / fLineLen);
            struct vec2 vToCenter = vec2_sub(obj->entity.vPos, vStart);
            float fProj = vec2_dot(vToCenter, vLineDir);
            float fClampedProj = (fProj < 0.0f) ? 0.0f : ((fProj > fLineLen) ? fLineLen : fProj);
            struct vec2 vClosest = vec2_add(vStart, vec2_scale(vLineDir, fClampedProj));
            if (vec2_dist_sq(vClosest, obj->entity.vPos) <= (float)(obj->entity.iCollisionRadius * obj->entity.iCollisionRadius))
            {
                bIntersect = true;
                vHit = vClosest;
            }
        }

        if (bIntersect)
        {
            float fDistSq = vec2_dist_sq(vStart, vHit);
            if (!bFoundHit || fDistSq < fClosestDistSq)
            {
                bFoundHit = true;
                fClosestDistSq = fDistSq;
                vClosestHit = vHit;
                pClosestTarget = obj;
            }
        }
    }

    if (bFoundHit)
    {
        *pOutHitPoint = vClosestHit;
        *ppOutTarget = pClosestTarget;
        return true;
    }

    *pOutHitPoint = vEnd;
    *ppOutTarget = NULL;
    return false;
}
