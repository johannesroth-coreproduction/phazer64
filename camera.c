#include "camera.h"
#include "ui.h"
#include <fmath.h>
#include <math.h>

static const float CAMERA_MIN_ZOOM = 0.05f;

/* Main camera instance - accessible globally */
camera2D g_mainCamera;

static inline float camera_safe_zoom(const camera2D *_pCamera)
{
    float fZoom = _pCamera->fZoom;
    return (fZoom > CAMERA_MIN_ZOOM) ? fZoom : CAMERA_MIN_ZOOM;
}

void camera_set_zoom(camera2D *_pCamera, float _fZoom)
{
    float fClamped = (_fZoom > CAMERA_MIN_ZOOM) ? _fZoom : CAMERA_MIN_ZOOM;
    float fDiff = fabsf(fClamped - CAMERA_ZOOM_DEFAULT);

    /* Snap to default if within threshold to maintain exact 1.0f for optimizations */
    _pCamera->fZoom = (fDiff < CAMERA_ZOOM_DEFAULT_SNAP_THRESHOLD) ? CAMERA_ZOOM_DEFAULT : fClamped;
}

float camera_get_zoom(const camera2D *_pCamera)
{
    return camera_safe_zoom(_pCamera);
}

void camera_init(camera2D *_pCamera, int _iScreenW, int _iScreenH)
{
    _pCamera->vPos = vec2_zero();
    _pCamera->vPrev = vec2_zero();

    _pCamera->vHalf = vec2i_make(_iScreenW / 2, _iScreenH / 2);
    camera_set_zoom(_pCamera, 1.0f);
}

/* Core function: Follow target with custom viewport rectangle */
void camera_follow_target_ellipse_custom_viewport(camera2D *_pCamera, struct vec2 _vTarget, float _fDeadZoneRadius, float _fLerp, struct vec2i _vViewportOffset,
                                                  struct vec2i _vViewportSize)
{
    /* Calculate effective aspect ratio from viewport */
    float fViewportAspect = (float)_vViewportSize.iX / (float)_vViewportSize.iY;

    /* Calculate viewport center in screen space */
    struct vec2i vViewportCenter = vec2i_make(_vViewportOffset.iX + _vViewportSize.iX / 2, _vViewportOffset.iY + _vViewportSize.iY / 2);

    /* Convert viewport center to world space offset (relative to screen center) */
    float fZoom = camera_safe_zoom(_pCamera);
    float fOffsetX = (float)(vViewportCenter.iX - _pCamera->vHalf.iX) / fZoom;
    float fOffsetY = (float)(vViewportCenter.iY - _pCamera->vHalf.iY) / fZoom;

    /* Adjust target to account for viewport offset (move target relative to viewport center) */
    struct vec2 vAdjustedTarget = vec2_sub(_vTarget, vec2_make(fOffsetX, fOffsetY));

    /* Vector from camera center to adjusted target */
    struct vec2 vDelta = vec2_sub(vAdjustedTarget, _pCamera->vPos);

    /*
     * We want to check if (dx / rx)^2 + (dy / ry)^2 > 1
     * where ry = Radius, rx = Radius * Aspect
     *
     * Equivalently, we can normalize the space by scaling X by (1 / Aspect).
     * Then we check against a circle of radius Radius.
     */
    float fScaledX = vDelta.fX / fViewportAspect;
    float fDistSq = (fScaledX * fScaledX) + (vDelta.fY * vDelta.fY);
    float fDeadZoneWorld = _fDeadZoneRadius / fZoom;
    float fRadiusSq = fDeadZoneWorld * fDeadZoneWorld;

    if (fDistSq > fRadiusSq)
    {
        float fDist = sqrtf(fDistSq);
        float fExcess = fDist - fDeadZoneWorld;

        /*
         * The excess vector in "circle space" is:
         * (ScaledX, DeltaY) normalized * Excess
         *
         * We need to convert that back to world space.
         * WorldX = CircleX * Aspect
         */

        float fDirX = fScaledX / fDist;
        float fDirY = vDelta.fY / fDist;

        /* How much we need to move in Circle Space to touch the boundary */
        float fMoveCircleX = fDirX * fExcess;
        float fMoveY = fDirY * fExcess;

        /* Convert X back to world space */
        float fMoveX = fMoveCircleX * fViewportAspect;

        /* Apply Lerp */
        struct vec2 vMove = vec2_make(fMoveX * _fLerp, fMoveY * _fLerp);
        _pCamera->vPos = vec2_add(_pCamera->vPos, vMove);

        /* Quantization must be applied externally by the caller if needed.
         * Some modes (SURFACE/JNR) need quantization to align with tilemaps,
         * while others (SPACE/PLANET) don't to prevent wobble. */
    }
}

/* Core function: Follow target with custom viewport rectangle */
void camera_follow_target_rect_custom_viewport(camera2D *_pCamera, struct vec2 _vTarget, float _fDeadZoneRadius, float _fLerp, struct vec2i _vViewportOffset,
                                               struct vec2i _vViewportSize)
{
    /* Calculate effective aspect ratio from viewport */
    float fViewportAspect = (float)_vViewportSize.iX / (float)_vViewportSize.iY;

    /* Calculate viewport center in screen space */
    struct vec2i vViewportCenter = vec2i_make(_vViewportOffset.iX + _vViewportSize.iX / 2, _vViewportOffset.iY + _vViewportSize.iY / 2);

    /* Convert viewport center to world space offset (relative to screen center) */
    float fZoom = camera_safe_zoom(_pCamera);
    float fOffsetX = (float)(vViewportCenter.iX - _pCamera->vHalf.iX) / fZoom;
    float fOffsetY = (float)(vViewportCenter.iY - _pCamera->vHalf.iY) / fZoom;

    /* Adjust target to account for viewport offset (move target relative to viewport center) */
    struct vec2 vAdjustedTarget = vec2_sub(_vTarget, vec2_make(fOffsetX, fOffsetY));

    /* Vector from camera center to adjusted target */
    struct vec2 vDelta = vec2_sub(vAdjustedTarget, _pCamera->vPos);

    /* Rect half extents in world space (aspect corrected on X) */
    float fHalfY = _fDeadZoneRadius / fZoom;
    float fHalfX = (_fDeadZoneRadius * fViewportAspect) / fZoom;

    float fMoveX = 0.0f;
    float fMoveY = 0.0f;

    if (vDelta.fX > fHalfX)
        fMoveX = vDelta.fX - fHalfX;
    else if (vDelta.fX < -fHalfX)
        fMoveX = vDelta.fX + fHalfX;

    if (vDelta.fY > fHalfY)
        fMoveY = vDelta.fY - fHalfY;
    else if (vDelta.fY < -fHalfY)
        fMoveY = vDelta.fY + fHalfY;

    if (fMoveX != 0.0f || fMoveY != 0.0f)
    {
        struct vec2 vMove = vec2_make(fMoveX * _fLerp, fMoveY * _fLerp);
        _pCamera->vPos = vec2_add(_pCamera->vPos, vMove);
    }
}

void camera_set_position(camera2D *_pCamera, struct vec2 _vPos)
{
    _pCamera->vPos = _vPos;
    _pCamera->vPrev = _vPos; /* Set previous to match to avoid velocity artifacts */
}

void camera_update(camera2D *_pCamera)
{
    _pCamera->vPrev = _pCamera->vPos;
}

void camera_world_to_screen(const camera2D *_pCamera, struct vec2 _vWorld, struct vec2i *_pOutScreen)
{
    if (!_pOutScreen)
        return;

    /* Match tilemap calculation order exactly: screen = half - cam * zoom + world * zoom */
    /* This is equivalent to: screen = half + (world - cam) * zoom, but matches tilemap precision */
    float fZoom = camera_safe_zoom(_pCamera);

    /* Calculate base position (same as tilemap) */
    float fBaseX = (float)_pCamera->vHalf.iX - _pCamera->vPos.fX * fZoom;
    float fBaseY = (float)_pCamera->vHalf.iY - _pCamera->vPos.fY * fZoom;

    /* Add world position scaled by zoom (equivalent to tile position * tileStep) */
    float fScreenX = fBaseX + _vWorld.fX * fZoom;
    float fScreenY = fBaseY + _vWorld.fY * fZoom;

    /* Convert to integer screen coordinates using floor (matches tilemap rendering exactly) */
    _pOutScreen->iX = (int)fm_floorf(fScreenX);
    _pOutScreen->iY = (int)fm_floorf(fScreenY);
}

void camera_world_to_screen_quantized(const camera2D *_pCamera, struct vec2 _vWorld, struct vec2i *_pOutScreen)
{
    if (!_pOutScreen)
        return;

    float fZoom = camera_safe_zoom(_pCamera);

    /* Quantize camera position for stable rendering (prevents sub-pixel wobble) */
    float fQuantizeStep = 1.0f / fZoom;
    float fCamX = (float)round_to_int(_pCamera->vPos.fX / fQuantizeStep) * fQuantizeStep;
    float fCamY = (float)round_to_int(_pCamera->vPos.fY / fQuantizeStep) * fQuantizeStep;

    /* Calculate base position using quantized camera */
    float fBaseX = (float)_pCamera->vHalf.iX - fCamX * fZoom;
    float fBaseY = (float)_pCamera->vHalf.iY - fCamY * fZoom;

    /* Add world position scaled by zoom */
    float fScreenX = fBaseX + _vWorld.fX * fZoom;
    float fScreenY = fBaseY + _vWorld.fY * fZoom;

    /* Convert to integer screen coordinates using rounding to handle sub-pixel position errors (e.g. collision pushout) */
    _pOutScreen->iX = (int)fm_floorf(fScreenX + 0.5f);
    _pOutScreen->iY = (int)fm_floorf(fScreenY + 0.5f);
}

void camera_screen_to_world(const camera2D *_pCamera, struct vec2i _vScreen, struct vec2 *_pOutWorld)
{
    if (!_pOutWorld)
        return;

    /* Convert screen to float world-relative position */
    struct vec2 vScreenFloat = vec2_make((float)_vScreen.iX, (float)_vScreen.iY);

    /* Subtract camera half viewport to get relative position */
    struct vec2 vRelZoomed = vec2_sub_vec2i(vScreenFloat, _pCamera->vHalf);

    /* Remove zoom scaling */
    struct vec2 vRel = vec2_scale(vRelZoomed, 1.0f / camera_safe_zoom(_pCamera));

    /* Add camera position to get world position */
    *_pOutWorld = vec2_add(_pCamera->vPos, vRel);
}

/* AABB vs camera view in world-space (simple, no wrapping).
 * For wrapping support, use gameplay-specific visibility functions. */
bool camera_is_entity_visible(const struct camera2D *_pCamera, const struct entity2D *_pEnt)
{
    /* Entity bounds in world space */
    float fEntLeft = _pEnt->vPos.fX - (float)_pEnt->vHalf.iX;
    float fEntRight = _pEnt->vPos.fX + (float)_pEnt->vHalf.iX;
    float fEntTop = _pEnt->vPos.fY - (float)_pEnt->vHalf.iY;
    float fEntBottom = _pEnt->vPos.fY + (float)_pEnt->vHalf.iY;

    /* Camera bounds in world space */
    float fInvZoom = 1.0f / camera_safe_zoom(_pCamera);
    float fCamHalfX = (float)_pCamera->vHalf.iX * fInvZoom;
    float fCamHalfY = (float)_pCamera->vHalf.iY * fInvZoom;

    float fCamLeft = _pCamera->vPos.fX - fCamHalfX;
    float fCamRight = _pCamera->vPos.fX + fCamHalfX;
    float fCamTop = _pCamera->vPos.fY - fCamHalfY;
    float fCamBottom = _pCamera->vPos.fY + fCamHalfY;

    if (fEntRight < fCamLeft)
        return false;
    if (fEntLeft > fCamRight)
        return false;
    if (fEntBottom < fCamTop)
        return false;
    if (fEntTop > fCamBottom)
        return false;

    return true;
}

/* Helper: Check if a point is within bounds (extracted to minimize duplication) */
static inline bool camera_check_point_in_bounds(float _fPosX, float _fPosY, float _fLeft, float _fRight, float _fTop, float _fBottom)
{
    /* Validate bounds are valid (left <= right, top <= bottom) */
    if (_fLeft > _fRight || _fTop > _fBottom)
        return false;

    if (_fPosX < _fLeft)
        return false;
    if (_fPosX > _fRight)
        return false;
    if (_fPosY < _fTop)
        return false;
    if (_fPosY > _fBottom)
        return false;
    return true;
}

bool camera_is_point_visible(const struct camera2D *_pCamera, struct vec2 _vPos, float _fMargin)
{
    float fInvZoom = 1.0f / camera_safe_zoom(_pCamera);
    float fHalfX = (float)_pCamera->vHalf.iX * fInvZoom + _fMargin;
    float fHalfY = (float)_pCamera->vHalf.iY * fInvZoom + _fMargin;

    float fCamLeft = _pCamera->vPos.fX - fHalfX;
    float fCamRight = _pCamera->vPos.fX + fHalfX;
    float fCamTop = _pCamera->vPos.fY - fHalfY;
    float fCamBottom = _pCamera->vPos.fY + fHalfY;

    return camera_check_point_in_bounds(_vPos.fX, _vPos.fY, fCamLeft, fCamRight, fCamTop, fCamBottom);
}

/* Test if a screen-space point is within the screen bounds + margin. */
bool camera_is_screen_point_visible(const struct camera2D *_pCamera, struct vec2i _vScreen, float _fMargin)
{
    float fLeft = -_fMargin;
    float fRight = (float)(_pCamera->vHalf.iX * 2) + _fMargin;
    float fTop = -_fMargin;
    float fBottom = (float)(_pCamera->vHalf.iY * 2) + _fMargin;

    return camera_check_point_in_bounds((float)_vScreen.iX, (float)_vScreen.iY, fLeft, fRight, fTop, fBottom);
}

/* Visibility + world→screen for the entity center. */
bool camera_entity_world_to_screen(const struct camera2D *_pCamera, const struct entity2D *_pEnt, struct vec2i *_pOutScreen)
{
    if (!camera_is_entity_visible(_pCamera, _pEnt))
        return false;

    camera_world_to_screen(_pCamera, _pEnt->vPos, _pOutScreen);

    return true;
}
