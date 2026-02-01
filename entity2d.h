#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "math2d.h" /* vec2, vec2i, vec2i_make, etc. */
#include "sprite.h" /* libdragon sprite_t – adjust include path if needed */

/* Forward declarations for camera - full definitions in camera.h */
struct camera2D;
extern struct camera2D g_mainCamera;

/* Forward declarations for camera functions used in inline helpers */
void camera_world_to_screen_quantized(const struct camera2D *_pCamera, struct vec2 _vWorld, struct vec2i *_pOutScreen);
bool camera_is_screen_point_visible(const struct camera2D *_pCamera, struct vec2i _vScreen, float _fMargin);

/* Role flags: whether an entity participates in certain systems. */
enum eEntityFlag
{
    ENTITY_FLAG_ACTIVE = 1 << 0,     // update?
    ENTITY_FLAG_VISIBLE = 1 << 1,    // render?
    ENTITY_FLAG_COLLIDABLE = 1 << 2, // collide?
};

/* Layer bits: used for render / logic partitioning. */
enum eEntityLayerBit
{
    ENTITY_LAYER_BACKGROUND = 1 << 0,
    ENTITY_LAYER_GAMEPLAY = 1 << 1,
    ENTITY_LAYER_FOREGROUND = 1 << 2,
    ENTITY_LAYER_UI = 1 << 3,
};

/* Shared “header” embedded in all world entities. */
struct entity2D
{
    struct vec2 vPos;   /* world-space center position        */
    struct vec2i vHalf; /* internal: half extents in pixels   */

    uint16_t uFlags;     /* ENTITY_FLAG_* bitmask              */
    uint16_t uLayerMask; /* ENTITY_LAYER_* bitmask             */

    int iCollisionRadius; /* simple circle collision radius */
    sprite_t *pSprite;    /* current sprite for rendering    */
    bool bWasColliding;   /* Previous frame collision state (for OnTriggerEnter/Stay/Exit) */

    float fAngleRad;  /* Rotation angle in radians (0 = Up/Default) */
    struct vec2 vVel; /* velocity */
    bool bGrabbed;    /* true when entity is grabbed by tractor beam */
};

/* Forward declarations for camera functions - full definitions in camera.h */
/* These are needed for inline functions below */
float camera_get_zoom(const struct camera2D *_pCamera);
bool camera_entity_world_to_screen(const struct camera2D *_pCamera, const struct entity2D *_pEnt, struct vec2i *_pOutScreen);

/* Init via explicit width/height (world units, usually pixels). */
static inline void entity2d_init_from_size(struct entity2D *_pEnt, struct vec2 _vPos, struct vec2i _vSize, /* width/height */
                                           sprite_t *_pSprite, uint16_t _uFlags, uint16_t _uLayerMask)
{
    struct vec2i vHalf;
    vHalf.iX = _vSize.iX / 2;
    vHalf.iY = _vSize.iY / 2;

    _pEnt->vPos = _vPos;
    _pEnt->vHalf = vHalf;
    _pEnt->uFlags = _uFlags;
    _pEnt->uLayerMask = _uLayerMask;
    _pEnt->pSprite = _pSprite;
    _pEnt->bWasColliding = false;
    _pEnt->fAngleRad = 0.0f;
    _pEnt->vVel = vec2_zero();
    _pEnt->bGrabbed = false;

    int iMinHalf = (vHalf.iX < vHalf.iY) ? vHalf.iX : vHalf.iY;
    _pEnt->iCollisionRadius = iMinHalf;
}

/* Convenience: derive size from sprite once, then reuse. */
static inline void entity2d_init_from_sprite(struct entity2D *_pEnt, struct vec2 _vPos, sprite_t *_pSprite, uint16_t _uFlags, uint16_t _uLayerMask)
{
    struct vec2i vSize;
    vSize.iX = _pSprite->width;
    vSize.iY = _pSprite->height;

    entity2d_init_from_size(_pEnt, _vPos, vSize, _pSprite, _uFlags, _uLayerMask);
}

/* Optional helper to read back logical width/height. */
static inline struct vec2i entity2d_get_size(const struct entity2D *_pEnt)
{
    struct vec2i vSize;
    vSize.iX = _pEnt->vHalf.iX * 2;
    vSize.iY = _pEnt->vHalf.iY * 2;
    return vSize;
}

/* Position accessors. */
static inline void entity2d_set_pos(struct entity2D *_pEnt, struct vec2 _vPos)
{
    _pEnt->vPos = _vPos;
}

static inline struct vec2 entity2d_get_pos(const struct entity2D *_pEnt)
{
    return _pEnt->vPos;
}

/* Flag helpers. */
static inline bool entity2d_is_active(const struct entity2D *_pEnt)
{
    return (_pEnt->uFlags & ENTITY_FLAG_ACTIVE) != 0;
}

static inline bool entity2d_is_visible(const struct entity2D *_pEnt)
{
    return (_pEnt->uFlags & ENTITY_FLAG_VISIBLE) != 0;
}

static inline bool entity2d_is_collidable(const struct entity2D *_pEnt)
{
    return (_pEnt->uFlags & ENTITY_FLAG_COLLIDABLE) != 0;
}

/* Layer helper (e.g. check if entity belongs to a layer). */
static inline bool entity2d_in_layer(const struct entity2D *_pEnt, uint16_t _uLayerMask)
{
    return (_pEnt->uLayerMask & _uLayerMask) != 0;
}

/* Circle-circle collision detection in world space
 * Note: Callers should verify entities are collidable before calling this function. */
static inline bool entity2d_check_collision_circle(const struct entity2D *_pEntA, const struct entity2D *_pEntB)
{
    struct vec2 vDelta = vec2_sub(_pEntA->vPos, _pEntB->vPos);
    float fDistSq = vec2_mag_sq(vDelta);
    int iRadiusSum = _pEntA->iCollisionRadius + _pEntB->iCollisionRadius;
    float fRadiusSumSq = (float)(iRadiusSum * iRadiusSum);

    return fDistSq <= fRadiusSumSq;
}

/* Circle-circle collision detection in screen space
 * _vScreenA: Screen position of first entity
 * _iRadiusA: Collision radius of first entity (in screen pixels)
 * _vScreenB: Screen position of second entity
 * _iRadiusB: Collision radius of second entity (in screen pixels)
 * Returns true if circles overlap in screen space */
static inline bool entity2d_check_collision_circle_screen(struct vec2i _vScreenA, int _iRadiusA, struct vec2i _vScreenB, int _iRadiusB)
{
    int iDx = _vScreenA.iX - _vScreenB.iX;
    int iDy = _vScreenA.iY - _vScreenB.iY;
    int iDistSq = iDx * iDx + iDy * iDy;
    int iRadiusSum = _iRadiusA + _iRadiusB;
    int iRadiusSumSq = iRadiusSum * iRadiusSum;

    return iDistSq <= iRadiusSumSq;
}

/* Point-to-entity collision detection
 * Checks if a world point is within the entity's collision circle
 * _pEnt: Entity to check against
 * _vPoint: World point to test
 * Returns true if point is within entity's collision radius */
static inline bool entity2d_check_point_collision(const struct entity2D *_pEnt, struct vec2 _vPoint)
{
    struct vec2 vDelta = vec2_sub(_vPoint, _pEnt->vPos);
    float fDistSq = vec2_mag_sq(vDelta);
    float fRadiusSq = (float)(_pEnt->iCollisionRadius * _pEnt->iCollisionRadius);

    return fDistSq <= fRadiusSq;
}

/* Circle-rect collision detection
 * _pEnt: Entity with circle collision (center position, radius)
 * _vRectTopLeft: Top-left corner of rectangle
 * _vRectSize: Width and height of rectangle
 * Returns true if circle overlaps with rectangle */
static inline bool entity2d_check_collision_circle_rect(const struct entity2D *_pEnt, struct vec2 _vRectTopLeft, struct vec2 _vRectSize)
{
    /* Calculate rectangle center and half extents */
    struct vec2 vRectCenter = vec2_add(_vRectTopLeft, vec2_scale(_vRectSize, 0.5f));
    struct vec2 vRectHalf = vec2_scale(_vRectSize, 0.5f);

    /* Find closest point on rectangle to circle center */
    struct vec2 vDelta = vec2_sub(_pEnt->vPos, vRectCenter);
    struct vec2 vClosest;
    vClosest.fX = (vDelta.fX < -vRectHalf.fX) ? -vRectHalf.fX : ((vDelta.fX > vRectHalf.fX) ? vRectHalf.fX : vDelta.fX);
    vClosest.fY = (vDelta.fY < -vRectHalf.fY) ? -vRectHalf.fY : ((vDelta.fY > vRectHalf.fY) ? vRectHalf.fY : vDelta.fY);

    /* Check distance from circle center to closest point */
    struct vec2 vDist = vec2_sub(vDelta, vClosest);
    float fDistSq = vec2_mag_sq(vDist);
    float fRadiusSq = (float)(_pEnt->iCollisionRadius * _pEnt->iCollisionRadius);

    return fDistSq <= fRadiusSq;
}

/* Rect-rect collision detection (AABB)
 * _vRectATopLeft: Top-left corner of first rectangle
 * _vRectASize: Width and height of first rectangle
 * _vRectBTopLeft: Top-left corner of second rectangle
 * _vRectBSize: Width and height of second rectangle
 * Returns true if rectangles overlap */
static inline bool entity2d_check_collision_rect_rect(struct vec2 _vRectATopLeft, struct vec2 _vRectASize, struct vec2 _vRectBTopLeft, struct vec2 _vRectBSize)
{
    /* Calculate rectangle bounds */
    float fALeft = _vRectATopLeft.fX;
    float fARight = _vRectATopLeft.fX + _vRectASize.fX;
    float fATop = _vRectATopLeft.fY;
    float fABottom = _vRectATopLeft.fY + _vRectASize.fY;

    float fBLeft = _vRectBTopLeft.fX;
    float fBRight = _vRectBTopLeft.fX + _vRectBSize.fX;
    float fBTop = _vRectBTopLeft.fY;
    float fBBottom = _vRectBTopLeft.fY + _vRectBSize.fY;

    /* Check for overlap */
    return !(fARight < fBLeft || fBRight < fALeft || fABottom < fBTop || fBBottom < fATop);
}

/* Collision event flags for OnTriggerEnter/Stay/Exit detection */
typedef struct CollisionEvents
{
    bool bOnTriggerEnter; /* Collision just started this frame */
    bool bOnTriggerStay;  /* Collision continues from previous frame */
    bool bOnTriggerExit;  /* Collision just ended this frame */
    bool bIsColliding;    /* Current collision state */
} CollisionEvents;

/* Reset collision state (call when deactivating entities to prevent stale state) */
static inline void entity2d_reset_collision_state(struct entity2D *_pEnt)
{
    _pEnt->bWasColliding = false;
}

/* Deactivate an entity (clears flags and resets collision state) */
static inline void entity2d_deactivate(struct entity2D *_pEnt)
{
    _pEnt->uFlags &= ~(ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE);
    entity2d_reset_collision_state(_pEnt);
}

/* Check collision between two entities and update collision state
 *
 * _pEntA: First entity to check (collision state will be updated in this entity)
 * _pEntB: Second entity to check
 *
 * Returns: CollisionEvents struct with event flags
 *
 * Note: Callers are expected to check active/collidable flags before calling this function
 * to avoid redundant checks. This function focuses on collision detection and event tracking.
 * The collision state is stored in _pEntA->bWasColliding.
 */
static inline CollisionEvents entity2d_check_collision_and_update(struct entity2D *_pEntA, const struct entity2D *_pEntB)
{
    CollisionEvents events = {false, false, false, false};

#ifdef SAFE_COLLISSIONS
    /* Validation checks - warn if entities are not properly set up for collision */
    if (!entity2d_is_active(_pEntA) || !entity2d_is_collidable(_pEntA))
    {
        debugf("WARNING: entity2d_check_collision_and_update called with inactive/non-collidable entity A\n");
    }
    if (!entity2d_is_active(_pEntB) || !entity2d_is_collidable(_pEntB))
    {
        debugf("WARNING: entity2d_check_collision_and_update called with inactive/non-collidable entity B\n");
    }
#endif

    /* Check current collision state */
    bool bIsColliding = entity2d_check_collision_circle(_pEntA, _pEntB);
    events.bIsColliding = bIsColliding;

    /* Determine collision event type */
    events.bOnTriggerEnter = !_pEntA->bWasColliding && bIsColliding;
    events.bOnTriggerStay = _pEntA->bWasColliding && bIsColliding;
    events.bOnTriggerExit = _pEntA->bWasColliding && !bIsColliding;

    /* Update collision state for next frame */
    _pEntA->bWasColliding = bIsColliding;

    return events;
}

/* Check if an entity will be rendered (visibility, camera bounds, sprite check)
 * Returns true if the entity should be rendered, false otherwise.
 * If true, outputs the screen position to avoid duplicate camera conversion.
 * This can be used to avoid setting render modes for entities that won't be drawn. */
static inline bool entity2d_will_render(const struct entity2D *_pEnt, struct vec2i *_pOutScreen)
{
    if (!entity2d_is_visible(_pEnt))
        return false;

    if (!camera_entity_world_to_screen(&g_mainCamera, _pEnt, _pOutScreen))
        return false; /* fully outside view */

    if (!_pEnt->pSprite)
        return false; /* no sprite to render */

    return true;
}

/* Internal helper for rendering with pre-computed screen position (no duplicate camera call) */
static inline bool entity2d_render_impl_with_screen(const struct entity2D *_pEnt, const struct vec2i *_pScreen, bool _bRotate)
{
    float fZoom = camera_get_zoom(&g_mainCamera);

    /* Set filter based on rotation requirement (bilinear usually better for rotation) */
    if (_bRotate || fZoom != 1.0f)
        rdpq_mode_filter(FILTER_BILINEAR);
    else
        rdpq_mode_filter(FILTER_POINT);

    rdpq_blitparms_t parms = {.cx = _pEnt->vHalf.iX, .cy = _pEnt->vHalf.iY, .scale_x = fZoom, .scale_y = fZoom, .theta = _bRotate ? _pEnt->fAngleRad : 0.0f};

    rdpq_sprite_blit(_pEnt->pSprite, _pScreen->iX, _pScreen->iY, &parms);

    return true;
}

/* Internal helper for rendering with optional rotation and filter overrides */
static inline bool entity2d_render_impl(const struct entity2D *_pEnt, bool _bRotate)
{
    struct vec2i vScreen;
    if (!entity2d_will_render(_pEnt, &vScreen))
        return false;

    return entity2d_render_impl_with_screen(_pEnt, &vScreen, _bRotate);
}

/* Simple rendering helper - renders entity's sprite using its stored sprite pointer */
static inline bool entity2d_render_simple(const struct entity2D *_pEnt)
{
    return entity2d_render_impl(_pEnt, false);
}

/* Check if an entity will be rendered with quantized camera (prevents sub-pixel wobble) */
static inline bool entity2d_will_render_quantized(const struct entity2D *_pEnt, struct vec2i *_pOutScreen)
{
    if (!entity2d_is_visible(_pEnt))
        return false;

    /* Use quantized camera conversion for stable rendering */
    if (!_pOutScreen)
        return false;

    camera_world_to_screen_quantized(&g_mainCamera, _pEnt->vPos, _pOutScreen);

    /* Check if visible (simplified - just check if on screen) */
    if (!camera_is_screen_point_visible(&g_mainCamera, *_pOutScreen, (float)_pEnt->vHalf.iX))
        return false;

    if (!_pEnt->pSprite)
        return false; /* no sprite to render */

    return true;
}

/* Internal helper for rendering with quantized camera position */
static inline bool entity2d_render_impl_quantized(const struct entity2D *_pEnt, bool _bRotate)
{
    struct vec2i vScreen;
    if (!entity2d_will_render_quantized(_pEnt, &vScreen))
        return false;

    return entity2d_render_impl_with_screen(_pEnt, &vScreen, _bRotate);
}

/* Simple rendering with quantized camera (prevents sub-pixel wobble for tilemap-aligned entities) */
static inline bool entity2d_render_simple_quantized(const struct entity2D *_pEnt)
{
    return entity2d_render_impl_quantized(_pEnt, false);
}

/* Rotated rendering helper */
static inline bool entity2d_render_rotated(const struct entity2D *_pEnt)
{
    return entity2d_render_impl(_pEnt, true);
}
