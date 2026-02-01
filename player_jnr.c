#include "player_jnr.h"
#include "audio.h"
#include "csv_helper.h"
#include "entity2d.h"
#include "frame_time.h"
#include "game_objects/gp_state.h"
#include "game_objects/triggers_dialogue.h"
#include "game_objects/triggers_load.h"
#include "libdragon.h"
#include "math2d.h"
#include "rdpq_mode.h"
#include "resource_helper.h"
#include "rng.h"
#include "sprite_anim.h"
#include "tilemap.h"
#include <fmath.h>

/* Animation clips (6 total: walk/run/jump × east/west) */
#define ANIM_CLIP_COUNT 6
enum eAnimClipIndex
{
    ANIM_WALK_EAST = 0,
    ANIM_WALK_WEST = 1,
    ANIM_RUN_EAST = 2,
    ANIM_RUN_WEST = 3,
    ANIM_JUMP_EAST = 4,
    ANIM_JUMP_WEST = 5
};
static sprite_anim_clip_t *m_aAnimClips[ANIM_CLIP_COUNT] = {NULL};

/* Animation player */
static sprite_anim_player_t m_animPlayer;

/* Player JNR entity */
static struct entity2D m_playerJnr;

/* Movement state */
static struct vec2 m_vVelocity = {0.0f, 0.0f};
static bool m_bOnGround = false;
static bool m_bWasOnGround = false;
static bool m_bPrevButtonA = false;
static bool m_bFlyMode = false;

/* Coyote time: allow jump for a few frames after leaving ground */
#define COYOTE_TIME_FRAMES 5
static int m_iCoyoteTimeFrames = 0;

/* Input buffering: store jump input for a few frames to use when landing */
#define JUMP_BUFFER_FRAMES 5
static int m_iJumpBufferFrames = 0;

/* Jump sound */
static wav64_t *m_pJumpSound = NULL;

/* Land sound */
static wav64_t *m_pLandSound = NULL;

/* Walk sound */
static wav64_t *m_pWalkSound = NULL;

/* Walk sound timing */
static float m_fWalkSoundTimer = 0.0f;
static bool m_bWasMoving = false;

#include "stick_normalizer.h"

/* Movement settings */
#define PLAYER_JNR_ACCELERATION 400.0f      /* pixels per second squared */
#define PLAYER_JNR_MAX_SPEED 100.0f         /* pixels per second */
#define PLAYER_JNR_MIN_X_SPEED 25.0f        /* minimum X speed threshold - no movement below this */
#define PLAYER_JNR_FRICTION 600.0f          /* pixels per second squared (when on ground) */
#define PLAYER_JNR_AIR_FRICTION 200.0f      /* pixels per second squared (when in air) */
#define PLAYER_JNR_JUMP_VELOCITY -280.0f    /* pixels per second (negative = up) */
#define PLAYER_JNR_GRAVITY 600.0f           /* pixels per second squared */
#define PLAYER_JNR_MAX_FALL_SPEED 300.0f    /* pixels per second */
#define PLAYER_JNR_FLY_ASCEND_SPEED -200.0f /* pixels per second (negative = up) when holding A in fly mode */

/* Walk sound timing (seconds) */
#define PLAYER_JNR_WALK_SOUND_DELAY_WALKING 0.42f
#define PLAYER_JNR_WALK_SOUND_DELAY_RUNNING 0.39f

/* Animation settings */
#define PLAYER_JNR_ANIM_FRAME_TIME_WALK 0.1f /* seconds per frame for walk animation */
#define PLAYER_JNR_ANIM_FRAME_TIME_RUN 0.1f  /* seconds per frame for run animation */
#define PLAYER_JNR_ANIM_FRAME_TIME_JUMP 0.2f /* seconds per frame for jump animation */
#define PLAYER_JNR_WALK_RUN_THRESHOLD 60.0f  /* pixels per second (50% of max speed) */

/* Tunable corner correction distance (how far to search for a gap) */
#define CORNER_CORRECTION_DISTANCE 2.0f

/* Collision detection settings */

/* Player collision box: similar to player_surface */
#define PLAYER_JNR_COLLISION_BOX_WIDTH 8
#define PLAYER_JNR_COLLISION_BOX_HEIGHT 25
#define PLAYER_JNR_COLLISION_BOX_OFFSET_X 6
#define PLAYER_JNR_COLLISION_BOX_OFFSET_Y 3

/* Collision box half extents */
#define PLAYER_JNR_COLLISION_BOX_HALF_WIDTH (PLAYER_JNR_COLLISION_BOX_WIDTH * 0.5f)
#define PLAYER_JNR_COLLISION_BOX_HALF_HEIGHT (PLAYER_JNR_COLLISION_BOX_HEIGHT * 0.5f)

/* Pre-computed collision center offset (from sprite center to collision box center) */
static struct vec2 m_vCollisionCenterOffset = {0.0f, 0.0f};

/* Collision box half extents as floats */
static struct vec2 m_vCollisionHalfExtents;

/* Get player collision box center position in world space */
static inline struct vec2 player_jnr_get_collision_center(struct vec2 _vEntityPos)
{
    return vec2_add(_vEntityPos, m_vCollisionCenterOffset);
}

/* Check if player is on ground by testing a small distance below */
static bool player_jnr_check_on_ground(struct vec2 _vPos)
{
    struct vec2 vCollisionCenter = player_jnr_get_collision_center(_vPos);

    /* Test a small distance below the player (2 pixels to account for floating point precision) */
    struct vec2 vTestPos = vec2_add(vCollisionCenter, vec2_make(0.0f, 2.0f));

    /* Use narrower box for ground check to prevent wall sticking */
    /* Shrink width by 2px on each side */
    struct vec2 vGroundCheckExtents = m_vCollisionHalfExtents;
    vGroundCheckExtents.fX -= 2.0f;
    if (vGroundCheckExtents.fX < 1.0f)
        vGroundCheckExtents.fX = 1.0f;

    /* Check collision with JNR collision layer */
    return tilemap_check_collision_layer(vTestPos, vGroundCheckExtents, TILEMAP_LAYER_JNR_COLLISION);
}

void player_jnr_init(void)
{
    /* Initial world position defaults to zero (will be set by gp_state) */
    struct vec2 vWorldPos = vec2_make(0.0f, 0.0f);

    /* Load animation clips */
    m_aAnimClips[ANIM_WALK_EAST] = sprite_anim_clip_load("rom:/player_jnr_walk_east_%02d.sprite", 8, PLAYER_JNR_ANIM_FRAME_TIME_WALK, SPRITE_ANIM_PLAYMODE_LOOP);
    m_aAnimClips[ANIM_WALK_WEST] = sprite_anim_clip_load("rom:/player_jnr_walk_west_%02d.sprite", 8, PLAYER_JNR_ANIM_FRAME_TIME_WALK, SPRITE_ANIM_PLAYMODE_LOOP);
    m_aAnimClips[ANIM_RUN_EAST] = sprite_anim_clip_load("rom:/player_jnr_run_east_%02d.sprite", 8, PLAYER_JNR_ANIM_FRAME_TIME_RUN, SPRITE_ANIM_PLAYMODE_LOOP);
    m_aAnimClips[ANIM_RUN_WEST] = sprite_anim_clip_load("rom:/player_jnr_run_west_%02d.sprite", 8, PLAYER_JNR_ANIM_FRAME_TIME_RUN, SPRITE_ANIM_PLAYMODE_LOOP);
    m_aAnimClips[ANIM_JUMP_EAST] = sprite_anim_clip_load("rom:/player_jnr_jump_east_%02d.sprite", 8, PLAYER_JNR_ANIM_FRAME_TIME_JUMP, SPRITE_ANIM_PLAYMODE_LOOP);
    m_aAnimClips[ANIM_JUMP_WEST] = sprite_anim_clip_load("rom:/player_jnr_jump_west_%02d.sprite", 8, PLAYER_JNR_ANIM_FRAME_TIME_JUMP, SPRITE_ANIM_PLAYMODE_LOOP);

    /* Check if clips loaded successfully */
    sprite_t *pInitialSprite = NULL;
    for (int i = 0; i < ANIM_CLIP_COUNT; ++i)
    {
        if (!m_aAnimClips[i])
        {
            debugf("Failed to load player_jnr animation clip %d\n", i);
        }
        else if (i == ANIM_WALK_EAST && m_aAnimClips[i]->pFrames && m_aAnimClips[i]->uFrameCount > 0)
        {
            /* Use first frame of walk_east as initial sprite */
            pInitialSprite = m_aAnimClips[i]->pFrames[0];
        }
    }

    /* Initialize entity */
    uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
    uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;

    entity2d_init_from_sprite(&m_playerJnr, vWorldPos, pInitialSprite, uFlags, uLayerMask);

    /* Initialize animation player with walk_east as default */
    if (m_aAnimClips[ANIM_WALK_EAST])
    {
        sprite_anim_player_init(&m_animPlayer, m_aAnimClips[ANIM_WALK_EAST], &m_playerJnr.pSprite, 1.0f);
    }

    /* Initialize collision half extents */
    m_vCollisionHalfExtents.fX = PLAYER_JNR_COLLISION_BOX_HALF_WIDTH;
    m_vCollisionHalfExtents.fY = PLAYER_JNR_COLLISION_BOX_HALF_HEIGHT;

    /* Pre-compute collision center offset */
    if (pInitialSprite)
    {
        float fSpriteCenterToTopLeftX = (float)pInitialSprite->width * 0.5f;
        float fSpriteCenterToTopLeftY = (float)pInitialSprite->height * 0.5f;

        float fCollisionBoxCenterX = (float)PLAYER_JNR_COLLISION_BOX_OFFSET_X + (float)PLAYER_JNR_COLLISION_BOX_HALF_WIDTH;
        float fCollisionBoxCenterY = (float)PLAYER_JNR_COLLISION_BOX_OFFSET_Y + (float)PLAYER_JNR_COLLISION_BOX_HALF_HEIGHT;

        m_vCollisionCenterOffset.fX = fCollisionBoxCenterX - fSpriteCenterToTopLeftX;
        m_vCollisionCenterOffset.fY = fCollisionBoxCenterY - fSpriteCenterToTopLeftY;
    }

    /* Reset movement state */
    m_vVelocity = vec2_zero();
    m_bOnGround = false;
    m_bWasOnGround = false;
    m_bPrevButtonA = false;
    m_bFlyMode = false;
    m_iCoyoteTimeFrames = 0;
    m_iJumpBufferFrames = 0;

    /* Load jump sound */
    m_pJumpSound = wav64_load("rom:/jnr_jump.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    /* Load land sound */
    m_pLandSound = wav64_load("rom:/jnr_land.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    /* Load walk sound */
    m_pWalkSound = wav64_load("rom:/jnr_walk.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
}

void player_jnr_free(void)
{
    /* Unregister animation player */
    sprite_anim_player_unregister(&m_animPlayer);

    /* Free all animation clips */
    for (int i = 0; i < ANIM_CLIP_COUNT; ++i)
    {
        if (m_aAnimClips[i])
        {
            sprite_anim_clip_free(m_aAnimClips[i]);
            m_aAnimClips[i] = NULL;
        }
    }

    /* Free jump sound */
    SAFE_CLOSE_WAV64(m_pJumpSound);

    /* Free land sound */
    SAFE_CLOSE_WAV64(m_pLandSound);

    /* Free walk sound */
    SAFE_CLOSE_WAV64(m_pWalkSound);

    /* Reset MIXER_CHANNEL_UFO frequency to normal */
    mixer_ch_set_freq(MIXER_CHANNEL_UFO, AUDIO_BITRATE);

    /* Reset MIXER_CHANNEL_ENGINE frequency to normal */
    mixer_ch_set_freq(MIXER_CHANNEL_ENGINE, AUDIO_BITRATE);

    entity2d_deactivate(&m_playerJnr);
}

void player_jnr_update(int _iStickX, bool _bButtonA, bool _bButtonLPressed)
{
    if (!entity2d_is_active(&m_playerJnr))
        return;

    float fDeltaTime = frame_time_delta_seconds();

    /* Block input during state transitions (landing/launching/fading) */
    /* BUT still run collision detection and physics so player can fall/land properly */
    if (!gp_state_accepts_input())
    {
        _iStickX = 0;
        _bButtonA = false;
        _bButtonLPressed = false;
    }

    /* Toggle fly mode on L button press */
    if (_bButtonLPressed)
    {
        m_bFlyMode = !m_bFlyMode;
    }

    /* Update horizontal movement (direct velocity control based on stick input) */
    float fFriction = m_bOnGround ? PLAYER_JNR_FRICTION : PLAYER_JNR_AIR_FRICTION;

    /* Process stick input with deadzone and normalization (same as ufo.c) */
    float fStickMagnitude = (_iStickX >= 0) ? (float)_iStickX : -(float)_iStickX;
    float fStickForce = 0.0f;
    int iStickDir = 0;

    if (fStickMagnitude >= STICK_DEADZONE)
    {
        /* Normalize stick force to 0..1 range, accounting for deadzone */
        /* Subtract deadzone so crossing threshold feels like slight tilt, not full force */
        float fEffectiveMagnitude = fStickMagnitude - STICK_DEADZONE;
        float fMaxEffectiveRange = STICK_MAX_MAGNITUDE - STICK_DEADZONE;
        fStickForce = fEffectiveMagnitude / fMaxEffectiveRange;
        if (fStickForce > 1.0f)
            fStickForce = 1.0f;

        /* Get direction (-1 or +1) */
        iStickDir = (_iStickX > 0) ? 1 : -1;
    }

    if (fStickForce > 0.0f)
    {
        /* Set velocity directly proportional to stick input (no acceleration) */
        /* Small stick = slow constant speed, full stick = max speed */
        float fDesiredSpeed = fStickForce * PLAYER_JNR_MAX_SPEED;

        /* Apply minimum speed threshold - clamp to minimum when crossing deadzone */
        if (fDesiredSpeed < PLAYER_JNR_MIN_X_SPEED)
        {
            fDesiredSpeed = PLAYER_JNR_MIN_X_SPEED;
        }

        m_vVelocity.fX = (float)iStickDir * fDesiredSpeed;
    }
    else
    {
        /* Apply friction when no stick input */
        if (m_vVelocity.fX > 0.0f)
        {
            m_vVelocity.fX -= fFriction * fDeltaTime;
            if (m_vVelocity.fX < 0.0f)
                m_vVelocity.fX = 0.0f;
        }
        else if (m_vVelocity.fX < 0.0f)
        {
            m_vVelocity.fX += fFriction * fDeltaTime;
            if (m_vVelocity.fX > 0.0f)
                m_vVelocity.fX = 0.0f;
        }
    }

    /* Check if on ground before applying jump */
    m_bWasOnGround = m_bOnGround;
    m_bOnGround = player_jnr_check_on_ground(m_playerJnr.vPos);

    /* Update coyote time: countdown when not on ground, reset when on ground */
    if (m_bOnGround)
    {
        m_iCoyoteTimeFrames = COYOTE_TIME_FRAMES;
    }
    else if (m_iCoyoteTimeFrames > 0)
    {
        m_iCoyoteTimeFrames--;
    }

    /* Update jump buffer: countdown each frame, reset when button is pressed */
    if (_bButtonA && !m_bPrevButtonA)
    {
        /* Button just pressed - set buffer */
        m_iJumpBufferFrames = JUMP_BUFFER_FRAMES;
    }
    else if (m_iJumpBufferFrames > 0)
    {
        m_iJumpBufferFrames--;
    }

    if (m_bFlyMode)
    {
        /* Fly mode: holding A makes player ascend (no gravity when A is pressed) */
        if (_bButtonA)
        {
            /* Set upward velocity, no gravity */
            m_vVelocity.fY = PLAYER_JNR_FLY_ASCEND_SPEED;
            m_bOnGround = false;
        }
        else
        {
            /* Not holding A: apply gravity */
            m_vVelocity.fY += PLAYER_JNR_GRAVITY * fDeltaTime;

            /* Clamp fall speed */
            if (m_vVelocity.fY > PLAYER_JNR_MAX_FALL_SPEED)
                m_vVelocity.fY = PLAYER_JNR_MAX_FALL_SPEED;
        }
    }
    else
    {
        /* Normal mode: handle jumping */
        bool bButtonAPressed = _bButtonA && !m_bPrevButtonA;
        bool bButtonAReleased = !_bButtonA && m_bPrevButtonA;
        bool bJustLanded = !m_bWasOnGround && m_bOnGround;

        /* Check if we can jump:
         * 1. Button just pressed AND (on ground OR coyote time active)
         * 2. Jump buffer active AND (on ground OR just landed)
         * 3. Button held AND just landed (original behavior)
         * 4. Button held AND on ground (original behavior) */
        bool bCanJumpFromCoyote = m_iCoyoteTimeFrames > 0;
        bool bCanJumpFromBuffer = m_iJumpBufferFrames > 0 && (m_bOnGround || bJustLanded);
        bool bShouldJump = (bButtonAPressed && (m_bOnGround || bCanJumpFromCoyote)) || bCanJumpFromBuffer || (_bButtonA && bJustLanded) || (_bButtonA && m_bOnGround);

        if (bShouldJump)
        {
            /* Jump */
            m_vVelocity.fY = PLAYER_JNR_JUMP_VELOCITY;
            m_bOnGround = false;
            /* Consume coyote time and jump buffer */
            m_iCoyoteTimeFrames = 0;
            m_iJumpBufferFrames = 0;

            /* Play jump sound with random frequency between 0.8-1.0 */
            if (m_pJumpSound)
            {
                float fFreq = AUDIO_BITRATE * rngf(0.5f, 1.0f);
                mixer_ch_set_freq(MIXER_CHANNEL_UFO, fFreq);
                wav64_play(m_pJumpSound, MIXER_CHANNEL_UFO);
            }
        }

        /* Variable jump height: if A is released while moving upward, reduce upward velocity */
        if (bButtonAReleased && !m_bOnGround && m_vVelocity.fY < 0.0f)
        {
            /* Reduce upward velocity significantly so player starts falling sooner */
            /* Clamp to a small upward velocity to allow for smooth transition */
            if (m_vVelocity.fY < -50.0f)
            {
                m_vVelocity.fY = -50.0f;
            }
        }

        /* Jump animation frame skip: if A is released and jump anim is on frame <= 2, jump to frame 3 */
        if (bButtonAReleased && m_animPlayer.pClip && (m_animPlayer.pClip == m_aAnimClips[ANIM_JUMP_EAST] || m_animPlayer.pClip == m_aAnimClips[ANIM_JUMP_WEST]))
        {
            if (m_animPlayer.uCurrentFrame <= 2)
            {
                m_animPlayer.uCurrentFrame = 3;
                /* Reset time accumulator so frame change is immediate */
                m_animPlayer.fTimeAccumulator = 0.0f;
                /* Update sprite immediately if auto-update is enabled */
                if (m_animPlayer.ppSprite && m_animPlayer.pClip->pFrames && m_animPlayer.uCurrentFrame < m_animPlayer.pClip->uFrameCount)
                {
                    *m_animPlayer.ppSprite = m_animPlayer.pClip->pFrames[m_animPlayer.uCurrentFrame];
                }
            }
        }

        m_bPrevButtonA = _bButtonA;

        /* Apply gravity */
        if (!m_bOnGround)
        {
            m_vVelocity.fY += PLAYER_JNR_GRAVITY * fDeltaTime;

            /* Clamp fall speed */
            if (m_vVelocity.fY > PLAYER_JNR_MAX_FALL_SPEED)
                m_vVelocity.fY = PLAYER_JNR_MAX_FALL_SPEED;
        }
        else
        {
            /* On ground, stop vertical velocity */
            if (m_vVelocity.fY > 0.0f)
                m_vVelocity.fY = 0.0f;
        }
    }

    /* Store ground state before movement (for landing detection) */
    bool bWasOnGroundBeforeMove = m_bOnGround;

    /* Calculate movement delta */
    struct vec2 vMovement = vec2_scale(m_vVelocity, fDeltaTime);

    /* Handle horizontal collision with corner correction */
    if (fabsf(vMovement.fX) > 1e-6f)
    {
        struct vec2 vDeltaX = vec2_make(vMovement.fX, 0.0f);
        struct vec2 vCenter = player_jnr_get_collision_center(m_playerJnr.vPos);

        /* Slight vertical shrink for horizontal sweep to avoid hitting the floor we are standing on */
        struct vec2 vSweepHalfExtents = m_vCollisionHalfExtents;
        vSweepHalfExtents.fY -= 0.1f;
        if (vSweepHalfExtents.fY < 0.1f)
            vSweepHalfExtents.fY = 0.1f;

        tilemap_sweep_result_t res = tilemap_sweep_box(vCenter, vDeltaX, vSweepHalfExtents, TILEMAP_COLLISION_JNR);

        if (res.bHit)
        {
            /* Hit a wall - Try Corner Correction */
            bool bCorrected = false;

            /* Only try correction if we hit a vertical wall */
            if (fabsf(res.vNormal.fX) > 0.5f)
            {
                /* Probe Up and Down */
                float aNudges[] = {-1.0f, 1.0f}; /* Directions: Up, Down */

                for (int i = 0; i < 2; ++i)
                {
                    float fDir = aNudges[i];
                    for (float fDist = 1.0f; fDist <= CORNER_CORRECTION_DISTANCE; fDist += 1.0f)
                    {
                        float fNudgeY = fDir * fDist;
                        struct vec2 vNudgedCenter = vec2_add(vCenter, vec2_make(0.0f, fNudgeY));

                        /* Check if nudge position is valid (not inside collision) */
                        if (tilemap_check_collision_layer(vNudgedCenter, m_vCollisionHalfExtents, TILEMAP_LAYER_JNR_COLLISION))
                            continue;

                        /* Sweep from nudged position */
                        tilemap_sweep_result_t resNudge = tilemap_sweep_box(vNudgedCenter, vDeltaX, vSweepHalfExtents, TILEMAP_COLLISION_JNR);

                        /* If clear (no hit), we found a path */
                        if (!resNudge.bHit)
                        {
                            /* Apply nudge and move */
                            m_playerJnr.vPos.fY += fNudgeY;
                            m_playerJnr.vPos.fX += vDeltaX.fX;
                            bCorrected = true;
                            break;
                        }
                    }
                    if (bCorrected)
                        break;
                }
            }

            if (!bCorrected)
            {
                /* Push out slightly (0.002f) to avoid "touching" state which can cause sticky behavior in vertical sweeps */
                m_playerJnr.vPos.fX += vDeltaX.fX * res.fTime + res.vNormal.fX * 0.002f;
                m_vVelocity.fX = 0.0f;
            }
        }
        else
        {
            m_playerJnr.vPos.fX += vDeltaX.fX;
        }
    }

    /* Handle vertical collision with corner correction */
    if (fabsf(vMovement.fY) > 1e-6f)
    {
        struct vec2 vDeltaY = vec2_make(0.0f, vMovement.fY);
        struct vec2 vCenter = player_jnr_get_collision_center(m_playerJnr.vPos);

        /* Slight horizontal shrink for vertical sweep to avoid hitting walls we are touching */
        struct vec2 vSweepHalfExtents = m_vCollisionHalfExtents;
        vSweepHalfExtents.fX -= 0.1f;
        if (vSweepHalfExtents.fX < 0.1f)
            vSweepHalfExtents.fX = 0.1f;

        tilemap_sweep_result_t res = tilemap_sweep_box(vCenter, vDeltaY, vSweepHalfExtents, TILEMAP_COLLISION_JNR);

        if (res.bHit)
        {
            /* Hit ceiling or floor - Try Corner Correction */
            bool bCorrected = false;

            /* Only try correction if we hit a horizontal surface (ceiling/floor) */
            if (fabsf(res.vNormal.fY) > 0.5f)
            {
                /* Probe Left and Right */
                float aNudges[] = {-1.0f, 1.0f}; /* Directions: Left, Right */

                for (int i = 0; i < 2; ++i)
                {
                    float fDir = aNudges[i];
                    for (float fDist = 1.0f; fDist <= CORNER_CORRECTION_DISTANCE; fDist += 1.0f)
                    {
                        float fNudgeX = fDir * fDist;
                        struct vec2 vNudgedCenter = vec2_add(vCenter, vec2_make(fNudgeX, 0.0f));

                        /* Check if nudge position is valid (not inside collision) */
                        if (tilemap_check_collision_layer(vNudgedCenter, m_vCollisionHalfExtents, TILEMAP_LAYER_JNR_COLLISION))
                            continue;

                        /* Sweep from nudged position */
                        tilemap_sweep_result_t resNudge = tilemap_sweep_box(vNudgedCenter, vDeltaY, vSweepHalfExtents, TILEMAP_COLLISION_JNR);

                        /* If clear (no hit), we found a path */
                        if (!resNudge.bHit)
                        {
                            /* Apply nudge and move */
                            m_playerJnr.vPos.fX += fNudgeX;
                            m_playerJnr.vPos.fY += vDeltaY.fY;
                            bCorrected = true;
                            break;
                        }
                    }
                    if (bCorrected)
                        break;
                }
            }

            if (!bCorrected)
            {
                /* Hit ceiling or floor - stop and push out */
                m_playerJnr.vPos.fY += vDeltaY.fY * res.fTime + res.vNormal.fY * 0.002f;

                if (vMovement.fY > 0.0f)
                {
                    /* Hit floor - land */
                    m_vVelocity.fY = 0.0f;
                    m_bOnGround = true;
                }
                else
                {
                    /* Hit ceiling - stop upward velocity */
                    m_vVelocity.fY = 0.0f;
                }
            }
        }
        else
        {
            m_playerJnr.vPos.fY += vDeltaY.fY;
        }
    }

    /* Update ground state after movement */
    m_bOnGround = player_jnr_check_on_ground(m_playerJnr.vPos);

    /* Check if we just landed during movement (for jump buffer and landing sound) */
    /* Use the state from BEFORE movement to detect landing */
    bool bJustLandedAfterMove = !bWasOnGroundBeforeMove && m_bOnGround;

    /* Play landing sound when touching ground */
    if (bJustLandedAfterMove && m_pLandSound && !m_bFlyMode)
    {
        float fFreq = AUDIO_BITRATE * rngf(0.2f, 1.0f);
        mixer_ch_set_freq(MIXER_CHANNEL_ENGINE, fFreq);
        wav64_play(m_pLandSound, MIXER_CHANNEL_ENGINE);
    }

    /* If we just landed and have jump buffer, trigger jump */
    if (bJustLandedAfterMove && m_iJumpBufferFrames > 0 && !m_bFlyMode)
    {
        m_vVelocity.fY = PLAYER_JNR_JUMP_VELOCITY;
        m_bOnGround = false;
        m_iCoyoteTimeFrames = 0;
        m_iJumpBufferFrames = 0;

        if (m_pJumpSound)
        {
            float fFreq = AUDIO_BITRATE * rngf(0.7f, 1.0f);
            mixer_ch_set_freq(MIXER_CHANNEL_UFO, fFreq);
            wav64_play(m_pJumpSound, MIXER_CHANNEL_UFO);
        }
    }

    if (m_bOnGround && m_vVelocity.fY > 0.0f)
    {
        m_vVelocity.fY = 0.0f;
    }

    /* Update coyote time after movement: reset if on ground, otherwise it was already decremented above */
    if (m_bOnGround)
    {
        m_iCoyoteTimeFrames = COYOTE_TIME_FRAMES;
    }

    /* Select and switch animation based on state */
    sprite_anim_clip_t *pDesiredClip = NULL;
    float fAbsVelocityX = (m_vVelocity.fX >= 0.0f) ? m_vVelocity.fX : -m_vVelocity.fX;
    bool bIsMoving = fAbsVelocityX > 1e-6f;

    /* Walk sound while moving on ground */
    if (m_fWalkSoundTimer > 0.0f)
    {
        m_fWalkSoundTimer -= fDeltaTime;
    }

    bool bIsRunning = fAbsVelocityX >= PLAYER_JNR_WALK_RUN_THRESHOLD;
    bool bShouldStep = m_pWalkSound && m_bOnGround && !m_bFlyMode && fAbsVelocityX >= PLAYER_JNR_MIN_X_SPEED;

    if (!bShouldStep)
    {
        m_fWalkSoundTimer = 0.0f;
        m_bWasMoving = false;
    }
    else
    {
        if (!m_bWasMoving)
        {
            /* First step should play immediately when starting to move */
            m_fWalkSoundTimer = 0.0f;
        }

        if (m_fWalkSoundTimer <= 0.0f)
        {
            float fFreq = AUDIO_BITRATE * (bIsRunning ? rngf(0.7f, 1.0f) : rngf(0.2f, 0.5f));
            mixer_ch_set_freq(MIXER_CHANNEL_ENGINE, fFreq);
            wav64_play(m_pWalkSound, MIXER_CHANNEL_ENGINE);

            m_fWalkSoundTimer = bIsRunning ? PLAYER_JNR_WALK_SOUND_DELAY_RUNNING : PLAYER_JNR_WALK_SOUND_DELAY_WALKING;
        }

        m_bWasMoving = true;
    }

    /* Determine direction from velocity/stick input, or fall back to current animation clip */
    bool bIsEast = true; /* default to east */
    if (m_vVelocity.fX > 1e-6f)
    {
        /* Moving right - use east */
        bIsEast = true;
    }
    else if (m_vVelocity.fX < -1e-6f)
    {
        /* Moving left - use west */
        bIsEast = false;
    }
    else if (fabsf((float)_iStickX) >= STICK_DEADZONE)
    {
        /* Not moving but stick input present (outside deadzone) - use stick direction */
        bIsEast = (_iStickX >= 0);
    }
    else if (m_animPlayer.pClip)
    {
        /* No clear direction from velocity/stick - use current animation clip direction */
        if (m_animPlayer.pClip == m_aAnimClips[ANIM_WALK_EAST] || m_animPlayer.pClip == m_aAnimClips[ANIM_RUN_EAST] || m_animPlayer.pClip == m_aAnimClips[ANIM_JUMP_EAST])
        {
            bIsEast = true;
        }
        else if (m_animPlayer.pClip == m_aAnimClips[ANIM_WALK_WEST] || m_animPlayer.pClip == m_aAnimClips[ANIM_RUN_WEST] || m_animPlayer.pClip == m_aAnimClips[ANIM_JUMP_WEST])
        {
            bIsEast = false;
        }
    }

    if (!m_bOnGround)
    {
        /* Jumping - use jump animation */
        pDesiredClip = bIsEast ? m_aAnimClips[ANIM_JUMP_EAST] : m_aAnimClips[ANIM_JUMP_WEST];
    }
    else if (bIsMoving)
    {
        /* On ground and moving - use walk or run based on speed */
        if (fAbsVelocityX < PLAYER_JNR_WALK_RUN_THRESHOLD)
        {
            /* Walk (slow) */
            pDesiredClip = bIsEast ? m_aAnimClips[ANIM_WALK_EAST] : m_aAnimClips[ANIM_WALK_WEST];
        }
        else
        {
            /* Run (fast) */
            pDesiredClip = bIsEast ? m_aAnimClips[ANIM_RUN_EAST] : m_aAnimClips[ANIM_RUN_WEST];
        }
    }
    else
    {
        /* Idle - use first frame of walk animation in last direction */
        pDesiredClip = bIsEast ? m_aAnimClips[ANIM_WALK_EAST] : m_aAnimClips[ANIM_WALK_WEST];
        if (pDesiredClip && m_animPlayer.pClip == pDesiredClip)
        {
            /* Already on walk animation, reset to first frame */
            sprite_anim_player_reset(&m_animPlayer);
        }
    }

    /* Switch animation if clip changed */
    if (pDesiredClip && m_animPlayer.pClip != pDesiredClip)
    {
        sprite_anim_player_set_clip(&m_animPlayer, pDesiredClip);
    }

    /* Update load trigger collision checks */
    triggers_load_update();
}

void player_jnr_render(void)
{
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);

    /* Use quantized rendering to prevent sub-pixel wobble against tilemap */
    entity2d_render_simple_quantized(&m_playerJnr);
}

struct vec2 player_jnr_get_position(void)
{
    return entity2d_get_pos(&m_playerJnr);
}

struct vec2 player_jnr_get_collision_half_extents(void)
{
    return m_vCollisionHalfExtents;
}

struct vec2 player_jnr_get_velocity(void)
{
    return m_vVelocity;
}

float player_jnr_get_speed(void)
{
    return vec2_mag(m_vVelocity);
}

bool player_jnr_is_on_ground(void)
{
    return m_bOnGround;
}

void player_jnr_set_position(struct vec2 _vPos)
{
    m_playerJnr.vPos = _vPos;
}

void player_jnr_set_position_from_data(const char *_pFolderName)
{
    if (!_pFolderName)
        return;

    struct vec2 vSpawnPos;
    if (csv_helper_load_spawn_position(_pFolderName, &vSpawnPos))
    {
        player_jnr_set_position(vSpawnPos);
    }
}

const struct entity2D *player_jnr_get_entity(void)
{
    return &m_playerJnr;
}