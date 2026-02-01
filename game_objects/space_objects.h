#pragma once

#include "../entity2d.h"
#include "../path_mover.h"
#include <stdbool.h>
#include <stdint.h>

/* Impact strength defines for weapon impacts on pieces */
#define IMPACT_STRENGTH_BULLET 1.5f /* Low impact */
#define IMPACT_STRENGTH_LASER 0.1f  /* Medium impact */
#define IMPACT_STRENGTH_BOMB 4.0f   /* Strong impact */

/* Currency meteor hit points (much higher than normal meteors) */
#define METEOR_CURRENCY_HITPOINTS 100
/* Configure meteor data */
/* Use same rotation speed range as normal meteors */
#define CURRENCY_METEOR_MAX_ROT_SPEED 0.05f

/* Forward declarations */
// sprite_t comes from entity2d.h -> sprite.h

typedef enum
{
    SO_METEOR,
    SO_NPC,
    SO_PIECE
} SpaceObjectType;

/* Meteor specific data */
typedef struct
{
    float fRotationSpeed;
    float fTintFrames;
    int iFramesAlive;
    uint8_t uCurrencyId; /* Currency ID (0 = no currency, 1-64 = currency ID) */
} MeteorData;

/* NPC specific data */
typedef struct
{
    int type; /* npc_type_t */
    float fThrusterAnimFrame;
    PathInstance *pPath;
    path_state_t eLastState;
    uint32_t uHitCooldownEndMs;
    bool bReachedTarget;
    struct vec2 vDirectTarget;
    bool bWaitForPlayer;

    /* Shield effect */
    uint32_t uShieldEndMs;

    /* Sprite pointers (owned by the instance or shared) */
    sprite_t *pSpriteAlien;
    sprite_t *pSpriteAlienHighlight;
    sprite_t *pSpriteThrusterMini;
    sprite_t *pSpriteThruster;
    sprite_t *pSpriteThrusterStrong;
    sprite_t *pSpriteShield;
} NpcData;

/* Piece specific data */
typedef struct
{
    int eDirection; /* ePieceDirection */
    uint16_t uUnlockFlag;
    float fRotationSpeed;
    bool bAssembleMode;
} PieceData;

typedef struct SpaceObject
{
    struct entity2D entity;
    SpaceObjectType type;

    union
    {
        MeteorData meteor;
        NpcData npc;
        PieceData piece;
    } data;

    int iHitPoints;
    bool bSleeping;
    bool markForDelete;
    bool bAllocated;

    /* Frame events */
    bool bCollisionEventUfo;

    /* Spatial hash connectivity */
    int next_in_cell;
} SpaceObject;

/* Initialization and Cleanup */
void space_objects_init(void);
void space_objects_free(void);

/* Main Loop */
void space_objects_update(void);
void space_objects_render(void);

/* Spawning */
SpaceObject *space_objects_spawn_meteor(struct vec2 pos);
SpaceObject *space_objects_spawn_npc(int type);
SpaceObject *space_objects_spawn_piece(int direction, uint16_t unlock_flag, struct vec2 pos);

/* Helpers */
int space_objects_get_active_count(void);
SpaceObject *space_objects_get_object(int index);
int space_objects_get_max_count(void);

/* Queries */
const struct entity2D *space_objects_get_closest_entity_on_screen(struct vec2 vFrom, const struct camera2D *pCamera, float fActivationMargin);
const struct entity2D *space_objects_get_closest_entity_in_viewcone(struct vec2 vFrom, float fFacingAngleRad, const struct camera2D *pCamera, float fViewconeHalfAngleRad,
                                                                    float fActivationMargin);
bool space_objects_check_bullet_collision(const struct entity2D *pBullet, int iDamage);
bool space_objects_check_laser_collision(struct vec2 vStart, struct vec2 vEnd, struct vec2 *pOutHitPoint, SpaceObject **ppOutTarget);
void space_objects_damage_in_radius(struct vec2 vCenter, float fRadius, int iDamage, struct vec2 vImpactDir);

/* Helper */
void space_object_apply_damage(SpaceObject *obj, int iDamage, struct vec2 vImpactDir);
void space_objects_play_explosion(struct vec2 vPos);
void space_objects_clear(void);
void space_objects_resolve_ufo_solid_collision(SpaceObject *obj, const struct entity2D *pUfo, CollisionEvents events, bool bPushUfo, float fUfoBounceForce,
                                               int iUfoBounceCooldownMs);
