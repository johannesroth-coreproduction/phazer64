#include "sprite_anim.h"
#include "frame_time.h"
#include "libdragon.h"
#include "resource_helper.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Maximum number of animation players that can be registered */
#define SPRITE_ANIM_MAX_PLAYERS 256

/* Global animation system state */
static sprite_anim_player_t *s_aPlayers[SPRITE_ANIM_MAX_PLAYERS];
static bool s_bSystemInitialized = false;

/* Load an animation clip from numbered sprite files */
sprite_anim_clip_t *sprite_anim_clip_load(const char *_pPathFormat, int _iFrameCount, float _fFrameTimeSeconds, eSpriteAnimPlayMode _ePlayMode)
{
    if (!_pPathFormat || _iFrameCount <= 0 || _fFrameTimeSeconds <= 0.0f)
    {
        return NULL;
    }

#ifdef DEV_BUILD
    /* Validate path format contains %d placeholder */
    if (!strstr(_pPathFormat, "%d") && !strstr(_pPathFormat, "%0"))
    {
        debugf("WARNING: sprite_anim_clip_load: path '%s' may not contain %%d placeholder\n", _pPathFormat);
    }
#endif

    /* Allocate clip structure */
    sprite_anim_clip_t *pClip = (sprite_anim_clip_t *)malloc(sizeof(sprite_anim_clip_t));
    if (!pClip)
    {
        return NULL;
    }

    /* Allocate array for sprite pointers */
    pClip->pFrames = (sprite_t **)malloc(sizeof(sprite_t *) * _iFrameCount);
    if (!pClip->pFrames)
    {
        free(pClip);
        return NULL;
    }

    /* Load each frame */
    char szPath[256];
    bool bAllLoaded = true;
    for (int i = 0; i < _iFrameCount; ++i)
    {
        /* Format path with frame number */
        snprintf(szPath, sizeof(szPath), _pPathFormat, i);
        pClip->pFrames[i] = sprite_load(szPath);
        if (!pClip->pFrames[i])
        {
            /* Failed to load this frame - mark for cleanup */
            bAllLoaded = false;
            break;
        }
    }

    if (!bAllLoaded)
    {
        /* Clean up loaded frames - free sprites that were successfully loaded */
        for (int i = 0; i < _iFrameCount; ++i)
        {
            SAFE_FREE_SPRITE(pClip->pFrames[i]);
        }
        free(pClip->pFrames);
        free(pClip);
        return NULL;
    }

    pClip->uFrameCount = (uint16_t)_iFrameCount;
    pClip->fFrameTimeSeconds = _fFrameTimeSeconds;
    pClip->ePlayMode = _ePlayMode;

    return pClip;
}

/* Free an animation clip and all its loaded sprites */
void sprite_anim_clip_free(sprite_anim_clip_t *_pClip)
{
    if (!_pClip)
        return;

    if (_pClip->pFrames)
    {
        /* Free all sprite frames */
        for (uint16_t i = 0; i < _pClip->uFrameCount; ++i)
        {
            SAFE_FREE_SPRITE(_pClip->pFrames[i]);
        }
        free(_pClip->pFrames);
        _pClip->pFrames = NULL;
    }

    free(_pClip);
}

/* Initialize the global animation system */
void sprite_anim_system_init(void)
{
    if (s_bSystemInitialized)
        return;

    for (uint16_t i = 0; i < SPRITE_ANIM_MAX_PLAYERS; ++i)
    {
        s_aPlayers[i] = NULL;
    }
    s_bSystemInitialized = true;
}

/* Initialize an animation player and auto-register it
 *
 * Note: _pPlayer must remain valid while registered. It's typically embedded in your game object struct.
 * We return it for convenience (method chaining, etc.), but you pass it because you own the memory.
 *
 * If the player is already registered, call sprite_anim_player_unregister() first.
 */
sprite_anim_player_t *sprite_anim_player_init(sprite_anim_player_t *_pPlayer, const sprite_anim_clip_t *_pClip, sprite_t **_ppSprite, float _fPlaybackSpeed)
{
    if (!_pPlayer || !_pClip)
        return NULL;

    /* Ensure system is initialized */
    if (!s_bSystemInitialized)
    {
        sprite_anim_system_init();
    }

    /* Find first NULL slot in array */
    uint16_t uFreeSlot = SPRITE_ANIM_MAX_PLAYERS;
    for (uint16_t i = 0; i < SPRITE_ANIM_MAX_PLAYERS; ++i)
    {
        if (s_aPlayers[i] == NULL)
        {
            uFreeSlot = i;
            break;
        }
    }

    /* Assert if array is full - fail fast (debug builds) */
    assert(uFreeSlot < SPRITE_ANIM_MAX_PLAYERS && "Animation player array full! Increase SPRITE_ANIM_MAX_PLAYERS.");

    /* Runtime check for release builds - return NULL if full */
    if (uFreeSlot >= SPRITE_ANIM_MAX_PLAYERS)
        return NULL;

    /* Initialize player state */
    _pPlayer->pClip = _pClip;
    _pPlayer->uCurrentFrame = 0;
    _pPlayer->fTimeAccumulator = 0.0f;
    _pPlayer->fPlaybackSpeed = (_fPlaybackSpeed > 0.0f) ? _fPlaybackSpeed : 1.0f;
    _pPlayer->iDirection = 1; /* Start playing forward */
    _pPlayer->bFinished = false;
    _pPlayer->pOnFinished = NULL;
    _pPlayer->pUserData = NULL;
    _pPlayer->ppSprite = _ppSprite; /* Set sprite pointer (NULL = disable auto-update) */

    /* Set initial sprite to first frame so render works immediately if called before first update */
    if (_ppSprite && _pClip->pFrames && _pClip->uFrameCount > 0)
        *_ppSprite = _pClip->pFrames[0];

    /* Add to array at free slot */
    s_aPlayers[uFreeSlot] = _pPlayer;

    return _pPlayer;
}

/* Unregister an animation player */
void sprite_anim_player_unregister(sprite_anim_player_t *_pPlayer)
{
    if (!_pPlayer)
        return;

    /* Find and NULL the slot */
    for (uint16_t i = 0; i < SPRITE_ANIM_MAX_PLAYERS; ++i)
    {
        if (s_aPlayers[i] == _pPlayer)
        {
            s_aPlayers[i] = NULL;
            break;
        }
    }
}

/* Update animation player */
void sprite_anim_player_update(sprite_anim_player_t *_pPlayer, float _fDeltaSeconds)
{
    if (!_pPlayer || !_pPlayer->pClip || _pPlayer->bFinished)
        return;

    /* Accumulate time */
    _pPlayer->fTimeAccumulator += _fDeltaSeconds * _pPlayer->fPlaybackSpeed;

    /* Calculate frame time with speed multiplier */
    float fFrameTime = _pPlayer->pClip->fFrameTimeSeconds;

    /* Advance frames as needed */
    while (_pPlayer->fTimeAccumulator >= fFrameTime)
    {
        _pPlayer->fTimeAccumulator -= fFrameTime;

        /* Advance frame based on direction */
        if (_pPlayer->iDirection > 0)
        {
            /* Playing forward */
            _pPlayer->uCurrentFrame++;
            if (_pPlayer->uCurrentFrame >= _pPlayer->pClip->uFrameCount)
            {
                /* Reached end */
                switch (_pPlayer->pClip->ePlayMode)
                {
                case SPRITE_ANIM_PLAYMODE_ONCE:
                    /* Stop at last frame */
                    _pPlayer->uCurrentFrame = _pPlayer->pClip->uFrameCount - 1;
                    _pPlayer->bFinished = true;
                    if (_pPlayer->pOnFinished)
                    {
                        _pPlayer->pOnFinished(_pPlayer->pUserData);
                    }
                    return;

                case SPRITE_ANIM_PLAYMODE_LOOP:
                    /* Loop back to start */
                    _pPlayer->uCurrentFrame = 0;
                    break;

                case SPRITE_ANIM_PLAYMODE_PINGPONG:
                    /* Reverse direction, stay at last frame */
                    _pPlayer->iDirection = -1;
                    _pPlayer->uCurrentFrame = _pPlayer->pClip->uFrameCount - 1;
                    break;
                }
            }
        }
        else
        {
            /* Playing backward (ping-pong) */
            if (_pPlayer->uCurrentFrame == 0)
            {
                /* Reached start - reverse direction */
                _pPlayer->iDirection = 1;
                _pPlayer->uCurrentFrame = 0;
            }
            else
            {
                _pPlayer->uCurrentFrame--;
            }
        }
    }
}

/* Get the current sprite from the animation player */
sprite_t *sprite_anim_player_get_sprite(const sprite_anim_player_t *_pPlayer)
{
    if (!_pPlayer || !_pPlayer->pClip)
        return NULL;

    if (_pPlayer->uCurrentFrame >= _pPlayer->pClip->uFrameCount)
        return NULL;

    return _pPlayer->pClip->pFrames[_pPlayer->uCurrentFrame];
}

/* Reset animation player to start */
void sprite_anim_player_reset(sprite_anim_player_t *_pPlayer)
{
    if (!_pPlayer)
        return;

    _pPlayer->uCurrentFrame = 0;
    _pPlayer->fTimeAccumulator = 0.0f;
    _pPlayer->iDirection = 1;
    _pPlayer->bFinished = false;
}

/* Set playback speed */
void sprite_anim_player_set_speed(sprite_anim_player_t *_pPlayer, float _fSpeed)
{
    if (!_pPlayer)
        return;

    _pPlayer->fPlaybackSpeed = (_fSpeed > 0.0f) ? _fSpeed : 1.0f;
}

/* Set animation clip (change clip without unregistering) */
void sprite_anim_player_set_clip(sprite_anim_player_t *_pPlayer, const sprite_anim_clip_t *_pClip)
{
    if (!_pPlayer || !_pClip)
        return;

    /* Change clip pointer */
    _pPlayer->pClip = _pClip;

    /* Reset animation state */
    _pPlayer->uCurrentFrame = 0;
    _pPlayer->fTimeAccumulator = 0.0f;
    _pPlayer->iDirection = 1;
    _pPlayer->bFinished = false;

    /* Immediately update sprite pointer if set */
    if (_pPlayer->ppSprite && _pClip->pFrames && _pClip->uFrameCount > 0)
    {
        *_pPlayer->ppSprite = _pClip->pFrames[0];
    }
}

/* Set finished callback */
void sprite_anim_player_set_finished_callback(sprite_anim_player_t *_pPlayer, void (*_pCallback)(void *), void *_pUserData)
{
    if (!_pPlayer)
        return;

    _pPlayer->pOnFinished = _pCallback;
    _pPlayer->pUserData = _pUserData;
}

/* Check if animation is finished */
bool sprite_anim_player_is_finished(const sprite_anim_player_t *_pPlayer)
{
    if (!_pPlayer)
        return true;

    return _pPlayer->bFinished;
}

/* Update all registered animation players */
void sprite_anim_system_update_all(void)
{
    if (!s_bSystemInitialized)
        return;

    float fDeltaSeconds = frame_time_delta_seconds();

    for (uint16_t i = 0; i < SPRITE_ANIM_MAX_PLAYERS; ++i)
    {
        sprite_anim_player_t *pPlayer = s_aPlayers[i];
        if (!pPlayer)
            continue; /* Skip NULL slots */

        /* Update animation state (may call pOnFinished callback) */
        sprite_anim_player_update(pPlayer, fDeltaSeconds);

        /* Auto-update sprite pointer if set */
        if (pPlayer->ppSprite)
        {
            *pPlayer->ppSprite = sprite_anim_player_get_sprite(pPlayer);
        }
    }
}
