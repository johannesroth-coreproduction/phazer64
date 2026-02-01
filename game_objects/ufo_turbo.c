#include "ufo_turbo.h"
#include "../audio.h"
#include "../dialogue.h"
#include "../font_helper.h"
#include "../frame_time.h"
#include "../math_helper.h"
#include "../meter_renderer.h"
#include "../minimap.h"
#include "../resource_helper.h"
#include "../ui.h"
#include "gp_state.h"
#include "libdragon.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "rdpq_tex.h"
#include "tractor_beam.h"
#include <stdio.h>

/* Turbo state */
static float m_fFuel = 100.0f; /* Fuel level: 0-100, full = 100, empty = 0 */
static sprite_t *m_spriteUfoTurbo = NULL;
static wav64_t *m_sfxNoTurbo = NULL;
static wav64_t *m_sfxTurbo = NULL;
static bool m_bPrevTurboPressed = false;
static bool m_bTurboSoundPlaying = false;
static float m_fRegenDelayTimer = 0.0f; /* Timer for regeneration delay (ms) */
static float m_fBurstTimer = 0.0f;      /* Timer for turbo burst (ms), 0 = inactive */

/* HUD sprites */
static sprite_t *m_pBtnA = NULL;

/* Initialize turbo system (loads sprites) */
void ufo_turbo_init(void)
{
    ufo_turbo_free();

    m_spriteUfoTurbo = sprite_load("rom:/ufo_turbo_00.sprite");
    m_fFuel = 100.0f;
    m_bPrevTurboPressed = false;
    m_bTurboSoundPlaying = false;
    m_fRegenDelayTimer = 0.0f;
    m_fBurstTimer = 0.0f;

    /* Load sound effects */
    m_sfxNoTurbo = wav64_load("rom:/ufo_no_turbo.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    m_sfxTurbo = wav64_load("rom:/ufo_turbo.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    if (m_sfxTurbo)
    {
        wav64_set_loop(m_sfxTurbo, true);
    }

    /* Initialize shared meter renderer (HUD frame/fill/cap) */
    meter_renderer_init();

    /* Load HUD sprites */
    m_pBtnA = sprite_load("rom:/btn_a_00.sprite");
}

void ufo_turbo_free(void)
{
    SAFE_FREE_SPRITE(m_spriteUfoTurbo);
    SAFE_FREE_SPRITE(m_pBtnA);
    SAFE_CLOSE_WAV64(m_sfxNoTurbo);
    SAFE_CLOSE_WAV64(m_sfxTurbo);

    meter_renderer_free();
}

/* Update turbo system (depletes fuel when button pressed) and returns effective multiplier */
float ufo_turbo_update(bool _bTurboPressed)
{
    float fDeltaMs = frame_time_delta_seconds() * 1000.0f; /* convert to ms */
    bool bTurboUnlocked = gp_state_unlock_get(GP_UNLOCK_TURBO);
    bool bTractorBlocked = tractor_beam_is_active();

    /* Update burst timer */
    if (m_fBurstTimer > 0.0f)
    {
        m_fBurstTimer -= fDeltaMs;
        if (m_fBurstTimer < 0.0f)
            m_fBurstTimer = 0.0f;
    }

    /* Check if turbo burst is active - behaves like holding A but doesn't deplete fuel.
     * Note: bursts are allowed even if turbo isn't unlocked (e.g., race coins). */
    bool bBurstActive = (m_fBurstTimer > 0.0f) && !bTractorBlocked;

    /* Detect when turbo button is pressed but fuel is 0 (check before reset) */
    bool bTurboPressedEdge = _bTurboPressed && !m_bPrevTurboPressed;
    m_bPrevTurboPressed = _bTurboPressed;

    // m_fFuel = 100.0f;

    /* Decide which turbo source is active this frame.
     * - Manual turbo (A button) only works when turbo is unlocked.
     * - While a burst is active, fuel must NOT be depleted, so manual turbo
     *   is effectively ignored until the burst ends. */
    bool bManualTurboRequested = bTurboUnlocked && _bTurboPressed && !bTractorBlocked && !bBurstActive;
    bool bCanManualTurbo = bManualTurboRequested && m_fFuel > 0.0f;

    float fMultiplier = 1.0f;
    bool bFuelDepletedNow = false;
    if (bCanManualTurbo)
    {
        bool bFuelWasAboveZero = m_fFuel > 0.0f;

        /* Reset regeneration delay timer when using turbo */
        m_fRegenDelayTimer = 0.0f;

        /* Calculate fuel depletion rate: full tank (100) lasts UFO_TURBO_FULL_FUEL_DURATION_MS */
        float fDepletionRate = 100.0f / (float)UFO_TURBO_FULL_FUEL_DURATION_MS; /* fuel per ms */
        m_fFuel -= fDepletionRate * fDeltaMs;

        /* Clamp fuel to 0 */
        if (m_fFuel < 0.0f)
            m_fFuel = 0.0f;

        fMultiplier = UFO_TURBO_MULTIPLIER;

        /* Play empty sound once when fuel runs out while holding A */
        bFuelDepletedNow = bFuelWasAboveZero && m_fFuel <= 0.0f;
    }
    else if (bBurstActive)
    {
        /* Burst acts like turbo but does not deplete fuel and should not pause refill. */
        fMultiplier = UFO_TURBO_MULTIPLIER;
    }

    /* Play empty sound when trying to turbo with no fuel, or when fuel runs out mid-hold */
    bool bShouldPlayNoTurbo = bTurboUnlocked && !bTractorBlocked && ((bTurboPressedEdge && m_fFuel <= 0.0f) || bFuelDepletedNow);
    if (bShouldPlayNoTurbo && m_sfxNoTurbo)
    {
        wav64_play(m_sfxNoTurbo, MIXER_CHANNEL_USER_INTERFACE);
        m_bTurboSoundPlaying = false;
    }

    /* Handle turbo sound playback:
     * - Manual turbo (A button) always plays the sound
     * - Burst plays the sound only if A is not currently held
     * - Sound continues as long as either manual turbo OR burst is active */
    bool bTurboSoundShouldPlay = bCanManualTurbo || bBurstActive;

    if (bTurboSoundShouldPlay)
    {
        /* Turbo sound should be playing - ensure it's playing */
        if (m_sfxTurbo)
        {
            if (!m_bTurboSoundPlaying || !mixer_ch_playing(MIXER_CHANNEL_USER_INTERFACE))
            {
                /* Sound should be playing but isn't - start it */
                wav64_play(m_sfxTurbo, MIXER_CHANNEL_USER_INTERFACE);
                m_bTurboSoundPlaying = true;
            }
        }
    }
    else
    {
        /* Turbo sound should stop - stop playing sound */
        if (m_bTurboSoundPlaying && mixer_ch_playing(MIXER_CHANNEL_USER_INTERFACE))
        {
            mixer_ch_stop(MIXER_CHANNEL_USER_INTERFACE);
        }
        m_bTurboSoundPlaying = false;
    }

    /* Fuel regeneration: when not using turbo (button) and fuel is not full.
     * Bursts do NOT pause regeneration. */
    if (!_bTurboPressed && m_fFuel < 100.0f)
    {
        /* Increment delay timer */
        m_fRegenDelayTimer += fDeltaMs;

        /* After delay, start regenerating fuel */
        if (m_fRegenDelayTimer >= (float)UFO_TURBO_FUEL_REGEN_DELAY_MS)
        {
            /* Calculate regeneration rate: fill from 0 to 100 in UFO_TURBO_FUEL_REGEN_TIME_MS */
            float fRegenRate = 100.0f / (float)UFO_TURBO_FUEL_REGEN_TIME_MS; /* fuel per ms */
            m_fFuel += fRegenRate * fDeltaMs;

            /* Clamp fuel to 100 */
            if (m_fFuel > 100.0f)
                m_fFuel = 100.0f;
        }
    }
    else
    {
        /* Reset delay timer when fuel is full or turbo is pressed */
        m_fRegenDelayTimer = 0.0f;
    }

    /* Return final turbo multiplier for this frame (1.0f if no turbo source active) */
    return fMultiplier;
}

/* Refill fuel to maximum (100) */
void ufo_turbo_refill(void)
{
    m_fFuel = 100.0f;
}

/* Trigger a short turbo burst (behaves like holding A for _fDurationMs, but doesn't deplete fuel) */
void ufo_turbo_trigger_burst(float _fDurationMs)
{
    m_fBurstTimer = _fDurationMs;
}

/* Get current fuel level (0-100) */
float ufo_turbo_get_fuel(void)
{
    return m_fFuel;
}

/* Get turbo sprite for rendering */
sprite_t *ufo_turbo_get_sprite(void)
{
    return m_spriteUfoTurbo;
}

/* Render turbo UI (fuel display) */
void ufo_turbo_render_ui(void)
{
    /* Hide turbo UI when turbo is not unlocked */
    if (!gp_state_unlock_get(GP_UNLOCK_TURBO))
        return;

    /* Don't render turbo UI during dialogue */
    if (dialogue_is_active())
        return;

    if (minimap_is_active())
        return;

    /* Don't render turbo UI when tractor beam is active */
    if (tractor_beam_is_active())
        return;

    /* Get meter frame size from shared renderer and position at top-right */
    struct vec2i vMeterSize = meter_renderer_get_frame_size();
    struct vec2i vFramePos = ui_get_pos_top_right(vMeterSize.iX, vMeterSize.iY);
    struct vec2i vBtnPos = ui_get_pos_top_right_sprite(m_pBtnA);

    vBtnPos.iY += 2 * UI_DESIGNER_PADDING; // extra for n64 layout feel

    vFramePos.iY = vBtnPos.iY + m_pBtnA->height + 4;
    vFramePos.iX -= 2;

    float fFuelPercent = clampf_01(m_fFuel / 100.0f);
    /* Render the turbo meter in green using shared renderer */
    meter_renderer_render(vFramePos, fFuelPercent, RGBA32(0, 255, 0, 255));

    /* Draw btn_a_00 h-centered below the hudframe with UI_DESIGNER_PADDING spacing */
    if (m_pBtnA)
    {
        rdpq_sprite_blit(m_pBtnA, vBtnPos.iX, vBtnPos.iY, NULL);
    }
}
