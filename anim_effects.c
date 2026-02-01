#include "anim_effects.h"
#include "camera.h"
#include "entity2d.h"
#include "frame_time.h"
#include "libdragon.h"
#include "sprite_anim.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Effect instance - combines entity2D and animation player */
typedef struct AnimEffectInstance
{
    struct entity2D entity;          /* Entity for rendering */
    sprite_anim_player_t animPlayer; /* Animation player */
} AnimEffectInstance;

/* Effect configuration data
 * Note: pSpritePathFormat must include a %d or %02d placeholder for frame numbers.
 * Frames are expected to be numbered 0 to (iFrameCount - 1).
 * Example: "rom:/explode_%02d.sprite" will load explode_00.sprite, explode_01.sprite, etc.
 */
static const AnimEffectConfig s_aEffectConfigs[ANIM_EFFECT_COUNT] = {
    [ANIM_EFFECT_EXPLOSION] =
        {
            .pSpritePathFormat = "rom:/explode_%02d.sprite", /* Path format with %02d for frame numbers */
            .iFrameCount = 7,                                /* Number of frames in the animation */
            .fFrameTimeSeconds = 0.04f,                      /* Time per frame in seconds (20 FPS) */
            .iPoolSize = 10,                                 /* Number of pooled instances for this effect */
        },
};

/* Animation clips - one per effect type (shared) */
static sprite_anim_clip_t *s_aEffectClips[ANIM_EFFECT_COUNT] = {NULL};

/* Effect instance pools - array of pools, one per effect type */
static AnimEffectInstance *s_aaEffectPools[ANIM_EFFECT_COUNT] = {NULL};

/* System initialization flag */
static bool s_bSystemInitialized = false;

/* Ring buffer indices - one per effect type for efficient slot finding */
static int s_aiNextRingIndex[ANIM_EFFECT_COUNT] = {0};

/* Callback when animation finishes - disables the entity and unregisters player */
static void anim_effect_on_finished(void *pUserData)
{
    if (!pUserData)
        return;

    AnimEffectInstance *pInstance = (AnimEffectInstance *)pUserData;
    entity2d_deactivate(&pInstance->entity);
    sprite_anim_player_unregister(&pInstance->animPlayer);
}

/* Initialize the animation effects system */
void anim_effects_init(void)
{
    if (s_bSystemInitialized)
        return;

    /* Load animation clips for each effect type */
    for (int i = 0; i < ANIM_EFFECT_COUNT; ++i)
    {
        const AnimEffectConfig *pConfig = &s_aEffectConfigs[i];
#ifdef DEV_BUILD
        if (!pConfig->pSpritePathFormat || pConfig->iFrameCount <= 0)
        {
            debugf("WARNING: anim_effects_init: Invalid config for effect %d\n", i);
            continue;
        }
#endif

        /* Load the animation clip */
        s_aEffectClips[i] = sprite_anim_clip_load(pConfig->pSpritePathFormat, pConfig->iFrameCount, pConfig->fFrameTimeSeconds, SPRITE_ANIM_PLAYMODE_ONCE);
#ifdef DEV_BUILD
        if (!s_aEffectClips[i])
        {
            debugf("ERROR: anim_effects_init: Failed to load clip for effect %d\n", i);
            continue;
        }
#endif

        /* Allocate pool for this effect type */
        int iPoolSize = pConfig->iPoolSize;
        s_aaEffectPools[i] = (AnimEffectInstance *)calloc(iPoolSize, sizeof(AnimEffectInstance));
#ifdef DEV_BUILD
        if (!s_aaEffectPools[i])
        {
            debugf("ERROR: anim_effects_init: Failed to allocate pool for effect %d\n", i);
            sprite_anim_clip_free(s_aEffectClips[i]);
            s_aEffectClips[i] = NULL;
            continue;
        }
#endif

        /* Initialize all instances in the pool */
        sprite_anim_clip_t *pClip = s_aEffectClips[i];
        sprite_t *pFirstFrame = (pClip && pClip->pFrames && pClip->uFrameCount > 0) ? pClip->pFrames[0] : NULL;

        /* Initialize ring index for this effect type */
        s_aiNextRingIndex[i] = 0;

        for (int j = 0; j < iPoolSize; ++j)
        {
            AnimEffectInstance *pInstance = &s_aaEffectPools[i][j];

            /* Initialize entity with first frame for size calculation (starts inactive with flags = 0) */
            if (pFirstFrame)
            {
                uint16_t uFlags = 0; /* Start inactive */
                uint16_t uLayerMask = ENTITY_LAYER_FOREGROUND;
                entity2d_init_from_sprite(&pInstance->entity, vec2_zero(), pFirstFrame, uFlags, uLayerMask);
            }

            /* Animation player will be initialized in anim_effects_play() when needed */
        }
    }

    s_bSystemInitialized = true;
}

/* Cleanup the animation effects system */
void anim_effects_cleanup(void)
{
    if (!s_bSystemInitialized)
        return;

    /* Unregister all active animation players and free resources */
    for (int i = 0; i < ANIM_EFFECT_COUNT; ++i)
    {
        if (s_aaEffectPools[i])
        {
            int iPoolSize = s_aEffectConfigs[i].iPoolSize;
            for (int j = 0; j < iPoolSize; ++j)
            {
                AnimEffectInstance *pInstance = &s_aaEffectPools[i][j];
                if (entity2d_is_active(&pInstance->entity))
                {
                    sprite_anim_player_unregister(&pInstance->animPlayer);
                }
            }
            free(s_aaEffectPools[i]);
            s_aaEffectPools[i] = NULL;
        }

        if (s_aEffectClips[i])
        {
            sprite_anim_clip_free(s_aEffectClips[i]);
            s_aEffectClips[i] = NULL;
        }
    }

    s_bSystemInitialized = false;
}

/* Play an effect at the specified position */
bool anim_effects_play(eAnimEffectType _eType, struct vec2 _vPos)
{
#ifdef DEV_BUILD
    if (!s_bSystemInitialized)
    {
        debugf("WARNING: anim_effects_play: System not initialized\n");
        return false;
    }

    if (_eType < 0 || _eType >= ANIM_EFFECT_COUNT)
    {
        debugf("WARNING: anim_effects_play: Invalid effect type %d\n", _eType);
        return false;
    }

    if (!s_aEffectClips[_eType] || !s_aaEffectPools[_eType])
    {
        debugf("WARNING: anim_effects_play: Effect type %d not properly initialized\n", _eType);
        return false;
    }
#endif

    int iPoolSize = s_aEffectConfigs[_eType].iPoolSize;
    AnimEffectInstance *pPool = s_aaEffectPools[_eType];
    sprite_anim_clip_t *pClip = s_aEffectClips[_eType];

    /* Ring buffer approach: try next slot first (fast path) */
    int iRingIndex = s_aiNextRingIndex[_eType];
    AnimEffectInstance *pTarget = NULL;

    /* Step 1: Check if the next ring slot is available */
    if (!entity2d_is_active(&pPool[iRingIndex].entity))
    {
        /* Fast path: next ring slot is free */
        pTarget = &pPool[iRingIndex];
        s_aiNextRingIndex[_eType] = (iRingIndex + 1) % iPoolSize;
    }
    else
    {
        /* Step 2: Next ring slot is busy - scan entire pool once for any free slot */
        bool bFoundFree = false;
        for (int i = 0; i < iPoolSize; ++i)
        {
            if (!entity2d_is_active(&pPool[i].entity))
            {
                pTarget = &pPool[i];
                bFoundFree = true;
                /* Update ring index to point after this slot for next time */
                s_aiNextRingIndex[_eType] = (i + 1) % iPoolSize;
                break;
            }
        }

        /* Step 3: No free slot found - overwrite the next ring slot (oldest gets replaced) */
        if (!bFoundFree)
        {
            pTarget = &pPool[iRingIndex];
            /* Stop the old effect */
            entity2d_deactivate(&pTarget->entity);
            sprite_anim_player_unregister(&pTarget->animPlayer);
            s_aiNextRingIndex[_eType] = (iRingIndex + 1) % iPoolSize;
        }
    }

    if (!pTarget)
    {
        return false;
    }

    /* Set position and activate entity */
    entity2d_set_pos(&pTarget->entity, _vPos);
    pTarget->entity.uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE; /* Not collidable */

    /* Initialize and start animation player */
    sprite_anim_player_init(&pTarget->animPlayer, pClip, &pTarget->entity.pSprite, 1.0f);
    sprite_anim_player_set_finished_callback(&pTarget->animPlayer, anim_effect_on_finished, pTarget);

    return true;
}

/* Update all active effects */
void anim_effects_update(void)
{
    /* Animation system update is handled in phazer.c update loop */
}

/* Render all active effects */
void anim_effects_render(void)
{
#ifdef DEV_BUILD
    if (!s_bSystemInitialized)
        return;
#endif

    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);

    for (int i = 0; i < ANIM_EFFECT_COUNT; ++i)
    {
        if (!s_aaEffectPools[i])
            continue;

        int iPoolSize = s_aEffectConfigs[i].iPoolSize;
        for (int j = 0; j < iPoolSize; ++j)
        {
            AnimEffectInstance *pInstance = &s_aaEffectPools[i][j];
            if (!entity2d_is_active(&pInstance->entity))
                continue;

            entity2d_render_simple(&pInstance->entity);
        }
    }
}
