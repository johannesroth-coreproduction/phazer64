#pragma once

#include "libdragon.h"
#include "math2d.h"
#include <stdbool.h>

/* Helper macro to calculate array size at compile time */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// Audio / mixer settings
#define MIXER_CHANNEL_MUSIC 0
#define MIXER_CHANNEL_WEAPONS 1
#define MIXER_CHANNEL_EXPLOSIONS 2
#define MIXER_CHANNEL_USER_INTERFACE 3
#define MIXER_CHANNEL_UFO 4
#define MIXER_CHANNEL_ENGINE 5
#define MIXER_CHANNEL_ITEMS 6
#define MIXER_CHANNEL_NPC_ALIEN 7
#define MIXER_CHANNEL_NPC_RHINO 8
#define MIXER_CHANNEL_COUNT 9
#define WAV_COMPRESSION 1 // we use compressed wavs, 1= VADPCM, 3= OPUS
#define AUDIO_BUFFERS 4
#define AUDIO_BITRATE 22050

// Audio speed settings
#define AUDIO_SPEED_MIN 0.5f // Below this speed, music speed factor is 0
#define AUDIO_SPEED_MAX 2.0f // At/above this speed, music speed factor is 1 (linear scaling between min and max)

// Base volume settings (0.0f to 1.0f)
#define AUDIO_BASE_VOLUME_MUSIC 0.5f
#define AUDIO_BASE_VOLUME_EXPLOSIONS 0.35f
#define AUDIO_BASE_VOLUME_WEAPONS 0.35f
#define AUDIO_BASE_VOLUME_UI 0.5f
#define AUDIO_BASE_VOLUME_UFO 0.35f
#define AUDIO_BASE_VOLUME_ENGINE 0.4f
#define AUDIO_BASE_VOLUME_ITEMS 0.6f
#define AUDIO_BASE_VOLUME_NPC_ALIEN 0.35f
#define AUDIO_BASE_VOLUME_NPC_RHINO 0.35f

// Distance-based volume attenuation constants for NPC engine sounds
#define NPC_ENGINE_DISTANCE_START_FADE 200.0f /* Distance where volume starts fading (half-screen away) */
#define NPC_ENGINE_DISTANCE_STOP 400.0f       /* Distance where sound stops completely (save CPU) */

// Audio ducking constants for UI overlays
#define AUDIO_DUCKING_TARGET_VOLUME 0.2f /* Target volume multiplier when ducking is active */
#define AUDIO_DUCKING_FADE_DURATION 0.5f /* Fade duration in seconds for ducking transitions */

// #define AUDIO_BASE_VOLUME_MUSIC 1.0f
// #define AUDIO_BASE_VOLUME_EXPLOSIONS 1.0f
// #define AUDIO_BASE_VOLUME_WEAPONS 1.0f
// #define AUDIO_BASE_VOLUME_UI 1.0f
// #define AUDIO_BASE_VOLUME_UFO 1.0f
// #define AUDIO_BASE_VOLUME_ENGINE 1.0f
// #define AUDIO_BASE_VOLUME_ITEMS 1.0f

void audio_init_system(void);

// Poll audio (call each frame)
void audio_poll(void);

// Refresh channel volumes based on save settings (call when volume settings change)
void audio_refresh_volumes(void);

// Update music speed based on UFO speed (call each frame)
void audio_update_music_speed(float fCurrentSpeed);

// Update engine sound frequency based on thrust (call each frame)
void audio_update_engine_freq(float fThrust);

// Update NPC engine sound frequency based on speed (call each frame)
void audio_update_npc_engine_freq(int channel, float fSpeed);

// Update stereo panning for a channel based on world position (call each frame for NPCs)
// fDistance: pre-calculated distance from camera to world position (for efficiency)
void audio_update_npc_pan_and_volume(int channel, float fBaseVolume, struct vec2 vWorldPos, float fDistance);

// Update stereo panning for UFO, ENGINE, and WEAPONS channels based on UFO/player screen position (call each frame)
void audio_update_player_pan(void);

/* Music types */
typedef enum
{
    MUSIC_NORMAL,      /* Loads "music.wav64" from folder */
    MUSIC_RACE,        /* Loads "race.wav64" from folder */
    MUSIC_STARTSCREEN, /* Loads "music_startscreen.wav64" from root */
    MUSIC_SHOP         /* Loads "crankhorn.wav64" from root */
} eMusicType;

/* Play music with fade transition
 * If music is already playing, it will fade out the current track, then fade in the new one.
 * @param type Music type (MUSIC_NORMAL loads "music.wav64", MUSIC_RACE loads "race.wav64", MUSIC_STARTSCREEN loads "music_startscreen.wav64" from root)
 * @param folderName Folder name (without "rom:/" prefix or trailing slash).
 *                   Required for MUSIC_NORMAL and MUSIC_RACE (must not be NULL).
 *                   Ignored for MUSIC_STARTSCREEN (can be NULL, always loads from root).
 * @return true if music was loaded and started, false if music file doesn't exist or couldn't be loaded
 */
bool audio_play_music(eMusicType type, const char *folderName);

/* Play music instantly without fade (stops current music immediately if playing)
 * @param type Music type (MUSIC_NORMAL loads "music.wav64", MUSIC_RACE loads "race.wav64", MUSIC_STARTSCREEN loads "music_startscreen.wav64" from root)
 * @param folderName Folder name (without "rom:/" prefix or trailing slash).
 *                   Required for MUSIC_NORMAL and MUSIC_RACE (must not be NULL).
 *                   Ignored for MUSIC_STARTSCREEN (can be NULL, always loads from root).
 * @return true if music was loaded and started, false if music file doesn't exist or couldn't be loaded
 */
bool audio_play_music_instant(eMusicType type, const char *folderName);

/* Stop music with fade out (fades to silence, no new music will play)
 */
void audio_stop_music(void);

/* Stop all audio channels except music (useful for transitions to menu/slideshow)
 */
void audio_stop_all_except_music(void);

/* Update music fade system (call each frame) */
void audio_update_music(void);

/* Sound group for loading and playing random sounds from a collection */
typedef struct
{
    wav64_t **sounds; // Array of wav64_t pointers
    int count;        // Number of sounds in the group
    int channel;      // Mixer channel to play on
} audio_sound_group_t;

/* Internal implementation - use the macro below instead */
void audio_sound_group_init_impl(audio_sound_group_t *group, const char **paths, int count, int channel, wav64_t **sound_array);

/* Initialize a sound group by loading all sound files
 * @param group Pointer to the sound group to initialize
 * @param paths Array of file paths (e.g., "rom:/sound_1.wav64", "rom:/sound_2.wav64", ...)
 * @param channel Mixer channel to use for playback
 * @param sound_array Pre-allocated array of wav64_t* pointers (must be at least as large as paths array)
 */
#define audio_sound_group_init(group, paths, channel, sound_array) audio_sound_group_init_impl(group, paths, ARRAY_SIZE(paths), channel, sound_array)

/* Play a random sound from the group
 * @param group The sound group to play from
 * @param stop_current If true, stops any currently playing sound on the channel before playing
 */
void audio_sound_group_play_random(audio_sound_group_t *group, bool stop_current);

/* Free a sound group and its resources
 * @param group The sound group to free
 */
void audio_sound_group_free(audio_sound_group_t *group);