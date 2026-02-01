#include "race_handler.h"
#include "../audio.h"
#include "../camera.h"
#include "../dialogue.h"
#include "../entity2d.h"
#include "../fade_manager.h"
#include "../font_helper.h"
#include "../frame_time.h"
#include "../game_objects/gp_state.h"
#include "../game_objects/starfield.h"
#include "../game_objects/tractor_beam.h"
#include "../game_objects/ufo.h"
#include "../game_objects/ufo_turbo.h"
#include "../math_helper.h"
#include "../menu.h"
#include "../minimap.h"
#include "../resource_helper.h"
#include "../ui.h"
#include "libdragon.h"
#include "n64sys.h"
#include "race_track.h"
#include "rdpq.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "rdpq_text.h"
#include "sprite.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only allow race activation UI / best time display once the game has progressed to this act */
#define MIN_PROGRESS_ACT ACT_OPENING

/* Keep post-race best/last UI visible for a short time after finishing */
#define POST_RACE_UI_DURATION_SECONDS 5.0f

/* Countdown sound */
static wav64_t *s_pCountdownSound = NULL;

/* Coin pickup sound */
static wav64_t *s_pCoinPickupSound = NULL;

/* Race finish sound */
static wav64_t *s_pRaceFinishSound = NULL;

/* Race handler state */
static struct
{
    bool bInitialized;
    uint16_t uCoinsPerLap;
    float fCoinTurboBurstDurationMs; /* Turbo burst duration when coin is collected (ms) */
    coin_state_t aCoinStates[RACE_HANDLER_MAX_COINS_PER_LAP];
    float aCoinProgress[RACE_HANDLER_MAX_COINS_PER_LAP]; /* s values for each coin */
    struct entity2D coinEntity;                          /* Single reusable coin entity */
    sprite_t *pCoinSprite;                               /* Coin sprite */
    sprite_t *pPickupSprite;                             /* Pickup slot sprite */
    sprite_t *pBtnCDownSprite;                           /* C-down button sprite for finish line trigger */
    rdpq_texparms_t pickupTexParms;                      /* Pickup texture parameters */
    uint16_t uActiveCoinIndex;                           /* Currently active coin (starts at 1) */
    uint16_t uCurrentLap;                                /* Current lap (1-3) */
    float aLapTimes[RACE_HANDLER_MAX_LAPS];              /* Lap times in seconds */
    float fLapStartTime;                                 /* Start time of current lap */
    float fPausedLapTime;                                /* Lap time when menu was opened (for display while paused) */
    bool bIsPaused;                                      /* Track if currently paused */
    uint16_t uTotalCoinsCollected;                       /* Total coins collected across all laps */
    uint16_t aLapCoinsCollected[RACE_HANDLER_MAX_LAPS];  /* Coins collected per lap */
    uint16_t aLapCoinsMissed[RACE_HANDLER_MAX_LAPS];     /* Coins missed per lap */
    bool bRaceActive;                                    /* Is race currently active */
    race_start_state_t eStartState;                      /* Race start sequence state */
    float fCountdownTimer;                               /* Timer for countdown */
    int iCountdownIndex;                                 /* Current countdown number (3, 2, 1, 0=GO) */
    bool bRaceWasStarted;                                /* Track if race was ever started (for finished check) */
    uint16_t uMaxLaps;                                   /* Maximum number of laps for this race */
    struct entity2D finishLineTriggerEntity;             /* Finish line trigger entity (circle) */
    bool bFinishLineTriggerSelected;                     /* Is UFO currently in finish line trigger */
    float fLastRunBestLapTime;                           /* Best lap time of the most recently completed run (seconds) */
    bool bHasLastRunBestLapTime;                         /* True if fLastRunBestLapTime is valid */
    const struct entity2D *pSavedUfoNextTarget;          /* UFO next-target before race start (temporary) */
    bool bHasSavedUfoNextTarget;                         /* True if pSavedUfoNextTarget is valid */
    float fPostRaceUiTimer;                              /* Seconds remaining to keep post-race BEST/LAST visible */
} m_handler;

/* Helper: Calculate forward distance along track loop */
static float dist_fwd(float _fA, float _fB, float _fL)
{
    if (_fB >= _fA)
        return _fB - _fA;
    else
        return (_fL - _fA) + _fB;
}

/* Helper: Format lap time as MM:SS:MS */
static void format_lap_time(float _fSeconds, char *_pOut, size_t _uSize)
{
    int iMinutes = (int)(_fSeconds / 60.0f);
    int iSeconds = (int)_fSeconds % 60;
    int iCentiseconds = (int)((_fSeconds - (int)_fSeconds) * 100.0f);
    snprintf(_pOut, _uSize, "%02d:%02d:%02d", iMinutes, iSeconds, iCentiseconds);
}

/* Place coin at given index */
static void place_coin_at_index(uint16_t _uIndex)
{
    if (_uIndex >= m_handler.uCoinsPerLap || !race_track_is_initialized())
        return;

    float fS = m_handler.aCoinProgress[_uIndex];
    struct vec2 vPos, vTangent, vNormal;
    if (!race_track_get_position_for_progress_with_normal(fS, &vPos, &vTangent, &vNormal))
        return;

    /* Calculate curvature to determine inside direction */
    /* Sample points slightly ahead and behind to determine turn direction */
    float fLookAhead = 50.0f; /* Small distance to sample ahead/behind */
    struct vec2 vPosAhead, vTangentAhead, vPosBehind, vTangentBehind;

    float fSAhead = fS + fLookAhead;
    if (fSAhead >= race_track_get_total_length())
        fSAhead -= race_track_get_total_length();
    float fSBehind = fS - fLookAhead;
    if (fSBehind < 0.0f)
        fSBehind += race_track_get_total_length();

    bool bHasAhead = race_track_get_position_for_progress(fSAhead, &vPosAhead, &vTangentAhead);
    bool bHasBehind = race_track_get_position_for_progress(fSBehind, &vPosBehind, &vTangentBehind);

    if (bHasAhead && bHasBehind)
    {
        /* Calculate cross product to determine turn direction */
        /* If tangent rotates CW, inside is to the right; if CCW, inside is to the left */
        struct vec2 vTangentChange = vec2_sub(vTangentAhead, vTangentBehind);
        float fCross = (vTangent.fX * vTangentChange.fY) - (vTangent.fY * vTangentChange.fX);

        /* fCross > 0 means CCW turn (inside is to the left of tangent) */
        /* fCross < 0 means CW turn (inside is to the right of tangent) */
        /* The normal from the track points in a consistent direction, but we need to flip it based on curvature */
        if (fCross < 0.0f)
        {
            /* CW turn: inside is to the right, so flip normal */
            vNormal = vec2_scale(vNormal, -1.0f);
        }
        /* For CCW turn, normal already points to inside */
    }

    /* Offset coin towards inner edge */
    vPos = vec2_add(vPos, vec2_scale(vNormal, RACE_HANDLER_COIN_OFFSET_INNER));

    /* Update coin entity position */
    entity2d_set_pos(&m_handler.coinEntity, vPos);
    m_handler.coinEntity.uFlags |= (ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE);

    /* Set as UFO target */
    ufo_set_next_target(&m_handler.coinEntity);
}

void race_handler_init(const char *_pRaceName, uint16_t _uCoinsPerLap, float _fCoinTurboBurstDurationMs, uint16_t _uMaxLaps)
{
    race_handler_free();

    if (!_pRaceName || _uCoinsPerLap == 0 || _uCoinsPerLap > RACE_HANDLER_MAX_COINS_PER_LAP || _uMaxLaps == 0 || _uMaxLaps > RACE_HANDLER_MAX_LAPS)
    {
        debugf("race_handler_init: Invalid parameters\n");
        return;
    }

    /* Initialize race track */
    race_track_init(_pRaceName);
    if (!race_track_is_initialized())
    {
        debugf("race_handler_init: Failed to initialize race track\n");
        return;
    }

    m_handler.uCoinsPerLap = _uCoinsPerLap;
    m_handler.fCoinTurboBurstDurationMs = _fCoinTurboBurstDurationMs;
    m_handler.uMaxLaps = _uMaxLaps;
    m_handler.bInitialized = true;

    /* Calculate coin progress values */
    float fTotalLength = race_track_get_total_length();
    m_handler.aCoinProgress[0] = 0.0f; /* Coin 0 at finish */
    /* Place coins 1 through (uCoinsPerLap - 1) evenly along the track */
    /* Divide track into uCoinsPerLap segments, place coins at positions 1/N, 2/N, ..., (N-1)/N */
    /* This ensures coin (uCoinsPerLap-1) is just before the finish, not at it */
    for (uint16_t i = 1; i < _uCoinsPerLap; ++i)
    {
        m_handler.aCoinProgress[i] = ((float)i / (float)_uCoinsPerLap) * fTotalLength;
    }

    /* Initialize coin states */
    memset(m_handler.aCoinStates, COIN_STATE_EMPTY, sizeof(coin_state_t) * _uCoinsPerLap);

    /* Initialize coin entity (not activated yet) */
    m_handler.pCoinSprite = sprite_load("rom:/race_coin_00.sprite");
    if (m_handler.pCoinSprite)
    {
        entity2d_init_from_sprite(&m_handler.coinEntity, vec2_zero(), m_handler.pCoinSprite, 0, ENTITY_LAYER_GAMEPLAY);
        m_handler.coinEntity.iCollisionRadius = (int)(RACE_HANDLER_COLLECTION_RADIUS);
    }
    /* Ensure coin entity is initially inactive */
    m_handler.coinEntity.uFlags &= ~(ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE);

    /* Load pickup slot texture */
    m_handler.pPickupSprite = sprite_load("rom:/race_pickup_00.sprite");
    m_handler.pickupTexParms = (rdpq_texparms_t){0};

    /* Load C-down button sprite for finish line trigger */
    m_handler.pBtnCDownSprite = sprite_load("rom:/btn_c_down_00.sprite");

    /* Load countdown sound */
    if (!s_pCountdownSound)
        s_pCountdownSound = wav64_load("rom:/countdown.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    /* Load coin pickup sound */
    if (!s_pCoinPickupSound)
        s_pCoinPickupSound = wav64_load("rom:/item_turbo_pickup.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    /* Load race finish sound */
    if (!s_pRaceFinishSound)
        s_pRaceFinishSound = wav64_load("rom:/race_finish.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    /* Reset race state */
    m_handler.uActiveCoinIndex = 1;
    m_handler.uCurrentLap = 0;
    m_handler.uTotalCoinsCollected = 0;
    m_handler.bRaceActive = false;
    m_handler.eStartState = RACE_START_NONE;
    m_handler.fCountdownTimer = 0.0f;
    m_handler.iCountdownIndex = 0;
    m_handler.bIsPaused = false;
    m_handler.fPausedLapTime = 0.0f;
    memset(m_handler.aLapTimes, 0, sizeof(float) * RACE_HANDLER_MAX_LAPS);
    memset(m_handler.aLapCoinsCollected, 0, sizeof(uint16_t) * RACE_HANDLER_MAX_LAPS);
    memset(m_handler.aLapCoinsMissed, 0, sizeof(uint16_t) * RACE_HANDLER_MAX_LAPS);

    /* Disable collision - track is rendered but inactive */
    race_track_set_collision_enabled(false);

    /* Initialize finish line trigger at progress 0.0 */
    struct vec2 vFinishPos, vFinishTangent;
    if (race_track_get_position_for_progress(0.0f, &vFinishPos, &vFinishTangent))
    {
        /* Create circle trigger entity with radius matching finish line half-width */
        float fTriggerRadius = RACE_TRACK_WIDTH * 0.4f;
        int iRadius = (int)fTriggerRadius;
        struct vec2i vSize = {iRadius * 2, iRadius * 2};
        entity2d_init_from_size(&m_handler.finishLineTriggerEntity, vFinishPos, vSize, NULL, ENTITY_FLAG_ACTIVE | ENTITY_FLAG_COLLIDABLE, ENTITY_LAYER_GAMEPLAY);
        m_handler.finishLineTriggerEntity.iCollisionRadius = iRadius;
        m_handler.finishLineTriggerEntity.uFlags &= ~ENTITY_FLAG_VISIBLE; /* Invisible trigger */
    }
    m_handler.bFinishLineTriggerSelected = false;
}

void race_handler_free(void)
{
    if (!m_handler.bInitialized)
        return;

    /* Free race track */
    race_track_free();

    /* Free sprites */
    SAFE_FREE_SPRITE(m_handler.pCoinSprite);
    SAFE_FREE_SPRITE(m_handler.pPickupSprite);
    SAFE_FREE_SPRITE(m_handler.pBtnCDownSprite);

    /* Free countdown sound */
    SAFE_CLOSE_WAV64(s_pCountdownSound);

    /* Free coin pickup sound */
    SAFE_CLOSE_WAV64(s_pCoinPickupSound);

    /* Free race finish sound */
    SAFE_CLOSE_WAV64(s_pRaceFinishSound);

    /* Reset frequency to normal (in case countdown was interrupted) */
    mixer_ch_set_freq(MIXER_CHANNEL_ITEMS, AUDIO_BITRATE);

    /* Reset state */
    memset(&m_handler, 0, sizeof(m_handler));
}

void race_handler_start_race(void)
{
    if (!m_handler.bInitialized || !race_track_is_initialized())
    {
        debugf("race_handler_start_race: Not initialized\n");
        return;
    }

    /* Mark that race was started */
    m_handler.bRaceWasStarted = true;

    /* Save current UFO next-target (coins will overwrite it during the race) */
    m_handler.pSavedUfoNextTarget = ufo_get_next_target();
    m_handler.bHasSavedUfoNextTarget = (m_handler.pSavedUfoNextTarget != NULL);

    /* Reset race state */
    m_handler.uActiveCoinIndex = 1;
    m_handler.uCurrentLap = 1;
    m_handler.uTotalCoinsCollected = 0;
    /* Reset per-run best lap cache (this is temporary data, not persisted) */
    m_handler.fLastRunBestLapTime = 0.0f;
    m_handler.bHasLastRunBestLapTime = false;
    m_handler.fPostRaceUiTimer = 0.0f;
    m_handler.eStartState = RACE_START_FADE_TO_BLACK;
    m_handler.fCountdownTimer = 0.0f;
    m_handler.iCountdownIndex = RACE_HANDLER_COUNTDOWN_TOTAL - 1; /* Start at 3 */
    m_handler.bIsPaused = false;
    m_handler.fPausedLapTime = 0.0f;
    memset(m_handler.aCoinStates, COIN_STATE_EMPTY, sizeof(coin_state_t) * m_handler.uCoinsPerLap);
    memset(m_handler.aLapTimes, 0, sizeof(float) * RACE_HANDLER_MAX_LAPS);
    memset(m_handler.aLapCoinsCollected, 0, sizeof(uint16_t) * RACE_HANDLER_MAX_LAPS);
    memset(m_handler.aLapCoinsMissed, 0, sizeof(uint16_t) * RACE_HANDLER_MAX_LAPS);

    /* Start fade to black */
    gp_state_cutscene_set(true);
    fade_manager_start(TO_BLACK);

    /* Fade out current music (race music will start instantly on GO) */
    audio_stop_music();
}

void race_handler_stop_race(void)
{
    if (!m_handler.bInitialized)
        return;

    /* Stop the race - reset state */
    m_handler.bRaceActive = false;
    m_handler.eStartState = RACE_START_NONE;
    m_handler.coinEntity.uFlags &= ~(ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE);

    /* Reset race state variables to prevent stale data when restarting */
    m_handler.uActiveCoinIndex = 1;
    m_handler.uCurrentLap = 0;
    m_handler.uTotalCoinsCollected = 0;
    m_handler.bIsPaused = false;
    m_handler.fPausedLapTime = 0.0f;
    memset(m_handler.aCoinStates, COIN_STATE_EMPTY, sizeof(coin_state_t) * m_handler.uCoinsPerLap);
    memset(m_handler.aLapTimes, 0, sizeof(float) * RACE_HANDLER_MAX_LAPS);
    memset(m_handler.aLapCoinsCollected, 0, sizeof(uint16_t) * RACE_HANDLER_MAX_LAPS);
    memset(m_handler.aLapCoinsMissed, 0, sizeof(uint16_t) * RACE_HANDLER_MAX_LAPS);

    /* Restore UFO next-target from before the race (if any) */
    if (m_handler.bHasSavedUfoNextTarget)
    {
        ufo_set_next_target(m_handler.pSavedUfoNextTarget);
    }
    m_handler.pSavedUfoNextTarget = NULL;
    m_handler.bHasSavedUfoNextTarget = false;

    /* Disable collision */
    race_track_set_collision_enabled(false);

    /* Exit cutscene mode if we were in one */
    gp_state_cutscene_set(false);

    /* Resume normal music */
    const char *pFolder = gp_state_get_current_folder();
    if (pFolder)
    {
        audio_play_music(MUSIC_NORMAL, pFolder);
    }

    /* Reset frequency to normal (in case countdown was interrupted) */
    mixer_ch_set_freq(MIXER_CHANNEL_ITEMS, AUDIO_BITRATE);
}

void race_handler_abort_race(void)
{
    /* Aborting a race should not leave any "LAST run" data behind */
    m_handler.fLastRunBestLapTime = 0.0f;
    m_handler.bHasLastRunBestLapTime = false;
    m_handler.fPostRaceUiTimer = 0.0f;

    race_handler_stop_race();
}

bool race_handler_was_started_and_finished(void)
{
    /* Race is finished if it was started, initialized, but is no longer active */
    return m_handler.bRaceWasStarted && m_handler.bInitialized && !race_handler_is_race_active();
}

void race_handler_reset_finished_flag(void)
{
    /* Reset the flag so SC_RACE_FINISHED can be checked again after a new race */
    m_handler.bRaceWasStarted = false;
}

/* Advance to next coin (marks current as collected or missed) */
static void advance_to_next_coin(bool _bCollected)
{
    /* Mark current coin state */
    if (m_handler.uActiveCoinIndex < m_handler.uCoinsPerLap)
    {
        m_handler.aCoinStates[m_handler.uActiveCoinIndex] = _bCollected ? COIN_STATE_COLLECTED : COIN_STATE_MISSED;
        if (_bCollected)
        {
            m_handler.uTotalCoinsCollected++;
            /* Track per lap */
            if (m_handler.uCurrentLap > 0 && m_handler.uCurrentLap <= m_handler.uMaxLaps)
                m_handler.aLapCoinsCollected[m_handler.uCurrentLap - 1]++;
        }
        else
        {
            /* Track missed per lap */
            if (m_handler.uCurrentLap > 0 && m_handler.uCurrentLap <= m_handler.uMaxLaps)
                m_handler.aLapCoinsMissed[m_handler.uCurrentLap - 1]++;
        }
    }

    /* Advance to next coin */
    m_handler.uActiveCoinIndex++;
    if (m_handler.uActiveCoinIndex >= m_handler.uCoinsPerLap)
        m_handler.uActiveCoinIndex = 0; /* Wrap to finish coin */

    /* Place next coin if valid and race is still active */
    /* Don't place a coin if we just completed a lap/race (coin 0 triggers completion) */
    if (m_handler.bRaceActive && m_handler.uActiveCoinIndex < m_handler.uCoinsPerLap)
    {
        place_coin_at_index(m_handler.uActiveCoinIndex);
    }
    else
    {
        m_handler.coinEntity.uFlags &= ~(ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE);
    }
}

/* Handle lap completion or race end */
static void complete_lap_or_race(float _fLapTime)
{
    m_handler.aLapTimes[m_handler.uCurrentLap - 1] = _fLapTime;
    if (m_handler.uCurrentLap < m_handler.uMaxLaps)
    {
        /* Start next lap */
        m_handler.uCurrentLap++;
        m_handler.uActiveCoinIndex = 1;
        m_handler.fLapStartTime = (float)get_ticks_ms() / 1000.0f;
        memset(m_handler.aCoinStates, COIN_STATE_EMPTY, sizeof(coin_state_t) * m_handler.uCoinsPerLap);
        place_coin_at_index(m_handler.uActiveCoinIndex);
    }
    else
    {
        /* Race complete - calculate and store best lap time */
        float fBestLapTime = 0.0f;
        bool bHasBestLap = false;
        for (uint16_t i = 0; i < m_handler.uMaxLaps; ++i)
        {
            if (m_handler.aLapTimes[i] > 0.0f)
            {
                if (!bHasBestLap || m_handler.aLapTimes[i] < fBestLapTime)
                {
                    fBestLapTime = m_handler.aLapTimes[i];
                    bHasBestLap = true;
                }
            }
        }

        if (bHasBestLap)
        {
            /* Store best lap time of THIS RUN (temporary) for UI comparison after finishing */
            m_handler.fLastRunBestLapTime = fBestLapTime;
            m_handler.bHasLastRunBestLapTime = true;
            m_handler.fPostRaceUiTimer = POST_RACE_UI_DURATION_SECONDS;

            /* Update best lap time if this is better than stored (or if stored is 0.0f) */
            float fStoredBest = gp_state_get_best_lap_time();
            if (fStoredBest == 0.0f || fBestLapTime < fStoredBest)
            {
                gp_state_set_best_lap_time(fBestLapTime);
            }
        }

        /* Play race finish sound */
        if (s_pRaceFinishSound)
            wav64_play(s_pRaceFinishSound, MIXER_CHANNEL_EXPLOSIONS);

        race_handler_stop_race();
    }
}

void race_handler_update(bool _bCDown)
{
    if (!m_handler.bInitialized || !race_track_is_initialized())
        return;

    if (!gp_state_accepts_input())
        _bCDown = false;
    else if (_bCDown && tractor_beam_is_active())
        _bCDown = false;

    /* Tick post-race UI timer (independent of proximity/selection) */
    if (m_handler.fPostRaceUiTimer > 0.0f)
    {
        m_handler.fPostRaceUiTimer -= frame_time_delta_seconds();
        if (m_handler.fPostRaceUiTimer < 0.0f)
            m_handler.fPostRaceUiTimer = 0.0f;
    }

    const bool bProgressActOk = (gp_state_act_get() >= MIN_PROGRESS_ACT);

    /* Update race track (always, for rendering) */
    race_track_update();

    /* Check finish line trigger collision (only when race is not active) */
    if (bProgressActOk && (m_handler.eStartState == RACE_START_NONE || (m_handler.bRaceWasStarted && !m_handler.bRaceActive)))
    {
        const struct entity2D *pUfoEntity = ufo_get_entity();
        if (pUfoEntity && entity2d_is_active(&m_handler.finishLineTriggerEntity))
        {
            m_handler.bFinishLineTriggerSelected = entity2d_check_collision_circle(pUfoEntity, &m_handler.finishLineTriggerEntity);

            /* Handle C-down input to restart race */
            if (m_handler.bFinishLineTriggerSelected && _bCDown)
            {
                race_handler_start_race();
            }
        }
        else
        {
            m_handler.bFinishLineTriggerSelected = false;
        }
    }
    else
    {
        m_handler.bFinishLineTriggerSelected = false;
    }

    /* Check if any pause menu is open (check this early, even during countdown) */
    eMenuState eCurrentMenuState = menu_get_state();
    bool bIsPaused = (eCurrentMenuState == MENU_STATE_PAUSE || eCurrentMenuState == MENU_STATE_PAUSE_SETTINGS || eCurrentMenuState == MENU_STATE_PAUSE_SAVE_CONFIRM ||
                      eCurrentMenuState == MENU_STATE_PAUSE_EXIT_RACE_CONFIRM);

    /* Handle pause timing for active races (even during countdown after GO) */
    if (m_handler.bRaceActive)
    {
        /* Store lap time when menu opens (transition from not paused to paused) */
        if (bIsPaused && !m_handler.bIsPaused)
        {
            float fCurrentTime = (float)get_ticks_ms() / 1000.0f;
            m_handler.fPausedLapTime = fCurrentTime - m_handler.fLapStartTime;
        }
        /* Adjust lap start time when menu closes (transition from paused to not paused) */
        else if (!bIsPaused && m_handler.bIsPaused)
        {
            float fCurrentTime = (float)get_ticks_ms() / 1000.0f;
            /* Adjust start time so that current time - start time = paused lap time */
            /* This prevents the time from jumping ahead when unpausing */
            m_handler.fLapStartTime = fCurrentTime - m_handler.fPausedLapTime;
        }
        m_handler.bIsPaused = bIsPaused;
    }

    /* Handle race start sequence */
    if (m_handler.eStartState != RACE_START_NONE && m_handler.eStartState != RACE_START_RACING)
    {
        switch (m_handler.eStartState)
        {
        case RACE_START_FADE_TO_BLACK:
            /* Wait until fade is complete */
            if (!fade_manager_is_busy())
            {
                /* Fade complete and screen is fully black, teleport UFO */
                struct vec2 vFinishPos, vFinishTangent;
                if (race_track_get_position_for_progress(0.0f, &vFinishPos, &vFinishTangent))
                {
                    ufo_set_position(vFinishPos);

                    /* Snap camera and sync starfield to prevent visual jumps */
                    gp_state_snap_space_transition();

                    /* Set UFO rotation directly to face race direction */
                    /* Angle convention: UP = 0°, RIGHT = 90°, DOWN = 180°, LEFT = 270° */
                    /* Use atan2(tangent.fX, -tangent.fY) to match UFO's angle calculation */
                    float fAngleRad = fm_atan2f(vFinishTangent.fX, -vFinishTangent.fY);
                    ufo_set_angle_rad(fAngleRad);

                    /* Set velocity direction for movement */
                    struct vec2 vVel = vec2_scale(vFinishTangent, 0.1f);
                    ufo_set_velocity(vVel);
                }
                m_handler.eStartState = RACE_START_FADE_FROM_BLACK;
                fade_manager_start(FROM_BLACK);
                /* Enable collision and place coin when screen is black (during fade-from-black) */
                place_coin_at_index(m_handler.uActiveCoinIndex);
                race_track_set_collision_enabled(true);
            }
            break;

        case RACE_START_FADE_FROM_BLACK:
            if (!fade_manager_is_busy())
            {
                /* Fade in complete, start countdown */
                m_handler.eStartState = RACE_START_COUNTDOWN;
                m_handler.fCountdownTimer = 0.0f;
                m_handler.iCountdownIndex = RACE_HANDLER_COUNTDOWN_TOTAL - 1; /* Start at 3 */
                /* Play countdown sound for "3" */
                if (s_pCountdownSound)
                {
                    float fFreq = AUDIO_BITRATE * 0.5f; /* 50% frequency for "3" */
                    wav64_play(s_pCountdownSound, MIXER_CHANNEL_ITEMS);
                    mixer_ch_set_freq(MIXER_CHANNEL_ITEMS, fFreq);
                }
            }
            break;

        case RACE_START_COUNTDOWN:
            /* Only advance countdown timer if not paused */
            if (!bIsPaused)
            {
                m_handler.fCountdownTimer += frame_time_delta_seconds();
            }
            if (m_handler.fCountdownTimer >= RACE_HANDLER_COUNTDOWN_DURATION)
            {
                m_handler.fCountdownTimer = 0.0f;
                m_handler.iCountdownIndex--;

                /* Play countdown sound with appropriate frequency */
                if (s_pCountdownSound && m_handler.iCountdownIndex >= 0)
                {
                    float fFreqMult = 1.0f; /* Default: 100% for "1" */

                    if (m_handler.iCountdownIndex == 3)
                        fFreqMult = 0.5f; /* 50% for "3" */
                    else if (m_handler.iCountdownIndex == 2)
                        fFreqMult = 0.7f; /* 70% for "2" */
                    else if (m_handler.iCountdownIndex == 1)
                        fFreqMult = 1.0f; /* 100% for "1" */
                    /* No sound for "GO" (iCountdownIndex == 0) */

                    if (m_handler.iCountdownIndex > 0)
                    {
                        float fFreq = AUDIO_BITRATE * fFreqMult;
                        wav64_play(s_pCountdownSound, MIXER_CHANNEL_ITEMS);
                        mixer_ch_set_freq(MIXER_CHANNEL_ITEMS, fFreq);
                    }
                }

                /* Start race when "GO" appears */
                if (m_handler.iCountdownIndex == 0)
                {
                    /* Reset frequency to normal */
                    mixer_ch_set_freq(MIXER_CHANNEL_ITEMS, AUDIO_BITRATE);
                    /* Instantly start race music on GO */
                    const char *pFolder = gp_state_get_current_folder();
                    if (pFolder)
                    {
                        audio_play_music_instant(MUSIC_RACE, pFolder);
                    }
                    /* Start racing immediately when GO appears */
                    m_handler.bRaceActive = true;
                    m_handler.fLapStartTime = (float)get_ticks_ms() / 1000.0f;
                    gp_state_cutscene_set(false);
                }

                if (m_handler.iCountdownIndex < 0)
                {
                    /* Countdown complete - transition to racing state (race already active) */
                    m_handler.eStartState = RACE_START_RACING;
                }
            }
            break;

        default:
            break;
        }
        return; /* Don't process race logic during start sequence */
    }

    /* Update race logic if active */
    if (!m_handler.bRaceActive)
        return;

    /* Don't update lap timer if any pause menu is open */
    if (bIsPaused)
        return;

    /* Update lap timer */
    float fCurrentTime = (float)get_ticks_ms() / 1000.0f;
    float fCurrentLapTime = fCurrentTime - m_handler.fLapStartTime;

    /* Get UFO position and progress */
    struct vec2 vUfoPos = ufo_get_position();
    float fUfoProgress = race_track_get_progress_for_position(vUfoPos);

    /* Check coin collection */
    if (entity2d_is_active(&m_handler.coinEntity) && entity2d_is_collidable(&m_handler.coinEntity))
    {
        const struct entity2D *pUfoEntity = ufo_get_entity();
        uint16_t uCurrentCoinIndex = m_handler.uActiveCoinIndex;

        if (pUfoEntity && entity2d_check_collision_circle(pUfoEntity, &m_handler.coinEntity))
        {
            /* Coin collected */
            ufo_turbo_trigger_burst(m_handler.fCoinTurboBurstDurationMs);
            if (s_pCoinPickupSound)
                wav64_play(s_pCoinPickupSound, MIXER_CHANNEL_ITEMS);
            advance_to_next_coin(true);
            if (uCurrentCoinIndex == 0)
                complete_lap_or_race(fCurrentLapTime);
        }
        else
        {
            /* Check if coin was missed - check if UFO has passed the coin */
            float fCoinProgress = m_handler.aCoinProgress[uCurrentCoinIndex];
            float fTrackLength = race_track_get_total_length();

            /* dist_fwd(UFO, Coin) = forward distance from UFO to coin */
            /* If coin is ahead: this will be small (< track_length/2) */
            /* If UFO has passed: this will be large (>= track_length/2, wrapping around) */
            float fUfoToCoin = dist_fwd(fUfoProgress, fCoinProgress, fTrackLength);

            /* Only check for missed if UFO has actually passed the coin */
            /* UFO has passed if the forward distance from UFO to coin is >= track_length/2 */
            if (fUfoToCoin >= (fTrackLength * 0.5f))
            {
                /* UFO has passed the coin - calculate how far past */
                /* Amount passed = track_length - fUfoToCoin */
                float fAmountPassed = fTrackLength - fUfoToCoin;

                if (fAmountPassed >= RACE_HANDLER_MISS_DISTANCE)
                {
                    /* Coin missed - UFO has passed it by MISS_DISTANCE or more */
                    advance_to_next_coin(false);
                    if (uCurrentCoinIndex == 0)
                        complete_lap_or_race(fCurrentLapTime);
                }
            }
        }
    }
}

static void render_coin_slots(void)
{
    if (!m_handler.bInitialized)
        return;

    struct vec2i vCenter = ui_get_pos_bottom_center(0, 0);

    /* Calculate total width: slots * 6px + gaps * 3px */
    const int iSlotSize = 6;
    const int iSlotPadding = 3;
    int iTotalWidth = (int)m_handler.uCoinsPerLap * iSlotSize + ((int)m_handler.uCoinsPerLap - 1) * iSlotPadding;
    int iStartX = vCenter.iX - (iTotalWidth / 2);

    /* Set up rendering mode and upload texture once */
    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_mode_alphacompare(1);
    rdpq_sprite_upload(TILE0, m_handler.pPickupSprite, &m_handler.pickupTexParms);

    /* Render coin slots using texture subrects */
    /* Display order: coins 1, 2, ..., N-1, then finish coin 0 */
    for (uint16_t i = 0; i < m_handler.uCoinsPerLap; ++i)
    {
        int iX = iStartX + (int)i * (iSlotSize + iSlotPadding);
        int iY = vCenter.iY - (iSlotSize / 2) - UI_DESIGNER_PADDING;

        /* Map display slot to coin index: slot 0 = coin 1, slot 1 = coin 2, ..., slot N-1 = coin 0 */
        uint16_t uCoinIndex = (i < m_handler.uCoinsPerLap - 1) ? (i + 1) : 0;

        /* Determine which subrect to use based on coin state */
        /* Texture is 18x6px: normal (0-6), missed (6-12), collected (12-18) */
        float fTexX0 = 0.0f; /* Default: normal state */
        float fTexX1 = 6.0f;
        if (m_handler.aCoinStates[uCoinIndex] == COIN_STATE_COLLECTED)
        {
            fTexX0 = 12.0f; /* Collected: x: 12-18 */
            fTexX1 = 18.0f;
        }
        else if (m_handler.aCoinStates[uCoinIndex] == COIN_STATE_MISSED)
        {
            fTexX0 = 6.0f; /* Missed: x: 6-12 */
            fTexX1 = 12.0f;
        }

        /* Draw 6x6 subrect from texture */
        rdpq_texture_rectangle_scaled(TILE0, iX, iY, iX + iSlotSize, iY + iSlotSize, fTexX0, 0.0f, fTexX1, 6.0f);
    }
}

static void render_lap_times(void)
{
    if (!m_handler.bInitialized)
        return;

    struct vec2i vPos = ui_get_pos_top_left_text();

    char szTimeBuffer[32];
    int iY = vPos.iY;

    /* Render current lap time: LAP X/Y: time */
    if (m_handler.bRaceActive && m_handler.uCurrentLap > 0)
    {
        float fCurrentLapTime;
        if (m_handler.bIsPaused)
        {
            /* Use stored paused lap time when menu is open */
            fCurrentLapTime = m_handler.fPausedLapTime;
        }
        else
        {
            /* Calculate current lap time normally */
            float fCurrentTime = (float)get_ticks_ms() / 1000.0f;
            fCurrentLapTime = fCurrentTime - m_handler.fLapStartTime;
        }
        format_lap_time(fCurrentLapTime, szTimeBuffer, sizeof(szTimeBuffer));
        rdpq_text_printf(NULL, FONT_NORMAL, vPos.iX, iY, "LAP %d/%d: %s", m_handler.uCurrentLap, m_handler.uMaxLaps, szTimeBuffer);
        iY += UI_FONT_Y_OFFSET;
    }

    /* Find and render best lap time */
    float fBestLapTime = 0.0f;
    bool bHasBestLap = false;
    for (uint16_t i = 0; i < m_handler.uCurrentLap - 1 && i < RACE_HANDLER_MAX_LAPS; ++i)
    {
        if (m_handler.aLapTimes[i] > 0.0f)
        {
            if (!bHasBestLap || m_handler.aLapTimes[i] < fBestLapTime)
            {
                fBestLapTime = m_handler.aLapTimes[i];
                bHasBestLap = true;
            }
        }
    }

    if (bHasBestLap)
    {
        format_lap_time(fBestLapTime, szTimeBuffer, sizeof(szTimeBuffer));
        rdpq_text_printf(NULL, FONT_NORMAL, vPos.iX, iY, "BEST: %s", szTimeBuffer);
    }
}

static void render_countdown(void)
{
    if (m_handler.eStartState != RACE_START_COUNTDOWN)
        return;

    const char *pText = (m_handler.iCountdownIndex == 0)   ? "GO!"
                        : (m_handler.iCountdownIndex == 1) ? "1"
                        : (m_handler.iCountdownIndex == 2) ? "2"
                        : (m_handler.iCountdownIndex == 3) ? "3"
                                                           : NULL;
    if (pText)
        rdpq_text_printf(&m_tpCenterBoth, FONT_NORMAL, 0, 0, "%s", pText);
}

void race_handler_render(void)
{
    if (!m_handler.bInitialized || !race_track_is_initialized())
        return;

    /* Render race track */
    race_track_render();

    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    /* Render coin entity if active */
    if (entity2d_is_visible(&m_handler.coinEntity))
    {
        entity2d_render_simple(&m_handler.coinEntity);
    }
}

void race_handler_render_ui(void)
{
    /* Early return if race is not initialized */
    if (!m_handler.bInitialized)
        return;

    /* Don't render race UI during dialogue */
    if (dialogue_is_active())
        return;

    /* Skip UI rendering if minimap is active */
    if (minimap_is_active())
        return;

    const bool bProgressActOk = (gp_state_act_get() >= MIN_PROGRESS_ACT);
    const bool bShowPostRaceBest = (bProgressActOk && (m_handler.fPostRaceUiTimer > 0.0f));
    const bool bShowTriggerUi = (bProgressActOk && m_handler.bFinishLineTriggerSelected && m_handler.eStartState == RACE_START_NONE);

    if (m_handler.eStartState == RACE_START_FADE_FROM_BLACK || m_handler.eStartState == RACE_START_COUNTDOWN || m_handler.eStartState == RACE_START_RACING)
    {
        /* Render UI */
        render_coin_slots();
        render_lap_times();
        render_countdown();
    }
    else if (bShowPostRaceBest)
    {
        /* Show coin slots and lap times for 5 seconds after race finishes */
        render_coin_slots();
        render_lap_times();
    }

    /* Render best/last lap time centered at top:
       - while selecting the finish-line trigger UI
       - OR for a short duration after finishing a race (independent of proximity) */
    if (bShowPostRaceBest || bShowTriggerUi)
    {
        /* Render best lap time centered at top */
        float fBestLapTime = gp_state_get_best_lap_time();
        if (fBestLapTime > 0.0f)
        {
            char szTimeBuffer[32];
            format_lap_time(fBestLapTime, szTimeBuffer, sizeof(szTimeBuffer));
            struct vec2i vTopCenter = ui_get_pos_top_center_text();
            rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, vTopCenter.iY, "BEST: %s", szTimeBuffer);

            /* If we have a best lap time from the most recent run, show it below BEST */
            if (m_handler.bHasLastRunBestLapTime && m_handler.fLastRunBestLapTime > 0.0f)
            {
                format_lap_time(m_handler.fLastRunBestLapTime, szTimeBuffer, sizeof(szTimeBuffer));
                rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, vTopCenter.iY + 4 + UI_FONT_Y_OFFSET, "LAST: %s", szTimeBuffer);
            }
        }

        /* Render C-down button above trigger (only while actually showing the trigger UI) */
        if (bShowTriggerUi && m_handler.pBtnCDownSprite)
        {
            struct vec2 vTriggerPos = m_handler.finishLineTriggerEntity.vPos;
            struct vec2i vScreenPos;
            camera_world_to_screen(&g_mainCamera, vTriggerPos, &vScreenPos);

            float fZoom = camera_get_zoom(&g_mainCamera);
            float fTriggerRadius = (float)m_handler.finishLineTriggerEntity.iCollisionRadius;

            int iBtnX = vScreenPos.iX - (m_handler.pBtnCDownSprite->width / 2) + 10;
            int iBtnY = vScreenPos.iY - (int)((fTriggerRadius * fZoom) - m_handler.pBtnCDownSprite->height) + 20;

            rdpq_set_mode_copy(false);
            rdpq_mode_alphacompare(1);
            rdpq_sprite_blit(m_handler.pBtnCDownSprite, iBtnX, iBtnY, NULL);
        }
    }
}

uint16_t race_handler_get_current_lap(void)
{
    return m_handler.uCurrentLap;
}

uint16_t race_handler_get_total_coins_collected(void)
{
    return m_handler.uTotalCoinsCollected;
}

uint16_t race_handler_get_lap_coins_collected(uint16_t _uLap)
{
    if (_uLap == 0 || _uLap > m_handler.uMaxLaps)
        return 0;
    return m_handler.aLapCoinsCollected[_uLap - 1];
}

uint16_t race_handler_get_lap_coins_missed(uint16_t _uLap)
{
    if (_uLap == 0 || _uLap > m_handler.uMaxLaps)
        return 0;
    return m_handler.aLapCoinsMissed[_uLap - 1];
}

float race_handler_get_lap_time(uint16_t _uLap)
{
    if (_uLap == 0 || _uLap > m_handler.uMaxLaps)
        return 0.0f;
    return m_handler.aLapTimes[_uLap - 1];
}

bool race_handler_is_race_active(void)
{
    return m_handler.eStartState != RACE_START_NONE;
}

bool race_handler_is_initialized(void)
{
    return m_handler.bInitialized;
}
