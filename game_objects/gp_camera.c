#include "gp_camera.h"

#include <math.h>

#include "../camera.h"
#include "../dialogue.h"
#include "../font_helper.h"
#include "../frame_time.h"
#include "../game_objects/ufo.h"
#include "../math2d.h"
#include "../math_helper.h"
#include "../minimap.h"
#include "../player_jnr.h"
#include "../player_surface.h"
#include "../stick_normalizer.h"
#include "../ui.h"
#include "libdragon.h"
#include "n64sys.h"

/* Static variables for camera state */
static float m_fTargetZoom = CAMERA_ZOOM_DEFAULT;
static uint32_t m_uZoomInStartTick = 0;
static struct vec2 m_vDebugTarget;
static bool m_bManualZoomActive = false;

/* Static variables for JNR camera Y-axis control */
static float m_fJnrYTranslation = 0.0f;
static uint32_t m_uJnrYDeadzoneStartTick = 0;
static bool m_bJnrYInDeadzone = false;

/* Dialogue inset (screen space) */
static float m_fInsetCurrentPx = 0.0f;
static bool m_bInsetTop = true;

void gp_camera_init(void)
{
    m_fTargetZoom = CAMERA_ZOOM_DEFAULT;
    m_uZoomInStartTick = 0;
    m_vDebugTarget = vec2_zero();
    m_bManualZoomActive = false;
    m_fInsetCurrentPx = 0.0f;
    m_bInsetTop = true;
}

/* Helper: Handle manual zoom controls (D-Pad Up/Down to zoom, Left/Right to reset) */
/* Only works in dev builds */
static inline void handle_manual_zoom_controls(bool _bDUp, bool _bDDown, bool _bDLeft, bool _bDRight)
{
#ifdef DEV_BUILD
    if (_bDUp || _bDDown)
    {
        /* Activate manual zoom mode */
        m_bManualZoomActive = true;

        /* Step-based zoom while holding */
        float fCurrentZoom = camera_get_zoom(&g_mainCamera);
        float fStep = CAMERA_ZOOM_MANUAL_STEP;
        float fNewZoom = fCurrentZoom;

        if (_bDUp)
            fNewZoom += fStep;
        if (_bDDown)
            fNewZoom -= fStep;

        fNewZoom = clampf(fNewZoom, CAMERA_ZOOM_MIN, CAMERA_ZOOM_MAX);
        camera_set_zoom(&g_mainCamera, fNewZoom);
        m_fTargetZoom = fNewZoom;
    }
    else if (_bDLeft || _bDRight)
    {
        /* Reset manual zoom mode */
        m_bManualZoomActive = false;
        camera_set_zoom(&g_mainCamera, CAMERA_ZOOM_DEFAULT);
        m_fTargetZoom = CAMERA_ZOOM_DEFAULT;
    }
#else
    /* D-pad camera control disabled in non-dev builds */
    (void)_bDUp;
    (void)_bDDown;
    (void)_bDLeft;
    (void)_bDRight;
#endif
}

/* Helper: Check if we're in a wrapping mode (PLANET or SURFACE with tilemap) */
static inline bool is_wrapping_mode(void)
{
    gp_state_t currentState = gp_state_get();
    return (currentState == PLANET || currentState == SURFACE) && g_mainTilemap.bInitialized;
}

/* Helper: Wrap camera position on X-axis (used before and after following in wrapping modes) */
static inline void wrap_camera_position(void)
{
    g_mainCamera.vPos.fX = tilemap_wrap_world_x(g_mainCamera.vPos.fX);
    /* NOTE: We do NOT wrap vPrev here! This preserves velocity information across wrap boundaries.
     * The tilemap rendering code wraps camera position internally, so it can handle an unwrapped vPrev. */
}

/* Helper: Calculate look-ahead offset based on velocity and parameters */
static inline struct vec2 calculate_look_ahead_offset(float _fVelMag, struct vec2 _vVel, float _fMinSpeed, float _fMaxSpeed, float _fFactor, float _fCurvePower, float _fLerpSpeed,
                                                      float _fYScale)
{
    float fLookAheadDist = 0.0f;
    if (_fVelMag > _fMinSpeed)
    {
        float fT = (_fVelMag - _fMinSpeed) / (_fMaxSpeed - _fMinSpeed);
        if (fT > 1.0f)
            fT = 1.0f;
        /* Apply curve to give more look-ahead at slow speeds */
        float fTCurved = powf(fT, _fCurvePower);
        fLookAheadDist = (_fFactor - UI_OVERSCAN_PADDING) * fTCurved;
    }

    /* Direction and Lag Compensation */
    struct vec2 vLookAheadOffset = vec2_zero();
    if (_fVelMag > 1e-6f)
    {
        struct vec2 vDir = vec2_normalize(_vVel);
        float fRatio = (float)SCREEN_H / (float)SCREEN_W;
        float fLag = 1.0f / _fLerpSpeed;

        vLookAheadOffset = vec2_make(vDir.fX * (fLookAheadDist + fLag), vDir.fY * (fLookAheadDist * fRatio + fLag) * _fYScale);
    }

    return vLookAheadOffset;
}

/* Helper: Apply zoom lerp with snapping to default (used by UFO and JNR modes) */
static inline void apply_zoom_lerp(float fFrameMul)
{
    if (m_bManualZoomActive)
        return; /* Manual zoom mode: zoom stays at current level */

    float fCurrentZoom = camera_get_zoom(&g_mainCamera);

    /* If both current and target are close to default, snap to it */
    bool bNearDefault = fabsf(fCurrentZoom - CAMERA_ZOOM_DEFAULT) < CAMERA_ZOOM_DEFAULT_SNAP_THRESHOLD;
    bool bTargetDefault = fabsf(m_fTargetZoom - CAMERA_ZOOM_DEFAULT) < CAMERA_ZOOM_DEFAULT_SNAP_THRESHOLD;

    if (bNearDefault && bTargetDefault)
    {
        camera_set_zoom(&g_mainCamera, CAMERA_ZOOM_DEFAULT);
    }
    else
    {
        /* Choose lerp speed based on zoom direction */
        float fZoomLerpSpeed = (m_fTargetZoom > fCurrentZoom) ? CAMERA_ZOOM_LERP_IN : CAMERA_ZOOM_LERP_OUT;
        float fZoomLerp = 1.0f - powf(1.0f - fZoomLerpSpeed, fFrameMul);
        float fNewZoom = fCurrentZoom + (m_fTargetZoom - fCurrentZoom) * fZoomLerp;

        fNewZoom = clampf(fNewZoom, CAMERA_ZOOM_MIN, CAMERA_ZOOM_MAX);

        if (fabsf(fNewZoom - CAMERA_ZOOM_DEFAULT) < CAMERA_ZOOM_DEFAULT_SNAP_THRESHOLD)
            fNewZoom = CAMERA_ZOOM_DEFAULT;

        camera_set_zoom(&g_mainCamera, fNewZoom);
    }
}

/* Helper: Check if entity is within camera Y bounds (shared by visibility checks) */
static inline bool check_entity_y_visible(const struct camera2D *_pCamera, const struct entity2D *_pEnt, float fCamHalfY)
{
    float fEntTop = _pEnt->vPos.fY - (float)_pEnt->vHalf.iY;
    float fEntBottom = _pEnt->vPos.fY + (float)_pEnt->vHalf.iY;
    float fCamTop = _pCamera->vPos.fY - fCamHalfY;
    float fCamBottom = _pCamera->vPos.fY + fCamHalfY;

    return !(fEntBottom < fCamTop || fEntTop > fCamBottom);
}

void gp_camera_ufo_update(bool _bDUp, bool _bDDown, bool _bDLeft, bool _bDRight)
{
    /* Get frame multiplier internally */
    float fFrameMul = frame_time_mul();

    /* Dialogue mode: simple player position follow */
    if (dialogue_is_active())
    {
        apply_zoom_lerp(fFrameMul);

        /* Wrap camera before following in PLANET mode */
        if (gp_state_get() == PLANET && g_mainTilemap.bInitialized)
        {
            wrap_camera_position();
        }

        struct vec2 vTarget = ufo_get_position();
        m_vDebugTarget = vTarget;
        float fCameraLerp = 1.0f - powf(1.0f - CAMERA_LERP, fFrameMul);
        // HACK so ufo gets slowly centered on screen during dialogue (lerps slower than normal)
        gp_camera_follow_target_ellipse_with_wrapping(&g_mainCamera, vTarget, CAMERA_DEADZONE_RADIUS * 0.1f, fCameraLerp * 0.5f);

        /* Wrap camera again after following */
        if (gp_state_get() == PLANET && g_mainTilemap.bInitialized)
        {
            wrap_camera_position();
        }
        return;
    }

    /* Minimap Override */
    if (minimap_is_active())
    {
        float fProgress = minimap_get_zoom_progress();

        /* 1. Zoom Control */
        /* Interpolate zoom directly based on minimap progress */
        float fMinimapZoom = MINIMAP_ZOOM_LEVEL;
        float fStartZoom = CAMERA_ZOOM_DEFAULT; /* Assume default start for simplicity */
        float fCurrentZoom = fStartZoom + (fMinimapZoom - fStartZoom) * fProgress;
        camera_set_zoom(&g_mainCamera, fCurrentZoom);
        /* Update target to match so it doesn't drift when we exit */
        m_fTargetZoom = CAMERA_ZOOM_DEFAULT;

        /* 2. Position Control */
        /* Target is UFO position + accumulated translation */
        /* Translation interpolation is handled internally by minimap_update during close */
        struct vec2 vUfoPos = ufo_get_position();
        struct vec2 vOffset = minimap_get_camera_translation();
        struct vec2 vTarget = vec2_add(vUfoPos, vOffset);

        m_vDebugTarget = vTarget;

        /* Use wrapping support for camera following in PLANET mode */
        if (gp_state_get() == PLANET && g_mainTilemap.bInitialized)
        {
            wrap_camera_position();
        }

        /* Use a slightly faster lerp for responsive minimap feel, or just standard */
        /* Blending from UFO follow to free cam is handled by the offset starting at 0 */
        camera_follow_target_ellipse(&g_mainCamera, vTarget, 0, CAMERA_LERP);

        return; /* Skip standard UFO camera logic */
    }

    /* Camera zoom controls (D-Pad Up/Down; Left/Right resets) */
    handle_manual_zoom_controls(_bDUp, _bDDown, _bDLeft, _bDRight);

    /* --------------------------------------------------------------------------
       2. Determine Camera Target & Zoom
       -------------------------------------------------------------------------- */
    struct vec2 vTarget;
    const struct entity2D *pLockedTarget = ufo_is_target_locked() ? ufo_get_locked_target() : NULL;

    /* Get speed and velocity for look-ahead */
    float fVelMag = ufo_get_speed();
    struct vec2 vVel = ufo_get_velocity();

    if (pLockedTarget)
    {
        /* --- TARGET LOCK MODE --- */
        float fDeadZone = CAMERA_DEADZONE_RADIUS_LOCK_ON;
        struct vec2 vUfo = ufo_get_position();
        struct vec2 vLock = pLockedTarget->vPos;

        /* Weight Y axis by aspect ratio to equalize screen space usage. */
        float fAspectRatio = (float)SCREEN_W / (float)SCREEN_H;
        struct vec2 vDiff = vec2_sub(vUfo, vLock);
        float fDist = sqrtf(vDiff.fX * vDiff.fX + (vDiff.fY * fAspectRatio) * (vDiff.fY * fAspectRatio));

        /* -- Zoom Logic -- */
        float fMinDim = (float)(SCREEN_W < SCREEN_H ? SCREEN_W : SCREEN_H);

        /* Account for overscan padding in fit size */
        float fSafePadding = (float)UI_OVERSCAN_PADDING * 2.0f;
        float fEffectiveMinDim = fMinDim - fSafePadding;

        float fFitSize = fEffectiveMinDim - fDeadZone * 2.0f; /* Available space at 1.0 zoom */

        float fRequiredZoom = CAMERA_ZOOM_DEFAULT;
        /* Start zooming out if distance exceeds threshold of fit size */
        if (fDist > fFitSize * CAMERA_ZOOM_START_THRESHOLD)
        {
            /* Required Zoom = AvailableScreenSpace / (Dist + Deadzone*2) */
            fRequiredZoom = fEffectiveMinDim / (fDist + fDeadZone * 2.0f);
            fRequiredZoom = clampf(fRequiredZoom, CAMERA_ZOOM_LOCK_ON_MIN, CAMERA_ZOOM_DEFAULT);
        }

        /* Update target zoom (only if not manually controlling) */
        if (!m_bManualZoomActive)
        {
            if (fRequiredZoom < m_fTargetZoom)
            {
                m_fTargetZoom = fRequiredZoom;
                m_uZoomInStartTick = 0;
            }
            else if (fRequiredZoom > m_fTargetZoom)
            {
                uint32_t uNow = get_ticks_ms();
                if (m_uZoomInStartTick == 0)
                    m_uZoomInStartTick = uNow;

                if (uNow - m_uZoomInStartTick > CAMERA_ZOOM_IN_LAG_MS)
                    m_fTargetZoom = fRequiredZoom;
            }
        }

        /* -- Positioning Logic -- */
        float fCurrentZoom = camera_get_zoom(&g_mainCamera);
        float fVisibleDist = (fEffectiveMinDim / fCurrentZoom) - fDeadZone * 2.0f;

        if (fDist > fVisibleDist && fDist > 1e-6f)
        {
            /* Bias = ratio of how much fits vs real distance. */
            float fBias = fVisibleDist / fDist;
            vTarget = vec2_mix(vUfo, vLock, fBias * 0.5f);
        }
        else
        {
            vTarget = vec2_mix(vUfo, vLock, 0.5f);
        }
    }
    else
    {
        /* --- FREE FLIGHT MODE (Look-Ahead) --- */

        /* Reset zoom if not manual */
        if (!m_bManualZoomActive)
            m_fTargetZoom = CAMERA_ZOOM_DEFAULT;

        /* Calculate look-ahead using helper function */
        struct vec2 vLookAheadOffset = calculate_look_ahead_offset(fVelMag,
                                                                   vVel,
                                                                   CAMERA_LOOK_AHEAD_MIN_SPEED,
                                                                   CAMERA_LOOK_AHEAD_MAX_SPEED,
                                                                   CAMERA_LOOK_AHEAD_FACTOR,
                                                                   CAMERA_LOOK_AHEAD_CURVE_POWER,
                                                                   CAMERA_LERP,
                                                                   1.0f);

        vTarget = vec2_add(ufo_get_position(), vLookAheadOffset);
    }

    /* Set deadzone based on mode */
    float fDeadZone = pLockedTarget ? CAMERA_DEADZONE_RADIUS_LOCK_ON : CAMERA_DEADZONE_RADIUS;

    m_vDebugTarget = vTarget;

    /* --------------------------------------------------------------------------
       3. Update Camera Zoom & Position
       -------------------------------------------------------------------------- */
    apply_zoom_lerp(fFrameMul);

    /* Wrap camera before following in PLANET mode to ensure camera and UFO are in same coordinate space */
    if (gp_state_get() == PLANET && g_mainTilemap.bInitialized)
    {
        wrap_camera_position();
    }

    /* Reduce camera lerp speed during bounce to prevent hectic camera movement */
    float fEffectiveLerp = CAMERA_LERP;
    if (ufo_is_bouncing())
    {
        /* Heavily reduce lerp during bounce (makes camera smoother, less jumpy) */
        fEffectiveLerp = CAMERA_LERP * CAMERA_BOUNCY_LERP_REDUCTION;
    }

    float fCameraLerp = 1.0f - powf(1.0f - fEffectiveLerp, fFrameMul);

    /* Use wrapping support for camera following in PLANET mode */
    gp_camera_follow_target_ellipse_with_wrapping(&g_mainCamera, vTarget, fDeadZone, fCameraLerp);

    /* Wrap camera again after following - camera_follow can push us outside canonical range */
    if (gp_state_get() == PLANET && g_mainTilemap.bInitialized)
    {
        wrap_camera_position();
    }
}

/* Calculate wrapped delta between two positions for camera following.
 * Returns the shortest path considering world wrapping on X-axis. */
struct vec2 gp_camera_calc_wrapped_delta(struct vec2 _vFrom, struct vec2 _vTo)
{
    struct vec2 vDelta = vec2_sub(_vTo, _vFrom);

    /* In PLANET/SURFACE modes, account for world wrapping on X-axis to find shortest path */
    if (is_wrapping_mode())
    {
        float fWorldWidth = tilemap_get_world_width_pixels();
        if (fWorldWidth > 0.0f)
        {
            /* Find shortest wrapped distance on X-axis */
            float fHalfWidth = fWorldWidth * 0.5f;
            if (vDelta.fX > fHalfWidth)
                vDelta.fX -= fWorldWidth; /* Wrap left is shorter */
            else if (vDelta.fX < -fHalfWidth)
                vDelta.fX += fWorldWidth; /* Wrap right is shorter */
        }
    }

    return vDelta;
}

/* Check if entity is visible with wrapping support for PLANET/SURFACE modes */
bool gp_camera_is_entity_visible_wrapped(const struct camera2D *_pCamera, const struct entity2D *_pEnt)
{
    /* Camera bounds in world space */
    float fInvZoom = 1.0f / camera_get_zoom(_pCamera);
    float fCamHalfX = (float)_pCamera->vHalf.iX * fInvZoom;
    float fCamHalfY = (float)_pCamera->vHalf.iY * fInvZoom;

    /* Y-axis check (no wrapping) - use helper */
    if (!check_entity_y_visible(_pCamera, _pEnt, fCamHalfY))
        return false;

    /* X-axis check with wrapping support in PLANET/SURFACE modes */
    if (is_wrapping_mode())
    {
        /* Use wrapped distance to check X visibility */
        struct vec2 vDelta = gp_camera_calc_wrapped_delta(_pCamera->vPos, _pEnt->vPos);
        float fWrappedDistX = fabsf(vDelta.fX);
        float fMaxDistX = fCamHalfX + (float)_pEnt->vHalf.iX;

        if (fWrappedDistX > fMaxDistX)
            return false;
    }
    else
    {
        /* Normal X-axis check (no wrapping) */
        float fEntLeft = _pEnt->vPos.fX - (float)_pEnt->vHalf.iX;
        float fEntRight = _pEnt->vPos.fX + (float)_pEnt->vHalf.iX;
        float fCamLeft = _pCamera->vPos.fX - fCamHalfX;
        float fCamRight = _pCamera->vPos.fX + fCamHalfX;

        if (fEntRight < fCamLeft)
            return false;
        if (fEntLeft > fCamRight)
            return false;
    }

    return true;
}

/* Check if point is visible with wrapping support for PLANET/SURFACE modes */
bool gp_camera_is_point_visible_wrapped(const struct camera2D *_pCamera, struct vec2 _vPos, float _fMargin)
{
    if (is_wrapping_mode())
    {
        /* Use wrapped distance to check visibility */
        struct vec2 vDelta = gp_camera_calc_wrapped_delta(_pCamera->vPos, _vPos);
        float fWrappedDistX = fabsf(vDelta.fX);
        float fWrappedDistY = fabsf(vDelta.fY);

        float fInvZoom = 1.0f / camera_get_zoom(_pCamera);
        float fCamHalfX = (float)_pCamera->vHalf.iX * fInvZoom + _fMargin;
        float fCamHalfY = (float)_pCamera->vHalf.iY * fInvZoom + _fMargin;

        return (fWrappedDistX <= fCamHalfX && fWrappedDistY <= fCamHalfY);
    }
    else
    {
        /* Normal mode: use standard check */
        return camera_is_point_visible(_pCamera, _vPos, _fMargin);
    }
}

/* Convert world position to screen with wrapping support for PLANET/SURFACE modes */
void gp_camera_world_to_screen_wrapped(const struct camera2D *_pCamera, struct vec2 _vWorldPos, struct vec2i *_pOutScreen)
{
    /* In PLANET/SURFACE modes, use wrapped position for screen conversion */
    if (is_wrapping_mode())
    {
        /* Calculate wrapped delta and create adjusted position for rendering */
        struct vec2 vDelta = gp_camera_calc_wrapped_delta(_pCamera->vPos, _vWorldPos);
        struct vec2 vAdjustedPos = vec2_add(_pCamera->vPos, vDelta);
        camera_world_to_screen(_pCamera, vAdjustedPos, _pOutScreen);
    }
    else
    {
        /* Normal mode: use position directly */
        camera_world_to_screen(_pCamera, _vWorldPos, _pOutScreen);
    }
}

/* Entity visibility + world→screen with wrapping support for PLANET/SURFACE modes */
bool gp_camera_entity_world_to_screen_wrapped(const struct camera2D *_pCamera, const struct entity2D *_pEnt, struct vec2i *_pOutScreen)
{
    if (!gp_camera_is_entity_visible_wrapped(_pCamera, _pEnt))
        return false;

    gp_camera_world_to_screen_wrapped(_pCamera, _pEnt->vPos, _pOutScreen);

    return true;
}

/* Camera follow with wrapping support - calculates wrapped delta for proper movement across world boundaries */
void gp_camera_follow_target_ellipse_with_wrapping(struct camera2D *_pCamera, struct vec2 _vTarget, float _fDeadZone, float _fLerp)
{
    /* Calculate wrapped delta to find shortest path */
    struct vec2 vDelta = gp_camera_calc_wrapped_delta(_pCamera->vPos, _vTarget);

    /* Create adjusted target using wrapped delta */
    struct vec2 vAdjustedTarget = vec2_add(_pCamera->vPos, vDelta);

    /* Check if dialogue is active and use custom viewport if so */
    if (dialogue_is_active() && m_fInsetCurrentPx > 0.5f)
    {
        /* Calculate viewport rect from inset data */
        struct vec2i vViewportOffset;
        struct vec2i vViewportSize;

        if (m_bInsetTop)
        {
            /* Inset at top: viewport starts below the inset */
            vViewportOffset = vec2i_make(0, (int)m_fInsetCurrentPx);
            vViewportSize = vec2i_make(SCREEN_W, SCREEN_H - (int)m_fInsetCurrentPx);
        }
        else
        {
            /* Inset at bottom: viewport is above the inset */
            vViewportOffset = vec2i_make(0, 0);
            vViewportSize = vec2i_make(SCREEN_W, SCREEN_H - (int)m_fInsetCurrentPx);
        }

        camera_follow_target_ellipse_custom_viewport(_pCamera, vAdjustedTarget, _fDeadZone, _fLerp, vViewportOffset, vViewportSize);
    }
    else
    {
        /* Follow the adjusted target using standard camera function */
        camera_follow_target_ellipse(_pCamera, vAdjustedTarget, _fDeadZone, _fLerp);
    }
}

/* Camera follow with wrapping support - calculates wrapped delta for proper movement across world boundaries */
void gp_camera_follow_target_rect_with_wrapping(struct camera2D *_pCamera, struct vec2 _vTarget, float _fDeadZone, float _fLerp)
{
    /* Calculate wrapped delta to find shortest path */
    struct vec2 vDelta = gp_camera_calc_wrapped_delta(_pCamera->vPos, _vTarget);

    /* Create adjusted target using wrapped delta */
    struct vec2 vAdjustedTarget = vec2_add(_pCamera->vPos, vDelta);

    /* Check if dialogue is active and use custom viewport if so */
    if (dialogue_is_active() && m_fInsetCurrentPx > 0.5f)
    {
        /* Calculate viewport rect from inset data */
        struct vec2i vViewportOffset;
        struct vec2i vViewportSize;

        if (m_bInsetTop)
        {
            /* Inset at top: viewport starts below the inset */
            vViewportOffset = vec2i_make(0, (int)m_fInsetCurrentPx);
            vViewportSize = vec2i_make(SCREEN_W, SCREEN_H - (int)m_fInsetCurrentPx);
        }
        else
        {
            /* Inset at bottom: viewport is above the inset */
            vViewportOffset = vec2i_make(0, 0);
            vViewportSize = vec2i_make(SCREEN_W, SCREEN_H - (int)m_fInsetCurrentPx);
        }

        camera_follow_target_rect_custom_viewport(_pCamera, vAdjustedTarget, _fDeadZone, _fLerp, vViewportOffset, vViewportSize);
    }
    else
    {
        /* Follow the adjusted target using standard camera function */
        camera_follow_target_rect(_pCamera, vAdjustedTarget, _fDeadZone, _fLerp);
    }
}

/* Update camera for surface player (wraps camera, follows target with wrapped delta) */
void gp_camera_surface_update(void)
{
    float fFrameMul = frame_time_mul();

    /* Dialogue mode: simple player position follow */
    if (dialogue_is_active())
    {
        /* Wrap camera before following to ensure camera and player are in same coordinate space */
        if (g_mainTilemap.bInitialized)
        {
            wrap_camera_position();
        }

        struct vec2 vTarget = player_surface_get_position();
        float fCameraLerp = 1.0f - powf(1.0f - CAMERA_LERP_SURFACE, fFrameMul);
        gp_camera_follow_target_ellipse_with_wrapping(&g_mainCamera, vTarget, CAMERA_DEADZONE_RADIUS_SURFACE, fCameraLerp);

        /* Wrap camera again after following */
        if (g_mainTilemap.bInitialized)
        {
            wrap_camera_position();
        }
        return;
    }

    /* Wrap camera before following to ensure camera and player are in same coordinate space */
    if (g_mainTilemap.bInitialized)
    {
        wrap_camera_position();
    }

    /* Follow target with wrapping support (quantization handled separately in rendering) */
    struct vec2 vTarget = player_surface_get_position();
    gp_camera_follow_target_ellipse_with_wrapping(&g_mainCamera, vTarget, CAMERA_DEADZONE_RADIUS_SURFACE, CAMERA_LERP_SURFACE);

    /* Wrap camera again after following - camera_follow can push us outside canonical range */
    if (g_mainTilemap.bInitialized)
    {
        wrap_camera_position();
    }
}

void gp_camera_set_dialogue_inset(int _iHeightPx, bool _bTop)
{
    if (_iHeightPx < 0)
        _iHeightPx = 0;

    m_fInsetCurrentPx = (float)_iHeightPx;
    m_bInsetTop = _bTop;
}

void gp_camera_render_ufo_debug(void)
{
    /* Debug: Draw green square showing stick input (direction and force) */
    float fStickForce = ufo_get_stick_force();
    int iStickAngle = ufo_get_stick_angle();
    if (fStickForce > 0.0f)
    {
        /* Convert angle (degrees, 0°=up, 90°=right) to direction vector */
        float fAngleRad = (float)iStickAngle * FM_PI / 180.0f;
        float fDirX = fm_sinf(fAngleRad);
        float fDirY = -fm_cosf(fAngleRad); /* negative because 0° = up (negative Y in screen space) */

        /* Scale by force and a visual multiplier */
        float fVisualScale = 50.0f; /* pixels of offset per unit force */
        struct vec2 vUfoPos = ufo_get_position();
        struct vec2 vInputOffset = vec2_make(fDirX * fStickForce * fVisualScale, fDirY * fStickForce * fVisualScale);
        struct vec2 vInputWorld = vec2_add(vUfoPos, vInputOffset);

        struct vec2i vInputScreen;
        camera_world_to_screen(&g_mainCamera, vInputWorld, &vInputScreen);
        rdpq_set_mode_fill(RGBA32(0, 255, 0, 255));
        rdpq_fill_rectangle(vInputScreen.iX - 2, vInputScreen.iY - 2, vInputScreen.iX + 2, vInputScreen.iY + 2);
    }

    /* Debug: Draw blue square showing actual ship velocity */
    struct vec2 vVel = ufo_get_velocity();
    struct vec2 vVelTarget = vec2_add(ufo_get_position(), vVel);

    struct vec2i vVelScreen;
    camera_world_to_screen(&g_mainCamera, vVelTarget, &vVelScreen);
    rdpq_set_mode_fill(RGBA32(0, 0, 255, 255));
    rdpq_fill_rectangle(vVelScreen.iX - 2, vVelScreen.iY - 2, vVelScreen.iX + 2, vVelScreen.iY + 2);

    /* Debug: Draw magenta square showing damped movement (camera target) */
    struct vec2i vTargetScreen;
    camera_world_to_screen(&g_mainCamera, m_vDebugTarget, &vTargetScreen);
    rdpq_set_mode_fill(RGBA32(255, 0, 255, 255));
    rdpq_fill_rectangle(vTargetScreen.iX - 2, vTargetScreen.iY - 2, vTargetScreen.iX + 2, vTargetScreen.iY + 2);

    rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 12, SCREEN_H - 24, "Speed: %.2f | Thrust: %.3f", ufo_get_speed(), ufo_get_thrust());

    /* Display both normalized and raw stick values */
    int8_t raw_x = joypad_get_inputs(JOYPAD_PORT_1).stick_x;
    int8_t raw_y = joypad_get_inputs(JOYPAD_PORT_1).stick_y;
    int8_t norm_x = stick_normalizer_get_x();
    int8_t norm_y = stick_normalizer_get_y();

    rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 12, SCREEN_H - 36, "X:%3d (%3d) | Y:%3d (%3d)", norm_x, raw_x, norm_y, raw_y);
}

float gp_camera_get_target_zoom(void)
{
    return m_fTargetZoom;
}

struct vec2 gp_camera_get_debug_target(void)
{
    return m_vDebugTarget;
}

void gp_camera_render_jnr_debug(void)
{
    /* Debug: Draw green square showing player position */
    struct vec2 vPlayerPos = player_jnr_get_position();
    struct vec2i vPlayerScreen;
    camera_world_to_screen(&g_mainCamera, vPlayerPos, &vPlayerScreen);
    rdpq_set_mode_fill(RGBA32(0, 255, 0, 255));
    rdpq_fill_rectangle(vPlayerScreen.iX - 2, vPlayerScreen.iY - 2, vPlayerScreen.iX + 2, vPlayerScreen.iY + 2);

    /* Debug: Draw blue square showing actual player velocity */
    struct vec2 vVel = player_jnr_get_velocity();
    struct vec2 vVelTarget = vec2_add(vPlayerPos, vVel);

    struct vec2i vVelScreen;
    camera_world_to_screen(&g_mainCamera, vVelTarget, &vVelScreen);
    rdpq_set_mode_fill(RGBA32(0, 0, 255, 255));
    rdpq_fill_rectangle(vVelScreen.iX - 2, vVelScreen.iY - 2, vVelScreen.iX + 2, vVelScreen.iY + 2);

    /* Debug: Draw magenta square showing camera target (with Y translation) */
    struct vec2i vTargetScreen;
    camera_world_to_screen(&g_mainCamera, m_vDebugTarget, &vTargetScreen);
    rdpq_set_mode_fill(RGBA32(255, 0, 255, 255));
    rdpq_fill_rectangle(vTargetScreen.iX - 2, vTargetScreen.iY - 2, vTargetScreen.iX + 2, vTargetScreen.iY + 2);

    rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 12, SCREEN_H - 24, "Speed: %.2f | Y Trans: %.2f", player_jnr_get_speed(), m_fJnrYTranslation);
    /* Display both normalized and raw stick values */
    int8_t raw_x = joypad_get_inputs(JOYPAD_PORT_1).stick_x;
    int8_t raw_y = joypad_get_inputs(JOYPAD_PORT_1).stick_y;
    int8_t norm_x = stick_normalizer_get_x();
    int8_t norm_y = stick_normalizer_get_y();

    rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 12, SCREEN_H - 36, "X:%3d (%3d) | Y:%3d (%3d)", norm_x, raw_x, norm_y, raw_y);
}

void gp_camera_jnr_update(bool _bDUp, bool _bDDown, bool _bDLeft, bool _bDRight, int _iStickY)
{
    /* Get frame multiplier internally */
    float fFrameMul = frame_time_mul();

    /* Dialogue mode: simple player position follow */
    if (dialogue_is_active())
    {
        apply_zoom_lerp(fFrameMul);

        struct vec2 vTarget = player_jnr_get_position();
        m_vDebugTarget = vTarget;
        float fCameraLerp = 1.0f - powf(1.0f - CAMERA_LERP_JNR, fFrameMul);
        /* Use wrapping function to get custom viewport support during dialogue */
        gp_camera_follow_target_ellipse_with_wrapping(&g_mainCamera, vTarget, CAMERA_DEADZONE_RADIUS_JNR, fCameraLerp);
        return;
    }

    /* Camera zoom controls (D-Pad Up/Down; Left/Right resets) */
    handle_manual_zoom_controls(_bDUp, _bDDown, _bDLeft, _bDRight);

    /* --------------------------------------------------------------------------
       2. Determine Camera Target with Look-Ahead
       -------------------------------------------------------------------------- */
    struct vec2 vTarget;

    /* Reset zoom if not manual */
    if (!m_bManualZoomActive)
        m_fTargetZoom = CAMERA_ZOOM_DEFAULT;

    /* Get speed and velocity for look-ahead */
    float fVelMag = player_jnr_get_speed();
    struct vec2 vVel = player_jnr_get_velocity();

    /* Calculate look-ahead using helper function with JNR-specific constants */
    struct vec2 vLookAheadOffset = calculate_look_ahead_offset(fVelMag,
                                                               vVel,
                                                               CAMERA_LOOK_AHEAD_JNR_MIN_SPEED,
                                                               CAMERA_LOOK_AHEAD_JNR_MAX_SPEED,
                                                               CAMERA_LOOK_AHEAD_JNR_FACTOR,
                                                               CAMERA_LOOK_AHEAD_JNR_CURVE_POWER,
                                                               CAMERA_LERP_JNR,
                                                               CAMERA_LOOK_AHEAD_JNR_Y_SCALE);

    /* Base target from player position + look-ahead */
    struct vec2 vPlayerPos = player_jnr_get_position();
    struct vec2 vBaseTarget = vec2_add(vPlayerPos, vLookAheadOffset);

    /* --------------------------------------------------------------------------
       3. Process Y-Axis Stick Input for Vertical Translation
       -------------------------------------------------------------------------- */
    /* Process stick Y input with deadzone (similar to X stick processing) */
    bool bOnGround = player_jnr_is_on_ground();
    int iStickY = bOnGround ? _iStickY : 0; /* Ignore Y-stick while airborne */
    float fStickYMagnitude = (iStickY >= 0) ? (float)iStickY : -(float)iStickY;
    float fStickYNormalized = 0.0f;

    if (fStickYMagnitude >= CAMERA_JNR_Y_DEADZONE)
    {
        /* Normalize stick Y to -1.0 to 1.0 range, accounting for deadzone */
        /* Subtract deadzone so crossing threshold feels like slight tilt, not full force */
        float fEffectiveMagnitude = fStickYMagnitude - CAMERA_JNR_Y_DEADZONE;
        float fMaxEffectiveRange = STICK_MAX_MAGNITUDE - CAMERA_JNR_Y_DEADZONE;
        float fNormalizedMagnitude = fEffectiveMagnitude / fMaxEffectiveRange;
        if (fNormalizedMagnitude > 1.0f)
            fNormalizedMagnitude = 1.0f;

        /* Apply sign from original stick value, then invert for screen space (negative Y = up) */
        fStickYNormalized = (iStickY >= 0) ? fNormalizedMagnitude : -fNormalizedMagnitude;
        fStickYNormalized = -fStickYNormalized;

        /* Apply Y translation to look-ahead target, clamped to max translation */
        float fDesiredYTranslation = fStickYNormalized * CAMERA_JNR_Y_MAX_TRANSLATION;
        m_fJnrYTranslation = fDesiredYTranslation;

        /* Reset deadzone tracking */
        m_bJnrYInDeadzone = false;
        m_uJnrYDeadzoneStartTick = 0;
    }
    else
    {
        /* Stick is in deadzone */
        if (!m_bJnrYInDeadzone)
        {
            /* Just entered deadzone - start timer */
            m_bJnrYInDeadzone = true;
            m_uJnrYDeadzoneStartTick = get_ticks_ms();
        }
        else
        {
            /* Already in deadzone - check if we should start lerping back */
            uint32_t uNow = get_ticks_ms();
            if (uNow - m_uJnrYDeadzoneStartTick > CAMERA_JNR_Y_RETURN_WAIT_MS)
            {
                /* Wait time elapsed - lerp back to center */
                float fReturnLerp = 1.0f - powf(1.0f - CAMERA_JNR_Y_RETURN_LERP, fFrameMul);
                m_fJnrYTranslation = m_fJnrYTranslation * (1.0f - fReturnLerp);

                /* Snap to zero if very close */
                if (fabsf(m_fJnrYTranslation) < 0.1f)
                    m_fJnrYTranslation = 0.0f;
            }
        }
    }

    /* Apply Y translation to target, clamped relative to player position */
    float fTargetY = vBaseTarget.fY + m_fJnrYTranslation;
    float fDeltaY = fTargetY - vPlayerPos.fY;
    fDeltaY = clampf(fDeltaY, -CAMERA_JNR_Y_MAX_TRANSLATION, CAMERA_JNR_Y_MAX_TRANSLATION);
    vTarget = vec2_make(vBaseTarget.fX, vPlayerPos.fY + fDeltaY);

    m_vDebugTarget = vTarget;

    /* --------------------------------------------------------------------------
       4. Update Camera Zoom & Position
       -------------------------------------------------------------------------- */
    apply_zoom_lerp(fFrameMul);

    float fCameraLerp = 1.0f - powf(1.0f - CAMERA_LERP_JNR, fFrameMul);
    /* Boost lerp as the player approaches screen edges */
    struct vec2i vPlayerScreen;
    camera_world_to_screen(&g_mainCamera, vPlayerPos, &vPlayerScreen);
    float fDistLeft = (float)vPlayerScreen.iX;
    float fDistRight = (float)(SCREEN_W - 1 - vPlayerScreen.iX);
    float fDistTop = (float)vPlayerScreen.iY;
    float fDistBottom = (float)(SCREEN_H - 1 - vPlayerScreen.iY);
    float fMinDist = fminf(fminf(fDistLeft, fDistRight), fminf(fDistTop, fDistBottom));
    float fMargin = CAMERA_JNR_EDGE_LERP_MARGIN;
    if (fMargin > 1.0f)
    {
        float fEdgeT = 1.0f - clampf(fMinDist / fMargin, 0.0f, 1.0f);
        fCameraLerp = fCameraLerp + (1.0f - fCameraLerp) * fEdgeT;
    }
    camera_follow_target_ellipse(&g_mainCamera, vTarget, CAMERA_DEADZONE_RADIUS_JNR, fCameraLerp);

    /* Note: JNR mode uses quantization during rendering (see entity2d_render_simple_quantized and tilemap_render_layers)
     * to prevent tilemap wobble while keeping camera lerp smooth. */
}