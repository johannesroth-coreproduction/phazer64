#include "audio.h"
#include "camera.h"
#include "dialogue.h"
#include "fade_manager.h"
#include "frame_time.h"
#include "game_objects/gp_camera.h"
#include "game_objects/gp_state.h"
#include "game_objects/ufo.h"
#include "libdragon.h"
#include "math_helper.h"
#include "menu.h"
#include "minimap.h"
#include "mixer.h"
#include "player_jnr.h"
#include "player_surface.h"
#include "profiler.h"
#include "resource_helper.h"
#include "rng.h"
#include "save.h"
#include "ui.h"
#include "upgrade_shop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Ducking system state */
typedef enum
{
    DUCKING_FADE_NONE, /* No fade in progress */
    DUCKING_FADE_IN,   /* Fading in ducking (volume decreasing) */
    DUCKING_FADE_OUT   /* Fading out ducking (volume increasing) */
} eDuckingFadeState;

static eDuckingFadeState s_eDuckingFadeState = DUCKING_FADE_NONE;
static float s_fDuckingFadeStartTime = 0.0f;
static float s_fDuckingFadeStartMultiplier = 1.0f;
static float s_fDuckingTargetMultiplier = 1.0f;
static float s_fDuckingCurrentMultiplier = 1.0f;

/* Stored target volumes for ducked channels (before ducking is applied) */
static float s_fUfoVolumeLeft = 0.0f;
static float s_fUfoVolumeRight = 0.0f;
static float s_fEngineVolumeLeft = 0.0f;
static float s_fEngineVolumeRight = 0.0f;
static float s_fNpcAlienVolumeLeft = 0.0f;
static float s_fNpcAlienVolumeRight = 0.0f;
static float s_fNpcRhinoVolumeLeft = 0.0f;
static float s_fNpcRhinoVolumeRight = 0.0f;

/* Forward declarations */
static void audio_update_ducking(void);
static float audio_get_ducking_multiplier(void);

void audio_init_system(void)
{
    audio_init(AUDIO_BITRATE, AUDIO_BUFFERS);
    mixer_init(MIXER_CHANNEL_COUNT);
    wav64_init_compression(WAV_COMPRESSION);

    mixer_ch_set_vol(MIXER_CHANNEL_EXPLOSIONS, AUDIO_BASE_VOLUME_EXPLOSIONS, AUDIO_BASE_VOLUME_EXPLOSIONS);
    mixer_ch_set_vol(MIXER_CHANNEL_WEAPONS, AUDIO_BASE_VOLUME_WEAPONS, AUDIO_BASE_VOLUME_WEAPONS);
    mixer_ch_set_vol(MIXER_CHANNEL_MUSIC, AUDIO_BASE_VOLUME_MUSIC, AUDIO_BASE_VOLUME_MUSIC);
    mixer_ch_set_vol(MIXER_CHANNEL_USER_INTERFACE, AUDIO_BASE_VOLUME_UI, AUDIO_BASE_VOLUME_UI);
    /* Initialize stored volumes for ducked channels */
    s_fUfoVolumeLeft = AUDIO_BASE_VOLUME_UFO;
    s_fUfoVolumeRight = AUDIO_BASE_VOLUME_UFO;
    s_fEngineVolumeLeft = AUDIO_BASE_VOLUME_ENGINE;
    s_fEngineVolumeRight = AUDIO_BASE_VOLUME_ENGINE;
    mixer_ch_set_vol(MIXER_CHANNEL_ENGINE, AUDIO_BASE_VOLUME_ENGINE, AUDIO_BASE_VOLUME_ENGINE);
    mixer_ch_set_vol(MIXER_CHANNEL_ITEMS, AUDIO_BASE_VOLUME_ITEMS, AUDIO_BASE_VOLUME_ITEMS);
    s_fNpcAlienVolumeLeft = AUDIO_BASE_VOLUME_NPC_ALIEN;
    s_fNpcAlienVolumeRight = AUDIO_BASE_VOLUME_NPC_ALIEN;
    mixer_ch_set_vol(MIXER_CHANNEL_NPC_ALIEN, AUDIO_BASE_VOLUME_NPC_ALIEN, AUDIO_BASE_VOLUME_NPC_ALIEN);
    s_fNpcRhinoVolumeLeft = AUDIO_BASE_VOLUME_NPC_RHINO;
    s_fNpcRhinoVolumeRight = AUDIO_BASE_VOLUME_NPC_RHINO;
    mixer_ch_set_vol(MIXER_CHANNEL_NPC_RHINO, AUDIO_BASE_VOLUME_NPC_RHINO, AUDIO_BASE_VOLUME_NPC_RHINO);
}

void audio_poll(void)
{
    PROF_SECTION_BEGIN(PROF_SECTION_AUDIO);

    /* Update ducking system */
    audio_update_ducking();

    /* Apply ducking to stored volumes and set them once before polling */
    float fDuckingMultiplier = audio_get_ducking_multiplier();
    mixer_ch_set_vol(MIXER_CHANNEL_UFO, s_fUfoVolumeLeft * fDuckingMultiplier, s_fUfoVolumeRight * fDuckingMultiplier);
    mixer_ch_set_vol(MIXER_CHANNEL_ENGINE, s_fEngineVolumeLeft * fDuckingMultiplier, s_fEngineVolumeRight * fDuckingMultiplier);
    mixer_ch_set_vol(MIXER_CHANNEL_NPC_ALIEN, s_fNpcAlienVolumeLeft * fDuckingMultiplier, s_fNpcAlienVolumeRight * fDuckingMultiplier);
    mixer_ch_set_vol(MIXER_CHANNEL_NPC_RHINO, s_fNpcRhinoVolumeLeft * fDuckingMultiplier, s_fNpcRhinoVolumeRight * fDuckingMultiplier);

    // Check whether one audio buffer is ready, otherwise wait for next
    // frame to perform mixing.
    if (audio_can_write())
    {
        short *pShBuffer = audio_write_begin();
        mixer_poll(pShBuffer, audio_get_buffer_length());
        audio_write_end();
    }
    // Update music fade system
    audio_update_music();
    PROF_SECTION_END(PROF_SECTION_AUDIO);
}

void audio_sound_group_init_impl(audio_sound_group_t *group, const char **paths, int count, int channel, wav64_t **sound_array)
{
    if (!group || !paths || !sound_array || count <= 0)
        return;

    group->sounds = sound_array;
    group->count = count;
    group->channel = channel;

    for (int i = 0; i < count; i++)
    {
        if (!sound_array[i])
        {
            sound_array[i] = wav64_load(paths[i], &(wav64_loadparms_t){.streaming_mode = 0});
            if (sound_array[i])
                wav64_set_loop(sound_array[i], false);
        }
    }
}

void audio_sound_group_play_random(audio_sound_group_t *group, bool stop_current)
{
    if (!group || !group->sounds || group->count <= 0)
        return;

    /* Stop any currently playing sound on the channel if requested */
    if (stop_current && mixer_ch_playing(group->channel))
        mixer_ch_stop(group->channel);

    /* Pick a random sound from the group */
    int iRandomIndex = rngi(0, group->count - 1);
    if (group->sounds[iRandomIndex])
    {
        wav64_play(group->sounds[iRandomIndex], group->channel);
    }
}

void audio_sound_group_free(audio_sound_group_t *group)
{
    if (!group || !group->sounds)
        return;

    for (int i = 0; i < group->count; i++)
    {
        SAFE_CLOSE_WAV64(group->sounds[i]);
    }

    /* We don't free group->sounds here because it's usually passed as a pointer
     * to a static array or managed externally. The caller should handle it if dynamically allocated. */
    group->count = 0;
}

void audio_refresh_volumes(void)
{
    /* Get volume settings from save system (0-100) */
    int iMusicVolume = save_get_music_volume();
    int iSfxVolume = save_get_sfx_volume();

    /* Convert to multiplier (100 = 1.0f) */
    float fMusicMultiplier = (float)iMusicVolume / 100.0f;
    float fSfxMultiplier = (float)iSfxVolume / 100.0f;

    /* Apply multipliers to channels */
    mixer_ch_set_vol(MIXER_CHANNEL_MUSIC, AUDIO_BASE_VOLUME_MUSIC * fMusicMultiplier, AUDIO_BASE_VOLUME_MUSIC * fMusicMultiplier);
    mixer_ch_set_vol(MIXER_CHANNEL_EXPLOSIONS, AUDIO_BASE_VOLUME_EXPLOSIONS * fSfxMultiplier, AUDIO_BASE_VOLUME_EXPLOSIONS * fSfxMultiplier);
    mixer_ch_set_vol(MIXER_CHANNEL_WEAPONS, AUDIO_BASE_VOLUME_WEAPONS * fSfxMultiplier, AUDIO_BASE_VOLUME_WEAPONS * fSfxMultiplier);
    mixer_ch_set_vol(MIXER_CHANNEL_USER_INTERFACE, AUDIO_BASE_VOLUME_UI * fSfxMultiplier, AUDIO_BASE_VOLUME_UI * fSfxMultiplier);
    /* Update stored volumes for ducked channels (ducking will be applied in audio_poll) */
    s_fUfoVolumeLeft = AUDIO_BASE_VOLUME_UFO * fSfxMultiplier;
    s_fUfoVolumeRight = AUDIO_BASE_VOLUME_UFO * fSfxMultiplier;
    s_fEngineVolumeLeft = AUDIO_BASE_VOLUME_ENGINE * fSfxMultiplier;
    s_fEngineVolumeRight = AUDIO_BASE_VOLUME_ENGINE * fSfxMultiplier;
    s_fNpcAlienVolumeLeft = AUDIO_BASE_VOLUME_NPC_ALIEN * fSfxMultiplier;
    s_fNpcAlienVolumeRight = AUDIO_BASE_VOLUME_NPC_ALIEN * fSfxMultiplier;
    s_fNpcRhinoVolumeLeft = AUDIO_BASE_VOLUME_NPC_RHINO * fSfxMultiplier;
    s_fNpcRhinoVolumeRight = AUDIO_BASE_VOLUME_NPC_RHINO * fSfxMultiplier;
}

void audio_update_music_speed(float fCurrentSpeed)
{
    // Music speed follows UFO speed: 0 below AUDIO_SPEED_MIN, linear to 1 at AUDIO_SPEED_MAX+
    float fSpeedFactor = 0.0f;
    if (fCurrentSpeed >= AUDIO_SPEED_MIN)
    {
        if (fCurrentSpeed >= AUDIO_SPEED_MAX)
        {
            fSpeedFactor = 1.0f;
        }
        else
        {
            float fSpeedRange = AUDIO_SPEED_MAX - AUDIO_SPEED_MIN;
            fSpeedFactor = (fCurrentSpeed - AUDIO_SPEED_MIN) / fSpeedRange;
        }
    }
    mixer_ch_set_freq(MIXER_CHANNEL_MUSIC, (AUDIO_BITRATE * 0.5f) * (1.0f + fSpeedFactor));
}

void audio_update_engine_freq(float fThrust)
{
    if (!mixer_ch_playing(MIXER_CHANNEL_ENGINE))
        return;
    /* Scale frequency based on thrust, similar to how thruster rendering scales */
    /* Base frequency at minimum thrust threshold, scales up with thrust */
    float fBaseFreq = AUDIO_BITRATE * 0.5f; /* Base frequency (half sample rate) */
    float fMinThrust = 0.01f;               /* Minimum thrust threshold */
    float fMaxThrust = 0.09f;               /* Turbo thrust threshold (UFO_THRUST + 0.01f) */
    float fMaxFreqMultiplier = 2.0f;        /* Max frequency multiplier (try 2.5f if needed) */

    float fThrustFactor = 0.0f;
    if (fThrust >= fMinThrust)
    {
        if (fThrust >= fMaxThrust)
        {
            fThrustFactor = 1.0f;
        }
        else
        {
            float fThrustRange = fMaxThrust - fMinThrust;
            fThrustFactor = (fThrust - fMinThrust) / fThrustRange;
        }
    }

    /* Scale frequency from base (0.5x) to max multiplier at max thrust */
    float fFreq = fBaseFreq * (1.0f + fThrustFactor * (fMaxFreqMultiplier - 1.0f));
    mixer_ch_set_freq(MIXER_CHANNEL_ENGINE, fFreq);
}

void audio_update_npc_engine_freq(int channel, float fSpeed)
{
    if (!mixer_ch_playing(channel))
        return;
    /* Scale frequency based on speed, similar to how thruster rendering scales */
    /* Base frequency at minimum speed threshold, scales up with speed */
    float fBaseFreq = AUDIO_BITRATE * 0.5f; /* Base frequency (half sample rate) */
    float fMinSpeed = 0.2f;                 /* NPC_ALIEN_THRUST_MIN_THRESHOLD */
    float fMaxSpeed = 3.0f;                 /* NPC_ALIEN_THRUST_STRONG_THRESHOLD */
    float fMaxFreqMultiplier = 2.0f;        /* Max frequency multiplier */

    float fSpeedFactor = 0.0f;
    if (fSpeed >= fMinSpeed)
    {
        if (fSpeed >= fMaxSpeed)
        {
            fSpeedFactor = 1.0f;
        }
        else
        {
            float fSpeedRange = fMaxSpeed - fMinSpeed;
            fSpeedFactor = (fSpeed - fMinSpeed) / fSpeedRange;
        }
    }

    /* Scale frequency from base (0.5x) to max multiplier at max speed */
    float fFreq = fBaseFreq * (1.0f + fSpeedFactor * (fMaxFreqMultiplier - 1.0f));
    mixer_ch_set_freq(channel, fFreq);
}

static void audio_calculate_pan_attenuation(struct vec2 vWorldPos, float fDistance, float *pLeftAttenuation, float *pRightAttenuation)
{
    /* Calculate panning based on screen position */
    struct vec2i vScreenPos;
    gp_camera_world_to_screen_wrapped(&g_mainCamera, vWorldPos, &vScreenPos);

    float fScreenCenterX = (float)SCREEN_W * 0.5f;
    float fPanFactor = ((float)vScreenPos.iX - fScreenCenterX) / fScreenCenterX;
    fPanFactor = clampf(fPanFactor, -1.0f, 1.0f);

    /* Calculate panning attenuation (left/right balance) */
    float fLeftAttenuation;
    float fRightAttenuation;
    if (fPanFactor <= 0.0f)
    {
        fLeftAttenuation = 1.0f;
        fRightAttenuation = 1.0f + (fPanFactor * 0.5f);
    }
    else
    {
        fLeftAttenuation = 1.0f - (fPanFactor * 0.5f);
        fRightAttenuation = 1.0f;
    }

    /* Optional distance attenuation */
    if (fDistance >= 0.0f)
    {
        float fDistanceAttenuation = 1.0f;
        if (fDistance > NPC_ENGINE_DISTANCE_START_FADE)
        {
            float fFadeRange = NPC_ENGINE_DISTANCE_STOP - NPC_ENGINE_DISTANCE_START_FADE;
            float fFadeDistance = fDistance - NPC_ENGINE_DISTANCE_START_FADE;
            fDistanceAttenuation = 1.0f - (fFadeDistance / fFadeRange);
            fDistanceAttenuation = clampf(fDistanceAttenuation, 0.0f, 1.0f);
        }

        fLeftAttenuation *= fDistanceAttenuation;
        fRightAttenuation *= fDistanceAttenuation;
    }

    *pLeftAttenuation = fLeftAttenuation;
    *pRightAttenuation = fRightAttenuation;
}

/* Helper function to update panning for a channel based on world position */
void audio_update_npc_pan_and_volume(int channel, float fBaseVolume, struct vec2 vWorldPos, float fDistance)
{
    /* If too far, don't update (caller should have already stopped the sound) */
    if (fDistance >= NPC_ENGINE_DISTANCE_STOP)
    {
        return;
    }

    /* Get volume settings from save system (0-100) */
    int iSfxVolume = save_get_sfx_volume();
    float fSfxMultiplier = (float)iSfxVolume / 100.0f;

    float fLeftAttenuation;
    float fRightAttenuation;
    audio_calculate_pan_attenuation(vWorldPos, fDistance, &fLeftAttenuation, &fRightAttenuation);

    float fBaseVolumeWithMultiplier = fBaseVolume * fSfxMultiplier;
    float fFinalVolumeLeft = fBaseVolumeWithMultiplier * fLeftAttenuation;
    float fFinalVolumeRight = fBaseVolumeWithMultiplier * fRightAttenuation;

    /* Store volumes for ducked channels instead of setting them directly (ducking will be applied in audio_poll) */
    if (channel == MIXER_CHANNEL_NPC_ALIEN)
    {
        s_fNpcAlienVolumeLeft = fFinalVolumeLeft;
        s_fNpcAlienVolumeRight = fFinalVolumeRight;
    }
    else if (channel == MIXER_CHANNEL_NPC_RHINO)
    {
        s_fNpcRhinoVolumeLeft = fFinalVolumeLeft;
        s_fNpcRhinoVolumeRight = fFinalVolumeRight;
    }
    else
    {
        /* Not a ducked channel - set volume directly */
        mixer_ch_set_vol(channel, fFinalVolumeLeft, fFinalVolumeRight);
    }
}

void audio_update_player_pan(void)
{
    /* Get current game state */
    gp_state_t eState = gp_state_get();

    /* Get appropriate position based on game state */
    struct vec2 vWorldPos;
    if (eState == SPACE || eState == PLANET)
    {
        vWorldPos = ufo_get_position();
    }
    else if (eState == SURFACE)
    {
        vWorldPos = player_surface_get_position();
    }
    else if (eState == JNR)
    {
        vWorldPos = player_jnr_get_position();
    }

    /* Get volume settings from save system (0-100) */
    int iSfxVolume = save_get_sfx_volume();
    float fSfxMultiplier = (float)iSfxVolume / 100.0f;

    /* Calculate base volumes with user settings applied */
    float fBaseVolumeUFO = AUDIO_BASE_VOLUME_UFO * fSfxMultiplier;
    float fBaseVolumeEngine = AUDIO_BASE_VOLUME_ENGINE * fSfxMultiplier;
    float fBaseVolumeWeapons = AUDIO_BASE_VOLUME_WEAPONS * fSfxMultiplier;

    /* Apply smooth panning attenuation based on distance from center:
     * - When pan = -1.0 (far left): right channel = 50%, left channel = 100%
     * - When pan = 0.0 (center): both channels = 100%
     * - When pan = 1.0 (far right): left channel = 50%, right channel = 100%
     * Smoothly interpolates between these values
     */
    float fLeftAttenuation;
    float fRightAttenuation;
    audio_calculate_pan_attenuation(vWorldPos, -1.0f, &fLeftAttenuation, &fRightAttenuation);

    /* Update volumes for UFO, ENGINE, and WEAPONS channels */
    /* Store UFO and ENGINE volumes instead of setting them directly (ducking will be applied in audio_poll) */
    s_fUfoVolumeLeft = fBaseVolumeUFO * fLeftAttenuation;
    s_fUfoVolumeRight = fBaseVolumeUFO * fRightAttenuation;
    s_fEngineVolumeLeft = fBaseVolumeEngine * fLeftAttenuation;
    s_fEngineVolumeRight = fBaseVolumeEngine * fRightAttenuation;
    mixer_ch_set_vol(MIXER_CHANNEL_WEAPONS, fBaseVolumeWeapons * fLeftAttenuation, fBaseVolumeWeapons * fRightAttenuation);
}

/* Music fade states */
typedef enum
{
    MUSIC_FADE_NONE, /* No fade in progress */
    MUSIC_FADE_OUT,  /* Fading out current music */
    MUSIC_FADE_IN    /* Fading in new music */
} eMusicFadeState;

/* Music system state */
static wav64_t *s_pCurrentMusic = NULL;
static eMusicFadeState s_eFadeState = MUSIC_FADE_NONE;
static float s_fFadeStartTime = 0.0f;
static float s_fFadeStartVolume = 0.0f;
static float s_fTargetVolume = 0.0f;
static char s_szCurrentMusicPath[512] = {0}; /* Track current music path to avoid unnecessary fades */

/* Pending music request (for fade out -> fade in transition) */
static eMusicType s_ePendingMusicType = MUSIC_NORMAL;
static char s_szPendingFolderName[256] = {0};
static bool s_bPendingMusicRequest = false;

/* Update ducking system (call each frame) */
static void audio_update_ducking(void)
{
    /* Check if ducking should be active and determine target multiplier */
    float fTargetMultiplier = 1.0f; /* Default: no ducking */
    eMenuState eMenuState = menu_get_state();

    if (fade_manager_is_busy() || fade_manager_is_opaque() || upgrade_shop_is_active() || eMenuState == MENU_STATE_PAUSE || eMenuState == MENU_STATE_PAUSE_SETTINGS ||
        eMenuState == MENU_STATE_PAUSE_SAVE_CONFIRM || eMenuState == MENU_STATE_PAUSE_EXIT_RACE_CONFIRM || eMenuState == MENU_STATE_CALIBRATION ||
        eMenuState == MENU_STATE_UPGRADE_SHOP)
    {
        /* Full ducking: complete silence */
        fTargetMultiplier = 0.0f;
    }
    else if (dialogue_is_active() || minimap_is_active())
    {
        /* Partial ducking: reduce to target volume */
        fTargetMultiplier = AUDIO_DUCKING_TARGET_VOLUME;
    }

    /* Check if target changed - start fade if needed */
    if (fabsf(fTargetMultiplier - s_fDuckingTargetMultiplier) > 0.01f)
    {
        /* Target changed - start fade */
        s_fDuckingFadeStartTime = (float)get_ticks_ms() / 1000.0f;
        s_fDuckingFadeStartMultiplier = s_fDuckingCurrentMultiplier;
        s_fDuckingTargetMultiplier = fTargetMultiplier;

        if (fTargetMultiplier < s_fDuckingCurrentMultiplier)
        {
            /* Ducking in (volume decreasing) */
            s_eDuckingFadeState = DUCKING_FADE_IN;
        }
        else
        {
            /* Ducking out (volume increasing) */
            s_eDuckingFadeState = DUCKING_FADE_OUT;
        }
    }

    /* Update fade if in progress */
    if (s_eDuckingFadeState != DUCKING_FADE_NONE)
    {
        float fCurrentTime = (float)get_ticks_ms() / 1000.0f;
        float fElapsed = fCurrentTime - s_fDuckingFadeStartTime;
        float fProgress = fElapsed / AUDIO_DUCKING_FADE_DURATION;

        if (fProgress >= 1.0f)
        {
            /* Fade complete */
            s_fDuckingCurrentMultiplier = s_fDuckingTargetMultiplier;
            s_eDuckingFadeState = DUCKING_FADE_NONE;
        }
        else
        {
            /* Interpolate between start and target */
            s_fDuckingCurrentMultiplier = s_fDuckingFadeStartMultiplier + (s_fDuckingTargetMultiplier - s_fDuckingFadeStartMultiplier) * fProgress;
        }
    }
    else
    {
        /* No fade in progress - ensure multiplier matches target */
        s_fDuckingCurrentMultiplier = s_fDuckingTargetMultiplier;
    }
}

/* Get current ducking multiplier */
static float audio_get_ducking_multiplier(void)
{
    return s_fDuckingCurrentMultiplier;
}

/* Get current target music volume (accounting for user volume settings) */
static float get_target_music_volume(void)
{
    int iMusicVolume = save_get_music_volume();
    float fMusicMultiplier = (float)iMusicVolume / 100.0f;
    return AUDIO_BASE_VOLUME_MUSIC * fMusicMultiplier;
}

/* Normal music playback frequency (no speed scaling) */
static float get_base_music_freq(void)
{
    return AUDIO_BITRATE;
}

/* Build music file path from type and folder */
static void build_music_path(char *szPath, size_t pathSize, eMusicType type, const char *folderName)
{
    const char *szFileName;

    /* Determine filename based on type */
    if (type == MUSIC_RACE)
    {
        szFileName = "race.wav64";
    }
    else if (type == MUSIC_STARTSCREEN)
    {
        szFileName = "music_startscreen.wav64";
        /* MUSIC_STARTSCREEN always loads from root */
        snprintf(szPath, pathSize, "rom:/%s", szFileName);
        return;
    }
    else if (type == MUSIC_SHOP)
    {
        szFileName = "crankhorn.wav64";
        /* MUSIC_SHOP always loads from root */
        snprintf(szPath, pathSize, "rom:/%s", szFileName);
        return;
    }
    else
    {
        szFileName = "music.wav64";
    }

    /* For MUSIC_NORMAL and MUSIC_RACE, folderName is required */
    if (folderName && folderName[0] != '\0')
    {
        snprintf(szPath, pathSize, "rom:/%s/%s", folderName, szFileName);
    }
    else
    {
        /* No folder provided - this is an error for MUSIC_NORMAL/MUSIC_RACE */
        snprintf(szPath, pathSize, "rom:/%s", szFileName);
    }
}

/* Check if music file exists and return path */
static bool check_music_file_exists(eMusicType type, const char *folderName, char *szPath, size_t pathSize)
{
    build_music_path(szPath, pathSize, type, folderName);
    FILE *pFile = fopen(szPath, "rb");
    if (!pFile)
    {
        debugf("Music file not found: %s\n", szPath);
        return false;
    }
    fclose(pFile);
    return true;
}

/* Check if the same music is already playing */
static bool is_same_music_playing(const char *szPath)
{
    return (s_pCurrentMusic && mixer_ch_playing(MIXER_CHANNEL_MUSIC) && s_eFadeState == MUSIC_FADE_NONE && strcmp(s_szCurrentMusicPath, szPath) == 0);
}

/* Stop and clean up current music */
static void stop_current_music(void)
{
    if (s_pCurrentMusic && mixer_ch_playing(MIXER_CHANNEL_MUSIC))
    {
        mixer_ch_stop(MIXER_CHANNEL_MUSIC);
    }
    SAFE_CLOSE_WAV64(s_pCurrentMusic);
    s_pCurrentMusic = NULL;
    s_eFadeState = MUSIC_FADE_NONE;
    s_bPendingMusicRequest = false;
    s_szCurrentMusicPath[0] = '\0';
}

/* Load music file and set it as current */
static wav64_t *load_music_file(const char *szPath)
{
    wav64_t *pNewMusic = wav64_load(szPath, &(wav64_loadparms_t){.streaming_mode = WAV64_STREAMING_FULL});
    if (!pNewMusic)
    {
        debugf("Failed to load music file: %s\n", szPath);
        return NULL;
    }

    /* Free old music if not playing */
    if (!mixer_ch_playing(MIXER_CHANNEL_MUSIC))
    {
        SAFE_CLOSE_WAV64(s_pCurrentMusic);
    }

    /* Set new music as current */
    s_pCurrentMusic = pNewMusic;
    wav64_set_loop(s_pCurrentMusic, true);
    strncpy(s_szCurrentMusicPath, szPath, sizeof(s_szCurrentMusicPath) - 1);
    s_szCurrentMusicPath[sizeof(s_szCurrentMusicPath) - 1] = '\0';

    return pNewMusic;
}

/* Start fade out of current music */
static void start_fade_out(void)
{
    s_eFadeState = MUSIC_FADE_OUT;
    s_fFadeStartTime = (float)get_ticks_ms() / 1000.0f;
    s_fFadeStartVolume = get_target_music_volume();
    s_fTargetVolume = 0.0f;
}

/* Load and start music with fade in */
static bool load_and_start_music(eMusicType type, const char *folderName)
{
    char szPath[512];
    if (!check_music_file_exists(type, folderName, szPath, sizeof(szPath)))
        return false;

    if (!load_music_file(szPath))
        return false;

    /* Start playing at volume 0, then fade in */
    mixer_ch_set_vol(MIXER_CHANNEL_MUSIC, 0.0f, 0.0f);
    /* Reset music pitch before starting new track */
    mixer_ch_set_freq(MIXER_CHANNEL_MUSIC, get_base_music_freq());
    wav64_play(s_pCurrentMusic, MIXER_CHANNEL_MUSIC);

    /* Start fade in */
    s_eFadeState = MUSIC_FADE_IN;
    s_fFadeStartTime = (float)get_ticks_ms() / 1000.0f;
    s_fFadeStartVolume = 0.0f;
    s_fTargetVolume = get_target_music_volume();

    return true;
}

/* Update music fade system (call each frame) */
void audio_update_music(void)
{
    float fCurrentTime = (float)get_ticks_ms() / 1000.0f;

    if (s_eFadeState == MUSIC_FADE_NONE)
    {
        /* Check if there's a pending music request */
        if (s_bPendingMusicRequest)
        {
            const char *pFolder = (s_szPendingFolderName[0] == '\0') ? NULL : s_szPendingFolderName;
            load_and_start_music(s_ePendingMusicType, pFolder);
            s_bPendingMusicRequest = false;
        }
        return;
    }

    float fElapsed = fCurrentTime - s_fFadeStartTime;
    float fProgress = fElapsed / (FADE_DURATION - 0.1f);

    if (fProgress >= 1.0f)
    {
        /* Fade complete */
        if (s_eFadeState == MUSIC_FADE_OUT)
        {
            /* Fade out complete - stop and free current music */
            if (mixer_ch_playing(MIXER_CHANNEL_MUSIC))
            {
                mixer_ch_stop(MIXER_CHANNEL_MUSIC);
            }
            SAFE_CLOSE_WAV64(s_pCurrentMusic);
            s_pCurrentMusic = NULL;
            s_eFadeState = MUSIC_FADE_NONE;
            s_szCurrentMusicPath[0] = '\0';
            /* Pending music will be loaded on next update */
        }
        else if (s_eFadeState == MUSIC_FADE_IN)
        {
            /* Fade in complete - set to target volume */
            float fTargetVol = get_target_music_volume();
            mixer_ch_set_vol(MIXER_CHANNEL_MUSIC, fTargetVol, fTargetVol);
            s_eFadeState = MUSIC_FADE_NONE;
        }
    }
    else
    {
        /* Update volume during fade */
        float fCurrentVolume = s_fFadeStartVolume + (s_fTargetVolume - s_fFadeStartVolume) * fProgress;
        mixer_ch_set_vol(MIXER_CHANNEL_MUSIC, fCurrentVolume, fCurrentVolume);
    }
}

/* Stop music with fade out (fades to silence, no new music will play) */
void audio_stop_music(void)
{
    if (s_pCurrentMusic && mixer_ch_playing(MIXER_CHANNEL_MUSIC) && s_eFadeState != MUSIC_FADE_OUT)
    {
        start_fade_out();
        /* Clear any pending music request */
        s_bPendingMusicRequest = false;
    }
}

/* Stop all audio channels except music (useful for transitions to menu/slideshow)
 */
void audio_stop_all_except_music(void)
{
    for (int i = MIXER_CHANNEL_MUSIC; i < MIXER_CHANNEL_COUNT; i++)
    {
        mixer_ch_stop(i);
    }
}

/* Load and start music instantly (no fade) */
static bool load_and_start_music_instant(eMusicType type, const char *folderName)
{
    char szPath[512];
    if (!check_music_file_exists(type, folderName, szPath, sizeof(szPath)))
        return false;

    /* Stop current music immediately if playing */
    stop_current_music();

    if (!load_music_file(szPath))
        return false;

    /* Start playing at full volume immediately (no fade) */
    float fTargetVol = get_target_music_volume();
    mixer_ch_set_vol(MIXER_CHANNEL_MUSIC, fTargetVol, fTargetVol);
    /* Reset music pitch before starting new track */
    mixer_ch_set_freq(MIXER_CHANNEL_MUSIC, get_base_music_freq());
    wav64_play(s_pCurrentMusic, MIXER_CHANNEL_MUSIC);
    s_eFadeState = MUSIC_FADE_NONE;
    s_bPendingMusicRequest = false;

    return true;
}

/* Play music instantly without fade */
bool audio_play_music_instant(eMusicType type, const char *folderName)
{
    char szPath[512];
    if (!check_music_file_exists(type, folderName, szPath, sizeof(szPath)))
    {
        stop_current_music();
        return false;
    }

    /* Check if the same music is already playing - if so, skip */
    if (is_same_music_playing(szPath))
        return true;

    /* Load and start instantly */
    return load_and_start_music_instant(type, folderName);
}

/* Play music with fade transition */
bool audio_play_music(eMusicType type, const char *folderName)
{
    char szPath[512];
    if (!check_music_file_exists(type, folderName, szPath, sizeof(szPath)))
    {
        /* If we have current music, fade it out and free it */
        if (s_pCurrentMusic && mixer_ch_playing(MIXER_CHANNEL_MUSIC))
        {
            start_fade_out();
        }
        else
        {
            SAFE_CLOSE_WAV64(s_pCurrentMusic);
        }
        s_bPendingMusicRequest = false;
        return false;
    }

    /* Check if the same music is already playing - if so, skip fade */
    if (is_same_music_playing(szPath))
        return true;

    /* If music is already playing, fade it out first, then queue the new music */
    if (s_pCurrentMusic && mixer_ch_playing(MIXER_CHANNEL_MUSIC) && s_eFadeState != MUSIC_FADE_OUT)
    {
        start_fade_out();

        /* Store pending music request */
        s_ePendingMusicType = type;
        /* For MUSIC_STARTSCREEN, folder is ignored (always loads from root) */
        if (type == MUSIC_STARTSCREEN)
        {
            s_szPendingFolderName[0] = '\0';
        }
        else
        {
            /* Store folder name now (before state might change) */
            if (folderName && folderName[0] != '\0')
            {
                strncpy(s_szPendingFolderName, folderName, sizeof(s_szPendingFolderName) - 1);
                s_szPendingFolderName[sizeof(s_szPendingFolderName) - 1] = '\0';
            }
            else
            {
                s_szPendingFolderName[0] = '\0';
            }
        }
        s_bPendingMusicRequest = true;
        return true;
    }

    /* No music playing or fade already in progress - load immediately */
    return load_and_start_music(type, folderName);
}