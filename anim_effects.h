#pragma once

#include "entity2d.h"
#include "math2d.h"
#include "sprite_anim.h"
#include <stdbool.h>

/* Effect types */
typedef enum eAnimEffectType
{
    ANIM_EFFECT_EXPLOSION,
    ANIM_EFFECT_COUNT, /* Total number of effects */
} eAnimEffectType;

/* Effect configuration - stores metadata for each effect type */
typedef struct AnimEffectConfig
{
    const char *pSpritePathFormat; /* Path format like "rom:/explode_%02d.sprite" */
    int iFrameCount;               /* Number of frames in the animation */
    float fFrameTimeSeconds;       /* Time per frame in seconds */
    int iPoolSize;                 /* Number of pooled instances for this effect */
} AnimEffectConfig;

/* Initialize the animation effects system (call once at startup) */
void anim_effects_init(void);

/* Cleanup the animation effects system */
void anim_effects_cleanup(void);

/* Play an effect at the specified position
 *
 * _eType: Type of effect to play
 * _vPos: World position where the effect should appear
 *
 * Returns: true if the effect was successfully started
 * Note: If all instances are busy, the oldest effect will be stopped and replaced
 */
bool anim_effects_play(eAnimEffectType _eType, struct vec2 _vPos);

/* Update all active effects (call once per frame) */
void anim_effects_update(void);

/* Render all active effects (call once per frame) */
void anim_effects_render(void);
