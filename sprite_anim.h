#pragma once

#include "sprite.h"
#include <stdbool.h>
#include <stdint.h>

/* Playback mode for animations */
typedef enum eSpriteAnimPlayMode
{
    SPRITE_ANIM_PLAYMODE_ONCE,     /* Play once and stop at last frame */
    SPRITE_ANIM_PLAYMODE_LOOP,     /* Loop from start to end continuously */
    SPRITE_ANIM_PLAYMODE_PINGPONG, /* Play forward, then backward, repeat */
} eSpriteAnimPlayMode;

/* Animation clip (shared, immutable data) - loaded once, reused by all instances */
typedef struct sprite_anim_clip
{
    sprite_t **pFrames;            /* Array of sprite pointers */
    uint16_t uFrameCount;          /* Number of frames */
    float fFrameTimeSeconds;       /* Time per frame in seconds */
    eSpriteAnimPlayMode ePlayMode; /* Playback mode */
} sprite_anim_clip_t;

/* Animation player (per-instance state) */
typedef struct sprite_anim_player
{
    const sprite_anim_clip_t *pClip;      /* Reference to the clip (not owned) */
    uint16_t uCurrentFrame;               /* Current frame index */
    float fTimeAccumulator;               /* Accumulated time since last frame change */
    float fPlaybackSpeed;                 /* Speed multiplier (1.0 = normal, 2.0 = double speed) */
    int8_t iDirection;                    /* 1 = forward, -1 = backward (for ping-pong) */
    bool bFinished;                       /* True if animation finished (for ONCE mode) */
    void (*pOnFinished)(void *pUserData); /* Optional callback when animation finishes */
    void *pUserData;                      /* User data passed to callback */
    sprite_t **ppSprite;                  /* Pointer to sprite pointer for auto-update (NULL = disabled) */
} sprite_anim_player_t;

/* Load an animation clip from numbered sprite files
 *
 * _pPathFormat: Format string like "rom:/meteor_%02d.sprite" (must include %d or %02d, etc.)
 * _iFrameCount: Number of frames to load (frames numbered 0 to _iFrameCount-1)
 * _fFrameTimeSeconds: Time per frame in seconds
 * _ePlayMode: Playback mode (ONCE, LOOP, PINGPONG)
 *
 * Returns: Pointer to allocated clip, or NULL on failure
 *
 * Note: The clip must be freed with sprite_anim_clip_free() when no longer needed
 */
sprite_anim_clip_t *sprite_anim_clip_load(const char *_pPathFormat, int _iFrameCount, float _fFrameTimeSeconds, eSpriteAnimPlayMode _ePlayMode);

/* Free an animation clip and all its loaded sprites
 *
 * _pClip: Clip to free (can be NULL)
 */
void sprite_anim_clip_free(sprite_anim_clip_t *_pClip);

/* Initialize the global animation system (call once at startup) */
void sprite_anim_system_init(void);

/* Initialize an animation player and auto-register it with the global system
 *
 * _pPlayer: Player to initialize (must remain valid while registered)
 * _pClip: Clip to play (must not be NULL)
 * _ppSprite: Pointer to sprite pointer for auto-update (e.g., &entity->pSprite). NULL to disable auto-update.
 * _fPlaybackSpeed: Initial playback speed (1.0 = normal speed)
 *
 * Returns: Pointer to the initialized player (same as _pPlayer) for convenience
 *
 * Note: Player is automatically registered. Auto-update is enabled by default if _ppSprite is non-NULL.
 */
sprite_anim_player_t *sprite_anim_player_init(sprite_anim_player_t *_pPlayer, const sprite_anim_clip_t *_pClip, sprite_t **_ppSprite, float _fPlaybackSpeed);

/* Unregister an animation player from the global system
 *
 * _pPlayer: Player to unregister (can be NULL)
 */
void sprite_anim_player_unregister(sprite_anim_player_t *_pPlayer);

/* Update animation player (call once per frame)
 *
 * _pPlayer: Player to update
 * _fDeltaSeconds: Delta time since last frame (use frame_time_delta_seconds())
 */
void sprite_anim_player_update(sprite_anim_player_t *_pPlayer, float _fDeltaSeconds);

/* Get the current sprite from the animation player
 *
 * _pPlayer: Player to get sprite from
 *
 * Returns: Current sprite_t* or NULL if no clip or finished
 */
sprite_t *sprite_anim_player_get_sprite(const sprite_anim_player_t *_pPlayer);

/* Reset animation player to start
 *
 * _pPlayer: Player to reset
 */
void sprite_anim_player_reset(sprite_anim_player_t *_pPlayer);

/* Set playback speed
 *
 * _pPlayer: Player to modify
 * _fSpeed: New playback speed (1.0 = normal, 2.0 = double speed, etc.)
 */
void sprite_anim_player_set_speed(sprite_anim_player_t *_pPlayer, float _fSpeed);

/* Set animation clip (change clip without unregistering)
 *
 * _pPlayer: Player to modify
 * _pClip: New clip to play (must not be NULL)
 *
 * Note: This efficiently changes the clip without unregistering/re-registering.
 * The animation is reset to frame 0 and the sprite pointer is immediately updated if set.
 */
void sprite_anim_player_set_clip(sprite_anim_player_t *_pPlayer, const sprite_anim_clip_t *_pClip);

/* Set finished callback
 *
 * _pPlayer: Player to modify
 * _pCallback: Callback function (can be NULL)
 * _pUserData: User data passed to callback
 */
void sprite_anim_player_set_finished_callback(sprite_anim_player_t *_pPlayer, void (*_pCallback)(void *), void *_pUserData);

/* Check if animation is finished (only meaningful for ONCE mode)
 *
 * _pPlayer: Player to check
 *
 * Returns: true if finished, false otherwise
 */
bool sprite_anim_player_is_finished(const sprite_anim_player_t *_pPlayer);

/* Update all registered animation players (call once per frame from main update loop) */
void sprite_anim_system_update_all(void);
