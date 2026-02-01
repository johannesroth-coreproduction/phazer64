#include "npc_alien.h"
#include "../audio.h"
#include "../camera.h"
#include "../dialogue.h"
#include "../entity2d.h"
#include "../frame_time.h"
#include "../math2d.h"
#include "../math_helper.h"
#include "../path_mover.h"
#include "../resource_helper.h"
#include "gp_state.h"
#include "libdragon.h"
#include "rdpq_mode.h"
#include "space_objects.h"
#include "ufo.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Settings */
#define NPC_ALIEN_ROTATE_LERP 0.3f
#define NPC_ALIEN_MIN_ROTATE_SPEED 0.1f
#define NPC_ALIEN_THRUST_MIN_THRESHOLD 0.2f
#define NPC_ALIEN_THRUST_NORMAL_THRESHOLD 1.2f
#define NPC_ALIEN_THRUST_STRONG_THRESHOLD 3.0f
#define NPC_ALIEN_THRUSTER_WOBBLE_FRAMES 4
#define NPC_ALIEN_PAUSE_DISTANCE 320.0f
#define NPC_ALIEN_RESUME_DISTANCE 160.0f
#define NPC_ALIEN_PATH_DISTANCE_THRESHOLD 100.0f
#define NPC_ALIEN_PATH_SPEED 3.0f
#define NPC_ALIEN_PATH_SINUS_AMPLITUDE 10.0f
#define NPC_ALIEN_PATH_SINUS_FREQUENCY 0.01f
#define NPC_ALIEN_ACCELERATION 0.08f
#define NPC_ALIEN_VELOCITY_DAMPING 0.98f
#define NPC_ALIEN_VELOCITY_DECAY 0.96f
#define NPC_ALIEN_MAX_SPEED 3.9f
#define NPC_ALIEN_SLOWDOWN_DISTANCE 30.0f
#define NPC_ALIEN_TARGET_REACHED_DEADZONE 8.0f
#define NPC_ALIEN_HIT_COOLDOWN_MS 1000
#define NPC_ALIEN_SHIELD_DURATION_MS 300

/* Shared engine sound resource */
static wav64_t *s_pEngineSound = NULL;

/* Helper: Get sprite paths */
static const char *get_sprite_path_alien(npc_type_t type)
{
    switch (type)
    {
    case NPC_TYPE_ALIEN:
        return "rom:/ufo_alien_00.sprite";
    case NPC_TYPE_RHINO:
        return "rom:/ufo_rhino_00.sprite";
    default:
        return NULL;
    }
}

static const char *get_sprite_path_highlight(npc_type_t type)
{
    switch (type)
    {
    case NPC_TYPE_ALIEN:
        return "rom:/ufo_alien_highlight_00.sprite";
    case NPC_TYPE_RHINO:
        return "rom:/ufo_rhino_highlight_00.sprite";
    default:
        return NULL;
    }
}

void npc_alien_configure_path_by_type(PathInstance *pPath, npc_type_t type)
{
    if (!pPath)
        return;

    path_mover_set_speed(pPath, NPC_ALIEN_PATH_SPEED);
    path_mover_set_mode(pPath, PATH_MODE_SINUS_FLY);
    path_mover_set_sinus_params(pPath, NPC_ALIEN_PATH_SINUS_AMPLITUDE, NPC_ALIEN_PATH_SINUS_FREQUENCY);
    path_mover_set_loop(pPath, (type == NPC_TYPE_RHINO));
}

NpcAlienInstance *npc_alien_create(npc_type_t type)
{
    if (type >= NPC_TYPE_COUNT)
        return NULL;

    SpaceObject *pObj = space_objects_spawn_npc(type);
    if (!pObj)
        return NULL;

    NpcData *pData = &pObj->data.npc;

    /* Initialize NpcData */
    pData->type = type;
    pData->fThrusterAnimFrame = 0.0f;
    pData->pPath = NULL;
    pData->eLastState = PATH_STATE_UNPLAYED;
    pData->uHitCooldownEndMs = 0;
    pData->bReachedTarget = false;
    pData->vDirectTarget = vec2_zero();
    pData->bWaitForPlayer = false;
    pData->uShieldEndMs = 0;

    /* Load shared engine sound if not already loaded */
    if (!s_pEngineSound)
    {
        s_pEngineSound = wav64_load("rom:/ufo_engine_loop.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
        if (s_pEngineSound)
        {
            wav64_set_loop(s_pEngineSound, true);
        }
    }

    /* Load sprites */
    const char *pAlienPath = get_sprite_path_alien(type);
    const char *pHighlightPath = get_sprite_path_highlight(type);

    if (pAlienPath)
        pData->pSpriteAlien = sprite_load(pAlienPath);
    if (pHighlightPath)
        pData->pSpriteAlienHighlight = sprite_load(pHighlightPath);

    pData->pSpriteThrusterMini = sprite_load("rom:/ufo_mini_thrust_00.sprite");
    pData->pSpriteThruster = sprite_load("rom:/ufo_thruster_00.sprite");
    pData->pSpriteThrusterStrong = sprite_load("rom:/ufo_thruster_strong_00.sprite");
    pData->pSpriteShield = sprite_load("rom:/ufo_shield_00.sprite");

    /* Initialize entity */
    uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
    uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;

    if (pData->pSpriteAlien)
    {
        entity2d_init_from_sprite(&pObj->entity, vec2_zero(), pData->pSpriteAlien, uFlags, uLayerMask);
    }

    pObj->entity.fAngleRad = 0.0f;
    pObj->entity.vVel = vec2_zero();
    pObj->iHitPoints = 100; /* Default HP */

    return pObj;
}

void npc_alien_destroy(NpcAlienInstance *pInstance)
{
    if (!pInstance)
        return;

    /* Cleanup resources */
    NpcData *pData = &pInstance->data.npc;

    /* Stop engine sound if playing */
    int iChannel = (pData->type == NPC_TYPE_ALIEN) ? MIXER_CHANNEL_NPC_ALIEN : MIXER_CHANNEL_NPC_RHINO;
    if (mixer_ch_playing(iChannel))
    {
        mixer_ch_stop(iChannel);
    }

    if (pData->pPath)
    {
        path_mover_free(pData->pPath);
        pData->pPath = NULL;
    }

    SAFE_FREE_SPRITE(pData->pSpriteAlien);
    SAFE_FREE_SPRITE(pData->pSpriteAlienHighlight);
    SAFE_FREE_SPRITE(pData->pSpriteThrusterMini);
    SAFE_FREE_SPRITE(pData->pSpriteThruster);
    SAFE_FREE_SPRITE(pData->pSpriteThrusterStrong);
    SAFE_FREE_SPRITE(pData->pSpriteShield);

    /* Mark for deletion in space_objects */
    pInstance->markForDelete = true;
    pInstance->entity.pSprite = NULL;

    /* Deactivate immediately to prevent rendering/updates before actual cleanup */
    entity2d_deactivate(&pInstance->entity);

    /* Notify UFO to clear any lock on this entity */
    ufo_deselect_entity_lock_and_marker(&pInstance->entity);
}

static bool npc_alien_update_hit_cooldown(NpcData *pData, uint32_t uCurrentMs)
{
    if (pData->uHitCooldownEndMs > 0 && uCurrentMs >= pData->uHitCooldownEndMs)
    {
        pData->uHitCooldownEndMs = 0;
    }
    return (pData->uHitCooldownEndMs > 0) && (uCurrentMs < pData->uHitCooldownEndMs);
}

static void npc_alien_update_rotation(SpaceObject *pObj, const struct vec2 *pvTargetPos, float fFrameMul)
{
    struct vec2 vToTarget = vec2_sub(*pvTargetPos, pObj->entity.vPos);
    float fDistanceToTarget = vec2_mag(vToTarget);

    if (fDistanceToTarget <= NPC_ALIEN_TARGET_REACHED_DEADZONE)
        return;

    float fTargetAngleRad = fm_atan2f(vToTarget.fX, -vToTarget.fY);
    float fDelta = angle_wrap_rad(fTargetAngleRad - pObj->entity.fAngleRad);
    float fRotateLerp = 1.0f - powf(1.0f - NPC_ALIEN_ROTATE_LERP, fFrameMul);
    pObj->entity.fAngleRad += fDelta * fRotateLerp;
    pObj->entity.fAngleRad = angle_wrap_rad_0_2pi(pObj->entity.fAngleRad);
}

static void npc_alien_update_physics(SpaceObject *pObj, const struct vec2 *pvTargetPos, bool bInHitCooldown, float fFrameMul)
{
    struct vec2 vToTarget = vec2_sub(*pvTargetPos, pObj->entity.vPos);
    float fDistanceToTarget = vec2_mag(vToTarget);
    bool bAccelerating = false;

    if (!bInHitCooldown && fDistanceToTarget > 1e-6f)
    {
        struct vec2 vDirToTarget = vec2_scale(vToTarget, 1.0f / fDistanceToTarget);
        float fAccelScale = 1.0f;
        if (fDistanceToTarget < NPC_ALIEN_SLOWDOWN_DISTANCE)
        {
            fAccelScale = 0.1f + (fDistanceToTarget / NPC_ALIEN_SLOWDOWN_DISTANCE) * 0.9f;
        }

        struct vec2 vAccel = vec2_scale(vDirToTarget, NPC_ALIEN_ACCELERATION * fAccelScale);
        pObj->entity.vVel = vec2_add(pObj->entity.vVel, vec2_scale(vAccel, fFrameMul));
        bAccelerating = true;

        float fVelDot = vec2_dot(pObj->entity.vVel, vDirToTarget);
        if (fVelDot < 0.0f || fDistanceToTarget < NPC_ALIEN_SLOWDOWN_DISTANCE * 0.5f)
        {
            struct vec2 vBrake = vec2_scale(vDirToTarget, fVelDot);
            pObj->entity.vVel = vec2_sub(pObj->entity.vVel, vec2_scale(vBrake, 0.3f * fFrameMul));
        }
    }

    float fDampingBase = bAccelerating ? NPC_ALIEN_VELOCITY_DAMPING : NPC_ALIEN_VELOCITY_DECAY;
    float fDamping = powf(fDampingBase, fFrameMul);
    pObj->entity.vVel = vec2_scale(pObj->entity.vVel, fDamping);

    float fSpeed = vec2_mag(pObj->entity.vVel);
    if (fSpeed > NPC_ALIEN_MAX_SPEED)
    {
        pObj->entity.vVel = vec2_scale(pObj->entity.vVel, NPC_ALIEN_MAX_SPEED / fSpeed);
    }
}

static void npc_alien_update_path_pause_resume(SpaceObject *pObj, const struct vec2 *pvPathPos, bool bInHitCooldown)
{
    NpcData *pData = &pObj->data.npc;
    struct vec2 vPlayerPos = ufo_get_position();
    float fUfoNpcDistance = vec2_dist(pObj->entity.vPos, vPlayerPos);
    float fNpcPathDistance = vec2_dist(pObj->entity.vPos, *pvPathPos);

    path_state_t eState = path_mover_get_state(pData->pPath);

    if (eState == PATH_STATE_PLAYING)
    {
        bool bShouldPauseForPlayer = pData->bWaitForPlayer && (fUfoNpcDistance > NPC_ALIEN_PAUSE_DISTANCE);
        if (bInHitCooldown || fNpcPathDistance > NPC_ALIEN_PATH_DISTANCE_THRESHOLD || bShouldPauseForPlayer)
        {
            path_mover_pause(pData->pPath);
        }
    }
    else if (eState == PATH_STATE_PAUSED && !bInHitCooldown)
    {
        if (fNpcPathDistance <= NPC_ALIEN_PATH_DISTANCE_THRESHOLD * 0.7f)
        {
            if (!pData->bWaitForPlayer || fUfoNpcDistance <= NPC_ALIEN_RESUME_DISTANCE)
            {
                path_mover_resume(pData->pPath);
            }
        }
    }
}

void npc_alien_update_object(SpaceObject *pObj)
{
    if (!pObj || !entity2d_is_active(&pObj->entity))
        return;

    NpcData *pData = &pObj->data.npc;
    float fFrameMul = frame_time_mul();
    uint32_t uCurrentMs = get_ticks_ms();
    bool bIsGrabbed = pObj->entity.bGrabbed;

    /* Determine which channel and base volume to use based on NPC type */
    int iChannel = (pData->type == NPC_TYPE_ALIEN) ? MIXER_CHANNEL_NPC_ALIEN : MIXER_CHANNEL_NPC_RHINO;
    float fBaseVolume = (pData->type == NPC_TYPE_ALIEN) ? AUDIO_BASE_VOLUME_NPC_ALIEN : AUDIO_BASE_VOLUME_NPC_RHINO;

    /* Calculate NPC speed */
    float fSpeed = vec2_mag(pObj->entity.vVel);
    bool bIsPlaying = mixer_ch_playing(iChannel);

    /* Calculate distance from camera once (used for both distance check and panning) */
    struct vec2 vCameraPos = g_mainCamera.vPos;
    struct vec2 vDelta = vec2_sub(pObj->entity.vPos, vCameraPos);
    float fDistance = vec2_mag(vDelta);
    bool bIsTooFar = (fDistance >= NPC_ENGINE_DISTANCE_STOP);

    /* Start or stop engine sound based on movement, distance, and grab state */
    if (bIsGrabbed || bIsTooFar)
    {
        /* Stop engine sound immediately if grabbed or too far away */
        if (bIsPlaying)
        {
            mixer_ch_stop(iChannel);
        }
    }
    else
    {
        /* Not grabbed and within range - handle sound playback based on movement */
        bool bShouldPlay = (fSpeed >= NPC_ALIEN_THRUST_MIN_THRESHOLD);

        if (bShouldPlay && !bIsPlaying)
        {
            /* Start playing engine sound */
            if (s_pEngineSound)
            {
                wav64_play(s_pEngineSound, iChannel);
                /* Set initial frequency based on current speed */
                audio_update_npc_engine_freq(iChannel, fSpeed);
            }
        }
        else if (!bShouldPlay && bIsPlaying)
        {
            /* Stop engine sound when not moving */
            mixer_ch_stop(iChannel);
        }
        else if (bShouldPlay && bIsPlaying)
        {
            /* Update frequency while playing */
            audio_update_npc_engine_freq(iChannel, fSpeed);
        }

        /* Update panning and distance attenuation for this NPC's channel */
        if (bIsPlaying)
        {
            audio_update_npc_pan_and_volume(iChannel, fBaseVolume, pObj->entity.vPos, fDistance);
        }
    }

    /* Check collision event from space_objects */
    if (pObj->bCollisionEventUfo)
    {
        /* Pause path and start cooldown */
        if (pData->pPath && path_mover_get_state(pData->pPath) == PATH_STATE_PLAYING)
        {
            path_mover_pause(pData->pPath);
        }
        pData->uHitCooldownEndMs = uCurrentMs + NPC_ALIEN_HIT_COOLDOWN_MS;
    }

    bool bInHitCooldown = npc_alien_update_hit_cooldown(pData, uCurrentMs);

    /* Update shield timer - clear when expired */
    if (pData->uShieldEndMs > 0 && uCurrentMs >= pData->uShieldEndMs)
    {
        pData->uShieldEndMs = 0;
    }

    struct vec2 vTargetPos = pObj->entity.vPos; /* Default target is self (stop/coast) */
    bool bUsingDirectTarget = false;
    bool bHasActiveObjective = false;

    if (pData->pPath)
    {
        if (pData->vDirectTarget.fX != 0.0f || pData->vDirectTarget.fY != 0.0f)
            pData->vDirectTarget = vec2_zero();
        vTargetPos = path_mover_get_current_pos(pData->pPath);
        bHasActiveObjective = true;
    }
    else if (pData->vDirectTarget.fX != 0.0f || pData->vDirectTarget.fY != 0.0f)
    {
        vTargetPos = pData->vDirectTarget;
        bUsingDirectTarget = true;
        bHasActiveObjective = true;
    }

    /* Removed early return: if no objective, we still run physics to damp velocity */

    bool bShouldPause = false;
    if (bUsingDirectTarget && pData->bWaitForPlayer)
    {
        struct vec2 vPlayerPos = ufo_get_position();
        if (vec2_dist(pObj->entity.vPos, vPlayerPos) > NPC_ALIEN_PAUSE_DISTANCE)
            bShouldPause = true;
    }

    /* Skip rotation and acceleration physics when grabbed by tractor beam
     * (prevents NPC from trying to fly away from beam)
     * Note: Collisions are handled in space_objects.c, so they still work when grabbed */
    if (!bIsGrabbed)
    {
        if (bHasActiveObjective && !bShouldPause && !bInHitCooldown)
        {
            npc_alien_update_rotation(pObj, &vTargetPos, fFrameMul);
        }

        npc_alien_update_physics(pObj, &vTargetPos, bInHitCooldown || bShouldPause, fFrameMul);
    }

    /* Update position (tractor beam sets velocity directly, so this still works) */
    pObj->entity.vPos = vec2_add(pObj->entity.vPos, vec2_scale(pObj->entity.vVel, fFrameMul));
    pData->fThrusterAnimFrame += fFrameMul;

    /* Target-reached checking still works when grabbed (checked after position update) */
    if (!bUsingDirectTarget && pData->pPath)
    {
        npc_alien_update_path_pause_resume(pObj, &vTargetPos, bInHitCooldown);

        /* State tracking */
        path_state_t eState = path_mover_get_state(pData->pPath);
        if (eState == PATH_STATE_FINISHED)
        {
            float fDist = vec2_dist(pObj->entity.vPos, vTargetPos);
            pData->bReachedTarget = (fDist <= NPC_ALIEN_TARGET_REACHED_DEADZONE);
        }
        else
        {
            pData->bReachedTarget = false;
        }
        pData->eLastState = eState;
    }
    else if (bUsingDirectTarget)
    {
        float fDist = vec2_dist(pObj->entity.vPos, pData->vDirectTarget);
        pData->bReachedTarget = (fDist <= NPC_ALIEN_TARGET_REACHED_DEADZONE);
    }
}

void npc_alien_render_object(SpaceObject *pObj, struct vec2i vScreen, float fZoom)
{
    if (!pObj)
        return;
    NpcData *pData = &pObj->data.npc;
    int iCenterX = vScreen.iX;
    int iCenterY = vScreen.iY;

    uint32_t uCurrentMs = get_ticks_ms();
    bool bInHitCooldown = (pData->uHitCooldownEndMs > 0) && (uCurrentMs < pData->uHitCooldownEndMs);
    float fSpeed = vec2_mag(pObj->entity.vVel);
    bool bIsGrabbed = pObj->entity.bGrabbed;

    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_BILINEAR);

    /* Don't render thruster when grabbed by tractor beam */
    if (!bInHitCooldown && !bIsGrabbed && fSpeed >= NPC_ALIEN_THRUST_MIN_THRESHOLD)
    {
        sprite_t *pThruster = NULL;
        if (fSpeed >= NPC_ALIEN_THRUST_STRONG_THRESHOLD)
            pThruster = pData->pSpriteThrusterStrong;
        else if (fSpeed >= NPC_ALIEN_THRUST_NORMAL_THRESHOLD)
            pThruster = pData->pSpriteThruster;
        else
            pThruster = pData->pSpriteThrusterMini;

        if (pThruster)
        {
            int iThrusterX = iCenterX;
            int iThrusterY = iCenterY;
            bool bPhase = (((uint32_t)(pData->fThrusterAnimFrame / NPC_ALIEN_THRUSTER_WOBBLE_FRAMES)) & 1U) != 0;
            if (bPhase)
            {
                float fBackX = -fm_sinf(pObj->entity.fAngleRad);
                float fBackY = fm_cosf(pObj->entity.fAngleRad);
                iThrusterX += (int)roundf(fBackX);
                iThrusterY += (int)roundf(fBackY);
            }

            rdpq_blitparms_t parms = {.cx = pThruster->width / 2, .cy = pThruster->height / 2, .scale_x = fZoom, .scale_y = fZoom, .theta = -pObj->entity.fAngleRad};
            rdpq_sprite_blit(pThruster, iThrusterX, iThrusterY, &parms);
        }
    }

    if (pData->pSpriteAlien)
    {
        rdpq_blitparms_t parms = {.cx = pObj->entity.vHalf.iX, .cy = pObj->entity.vHalf.iY, .scale_x = fZoom, .scale_y = fZoom, .theta = -pObj->entity.fAngleRad};
        rdpq_sprite_blit(pData->pSpriteAlien, iCenterX, iCenterY, &parms);
    }

    if (pData->pSpriteAlienHighlight)
    {
        rdpq_blitparms_t parms = {.cx = pObj->entity.vHalf.iX, .cy = pObj->entity.vHalf.iY, .scale_x = fZoom, .scale_y = fZoom};
        rdpq_sprite_blit(pData->pSpriteAlienHighlight, iCenterX, iCenterY, &parms);
    }

    /* Render shield effect when active */
    bool bShieldActive = (pData->uShieldEndMs > 0) && (uCurrentMs < pData->uShieldEndMs);
    if (bShieldActive && pData->pSpriteShield)
    {
        rdpq_blitparms_t parms = {.cx = pObj->entity.vHalf.iX, .cy = pObj->entity.vHalf.iY, .scale_x = fZoom, .scale_y = fZoom};
        rdpq_sprite_blit(pData->pSpriteShield, iCenterX, iCenterY, &parms);
    }
}

/* Getters/Setters */
const struct entity2D *npc_alien_get_entity(NpcAlienInstance *pInstance)
{
    if (!pInstance)
        return NULL;
    return &pInstance->entity;
}

PathInstance **npc_alien_get_path_ptr(NpcAlienInstance *pInstance)
{
    if (!pInstance)
        return NULL;
    return &pInstance->data.npc.pPath;
}

void npc_alien_set_path(NpcAlienInstance *pInstance, PathInstance *pPath, bool bPositionEntity, bool bWaitForPlayer)
{
    if (!pInstance)
        return;
    NpcData *pData = &pInstance->data.npc;

    if (pData->pPath)
    {
        path_mover_free(pData->pPath);
        pData->pPath = NULL;
    }
    pData->pPath = pPath;
    pData->bWaitForPlayer = bWaitForPlayer;

    if (pPath && bPositionEntity)
    {
        struct vec2 vInitialPos = path_mover_get_current_pos(pPath);
        entity2d_set_pos(&pInstance->entity, vInitialPos);
    }
    npc_alien_reset_reached_target(pInstance);
    pData->vDirectTarget = vec2_zero();
}

void npc_alien_set_direct_target(NpcAlienInstance *pInstance, struct vec2 vTarget, bool bWaitForPlayer)
{
    if (!pInstance)
        return;
    NpcData *pData = &pInstance->data.npc;
    pData->vDirectTarget = vTarget;
    pData->bWaitForPlayer = bWaitForPlayer;
    npc_alien_reset_reached_target(pInstance);
    pData->pPath = NULL;
}

bool npc_alien_get_reached_target(NpcAlienInstance *pInstance)
{
    return pInstance ? pInstance->data.npc.bReachedTarget : false;
}

void npc_alien_reset_reached_target(NpcAlienInstance *pInstance)
{
    if (pInstance)
        pInstance->data.npc.bReachedTarget = false;
}

/* Legacy wrappers removed */
