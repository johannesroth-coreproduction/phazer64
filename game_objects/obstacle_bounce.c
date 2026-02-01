#include "obstacle_bounce.h"
#include "../camera.h"
#include "../resource_helper.h"
#include "libdragon.h"
#include "ufo.h"
#include <stddef.h>

/* Bounce obstacle settings */
#define BOUNCE_COOLDOWN_MS 1000 /* Duration of bounce cooldown effect in milliseconds */

/* Sprite - loaded once and reused */
static sprite_t *m_pBounceSprite = NULL;

/* Bounce obstacle instances */
#define MAX_BOUNCE_OBSTACLES 32
static ObstacleBounceInstance m_aBounceObstacles[MAX_BOUNCE_OBSTACLES];
static size_t m_iBounceObstacleCount = 0;

/* Initialize bounce obstacles (loads sprites) */
void obstacle_bounce_init(void)
{
    /* Load sprite once */
    if (!m_pBounceSprite)
        m_pBounceSprite = sprite_load("rom:/obstacle_bounce_00.sprite");

    obstacle_bounce_reset();
}

/* Reset bounce obstacles (clears all obstacles) */
void obstacle_bounce_reset(void)
{
    m_iBounceObstacleCount = 0;
}

/* Free bounce obstacles (frees sprites and clears obstacles) */
void obstacle_bounce_free(void)
{
    obstacle_bounce_reset();

    SAFE_FREE_SPRITE(m_pBounceSprite);
}

/* Add a bounce obstacle at the specified position */
void obstacle_bounce_add(struct vec2 _vPos)
{
    if (m_iBounceObstacleCount >= MAX_BOUNCE_OBSTACLES)
    {
        debugf("Bounce obstacle array full\n");
        return; /* Array full */
    }

    ObstacleBounceInstance *pObstacle = &m_aBounceObstacles[m_iBounceObstacleCount];

    if (!m_pBounceSprite)
    {
        debugf("Bounce sprite not loaded\n");
        return;
    }

    /* Initialize entity from pre-loaded sprite */
    uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
    uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;

    entity2d_init_from_sprite(&pObstacle->entity, _vPos, m_pBounceSprite, uFlags, uLayerMask);

    m_iBounceObstacleCount++;
}

/* Update bounce obstacles (check collisions) */
void obstacle_bounce_update(void)
{
    const struct entity2D *pUfoEntity = ufo_get_entity();
    if (!pUfoEntity || !entity2d_is_collidable(pUfoEntity))
        return;

    for (size_t i = 0; i < m_iBounceObstacleCount; ++i)
    {
        ObstacleBounceInstance *pObstacle = &m_aBounceObstacles[i];

        if (!entity2d_is_active(&pObstacle->entity))
            continue;

        if (!entity2d_is_collidable(&pObstacle->entity))
            continue;

        /* Use collision helper to check collision state */
        CollisionEvents events = entity2d_check_collision_and_update(&pObstacle->entity, pUfoEntity);

        /* Handle collision events */
        if (events.bOnTriggerEnter)
        {
            /* Bounce: decimate speed (0.5) and inverse velocity direction */
            struct vec2 vVel = ufo_get_velocity();
            vVel = vec2_scale(vVel, 0.5f);  /* Reduce to 0.5x speed */
            vVel = vec2_scale(vVel, -1.0f); /* Invert direction */
            ufo_set_velocity(vVel);
            /* Apply bounce cooldown effect to reduce thrust effectiveness */
            ufo_apply_bounce_effect(BOUNCE_COOLDOWN_MS);
        }
    }
}

/* Render bounce obstacles */
void obstacle_bounce_render(void)
{
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1); /* draw pixels with alpha >= 1 (colorkey style) */

    for (size_t i = 0; i < m_iBounceObstacleCount; ++i)
    {
        const ObstacleBounceInstance *pObstacle = &m_aBounceObstacles[i];
        const struct entity2D *pEnt = &pObstacle->entity;
        entity2d_render_simple(pEnt);
    }
}
