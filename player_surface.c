#include "player_surface.h"
#include "entity2d.h"
#include "frame_time.h"
#include "game_objects/gp_camera.h"
#include "game_objects/gp_state.h"
#include "game_objects/triggers_dialogue.h"
#include "game_objects/triggers_load.h"
#include "game_objects/ufo.h"
#include "libdragon.h"
#include "math2d.h"
#include "rdpq_mode.h"
#include "resource_helper.h"
#include "sprite_anim.h"
#include "stick_normalizer.h"
#include "tilemap.h"
#include <fmath.h>
#include <stdio.h>
#include <stdlib.h>

/* Player surface animation - 8 directions (00=south, then CCW), 2 frames each */
#define PLAYER_SURFACE_DIR_COUNT 8
#define PLAYER_SURFACE_FRAMES_PER_DIR 2
#define PLAYER_SURFACE_ANIM_FRAME_TIME 0.1f

static sprite_anim_clip_t *m_aAnimClips[PLAYER_SURFACE_DIR_COUNT] = {NULL};

/* Animation player */
static sprite_anim_player_t m_animPlayer;

/* Player surface entity */
static struct entity2D m_playerSurface;

/* Current direction index (0-7) - keeps last direction when not moving */
static int m_iCurrentDirection = 0;

/* Collision state tracking for player_surface vs UFO trigger */
static bool m_bPlayerInUfoTrigger = false;

/* Sticky normal for diagonal sweep to prevent normal flip-flop */
static struct vec2 m_vLastDiagNormal = {0.0f, 0.0f};
static int m_iLastDiagNormalFrames = 0;

/* Movement settings */
#define PLAYER_SURFACE_SPEED 60.0f /* pixels per second at max stick input */

/* Tunable corner correction distance - adjust sliding around pixel edges */
#define CORNER_CORRECTION_DISTANCE 2.0f
/* Push-out applied after a sweep hit to escape “touching” states */
#define PUSH_OUT_EPSILON 0.01f

/* Debug logging for corner handling (set to 1 to enable) */
#define DEBUG_CORNER_LOG 0

/* Player collision box: */
#define PLAYER_COLLISION_BOX_WIDTH 5
#define PLAYER_COLLISION_BOX_HEIGHT 4
#define PLAYER_COLLISION_BOX_OFFSET_X 1
#define PLAYER_COLLISION_BOX_OFFSET_Y 7

/* Collision box half extents (integers since collision box is pixel-aligned) */
#define PLAYER_COLLISION_BOX_HALF_WIDTH (PLAYER_COLLISION_BOX_WIDTH * 0.5f)
#define PLAYER_COLLISION_BOX_HALF_HEIGHT (PLAYER_COLLISION_BOX_HEIGHT * 0.5f)

/* Pre-computed collision center offset (from sprite center to collision box center) */
static struct vec2 m_vCollisionCenterOffset = {0.0f, 0.0f};

/* Collision box half extents as floats (stored as float to avoid int-to-float conversion overhead on N64) */
static struct vec2 m_vCollisionHalfExtents;

/* Get direction index (0-7) from stick input.
 * 0 = south, then CCW: 1=SE, 2=E, 3=NE, 4=N, 5=NW, 6=W, 7=SW */
static int player_surface_get_direction_index(int _iStickX, int _iStickY)
{
    float fMagnitudeSq = (float)(_iStickX * _iStickX + _iStickY * _iStickY);
    if (fMagnitudeSq < STICK_DEADZONE_SQ)
        return m_iCurrentDirection; /* Keep last direction if within deadzone */

    /* Calculate angle using fm_atan2(stickX, -stickY) to fix Y inversion */
    /* atan2(stickX, -stickY): 0°=down(S), 90°=right(E), 180°=up(N), 270°=left(W) */
    float fAngleRad = fm_atan2f((float)_iStickX, (float)-_iStickY);

    /* Convert to degrees */
    float fAngleDeg = fAngleRad * (180.0f / FM_PI);

    /* Normalize to 0-360 range */
    if (fAngleDeg < 0.0f)
        fAngleDeg += 360.0f;

    /* Map atan2 angles to our system where 0°=south, going CCW */
    /* atan2(stickX, -stickY): 0°(S)→our 0°, 90°(E)→our 90°, 180°(N)→our 180°, 270°(W)→our 270° */
    /* No shift needed - already aligned! */

    /* Divide into 8 sectors (45° each), each sector centered at: 0°, 45°, 90°, 135°, 180°, 225°, 270°, 315° */
    /* Add 22.5° to center the sectors properly */
    int iDirection = (int)((fAngleDeg + 22.5f) / 45.0f) % PLAYER_SURFACE_DIR_COUNT;
    return iDirection;
}

/* Convert direction index (0-7) to normalized unit vector for movement.
 * Direction 0 = south (0°), then CCW: 1=SE(45°), 2=E(90°), 3=NE(135°), 4=N(180°), 5=NW(225°), 6=W(270°), 7=SW(315°)
 * Returns unit vector in world space (positive Y = down/south, positive X = right/east) */
static struct vec2 player_surface_get_direction_vector(int _iDirectionIndex)
{
    /* Pre-computed unit vectors for 8 directions (45° increments) */
    /* Using 1/√2 ≈ 0.70710678118 for diagonal directions */
    static const float fInvSqrt2 = 0.70710678118f;

    static const struct vec2 aDirectionVectors[PLAYER_SURFACE_DIR_COUNT] = {
        {0.0f, 1.0f},             /* 0: South (0°) */
        {fInvSqrt2, fInvSqrt2},   /* 1: Southeast (45°) */
        {1.0f, 0.0f},             /* 2: East (90°) */
        {fInvSqrt2, -fInvSqrt2},  /* 3: Northeast (135°) */
        {0.0f, -1.0f},            /* 4: North (180°) */
        {-fInvSqrt2, -fInvSqrt2}, /* 5: Northwest (225°) */
        {-1.0f, 0.0f},            /* 6: West (270°) */
        {-fInvSqrt2, fInvSqrt2}   /* 7: Southwest (315°) */
    };

    /* Clamp direction index to valid range */
    int iDir = _iDirectionIndex;
    if (iDir < 0)
        iDir = 0;
    if (iDir >= PLAYER_SURFACE_DIR_COUNT)
        iDir = PLAYER_SURFACE_DIR_COUNT - 1;

    return aDirectionVectors[iDir];
}

/* Get player collision box center position in world space (optimized: just add pre-computed offset) */
static inline struct vec2 player_surface_get_collision_center(struct vec2 _vEntityPos)
{
    return vec2_add(_vEntityPos, m_vCollisionCenterOffset);
}

static sprite_anim_clip_t *player_surface_anim_clip_load_dir(int _iDirIndex)
{
    char szPath[64];

    if (_iDirIndex < 0 || _iDirIndex >= PLAYER_SURFACE_DIR_COUNT)
        return NULL;

    sprite_anim_clip_t *pClip = (sprite_anim_clip_t *)malloc(sizeof(sprite_anim_clip_t));
    if (!pClip)
        return NULL;

    pClip->pFrames = (sprite_t **)malloc(sizeof(sprite_t *) * PLAYER_SURFACE_FRAMES_PER_DIR);
    if (!pClip->pFrames)
    {
        free(pClip);
        return NULL;
    }

    bool bAllLoaded = true;
    for (int i = 0; i < PLAYER_SURFACE_FRAMES_PER_DIR; ++i)
    {
        int iFrameIndex = _iDirIndex * PLAYER_SURFACE_FRAMES_PER_DIR + i;
        snprintf(szPath, sizeof(szPath), "rom:/player_surface_small_dir_%02d.sprite", iFrameIndex);
        pClip->pFrames[i] = sprite_load(szPath);
        if (!pClip->pFrames[i])
        {
            bAllLoaded = false;
            break;
        }
    }

    if (!bAllLoaded)
    {
        for (int i = 0; i < PLAYER_SURFACE_FRAMES_PER_DIR; ++i)
        {
            SAFE_FREE_SPRITE(pClip->pFrames[i]);
        }
        free(pClip->pFrames);
        free(pClip);
        return NULL;
    }

    pClip->uFrameCount = PLAYER_SURFACE_FRAMES_PER_DIR;
    pClip->fFrameTimeSeconds = PLAYER_SURFACE_ANIM_FRAME_TIME;
    pClip->ePlayMode = SPRITE_ANIM_PLAYMODE_LOOP;

    return pClip;
}

void player_surface_init(struct vec2 _vWorldPos)
{
    /* Load all 8 direction animation clips */
    for (int i = 0; i < PLAYER_SURFACE_DIR_COUNT; ++i)
    {
        m_aAnimClips[i] = player_surface_anim_clip_load_dir(i);
        if (!m_aAnimClips[i])
        {
            debugf("Failed to load player_surface_small_dir_%02d.sprite\n", i * PLAYER_SURFACE_FRAMES_PER_DIR);
        }
    }

    /* Initialize entity with first sprite (south direction) */
    uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
    uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;

    sprite_t *pInitialSprite = NULL;
    if (m_aAnimClips[0] && m_aAnimClips[0]->pFrames && m_aAnimClips[0]->uFrameCount > 0)
    {
        pInitialSprite = m_aAnimClips[0]->pFrames[0];
    }
    entity2d_init_from_sprite(&m_playerSurface, _vWorldPos, pInitialSprite, uFlags, uLayerMask);

    /* Note: m_playerSurface.vHalf is used for rendering (sprite center), so we keep it as sprite half extents.
     * We use separate m_vCollisionHalfExtents for collision detection. */

    /* Initialize collision half extents as floats (avoid int-to-float conversion overhead on N64) */
    m_vCollisionHalfExtents.fX = (float)PLAYER_COLLISION_BOX_HALF_WIDTH;
    m_vCollisionHalfExtents.fY = (float)PLAYER_COLLISION_BOX_HALF_HEIGHT;

    /* Initialize animation player with south direction as default */
    if (m_aAnimClips[0])
    {
        sprite_anim_player_init(&m_animPlayer, m_aAnimClips[0], &m_playerSurface.pSprite, 1.0f);
        sprite_anim_player_set_speed(&m_animPlayer, 0.0f);
    }

    /* Pre-compute collision center offset (from sprite center to collision box center) */
    /* Use first sprite for collision calculations (assuming all sprites have same size) */
    if (pInitialSprite)
    {
        /* Sprite center to sprite top-left offset */
        float fSpriteCenterToTopLeftX = (float)pInitialSprite->width * 0.5f;
        float fSpriteCenterToTopLeftY = (float)pInitialSprite->height * 0.5f;

        /* Collision box center relative to sprite top-left */
        float fCollisionBoxCenterX = (float)PLAYER_COLLISION_BOX_OFFSET_X + (float)PLAYER_COLLISION_BOX_HALF_WIDTH;
        float fCollisionBoxCenterY = (float)PLAYER_COLLISION_BOX_OFFSET_Y + (float)PLAYER_COLLISION_BOX_HALF_HEIGHT;

        /* Offset from sprite center to collision box center */
        m_vCollisionCenterOffset.fX = fCollisionBoxCenterX - fSpriteCenterToTopLeftX;
        m_vCollisionCenterOffset.fY = fCollisionBoxCenterY - fSpriteCenterToTopLeftY;
    }
}

void player_surface_free(void)
{
    /* Unregister animation player */
    sprite_anim_player_unregister(&m_animPlayer);

    /* Free all animation clips */
    for (int i = 0; i < PLAYER_SURFACE_DIR_COUNT; ++i)
    {
        if (m_aAnimClips[i])
        {
            sprite_anim_clip_free(m_aAnimClips[i]);
            m_aAnimClips[i] = NULL;
        }
    }

    /* Deactivate entity */
    entity2d_deactivate(&m_playerSurface);
}

void player_surface_update(int _iStickX, int _iStickY)
{
    if (!entity2d_is_active(&m_playerSurface))
        return;

    /* Block input during state transitions (landing/launching/fading) */
    if (!gp_state_accepts_input())
    {
        /* No input processing during transitions */
        _iStickX = 0;
        _iStickY = 0;
    }

    /* Process stick input with deadzone and normalization (same as player_jnr.c) */
    struct vec2 vStickInput = vec2_make((float)_iStickX, (float)-_iStickY);
    float fStickMagnitude = vec2_mag(vStickInput);
    float fStickForce = 0.0f;

    if (fStickMagnitude >= STICK_DEADZONE)
    {
        /* Normalize stick force to 0..1 range, accounting for deadzone */
        /* Subtract deadzone so crossing threshold feels like slight tilt, not full force */
        float fEffectiveMagnitude = fStickMagnitude - STICK_DEADZONE;
        float fMaxEffectiveRange = STICK_MAX_MAGNITUDE - STICK_DEADZONE;
        fStickForce = fEffectiveMagnitude / fMaxEffectiveRange;
        if (fStickForce > 1.0f)
            fStickForce = 1.0f;
    }

    /* Determine direction index (snaps to 45-degree angles) - this also selects the sprite frame */
    /* Only update direction if stick is outside deadzone */
    if (fStickMagnitude >= STICK_DEADZONE)
    {
        m_iCurrentDirection = player_surface_get_direction_index(_iStickX, _iStickY);
    }

    /* Update animation clip and playback based on movement */
    sprite_anim_clip_t *pDesiredClip = NULL;
    if (m_iCurrentDirection >= 0 && m_iCurrentDirection < PLAYER_SURFACE_DIR_COUNT)
    {
        pDesiredClip = m_aAnimClips[m_iCurrentDirection];
    }

    bool bIsMoving = fStickForce > 1e-6f;
    if (pDesiredClip)
    {
        if (m_animPlayer.pClip != pDesiredClip)
        {
            sprite_anim_player_set_clip(&m_animPlayer, pDesiredClip);
        }

        if (bIsMoving)
        {
            sprite_anim_player_set_speed(&m_animPlayer, 1.0f);
        }
        else
        {
            sprite_anim_player_set_speed(&m_animPlayer, 0.0f);
            sprite_anim_player_reset(&m_animPlayer);
            if (m_animPlayer.ppSprite && m_animPlayer.pClip->pFrames && m_animPlayer.pClip->uFrameCount > 0)
            {
                *m_animPlayer.ppSprite = m_animPlayer.pClip->pFrames[0];
            }
        }
    }

    /* Get snapped direction vector (45-degree multiple) that matches the sprite frame */
    struct vec2 vMovement = player_surface_get_direction_vector(m_iCurrentDirection);

    /* Apply movement if stick force is above threshold */
    if (fStickForce > 1e-6f)
    {
        /* Scale by speed (pixels per second) and delta time */
        float fMaxDistance = PLAYER_SURFACE_SPEED * fStickForce * frame_time_delta_seconds();

        /* Collision detection: X/Y Split with Corner Correction */
        if (g_mainTilemap.bInitialized)
        {
            struct vec2 vDesiredMove = vec2_scale(vMovement, fMaxDistance);
            struct vec2 vCenterStart = player_surface_get_collision_center(m_playerSurface.vPos);

            /* Box shrink amount for sliding and validity checks.
               Needs to be enough to avoid "touching" ghost collisions (>= 0.1f),
               but not so large that we miss valid corners. */
            float fShrink = 0.25f;

            /* Create a general probe box for validity checks (shrunk on all sides) */
            struct vec2 vProbeExtents = m_vCollisionHalfExtents;
            vProbeExtents.fX -= fShrink;
            vProbeExtents.fY -= fShrink;
            if (vProbeExtents.fX < 0.1f)
                vProbeExtents.fX = 0.1f;
            if (vProbeExtents.fY < 0.1f)
                vProbeExtents.fY = 0.1f;

            /* Detect diagonal case up front: do a combined sweep and, if we hit anything,
               project the desired move onto the tangent (slide) to avoid flip-flopping.
               Keep a sticky normal for a few frames to avoid alternating normals. */
            if (fabsf(vDesiredMove.fX) > 1e-6f && fabsf(vDesiredMove.fY) > 1e-6f)
            {
                /* Use full extents (no shrink) to keep detection stable around corners/bumps */
                struct vec2 vSweepDiagExtents = m_vCollisionHalfExtents;

                tilemap_sweep_result_t resDiag = tilemap_sweep_box(vCenterStart, vDesiredMove, vSweepDiagExtents, TILEMAP_COLLISION_SURFACE);
                if (resDiag.bHit)
                {
                    /* Pick a stable normal: prefer last frame's normal if still opposing motion to prevent flip-flop */
                    struct vec2 vStableNormal = resDiag.vNormal;
                    if (m_iLastDiagNormalFrames > 0)
                    {
                        float fOppose = vec2_dot(vDesiredMove, m_vLastDiagNormal);
                        if (fOppose < -1e-4f)
                        {
                            vStableNormal = m_vLastDiagNormal;
                        }
                    }
                    m_vLastDiagNormal = vStableNormal;
                    m_iLastDiagNormalFrames = 3; /* keep for a few frames */

                    /* Small depenetration along the stable normal to avoid staying in contact */
                    struct vec2 vPush = vec2_scale(vStableNormal, PUSH_OUT_EPSILON);
                    m_playerSurface.vPos = vec2_add(m_playerSurface.vPos, vPush);
                    vCenterStart = player_surface_get_collision_center(m_playerSurface.vPos);

                    struct vec2 vDesiredOrig = vDesiredMove;
                    float fDot = vDesiredMove.fX * vStableNormal.fX + vDesiredMove.fY * vStableNormal.fY;
                    struct vec2 vSlide = vec2_sub(vDesiredMove, vec2_scale(vStableNormal, fDot));

                    /* Only accept the slide if it keeps a meaningful component (avoids zeroing X when pushing left on a flat wall).
                       Otherwise keep the original desired move and let axis splits handle it. */
                    float fLenOrig = vec2_mag(vDesiredOrig);
                    float fLenSlide = vec2_mag(vSlide);
                    if (fLenSlide > fLenOrig * 0.2f)
                    {
                        vDesiredMove = vSlide;
                    }
                    else
                    {
                        vDesiredMove = vDesiredOrig;
                    }

                    if (DEBUG_CORNER_LOG)
                        debugf("[PS CornerDiag] pos(%.2f, %.2f) move(%.2f, %.2f) hit n=(%.2f, %.2f) slide->(%.2f, %.2f) keepSlide=%d\n",
                               m_playerSurface.vPos.fX,
                               m_playerSurface.vPos.fY,
                               vStickInput.fX,
                               vStickInput.fY,
                               vStableNormal.fX,
                               vStableNormal.fY,
                               vDesiredMove.fX,
                               vDesiredMove.fY,
                               (fLenSlide > fLenOrig * 0.2f) ? 1 : 0);
                }
                else if (DEBUG_CORNER_LOG && resDiag.bHit)
                {
                    debugf("[PS CornerDiag] pos(%.2f, %.2f) move(%.2f, %.2f) hit non-corner n=(%.2f, %.2f) t=%.3f\n",
                           m_playerSurface.vPos.fX,
                           m_playerSurface.vPos.fY,
                           vDesiredMove.fX,
                           vDesiredMove.fY,
                           resDiag.vNormal.fX,
                           resDiag.vNormal.fY,
                           resDiag.fTime);
                }
                else if (DEBUG_CORNER_LOG && !resDiag.bHit)
                {
                    debugf("[PS CornerDiag] pos(%.2f, %.2f) move(%.2f, %.2f) no hit\n", m_playerSurface.vPos.fX, m_playerSurface.vPos.fY, vDesiredMove.fX, vDesiredMove.fY);
                }
            }

            /* Decay sticky normal if not used this frame */
            if (m_iLastDiagNormalFrames > 0)
                m_iLastDiagNormalFrames--;

            /* X Axis Movement */
            if (fabsf(vDesiredMove.fX) > 1e-6f)
            {
                /* Calculate X step with corner correction */
                struct vec2 vDeltaX = vec2_make(vDesiredMove.fX, 0.0f);
                struct vec2 vCenter = player_surface_get_collision_center(m_playerSurface.vPos);

                /* Sweep Box: Full Width, Shrunk Height */
                struct vec2 vSweepXExtents = m_vCollisionHalfExtents;
                vSweepXExtents.fY -= fShrink;
                if (vSweepXExtents.fY < 0.1f)
                    vSweepXExtents.fY = 0.1f;

                tilemap_sweep_result_t res = tilemap_sweep_box(vCenter, vDeltaX, vSweepXExtents, TILEMAP_COLLISION_SURFACE);

                /* Only process collision if we actually hit something in the X direction */
                if (res.bHit)
                {
                    bool bCorrected = false;
                    /* Hit vertical wall - Try Nudge Y */
                    /* Only nudge if we hit a wall that opposes our movement (Normal.X opposes Delta.X)
                       and is predominantly vertical */
                    if (fabsf(res.vNormal.fX) > 0.5f)
                    {
                        float aNudges[] = {-1.0f, 1.0f};
                        for (int pass = 0; pass < 2 && !bCorrected; ++pass)
                        {
                            bool bAllowOppositeInput = (pass == 1); /* Second pass can move against input to escape corner glue */
                            for (int i = 0; i < 2 && !bCorrected; ++i)
                            {
                                float fDir = aNudges[i];
                                for (float fDist = 1.0f; fDist <= CORNER_CORRECTION_DISTANCE; fDist += 1.0f)
                                {
                                    float fNudgeY = fDir * fDist;
                                    struct vec2 vNudgedCenter = vec2_add(vCenter, vec2_make(0.0f, fNudgeY));

                                    /* Validity Check: Use Shrunk Probe Box */
                                    if (!tilemap_can_walk_box(vNudgedCenter, vProbeExtents, false, false))
                                        continue;

                                    /* Anti-Fighting: prefer not to move opposite to input unless we are already stuck */
                                    if (!bAllowOppositeInput && vDesiredMove.fY * fNudgeY < -1e-6f)
                                        continue;

                                    tilemap_sweep_result_t resNudge = tilemap_sweep_box(vNudgedCenter, vDeltaX, vSweepXExtents, TILEMAP_COLLISION_SURFACE);
                                    if (!resNudge.bHit)
                                    {
                                        m_playerSurface.vPos.fY += fNudgeY;
                                        m_playerSurface.vPos.fX += vDesiredMove.fX;
                                        bCorrected = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (!bCorrected)
                    {
                        /* If collision time is very small (near 0), we are blocked on X.
                           We stop X movement.
                           BUT, if we are gliding along a wall, we might still have Y movement.
                           The Push-out logic helps us stay valid. */
                        m_playerSurface.vPos.fX += vDeltaX.fX * res.fTime + res.vNormal.fX * PUSH_OUT_EPSILON;

                        if (DEBUG_CORNER_LOG && res.bCornerish)
                        {
                            debugf("[PS CornerX ] pos(%.2f, %.2f) dX=%.3f n=(%.2f, %.2f) t=%.3f\n",
                                   m_playerSurface.vPos.fX,
                                   m_playerSurface.vPos.fY,
                                   vDesiredMove.fX,
                                   res.vNormal.fX,
                                   res.vNormal.fY,
                                   res.fTime);
                        }
                    }
                }
                else
                {
                    m_playerSurface.vPos.fX += vDeltaX.fX;
                }
            }

            /* Y Axis Movement */
            if (fabsf(vDesiredMove.fY) > 1e-6f)
            {
                /* Calculate Y step with corner correction */
                struct vec2 vDeltaY = vec2_make(0.0f, vDesiredMove.fY);
                /* IMPORTANT: Re-calculate center because X-step might have moved us! */
                struct vec2 vCenter = player_surface_get_collision_center(m_playerSurface.vPos);

                /* Sweep Box: Full Height, Shrunk Width */
                struct vec2 vSweepYExtents = m_vCollisionHalfExtents;
                vSweepYExtents.fX -= fShrink;
                if (vSweepYExtents.fX < 0.1f)
                    vSweepYExtents.fX = 0.1f;

                tilemap_sweep_result_t res = tilemap_sweep_box(vCenter, vDeltaY, vSweepYExtents, TILEMAP_COLLISION_SURFACE);

                /* Only process collision if we actually hit something in the Y direction */
                if (res.bHit)
                {
                    bool bCorrected = false;
                    /* Hit horizontal wall - Try Nudge X */
                    if (fabsf(res.vNormal.fY) > 0.5f)
                    {
                        float aNudges[] = {-1.0f, 1.0f};
                        for (int pass = 0; pass < 2 && !bCorrected; ++pass)
                        {
                            bool bAllowOppositeInput = (pass == 1);
                            for (int i = 0; i < 2 && !bCorrected; ++i)
                            {
                                float fDir = aNudges[i];
                                for (float fDist = 1.0f; fDist <= CORNER_CORRECTION_DISTANCE; fDist += 1.0f)
                                {
                                    float fNudgeX = fDir * fDist;
                                    struct vec2 vNudgedCenter = vec2_add(vCenter, vec2_make(fNudgeX, 0.0f));

                                    /* Validity Check: Use Shrunk Probe Box */
                                    if (!tilemap_can_walk_box(vNudgedCenter, vProbeExtents, false, false))
                                        continue;

                                    /* Anti-Fighting */
                                    if (!bAllowOppositeInput && vDesiredMove.fX * fNudgeX < -1e-6f)
                                        continue;

                                    tilemap_sweep_result_t resNudge = tilemap_sweep_box(vNudgedCenter, vDeltaY, vSweepYExtents, TILEMAP_COLLISION_SURFACE);
                                    if (!resNudge.bHit)
                                    {
                                        m_playerSurface.vPos.fX += fNudgeX;
                                        m_playerSurface.vPos.fY += vDesiredMove.fY;
                                        bCorrected = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (!bCorrected)
                    {
                        /* Push out */
                        m_playerSurface.vPos.fY += vDeltaY.fY * res.fTime + res.vNormal.fY * PUSH_OUT_EPSILON;

                        /* Corner case: if this was a corner-ish hit and Y is dominant, drop further X this frame */
                        if (res.bCornerish && fabsf(vDesiredMove.fY) > fabsf(vDesiredMove.fX))
                        {
                            /* Nothing else to do; X already processed, this just prevents future extension if logic changes */
                        }

                        if (DEBUG_CORNER_LOG && res.bCornerish)
                        {
                            debugf("[PS CornerY ] pos(%.2f, %.2f) dY=%.3f n=(%.2f, %.2f) t=%.3f\n",
                                   m_playerSurface.vPos.fX,
                                   m_playerSurface.vPos.fY,
                                   vDesiredMove.fY,
                                   res.vNormal.fX,
                                   res.vNormal.fY,
                                   res.fTime);
                        }
                    }
                }
                else
                {
                    m_playerSurface.vPos.fY += vDeltaY.fY;
                }
            }
        }
        else
        {
            /* No tilemap, allow full movement */
            struct vec2 vDesiredMove = vec2_scale(vMovement, fMaxDistance);
            m_playerSurface.vPos = vec2_add(m_playerSurface.vPos, vDesiredMove);
        }
    }

    /* Check collision with UFO */
    const struct entity2D *pUfoEntity = ufo_get_entity();
    if (pUfoEntity != NULL)
    {
        /* Check collision and update state */
        CollisionEvents events = entity2d_check_collision_and_update(&m_playerSurface, pUfoEntity);
        m_bPlayerInUfoTrigger = events.bIsColliding;
    }
    else
    {
        m_bPlayerInUfoTrigger = false;
    }

    /* Update load trigger collision checks */
    triggers_load_update();

    /* Wrap X coordinate to stay within world bounds */
    if (g_mainTilemap.bInitialized)
    {
        m_playerSurface.vPos.fX = tilemap_wrap_world_x(m_playerSurface.vPos.fX);
    }
}

void player_surface_render(void)
{
    if (!entity2d_is_visible(&m_playerSurface) || !m_playerSurface.pSprite)
        return;

    /* In SURFACE mode, render to intermediate surface using wrapped camera coordinates */
    /* Must use tilemap_world_to_surface instead of camera_world_to_screen to match tilemap rendering */

    /* Ensure both positions are in canonical wrapped space for consistent delta calculation */
    struct vec2 vPlayerWrapped = m_playerSurface.vPos;
    if (g_mainTilemap.bInitialized)
    {
        vPlayerWrapped.fX = tilemap_wrap_world_x(vPlayerWrapped.fX);
    }

    /* Calculate wrapped delta to find shortest rendering path */
    struct vec2 vDelta = gp_camera_calc_wrapped_delta(g_mainCamera.vPos, vPlayerWrapped);
    struct vec2 vAdjustedPos = vec2_add(g_mainCamera.vPos, vDelta);

    struct vec2i vSurfacePos;
    /* Use smooth (non-quantized) conversion for player to avoid snapping to integer positions */
    if (!tilemap_world_to_surface_smooth(vAdjustedPos, &vSurfacePos))
    {
        debugf("SURFACE FAIL: PlayerRaw=%.2f PlayerWrapped=%.2f Cam=%.2f Delta=%.2f Adjusted=%.2f\n",
               m_playerSurface.vPos.fX,
               vPlayerWrapped.fX,
               g_mainCamera.vPos.fX,
               vDelta.fX,
               vAdjustedPos.fX);
    }

    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);

    float fZoom = camera_get_zoom(&g_mainCamera);
    if (fZoom != 1.0f)
        rdpq_mode_filter(FILTER_BILINEAR);
    else
        rdpq_mode_filter(FILTER_POINT);

    rdpq_blitparms_t parms = {.cx = m_playerSurface.vHalf.iX, .cy = m_playerSurface.vHalf.iY, .scale_x = fZoom, .scale_y = fZoom};
    rdpq_sprite_blit(m_playerSurface.pSprite, vSurfacePos.iX, vSurfacePos.iY, &parms);
}

struct vec2 player_surface_get_position(void)
{
    return entity2d_get_pos(&m_playerSurface);
}

const struct entity2D *player_surface_get_entity(void)
{
    return &m_playerSurface;
}

struct vec2 player_surface_get_collision_half_extents(void)
{
    return m_vCollisionHalfExtents;
}

bool player_surface_near_ufo(void)
{
    return m_bPlayerInUfoTrigger;
}

void player_surface_set_position(struct vec2 _vPos)
{
    m_playerSurface.vPos = _vPos;
}
