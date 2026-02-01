#include "ufo.h"
#include "../audio.h"
#include "../camera.h"
#include "../csv_helper.h"
#include "../dialogue.h"
#include "../entity2d.h"
#include "../frame_time.h"
#include "../math_helper.h"
#include "../minimap.h"
#include "../resource_helper.h"
#include "../save.h"
#include "../tilemap.h"
#include "gp_camera.h"
#include "gp_state.h"
#include "mixer.h"
#include "rdpq_mode.h"
#include "space_objects.h"
#include "tractor_beam.h"
#include "ufo_turbo.h"
#include "weapons.h"
#include <math.h>
#include <stdio.h>

/* UFO sprite settings */
static sprite_t *m_spriteUfo = NULL;
static sprite_t *m_spriteUfoMiniThrust = NULL;
static sprite_t *m_spriteUfoThruster = NULL;
static sprite_t *m_spriteUfoThrusterStrong = NULL;
static sprite_t *m_spriteUfoHighlight = NULL;
static sprite_t *m_spriteUfoWeaponGlow = NULL;
static sprite_t *m_spriteLockOn = NULL;
static sprite_t *m_spriteLockSelection = NULL;
static sprite_t *m_spriteNextTarget = NULL;

static wav64_t *m_sfxLaunch = NULL;
static wav64_t *m_sfxLand = NULL;
static wav64_t *m_sfxDoorOpen = NULL;
static wav64_t *m_sfxDoorClose = NULL;
static wav64_t *m_sfxEngine = NULL;
static wav64_t *m_sfxBounce = NULL;

#include "../stick_normalizer.h"

// Movement settings
#define UFO_ROTATE_LERP 0.85f              // how fast the UFO rotates toward target angle
#define UFO_THRUST 0.08f                   // base acceleration per frame
#define UFO_VELOCITY_DAMPING 0.98f         // velocity damping during acceleration (0..1, closer to 1 = slower decay)
#define UFO_VELOCITY_DECAY 0.96f           // velocity decay when not accelerating (0..1, closer to 1 = slower decay)
#define UFO_ROTATE_ALIGN_EPSILON_DEG 30.0f // how exact the rotation must match target to apply thrust

// polar pushback settings
#define UFO_POLAR_BUFFER_TILES 2           // number of tile rows from top/bottom where polar pushback starts (actual map tiles)
#define UFO_POLAR_BUFFER_REPEATED_TILES 2  // additional tile rows beyond map edges (repeated tiles for smoother transition)
#define UFO_POLAR_PUSHBACK_SCALE 4.0f      // pushback force scale factor (multiplies current thrust capability)
#define UFO_POLAR_VELOCITY_RESISTANCE 0.2f // resistance factor for velocity toward pole (0.0-1.0, higher = more resistance)
#define UFO_POLAR_SINE_FREQ 0.15f          // frequency of sine wave oscillation for rubber-bandy feel

// GFX settings
#define UFO_THRUST_MIN_THRESHOLD 0.01f                  // minimum thrust to show mini thruster
#define UFO_THRUST_NORMAL_THRESHOLD 0.04f               // thrust threshold to show normal thruster
#define UFO_THRUST_STRONG_THRESHOLD 0.06f               // thrust threshold to show strong thruster
#define UFO_THRUST_TURBO_THRESHOLD (UFO_THRUST + 0.01f) // thrust threshold to show turbo sprite
#define UFO_THRUSTER_WOBBLE_FRAMES 4                    // frames to hold each thruster offset phase
#define UFO_SHADOW_TARGET_SIZE 0.5f                     // shadow scale factor
#define UFO_SHADOW_OFFSET 48.0f                         // base shadow vertical offset
#define UFO_SHADOW_HEIGHT_OFFSET 6.0f                   // additional shadow offset for height adjustment

// Animation settings
#define UFO_LANDING_DURATION 1.5f // animation duration in seconds

// Collision settings
#define UFO_COLLISION_RADIUS 8 // collision radius in pixels (centered)

// Target lock settings
#define UFO_TARGET_DESELECT_MARGIN 100.0f             // margin in world-space units before target deselects
#define UFO_TARGET_VIEWCONE_HALF_ANGLE_DEG 30.0f      // half-angle of viewcone for target locking (±30° = 60° total)
#define UFO_TARGET_LOCK_ACTIVATION_MARGIN 5.0f        // margin for initial target lock activation (smaller than deselect margin)
#define UFO_NEXT_TARGET_ONSCREEN_MARGIN 5.0f          // margin for detecting if next target is on screen (for indicator positioning)
#define UFO_NEXT_TARGET_INDICATOR_LERP_TO_TARGET 0.5f // lerp speed when moving indicator towards target (lower = slower)
#define UFO_NEXT_TARGET_INDICATOR_LERP_TO_UFO 0.05f   // lerp speed when moving indicator towards UFO (faster)
#define UFO_NEXT_TARGET_INDICATOR_MIN_DISTANCE 32.0f  // minimum screen distance indicator can be from UFO (prevents clipping) - converted to world space using zoom

/* UFO animation types */
typedef enum
{
    UFO_ANIM_NONE,
    UFO_ANIM_SPACE_TO_PLANET,   /* scale 1.0 -> 0.0 at current pos */
    UFO_ANIM_PLANET_TO_SURFACE, /* scale 1.0 -> shadow_size, move to shadow pos */
    UFO_ANIM_SURFACE_TO_PLANET, /* scale shadow_size -> 1.0 at shadow pos, move to normal pos */
    UFO_ANIM_PLANET_TO_SPACE    /* scale 0.0 -> 1.0 at current pos */
} ufo_animation_type_t;

/* Internal UFO instance, embedding entity2D. */
typedef struct UfoInstance
{
    struct entity2D entity; /* shared header: position, extents, flags, layer, sprite */
    struct vec2 vVel;       /* world-space velocity */
    float fSpeed;           /* magnitude of velocity (cached for performance) */

    float fAngleRad; /* current facing direction (smoothed) */
    bool bAligned;   /* rotation close enough to target to apply thrust */
    float fThrust;   /* current thrust value (updated every frame) */

    /* joystick debug output */
    float fStickForce;
    int iStickAngle;

    /* Animation State */
    ufo_animation_type_t animType;
    float fAnimTimer;

    /* Shadow position in world space */
    struct vec2 vShadowPos;

} UfoInstance;

static UfoInstance m_ufo;
static uint32_t m_uBounceCooldownEndMs = 0;   /* Milliseconds when bounce cooldown ends (0 = no cooldown) */
static float m_fBounceThrustReduction = 1.0f; /* Thrust reduction factor during bounce cooldown */
static const struct entity2D *m_pTargetMeteor = NULL;
static bool m_bPrevTargetButton = false;
static const struct entity2D *m_pNextTarget = NULL;
static const struct entity2D *m_pPotentialTarget = NULL; /* Cached potential target, calculated once per frame */
static float m_fThrusterAnimFrame = 0.0f;
static float m_fPolarOscillationTime = 0.0f;                 /* Time accumulator for polar sine wave oscillation */
static struct vec2 m_vNextTargetIndicatorPos = {0.0f, 0.0f}; /* Current lerped position of next target indicator */

static bool ufo_target_is_visible(const struct entity2D *_pEntity)
{
    return _pEntity && entity2d_is_active(_pEntity) && camera_is_point_visible(&g_mainCamera, _pEntity->vPos, UFO_TARGET_DESELECT_MARGIN);
}

static bool ufo_entity_to_screen(const struct entity2D *_pEntity, struct vec2i *_pOut)
{
    if (!ufo_target_is_visible(_pEntity))
        return false;

    camera_world_to_screen(&g_mainCamera, _pEntity->vPos, _pOut);
    return true;
}

static bool ufo_compute_next_target_indicator(struct vec2 *_pOutTargetEntityPos, struct vec2 *_pOutUfoPos, float *_pAngleRad, bool *_pOutMovingTowardsTarget,
                                              float *_pOutTargetDistance, bool *_pOutInCloseProximity)
{
    if (!m_pNextTarget)
        return false;

    /* Check if entity is still valid and active */
    /* Note: This will crash if m_pNextTarget is a dangling pointer, but we can't validate pointers in C */
    /* The target should be cleared before the entity is despawned (see script step 21) */
    if (!entity2d_is_active(m_pNextTarget))
    {
        /* Entity is no longer valid, clear target */
        m_pNextTarget = NULL;
        return false;
    }

    struct vec2 vDelta = vec2_sub(m_pNextTarget->vPos, m_ufo.entity.vPos);
    float fMagSq = vec2_mag_sq(vDelta);
    if (fMagSq <= 1e-6f)
        return false;

    float fMag = sqrtf(fMagSq);
    struct vec2 vDir = vec2_scale(vDelta, 1.0f / fMag);
    float fAngle = fm_atan2f(vDir.fX, -vDir.fY);

    /* Check if target is on screen with margin */
    bool bTargetOnScreen = camera_is_point_visible(&g_mainCamera, m_pNextTarget->vPos, UFO_NEXT_TARGET_ONSCREEN_MARGIN);

    bool bMovingTowardsTarget = false;
    float fTargetDistance = 0.0f;

    /* Convert screen distance to world distance based on current zoom */
    float fZoom = camera_get_zoom(&g_mainCamera);
    float fMinDistWorld = UFO_NEXT_TARGET_INDICATOR_MIN_DISTANCE / fZoom;

    if (bTargetOnScreen)
    {
        /* Calculate entity size metrics (used for padding and proximity detection) */
        float fMaxHalf = (float)((m_pNextTarget->vHalf.iX > m_pNextTarget->vHalf.iY) ? m_pNextTarget->vHalf.iX : m_pNextTarget->vHalf.iY);
        float fPadding = fMaxHalf * 1.5f;
        float fEntityRadius = (float)m_pNextTarget->iCollisionRadius;
        if (fEntityRadius <= 0.0f)
            fEntityRadius = fMaxHalf; /* Fallback to half extents */

        float fDistanceToEntity = fMag; /* Reuse sqrt from above */
        float fCloseThreshold = fPadding + fEntityRadius + 16.0f;
        bool bInCloseProximity = (fDistanceToEntity < fCloseThreshold);

        if (bInCloseProximity)
        {
            /* Close proximity: use half distance to prevent penetration */
            fTargetDistance = fDistanceToEntity * 0.5f;
        }
        else
        {
            /* Normal case: distance minus padding, clamped to minimum (in world space) */
            fTargetDistance = clampf(fDistanceToEntity - fPadding, fMinDistWorld, fDistanceToEntity);
        }

        if (_pOutInCloseProximity)
            *_pOutInCloseProximity = bInCloseProximity;
        bMovingTowardsTarget = true;
    }
    else
    {
        /* Target not on screen: show direction around UFO at minimum distance (in world space) */
        fTargetDistance = fMinDistWorld;
        bMovingTowardsTarget = false;
    }

    if (_pOutTargetEntityPos)
        *_pOutTargetEntityPos = m_pNextTarget->vPos;

    if (_pOutUfoPos)
        *_pOutUfoPos = m_ufo.entity.vPos;

    if (_pAngleRad)
        *_pAngleRad = fAngle;

    if (_pOutMovingTowardsTarget)
        *_pOutMovingTowardsTarget = bMovingTowardsTarget;

    if (_pOutTargetDistance)
        *_pOutTargetDistance = fTargetDistance;

    return true;
}

void ufo_clear_target_lock(void)
{
    m_pTargetMeteor = NULL;
    m_pNextTarget = NULL;
}

/* -------------------------------------------------------------------------- */
/* Init                                                                       */
/* -------------------------------------------------------------------------- */

float ufo_get_angle_rad(void)
{
    return m_ufo.fAngleRad;
}

void ufo_set_angle_rad(float _fAngleRad)
{
    m_ufo.fAngleRad = angle_wrap_rad_0_2pi(_fAngleRad);
}

void ufo_set_position(struct vec2 _vPos)
{
    m_ufo.entity.vPos = _vPos;
}

/* Depth into polar band:
 * dist = distance from map edge (top: y, bottom: worldH - y). Negative means outside.
 * 0 at inner boundary, 1 at outer boundary.
 */
static inline float polar_depth(float _fDist, float _fInnerPx, float _fOuterPx)
{
    float fTotal = _fInnerPx + _fOuterPx;
    if (fTotal <= 0.0f)
        return 0.0f;

    if (_fDist >= _fInnerPx || _fDist < -_fOuterPx)
        return 0.0f;

    float fDepth = (_fInnerPx - _fDist) / fTotal; /* inner->0, outer->1 */
    fDepth = clampf_01(fDepth);

    if (_fOuterPx == 0.0f && fDepth == 0.0f)
        fDepth = 0.1f;

    return fDepth;
}

void ufo_set_position_from_data(const char *_pFolderName)
{
    if (!_pFolderName)
        return;

    struct vec2 vSpawnPos;
    if (csv_helper_load_spawn_position(_pFolderName, &vSpawnPos))
    {
        ufo_set_position(vSpawnPos);
    }
}

void ufo_init(void)
{
    /* Always free existing resources first to avoid leaks or double-loading */
    ufo_free();

    /* Initial world position defaults to zero (will be set by gp_state) */
    m_ufo.entity.vPos = vec2_zero();
    m_ufo.vVel = vec2_zero();
    m_ufo.fSpeed = 0.0f;

    m_ufo.fAngleRad = 0.0f;
    m_ufo.fStickForce = 0.0f;
    m_ufo.fThrust = 0.0f;
    m_ufo.iStickAngle = 0;
    m_ufo.bAligned = false;
    m_pTargetMeteor = NULL;
    m_bPrevTargetButton = false;
    m_pNextTarget = NULL;
    m_fThrusterAnimFrame = 0.0f;
    m_vNextTargetIndicatorPos = vec2_zero();

    /* Animation defaults */
    m_ufo.animType = UFO_ANIM_NONE;
    m_ufo.fAnimTimer = 0.0f;

    /* Shadow position defaults to entity position */
    m_ufo.vShadowPos = m_ufo.entity.vPos;

    /* Load sprites. */
    m_spriteUfo = sprite_load("rom:/ufo_00.sprite");
    m_spriteUfoMiniThrust = sprite_load("rom:/ufo_mini_thrust_00.sprite");
    m_spriteUfoThruster = sprite_load("rom:/ufo_thruster_00.sprite");
    m_spriteUfoThrusterStrong = sprite_load("rom:/ufo_thruster_strong_00.sprite");
    m_spriteUfoHighlight = sprite_load("rom:/ufo_highlight_00.sprite");
    m_spriteUfoWeaponGlow = sprite_load("rom:/ufo_weapon_glow_00.sprite");
    m_spriteLockOn = sprite_load("rom:/lock_on_00.sprite");
    m_spriteLockSelection = sprite_load("rom:/lock_selection_00.sprite");
    m_spriteNextTarget = sprite_load("rom:/next_target_00.sprite");

    /* Load sounds. */
    m_sfxLaunch = wav64_load("rom:/ufo_launch.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    m_sfxLand = wav64_load("rom:/ufo_land.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    m_sfxDoorOpen = wav64_load("rom:/ufo_door_open.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    m_sfxDoorClose = wav64_load("rom:/ufo_door_close.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    m_sfxEngine = wav64_load("rom:/ufo_engine_loop.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    m_sfxBounce = wav64_load("rom:/ufo_bounce.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    if (m_sfxEngine)
    {
        wav64_set_loop(m_sfxEngine, true);
        wav64_play(m_sfxEngine, MIXER_CHANNEL_ENGINE);
        /* Set initial frequency to slow (thrust = 0) to prevent fast playback at start */
        audio_update_engine_freq(0.0f);
    }

    /* Initialize turbo system */
    ufo_turbo_init();

    /* Wire into entity2D. We use the UFO body sprite as logical size. */
    uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
    uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;

    entity2d_init_from_sprite(&m_ufo.entity, m_ufo.entity.vPos, m_spriteUfo, uFlags, uLayerMask);

    /* Override collision radius */
    m_ufo.entity.iCollisionRadius = UFO_COLLISION_RADIUS;
}

void ufo_free(void)
{
    /* Ensure channels are stopped before releasing UFO audio resources */
    mixer_ch_stop(MIXER_CHANNEL_UFO);
    mixer_ch_stop(MIXER_CHANNEL_ENGINE);

    SAFE_FREE_SPRITE(m_spriteUfo);
    SAFE_FREE_SPRITE(m_spriteUfoMiniThrust);
    SAFE_FREE_SPRITE(m_spriteUfoThruster);
    SAFE_FREE_SPRITE(m_spriteUfoThrusterStrong);
    SAFE_FREE_SPRITE(m_spriteUfoHighlight);
    SAFE_FREE_SPRITE(m_spriteUfoWeaponGlow);
    SAFE_FREE_SPRITE(m_spriteLockOn);
    SAFE_FREE_SPRITE(m_spriteLockSelection);
    SAFE_FREE_SPRITE(m_spriteNextTarget);

    SAFE_CLOSE_WAV64(m_sfxLaunch);
    SAFE_CLOSE_WAV64(m_sfxLand);
    SAFE_CLOSE_WAV64(m_sfxDoorOpen);
    SAFE_CLOSE_WAV64(m_sfxDoorClose);
    SAFE_CLOSE_WAV64(m_sfxEngine);
    SAFE_CLOSE_WAV64(m_sfxBounce);

    ufo_turbo_free();
}

/* -------------------------------------------------------------------------- */
/* Animation Control                                                          */
/* -------------------------------------------------------------------------- */

void ufo_start_transition_animation(gp_state_t _StateFrom, gp_state_t _StateTo)
{
    if (ufo_is_transition_playing())
        return;

    m_ufo.fAnimTimer = 0.0f;
    m_ufo.vVel = vec2_zero(); /* Stop movement immediately */

    if (_StateFrom == SPACE && _StateTo == PLANET)
    {
        m_ufo.animType = UFO_ANIM_SPACE_TO_PLANET;
    }
    else if (_StateFrom == PLANET && _StateTo == SURFACE)
    {
        m_ufo.animType = UFO_ANIM_PLANET_TO_SURFACE;
    }
    else if (_StateFrom == SURFACE && _StateTo == PLANET)
    {
        m_ufo.animType = UFO_ANIM_SURFACE_TO_PLANET;
    }
    else if (_StateFrom == PLANET && _StateTo == SPACE)
    {
        m_ufo.animType = UFO_ANIM_PLANET_TO_SPACE;
    }
    else
    {
        m_ufo.animType = UFO_ANIM_NONE;
    }

    /* Stop engine sound during landing/launching animations */
    if (mixer_ch_playing(MIXER_CHANNEL_ENGINE))
    {
        mixer_ch_stop(MIXER_CHANNEL_ENGINE);
    }

    if (_StateTo > _StateFrom)
    {
        wav64_play(m_sfxLand, MIXER_CHANNEL_UFO);
    }
    else if (_StateTo < _StateFrom)
    {
        wav64_play(m_sfxLaunch, MIXER_CHANNEL_UFO);
    }

    ufo_clear_target_lock();
}

void ufo_end_transition_animation(gp_state_t _TargetState)
{
    /* Manually end a held landing/launch animation and resume normal control. */
    m_ufo.animType = UFO_ANIM_NONE;
    m_ufo.fAnimTimer = 0.0f;

    /* Handle engine sound based on target state */
    if (_TargetState == PLANET || _TargetState == SPACE)
    {
        if (m_sfxEngine && !mixer_ch_playing(MIXER_CHANNEL_ENGINE))
        {
            wav64_play(m_sfxEngine, MIXER_CHANNEL_ENGINE);
            /* Set initial frequency to slow (thrust = 0) to prevent fast playback at start */
            audio_update_engine_freq(0.0f);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Update                                                                     */
/* -------------------------------------------------------------------------- */

/* Internal helper: Recalculate shadow position based on current UFO position and tilemap. */
static void ufo_internal_update_shadow(void)
{
    float fShadowOffsetY = UFO_SHADOW_OFFSET;

    /* If we are on a planet or transitioning to/from one, we need the precise ground distance */
    /* Check tilemap if initialized (valid for PLANET and SURFACE) */
    if (g_mainTilemap.bInitialized)
    {
        /* Convert shadow position to screen coordinates to check tilemap */
        struct vec2 vShadowCheckWorld = vec2_make(m_ufo.entity.vPos.fX, m_ufo.entity.vPos.fY + fShadowOffsetY + UFO_SHADOW_HEIGHT_OFFSET);
        struct vec2i vShadowCheckScreen;
        camera_world_to_screen(&g_mainCamera, vShadowCheckWorld, &vShadowCheckScreen);

        int iShadowLayer = tilemap_get_highest_tile_layer(vShadowCheckScreen.iX, vShadowCheckScreen.iY);
        if (iShadowLayer == 0)
        {
            fShadowOffsetY += UFO_SHADOW_HEIGHT_OFFSET;
        }
    }

    /* Store shadow position in world space */
    m_ufo.vShadowPos = vec2_make(m_ufo.entity.vPos.fX, m_ufo.entity.vPos.fY + fShadowOffsetY);
}

void ufo_recover_surface_position_mode(void)
{
    /* At this point, entity.vPos is the orbit position (set by enter_state_surface) */
    /* Shadow position is calculated based on orbit position */
    ufo_internal_update_shadow();

    /* Move entity to shadow/ground position for collision detection in SURFACE mode */
    /* This is critical: player_surface needs to collide with UFO at ground level */
    ufo_set_position(ufo_get_shadow_position());

    // no engine sound when landed
    if (mixer_ch_playing(MIXER_CHANNEL_ENGINE))
    {
        mixer_ch_stop(MIXER_CHANNEL_ENGINE);
    }
}

void ufo_recover_planet_position_mode(void)
{
    /* At this point, entity.vPos is the orbit position (set by enter_state_planet) */
    /* We need to recover the shadow position from where we were on the surface */

    /* The shadow position was already stored when we were in SURFACE mode */
    /* Calculate it based on current orbit position */
    ufo_internal_update_shadow();

    /* Set animation state to start of launch animation (SURFACE->PLANET) */
    /* This ensures the animation starts from the shadow/ground position */
    // m_ufo.animType = UFO_ANIM_NONE;
    // m_ufo.fAnimTimer = 0.0f; /* Start of animation */
}

/* Helper function to find target with viewcone, falling back to closest on-screen meteor */
static const struct entity2D *ufo_find_target_with_fallback(struct vec2 _vFrom, float _fFacingAngleRad, float _fViewconeHalfAngleRad)
{
    /* First try to find target within viewcone */
    const struct entity2D *pTarget =
        space_objects_get_closest_entity_in_viewcone(_vFrom, _fFacingAngleRad, &g_mainCamera, _fViewconeHalfAngleRad, UFO_TARGET_LOCK_ACTIVATION_MARGIN);

    /* If no target found in viewcone, check if next-closest meteor is on screen */
    if (pTarget == NULL)
    {
        pTarget = space_objects_get_closest_entity_on_screen(_vFrom, &g_mainCamera, UFO_TARGET_LOCK_ACTIVATION_MARGIN);
    }

    return pTarget;
}

void ufo_update(bool _bTurboPressed, bool _bTargetLockPressed, bool _bTractorBeamPressed, int _iStickX, int _iStickY)
{
    /* Disable UFO input processing when gameplay input is blocked (minimap, cutscenes, transitions) */
    /* UFO continues to move with existing velocity (physics update runs below) */
    if (!gp_state_accepts_input())
    {
        _bTurboPressed = false;
        _bTargetLockPressed = false;
        _bTractorBeamPressed = false;
        _iStickX = 0;
        _iStickY = 0;
    }

    if (!entity2d_is_active(&m_ufo.entity))
        return;

    /* Handle landing/launching animation */
    if (m_ufo.animType != UFO_ANIM_NONE)
    {
        m_ufo.fAnimTimer += frame_time_delta_seconds();
        float t = m_ufo.fAnimTimer / UFO_LANDING_DURATION;
        if (t >= 1.0f)
        {
            /* Hold final animation state until explicitly ended. */
            m_ufo.fAnimTimer = UFO_LANDING_DURATION;
        }

        /* Stop physics/input during animation */
        m_ufo.fThrust = 0.0f;
        m_ufo.vVel = vec2_zero();
        return;
    }

    float fFrameMul = frame_time_mul();

    uint32_t uCurrentMs = get_ticks_ms();

    /* Update turbo system (checks expiration) and get effective multiplier */
    float fTurboMultiplier = ufo_turbo_update(_bTurboPressed);

    /* Check if bounce cooldown has expired */
    if (m_uBounceCooldownEndMs > 0 && uCurrentMs >= m_uBounceCooldownEndMs)
    {
        /* Bounce cooldown expired, restore normal thrust */
        m_fBounceThrustReduction = 1.0f;
        m_uBounceCooldownEndMs = 0;
    }

    int iStickX = _iStickX;
    int iStickY = _iStickY;

    /* Check if any weapons are unlocked - target lock requires weapons */
    bool bWeaponsUnlocked = weapons_any_unlocked();

    /* In toggle mode, if target is locked and beam is NOT active, exclude R from target lock
     * This prevents R from toggling off the target lock, allowing tractor beam to activate instead
     * Note: _bTractorBeamPressed is an edge event (only true on press), not held state */
    bool bTargetLockInput;
    if (save_get_target_lock_toggle_mode() && ufo_is_target_locked() && !tractor_beam_is_active())
    {
        /* Only pass Z, not R - R will go to tractor beam instead */
        bTargetLockInput = _bTargetLockPressed;
    }
    else
    {
        /* Normal case: Z (held) or R (edge press) can lock target
         * In toggle mode, R edge press locks target
         * In hold mode, Z held locks target (R edge press also works for initial lock) */
        bTargetLockInput = _bTargetLockPressed || _bTractorBeamPressed;
    }

    bool bTargetHeld = bTargetLockInput && bWeaponsUnlocked;
    bool bTargetPressedEdge = bTargetHeld && !m_bPrevTargetButton;
    m_bPrevTargetButton = bTargetHeld;

    /* Target lock logic - only for meteors in SPACE */
    if (gp_state_get() == SPACE && bWeaponsUnlocked)
    {
        /* Calculate viewcone parameters */
        float fViewconeHalfAngleRad = (UFO_TARGET_VIEWCONE_HALF_ANGLE_DEG * FM_PI / 180.0f);

        if (save_get_target_lock_toggle_mode())
        {
            /* Toggle mode: toggle target lock on button press */
            if (bTargetPressedEdge)
            {
                if (m_pTargetMeteor != NULL && ufo_target_is_visible(m_pTargetMeteor))
                {
                    /* Already locked: toggle off */
                    m_pTargetMeteor = NULL;
                }
                else
                {
                    /* Not locked: snap to nearest visible target within viewcone, with fallback to closest on-screen */
                    m_pTargetMeteor = ufo_find_target_with_fallback(m_ufo.entity.vPos, m_ufo.fAngleRad, fViewconeHalfAngleRad);
                }
            }

            /* If target is destroyed or missing, disable target lock */
            if (m_pTargetMeteor != NULL && !ufo_target_is_visible(m_pTargetMeteor))
            {
                m_pTargetMeteor = NULL;
            }
        }
        else
        {
            /* Hold mode: target lock only while button is held */
            if (!bTargetHeld)
            {
                m_pTargetMeteor = NULL;
            }
            else
            {
                if (bTargetPressedEdge)
                {
                    /* Snap to nearest visible target within viewcone, with fallback to closest on-screen */
                    m_pTargetMeteor = ufo_find_target_with_fallback(m_ufo.entity.vPos, m_ufo.fAngleRad, fViewconeHalfAngleRad);
                }
                else if (!ufo_target_is_visible(m_pTargetMeteor))
                {
                    /* Lost target while holding: do not auto-snap until button pressed again */
                    m_pTargetMeteor = NULL;
                }
            }
        }
    }
    else
    {
        /* Not in SPACE or no weapons: clear any existing meteor target lock */
        m_pTargetMeteor = NULL;
    }

    /* Cache potential target (calculated once per frame) */
    if (bWeaponsUnlocked && !minimap_is_active())
    {
        float fViewconeHalfAngleRad = (UFO_TARGET_VIEWCONE_HALF_ANGLE_DEG * FM_PI / 180.0f);
        m_pPotentialTarget = ufo_find_target_with_fallback(m_ufo.entity.vPos, m_ufo.fAngleRad, fViewconeHalfAngleRad);
    }
    else
    {
        m_pPotentialTarget = NULL;
    }

    bool bHasTarget = ufo_target_is_visible(m_pTargetMeteor);
    if (!bHasTarget && m_pTargetMeteor != NULL)
    {
        /* Locked target is off-screen; unlock it using UFO_TARGET_DESELECT_MARGIN */
        m_pTargetMeteor = NULL;
    }

    /* Magnitude squared (defer sqrt until needed) */
    float fMagSq = (float)(iStickX * iStickX + iStickY * iStickY);

    /* By default, keep current angle as target (no sudden jump when stick in deadzone) */
    float fTargetAngleRad = m_ufo.fAngleRad;
    float fMoveAngleRad = m_ufo.fAngleRad;
    float fMoveDirX = 0.0f;
    float fMoveDirY = 0.0f;

    if (fMagSq < STICK_DEADZONE_SQ)
    {
        /* Stick too weak: keep last angle but show zero force */
        m_ufo.fStickForce = 0.0f;
    }
    else
    {
        /* Only compute sqrt when outside deadzone */
        float fMag = sqrtf(fMagSq);

        /* Normalized 0..1, accounting for deadzone */
        /* Subtract deadzone so crossing threshold feels like slight tilt, not full force */
        float fEffectiveMagnitude = fMag - STICK_DEADZONE;
        float fMaxEffectiveRange = STICK_MAX_MAGNITUDE - STICK_DEADZONE;
        m_ufo.fStickForce = fEffectiveMagnitude / fMaxEffectiveRange;
        if (m_ufo.fStickForce > 1.0f)
            m_ufo.fStickForce = 1.0f;

        /* Angle with UP = 0°, RIGHT = 90°, DOWN = 180°, LEFT = 270° */
        float fAngleDeg = fm_atan2f((float)iStickX, (float)iStickY) * (180.0f / FM_PI);

        /* Normalize to [0, 360) */
        if (fAngleDeg < 0.0f)
            fAngleDeg += 360.0f;

        m_ufo.iStickAngle = (int)(fAngleDeg + 0.5f);
        fMoveAngleRad = (float)m_ufo.iStickAngle * FM_PI / 180.0f;

        fMoveDirX = fm_sinf(fMoveAngleRad);
        fMoveDirY = -fm_cosf(fMoveAngleRad);
    }

    if (bHasTarget)
    {
        struct vec2 vDelta = vec2_sub(m_pTargetMeteor->vPos, m_ufo.entity.vPos);
        if (vec2_mag_sq(vDelta) > 1e-6f)
            fTargetAngleRad = fm_atan2f(vDelta.fX, -vDelta.fY);
    }
    else if (m_ufo.fStickForce > 0.0f)
    {
        fTargetAngleRad = fMoveAngleRad;
    }

    /* --- Smoothly rotate UFO toward target angle --- */

    /* Calculate shortest angular difference (angle_wrap_rad handles this correctly) */
    float fDelta = angle_wrap_rad(fTargetAngleRad - m_ufo.fAngleRad);
    float fRotateLerp = 1.0f - powf(1.0f - UFO_ROTATE_LERP, fFrameMul);
    m_ufo.fAngleRad += fDelta * fRotateLerp;
    /* Wrap to [0, 2*PI) to prevent unbounded accumulation */
    m_ufo.fAngleRad = angle_wrap_rad_0_2pi(m_ufo.fAngleRad);

    /* Remaining angle error for gating (in degrees) */
    float fRemaining = angle_wrap_rad(fTargetAngleRad - m_ufo.fAngleRad);
    float fAngleErrorDeg = fabsf(fRemaining * 180.0f / FM_PI);
    m_ufo.bAligned = (fAngleErrorDeg <= UFO_ROTATE_ALIGN_EPSILON_DEG);

    /* --- Apply thrust only if rotation is close enough to target --- */

    m_ufo.fThrust = 0.0f; /* Reset thrust each frame */

    bool bThrustRequested = (m_ufo.fStickForce > 0.0f || fTurboMultiplier > 1.0f);
    bool bThrusting = false;
    if (m_ufo.bAligned && bThrustRequested)
    {
        /* Heading convention: 0° = up, 90° = right, etc. */
        float fDirX = m_ufo.fStickForce > 0.0f ? fMoveDirX : fm_sinf(m_ufo.fAngleRad);
        float fDirY = m_ufo.fStickForce > 0.0f ? fMoveDirY : -fm_cosf(m_ufo.fAngleRad);

        float fForce = (fTurboMultiplier > 1.0f) ? 1.0f : m_ufo.fStickForce;
        /* Apply bounce cooldown reduction if active */
        m_ufo.fThrust = UFO_THRUST * fForce * fTurboMultiplier * m_fBounceThrustReduction;
        struct vec2 vAccel = vec2_make(fDirX * m_ufo.fThrust, fDirY * m_ufo.fThrust);

        m_ufo.vVel = vec2_add(m_ufo.vVel, vec2_scale(vAccel, fFrameMul));
        bThrusting = true;
    }

    /* --- Apply velocity damping: different rates for acceleration vs deceleration --- */
    float fDampingBase = bThrusting ? UFO_VELOCITY_DAMPING : UFO_VELOCITY_DECAY;
    float fDamping = powf(fDampingBase, fFrameMul);
    m_ufo.vVel = vec2_scale(m_ufo.vVel, fDamping);

    /* --- Integrate position from velocity (pure world space) --- */
    /* In SURFACE mode, UFO doesn't move - skip position updates */
    if (gp_state_get() != SURFACE)
    {
        m_ufo.entity.vPos = vec2_add(m_ufo.entity.vPos, vec2_scale(m_ufo.vVel, fFrameMul));
    }

    /* --- Polar boundary logic for PLANET state --- */
    if (gp_state_get() == PLANET && g_mainTilemap.bInitialized && g_mainTilemap.uWorldHeightTiles > 0)
    {
        float fWorldH = (float)g_mainTilemap.uWorldHeightTiles * (float)TILE_SIZE;

        float fInnerPx = (float)UFO_POLAR_BUFFER_TILES * (float)TILE_SIZE;
        float fOuterPx = (float)UFO_POLAR_BUFFER_REPEATED_TILES * (float)TILE_SIZE;

        /* In PLANET mode, entity position is the logical position */
        float fY = m_ufo.entity.vPos.fY;

        float fDepthTop = polar_depth(fY, fInnerPx, fOuterPx);
        float fDepthBottom = polar_depth(fWorldH - fY, fInnerPx, fOuterPx);

        /* Choose pole: +1 pushes down (away from top), -1 pushes up (away from bottom) */
        float fBandDepth = 0.0f;
        float fPushSign = 0.0f;

        if (fDepthTop > fDepthBottom)
        {
            fBandDepth = fDepthTop;
            fPushSign = +1.0f;
        }
        else if (fDepthBottom > 0.0f)
        {
            fBandDepth = fDepthBottom;
            fPushSign = -1.0f;
        }

        if (fBandDepth > 0.0f)
        {
            float fBasePushback = (UFO_THRUST * fTurboMultiplier) * UFO_POLAR_PUSHBACK_SCALE;

            float fDepthCurve = fBandDepth * fBandDepth;
            float fSineWave = 1.0f + 0.3f * fm_sinf(m_fPolarOscillationTime * UFO_POLAR_SINE_FREQ);

            /* Positive if moving toward the pole */
            float fVelTowardPole = -m_ufo.vVel.fY * fPushSign;

            float fVelResist = 0.0f;
            if (fVelTowardPole > 0.0f)
                fVelResist = fVelTowardPole * UFO_POLAR_VELOCITY_RESISTANCE * fBandDepth;

            float fOpposingAccel = (fBasePushback * fDepthCurve * fSineWave) + fVelResist;

            m_ufo.vVel.fY += (fOpposingAccel * fFrameMul) * fPushSign;
        }

        /* Option B clamp: allow travel into repeated region */
        {
            float fMinY = -fOuterPx;
            float fMaxY = fWorldH + fOuterPx;

            if (m_ufo.entity.vPos.fY < fMinY)
            {
                m_ufo.entity.vPos.fY = fMinY;
                if (m_ufo.vVel.fY < 0.0f)
                    m_ufo.vVel.fY = 0.0f;
            }
            else if (m_ufo.entity.vPos.fY > fMaxY)
            {
                m_ufo.entity.vPos.fY = fMaxY;
                if (m_ufo.vVel.fY > 0.0f)
                    m_ufo.vVel.fY = 0.0f;
            }
        }

        /* Wrap X coordinate to stay within world bounds in PLANET state */
        m_ufo.entity.vPos.fX = tilemap_wrap_world_x(m_ufo.entity.vPos.fX);
    }

    /* Advance polar oscillation time */
    if (gp_state_get() == PLANET)
        m_fPolarOscillationTime += fFrameMul;
    else
        m_fPolarOscillationTime = 0.0f;

    /* --- Cache speed for performance --- */
    m_ufo.fSpeed = vec2_mag(m_ufo.vVel);

    /* --- Update engine sound frequency based on thrust --- */
    audio_update_engine_freq(m_ufo.fThrust);

    /* --- Calculate shadow position in world space --- */
    /* In SURFACE mode, shadow position is static (UFO doesn't move), so skip recalculation unless updating animation */
    if (gp_state_get() != SURFACE || m_ufo.animType != UFO_ANIM_NONE)
    {
        ufo_internal_update_shadow();
    }

    /* Advance thruster animation time using frame multiplier */
    m_fThrusterAnimFrame += fFrameMul;
}

/* -------------------------------------------------------------------------- */
/* Render                                                                     */
/* -------------------------------------------------------------------------- */

static bool ufo_update_indicator_logic(bool _bInstant, float *_pOutAngle)
{
    struct vec2 vTargetEntityPos;
    struct vec2 vUfoPos;
    float fClosestDirAngle = 0.0f;
    bool bMovingTowardsTarget = false;
    float fTargetDistance = 0.0f;
    bool bInCloseProximity = false;

    if (!ufo_compute_next_target_indicator(&vTargetEntityPos, &vUfoPos, &fClosestDirAngle, &bMovingTowardsTarget, &fTargetDistance, &bInCloseProximity))
    {
        return false;
    }

    if (_pOutAngle)
        *_pOutAngle = fClosestDirAngle;

    struct vec2 vLineDir = vec2_sub(vTargetEntityPos, vUfoPos);
    float fLineLength = vec2_mag(vLineDir);

    if (fLineLength > 1e-6f)
    {
        /* Normalize direction once */
        vLineDir = vec2_scale(vLineDir, 1.0f / fLineLength);

        struct vec2 vTargetOnLine = vec2_add(vUfoPos, vec2_scale(vLineDir, fTargetDistance));

        if (_bInstant)
        {
            m_vNextTargetIndicatorPos = vTargetOnLine;
        }
        else
        {
            /* Convert screen distance to world distance based on current zoom */
            float fZoom = camera_get_zoom(&g_mainCamera);
            float fMinDist = UFO_NEXT_TARGET_INDICATOR_MIN_DISTANCE / fZoom;

            /* Project current indicator onto line and clamp */
            struct vec2 vToIndicator = vec2_sub(m_vNextTargetIndicatorPos, vUfoPos);
            float fCurrentDist = vec2_dot(vToIndicator, vLineDir);

            if (!bInCloseProximity)
                fCurrentDist = clampf(fCurrentDist, fMinDist, fLineLength);
            else
                fCurrentDist = clampf(fCurrentDist, 0.0f, fLineLength);

            /* Calculate lerp speed with proximity boost when moving towards UFO */
            float fFrameMul = frame_time_mul();
            float fLerpSpeed = bMovingTowardsTarget ? UFO_NEXT_TARGET_INDICATOR_LERP_TO_TARGET : UFO_NEXT_TARGET_INDICATOR_LERP_TO_UFO;

            if (!bMovingTowardsTarget && fCurrentDist < fMinDist * 3.0f)
            {
                /* Boost lerp speed when close to UFO (1.0x to 3.0x multiplier) */
                float fProximityFactor = 1.0f - clampf_01((fCurrentDist - fMinDist) / (fMinDist * 2.0f));
                fLerpSpeed *= (1.0f + fProximityFactor * 2.0f);
            }
            fLerpSpeed = clampf_01(fLerpSpeed);

            /* Calculate lerp factor with easing */
            float fLerp = 1.0f - powf(1.0f - fLerpSpeed, fFrameMul);
            if (bMovingTowardsTarget)
                fLerp *= fLerp; /* Ease-in */
            else
            {
                /* Cubic ease-out: 1 - (1-t)^3 */
                float fOneMinusT = 1.0f - fLerp;
                fLerp = 1.0f - fOneMinusT * fOneMinusT * fOneMinusT;
            }

            /* Lerp along line */
            struct vec2 vCurrentOnLine = vec2_add(vUfoPos, vec2_scale(vLineDir, fCurrentDist));
            m_vNextTargetIndicatorPos = vec2_mix(vCurrentOnLine, vTargetOnLine, fLerp);

            /* Final clamp (skip in close-proximity mode) */
            if (!bInCloseProximity)
            {
                float fFinalDist = vec2_mag(vec2_sub(m_vNextTargetIndicatorPos, vUfoPos));
                if (fFinalDist < fMinDist)
                    m_vNextTargetIndicatorPos = vec2_add(vUfoPos, vec2_scale(vLineDir, fMinDist));
            }

            /* In minimap mode, prevent overshooting the target */
            if (minimap_is_active())
            {
                float fFinalDist = vec2_mag(vec2_sub(m_vNextTargetIndicatorPos, vUfoPos));
                if (fFinalDist > fLineLength)
                {
                    m_vNextTargetIndicatorPos = vec2_add(vUfoPos, vec2_scale(vLineDir, fLineLength));
                }
            }
        }
    }
    else
    {
        m_vNextTargetIndicatorPos = vUfoPos;
    }

    return true;
}

void ufo_render_target_lock(void)
{
    /* Early out if no weapons are unlocked */
    if (!weapons_any_unlocked())
        return;

    float fZoom = camera_get_zoom(&g_mainCamera);
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_BILINEAR);

    struct vec2i vTargetScreen;
    if (m_spriteLockOn && ufo_entity_to_screen(m_pTargetMeteor, &vTargetScreen))
    {
        rdpq_sprite_blit(m_spriteLockOn,
                         vTargetScreen.iX,
                         vTargetScreen.iY,
                         &(rdpq_blitparms_t){.cx = m_spriteLockOn->width / 2, .cy = m_spriteLockOn->height / 2, .scale_x = fZoom, .scale_y = fZoom});
        return;
    }

    struct vec2i vClosestScreen;
    /* Show preview of what would be selected when pressing Z (use cached potential target) */
    const struct entity2D *pSelected = m_pPotentialTarget;
    if (m_spriteLockSelection && ufo_entity_to_screen(pSelected, &vClosestScreen))
    {
        rdpq_sprite_blit(m_spriteLockSelection,
                         vClosestScreen.iX,
                         vClosestScreen.iY,
                         &(rdpq_blitparms_t){.cx = m_spriteLockSelection->width / 2, .cy = m_spriteLockSelection->height / 2, .scale_x = fZoom, .scale_y = fZoom});
    }
}

void ufo_render(void)
{
    const struct entity2D *pEnt = &m_ufo.entity;

    if (!entity2d_is_visible(pEnt))
        return;

    float fZoom = camera_get_zoom(&g_mainCamera);

    /* If GP state is SURFACE, render only the UFO body at shadow position to the intermediate surface */
    if (gp_state_get() == SURFACE)
    {
        /* In SURFACE mode, render to intermediate surface using wrapped camera coordinates */
        /* Must use tilemap_world_to_surface instead of camera_world_to_screen to match tilemap rendering */

        /* Ensure both positions are in canonical wrapped space for consistent delta calculation */
        struct vec2 vShadowWrapped = m_ufo.vShadowPos;
        struct vec2 vCamWrapped = g_mainCamera.vPos;
        if (g_mainTilemap.bInitialized)
        {
            vShadowWrapped.fX = tilemap_wrap_world_x(vShadowWrapped.fX);
            vCamWrapped.fX = tilemap_wrap_world_x(vCamWrapped.fX);
        }

        /* Calculate wrapped delta to find shortest rendering path */
        struct vec2 vDelta = gp_camera_calc_wrapped_delta(vCamWrapped, vShadowWrapped);
        struct vec2 vAdjustedPos = vec2_add(vCamWrapped, vDelta);

        /* Check visibility using adjusted position with larger margin to account for sprite size */
        if (!gp_camera_is_point_visible_wrapped(&g_mainCamera, vAdjustedPos, (float)pEnt->vHalf.iX * 3.0f))
            return; /* shadow position not visible */

        /* Convert adjusted shadow position from world to surface (undistorted intermediate buffer) */
        struct vec2i vShadowSurface;
        if (!tilemap_world_to_surface(vAdjustedPos, &vShadowSurface))
            return; /* shadow position outside surface bounds */

        rdpq_set_mode_standard();
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_filter(FILTER_BILINEAR);

        /* Render to surface - distortion will be applied when surface is composited to screen */
        rdpq_sprite_blit(m_spriteUfo,
                         vShadowSurface.iX,
                         vShadowSurface.iY,
                         &(rdpq_blitparms_t){.cx = pEnt->vHalf.iX,
                                             .cy = pEnt->vHalf.iY,
                                             .scale_x = fZoom * UFO_SHADOW_TARGET_SIZE,
                                             .scale_y = fZoom * UFO_SHADOW_TARGET_SIZE,
                                             .theta = -m_ufo.fAngleRad});
        return;
    }

    /* For non-SURFACE modes, check entity visibility using wrapped check */
    struct vec2i vScreen;
    if (!gp_camera_entity_world_to_screen_wrapped(&g_mainCamera, pEnt, &vScreen))
        return; /* fully outside view */

    /* Base center for all UFO sprites (Logical Position) */
    int iCenterX = vScreen.iX;
    int iCenterY = vScreen.iY;

    /* Calculate shadow offset from stored shadow position (for animation calculations) */
    float fShadowOffsetY = m_ufo.vShadowPos.fY - m_ufo.entity.vPos.fY;

    /* Animation: Calculate visual offset and scale */
    struct vec2 vRenderOffset = vec2_zero();
    float fScale = 1.0f;

    if (m_ufo.animType != UFO_ANIM_NONE)
    {
        float t = m_ufo.fAnimTimer / UFO_LANDING_DURATION;
        if (t > 1.0f)
            t = 1.0f;
        float t_smooth = t * t * (3.0f - 2.0f * t);

        if (m_ufo.animType == UFO_ANIM_SPACE_TO_PLANET)
        {
            fScale = 1.0f - t_smooth;
        }
        else if (m_ufo.animType == UFO_ANIM_PLANET_TO_SPACE)
        {
            fScale = t_smooth;
        }
        else if (m_ufo.animType == UFO_ANIM_PLANET_TO_SURFACE)
        {
            fScale = 1.0f + (UFO_SHADOW_TARGET_SIZE - 1.0f) * t_smooth;
            /* Move from 0 to fShadowOffsetY */
            vRenderOffset.fY = fShadowOffsetY * t_smooth;
        }
        else if (m_ufo.animType == UFO_ANIM_SURFACE_TO_PLANET)
        {
            fScale = UFO_SHADOW_TARGET_SIZE + (1.0f - UFO_SHADOW_TARGET_SIZE) * t_smooth;
            /* Move from fShadowOffsetY to 0 */
            vRenderOffset.fY = fShadowOffsetY * (1.0f - t_smooth);
        }
    }

    /* Determine if shadow should be drawn */
    /* Shadow is visible on PLANET, or during transitions to/from SURFACE */
    /* Shadow is always at the "ground" position relative to the logical UFO pos */
    if (gp_state_get() == PLANET || m_ufo.animType == UFO_ANIM_PLANET_TO_SURFACE || m_ufo.animType == UFO_ANIM_SURFACE_TO_PLANET)
    {
        /* Check if shadow position is visible (use wrapped version for PLANET mode) */
        if (gp_camera_is_point_visible_wrapped(&g_mainCamera, m_ufo.vShadowPos, 0.0f))
        {
            /* Convert shadow position from world space to screen space (use wrapped version) */
            struct vec2i vShadowScreen;
            gp_camera_world_to_screen_wrapped(&g_mainCamera, m_ufo.vShadowPos, &vShadowScreen);

            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT); // output = TEX0 * PRIM (RGB and A)
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);   // normal alpha blending using combiner alpha
            rdpq_mode_filter(FILTER_BILINEAR);
            rdpq_set_prim_color(RGBA32(0, 0, 0, 128)); // black, adjustable opacity

            rdpq_sprite_blit(m_spriteUfo,
                             vShadowScreen.iX,
                             vShadowScreen.iY,
                             &(rdpq_blitparms_t){.cx = pEnt->vHalf.iX,
                                                 .cy = pEnt->vHalf.iY,
                                                 .scale_x = fZoom * UFO_SHADOW_TARGET_SIZE,
                                                 .scale_y = fZoom * UFO_SHADOW_TARGET_SIZE,
                                                 .theta = -m_ufo.fAngleRad});
        }
    }

    /* Apply offset for UFO body rendering */
    int iRenderX = iCenterX + (int)vRenderOffset.fX;
    int iRenderY = iCenterY + (int)vRenderOffset.fY;

    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_BILINEAR);

    if (fScale != 0.0f)
    {
        /* Draw thruster effect based on thrust level */
        /* Only draw thrusters if not busy (animating) */
        if (!ufo_is_transition_playing() && m_ufo.bAligned && m_ufo.fThrust >= UFO_THRUST_MIN_THRESHOLD)
        {
            bool bTargetLocked = ufo_is_target_locked();
            float fThrusterAngleRad = (bTargetLocked && m_ufo.fStickForce > 0.0f) ? ((float)m_ufo.iStickAngle * FM_PI / 180.0f) : m_ufo.fAngleRad;

            sprite_t *pThrusterSprite = NULL;
            if (m_ufo.fThrust >= UFO_THRUST_TURBO_THRESHOLD)
                pThrusterSprite = ufo_turbo_get_sprite();
            else if (m_ufo.fThrust >= UFO_THRUST_STRONG_THRESHOLD)
                pThrusterSprite = m_spriteUfoThrusterStrong;
            else if (m_ufo.fThrust >= UFO_THRUST_NORMAL_THRESHOLD)
                pThrusterSprite = m_spriteUfoThruster;
            else
                pThrusterSprite = m_spriteUfoMiniThrust;

            int iThrusterX = iRenderX;
            int iThrusterY = iRenderY;

            bool bThrusterOffsetPhase = (((uint32_t)(m_fThrusterAnimFrame / UFO_THRUSTER_WOBBLE_FRAMES)) & 1U) != 0;
            if (bThrusterOffsetPhase)
            {
                /* Push the thruster 1px backward along facing direction for a subtle flicker */
                float fBackX = -fm_sinf(fThrusterAngleRad);
                float fBackY = fm_cosf(fThrusterAngleRad);
                iThrusterX += (int)roundf(fBackX);
                iThrusterY += (int)roundf(fBackY);
            }

            rdpq_sprite_blit(pThrusterSprite,
                             iThrusterX,
                             iThrusterY,
                             &(rdpq_blitparms_t){.cx = pEnt->vHalf.iX, .cy = pEnt->vHalf.iY, .scale_x = fZoom * fScale, .scale_y = fZoom * fScale, .theta = -fThrusterAngleRad});
        }

        /* Draw UFO body */
        rdpq_sprite_blit(m_spriteUfo,
                         iRenderX,
                         iRenderY,
                         &(rdpq_blitparms_t){.cx = pEnt->vHalf.iX, .cy = pEnt->vHalf.iY, .scale_x = fZoom * fScale, .scale_y = fZoom * fScale, .theta = -m_ufo.fAngleRad});

        rdpq_sprite_blit(m_spriteUfoHighlight,
                         iRenderX,
                         iRenderY,
                         &(rdpq_blitparms_t){.cx = pEnt->vHalf.iX, .cy = pEnt->vHalf.iY, .scale_x = fZoom * fScale, .scale_y = fZoom * fScale});

        /* Only render weapon glow if weapon is firing */
        if (weapons_is_firing())
        {
            color_t uWeaponColor = weapons_get_current_color();
            uWeaponColor.a = 96;
            rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT); // output = TEX0 * PRIM (RGB and A)
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_filter(FILTER_BILINEAR);
            rdpq_set_prim_color(uWeaponColor);
            rdpq_sprite_blit(m_spriteUfoWeaponGlow,
                             iRenderX,
                             iRenderY,
                             &(rdpq_blitparms_t){.cx = pEnt->vHalf.iX, .cy = pEnt->vHalf.iY, .scale_x = fZoom * fScale, .scale_y = fZoom * fScale, .theta = -m_ufo.fAngleRad});
        }
    }

    bool bTargetVisible = ufo_target_is_visible(m_pTargetMeteor);

    /* Draw direction indicator toward closest meteor when not locked */
    /* Skip rendering during dialogue */
    if (!bTargetVisible && m_spriteNextTarget && !dialogue_is_active())
    {
        float fClosestDirAngle = 0.0f;
        if (ufo_update_indicator_logic(false, &fClosestDirAngle))
        {
            /* Convert to screen space */
            struct vec2i vIndicatorScreen;
            camera_world_to_screen(&g_mainCamera, m_vNextTargetIndicatorPos, &vIndicatorScreen);

            /* Check if indicator position is visible on screen */
            if (camera_is_point_visible(&g_mainCamera, m_vNextTargetIndicatorPos, 0.0f))
            {
                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_filter(FILTER_BILINEAR);

                /* Rotation is always fresh, never lerped - render without zoom scaling */
                rdpq_sprite_blit(m_spriteNextTarget,
                                 vIndicatorScreen.iX,
                                 vIndicatorScreen.iY,
                                 &(rdpq_blitparms_t){.cx = m_spriteNextTarget->width / 2, .cy = m_spriteNextTarget->height / 2, .theta = -fClosestDirAngle});
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Getters                                                                    */
/* -------------------------------------------------------------------------- */

struct vec2 ufo_get_position(void)
{
    return m_ufo.entity.vPos;
}

struct vec2 ufo_get_velocity(void)
{
    return m_ufo.vVel;
}

float ufo_get_speed(void)
{
    return m_ufo.fSpeed;
}

float ufo_get_stick_force(void)
{
    return m_ufo.fStickForce;
}

int ufo_get_stick_angle(void)
{
    return m_ufo.iStickAngle;
}

float ufo_get_thrust(void)
{
    return m_ufo.fThrust;
}

const struct entity2D *ufo_get_entity(void)
{
    return &m_ufo.entity;
}

struct vec2 ufo_get_shadow_position(void)
{
    return m_ufo.vShadowPos;
}

bool ufo_can_land(void)
{
    if (!g_mainTilemap.bInitialized)
        return false;

    /* Check a box area around the shadow position to ensure safe landing.
     * Use the UFO collision radius to define a square area that must be clear
     * for the player_surface to move around after landing. */
    struct vec2 vShadowPos = ufo_get_shadow_position();

    /* Define box half-extents based on UFO collision radius */
    struct vec2 vHalfExtents = vec2_make((float)UFO_COLLISION_RADIUS, (float)UFO_COLLISION_RADIUS);

    /* Use tilemap_can_walk_box to check if the entire area is walkable and landable
     * (has ground on layer 1, no blocking tiles on layer 2, and no decoration tiles on layers 3 or 4) */
    return tilemap_can_walk_box(vShadowPos, vHalfExtents, false, true);
}

void ufo_set_velocity(struct vec2 _vVel)
{
    m_ufo.vVel = _vVel;
}

bool ufo_is_target_locked(void)
{
    return (m_pTargetMeteor != NULL) && entity2d_is_active(m_pTargetMeteor);
}

const struct entity2D *ufo_get_locked_target(void)
{
    return m_pTargetMeteor;
}

const struct entity2D *ufo_get_potential_target(void)
{
    /* Return cached potential target calculated once per frame in ufo_update() */
    return m_pPotentialTarget;
}

void ufo_set_next_target(const struct entity2D *_pEntity)
{
    m_pNextTarget = _pEntity;
    /* Initialize indicator position to UFO position when target is set */
    if (_pEntity)
    {
        /* Calculate immediate target position to avoid lerp delay on first frame */
        ufo_update_indicator_logic(true, NULL);
    }
}

const struct entity2D *ufo_get_next_target(void)
{
    return m_pNextTarget;
}

void ufo_deselect_entity_lock_and_marker(const struct entity2D *_pEntity)
{
    if (m_pNextTarget == _pEntity)
    {
        m_pNextTarget = NULL;
    }
    if (m_pTargetMeteor == _pEntity)
    {
        m_pTargetMeteor = NULL;
    }
}

void ufo_apply_bounce_effect(uint32_t _uDurationMs)
{
    /* Apply bounce: reduce thrust effectiveness during cooldown */
    m_fBounceThrustReduction = 0.2f; /* 20% effectiveness during cooldown */
    m_uBounceCooldownEndMs = get_ticks_ms() + _uDurationMs;

    /* Play bounce sound once on UFO channel */
    if (m_sfxBounce)
    {
        wav64_play(m_sfxBounce, MIXER_CHANNEL_UFO);
    }
}

bool ufo_is_bouncing(void)
{
    return (m_uBounceCooldownEndMs > 0) && (get_ticks_ms() < m_uBounceCooldownEndMs);
}

bool ufo_is_transition_playing(void)
{
    return (m_ufo.animType != UFO_ANIM_NONE) && (m_ufo.fAnimTimer < UFO_LANDING_DURATION);
}

void ufo_play_door(bool _bOpening)
{
    if (_bOpening)
    {
        wav64_play(m_sfxDoorOpen, MIXER_CHANNEL_UFO);
    }
    else
    {
        wav64_play(m_sfxDoorClose, MIXER_CHANNEL_UFO);
    }
}
