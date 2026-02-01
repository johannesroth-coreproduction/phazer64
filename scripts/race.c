#include "../audio.h"
#include "../game_objects/gp_state.h"
#include "../game_objects/race_handler.h"
#include "../gameplay_script.h"
#include <stddef.h>

/* Custom callback: returns true if we should skip race wait (best lap < 45s already OR race finished) */
static int should_skip_race_wait(void *user_data)
{
    float fBestLapTime = gp_state_get_best_lap_time();
    bool bRaceFinished = race_handler_was_started_and_finished();
    /* Skip if best lap < 45s (from save) OR race already finished */
    return (fBestLapTime > 0.0f && fBestLapTime <= 45.0f) || bRaceFinished ? 1 : 0;
}

/* Helper to create custom callback parameter */
static script_param_t p_custom_callback(int (*callback)(void *), void *user_data)
{
    script_param_t param = {0};
    param.callback_param.callback = (void (*)(void *))callback;
    param.callback_param.user_data = user_data;
    return param;
}

ScriptInstance *script_race(void)
{
    SCRIPT_BEGIN();

    /* If gp turbo flag is unlocked, stop */
    IF(SC_SAVE_FLAG_SET, p_flag(GP_UNLOCK_TURBO), SA_STOP_SCRIPT, NO_PARAMS);

    /* Early check: did the user finish below 45s before this script was started already? */
    /* If yes, play early dialogue, then skip race wait and go straight to win sequence */
    IF(SC_RACE_TIME_LE, p_timer(45.0f), SA_START_DIALOGUE, p_dialogue("d_race_won_00_early"));
    WAIT(SC_DIALOGUE_FINISHED, NO_PARAMS);

    /* Reset the flag so we can detect the next race finish */
    STEP(SA_RESET_RACE_FINISHED, NO_PARAMS);

    /* Wait for race to be started and finished, OR skip if best lap < 45s already (from save) */
    /* Use custom callback to check: skip if best lap < 45s OR race finished */
    WAIT(SC_CUSTOM, p_custom_callback(should_skip_race_wait, NULL));

    /* Check result - if best lap time <= 45 seconds, it's WON */
    /* If best lap time > 45 seconds, it's LOST - play lost dialogue and rerun script */
    /* Use IF_ELSE to properly branch: won path vs lost path */
    IF_ELSE(SC_RACE_TIME_LE, p_timer(45.0f), SA_START_DIALOGUE, p_dialogue("d_race_won_00"), SA_START_DIALOGUE, p_dialogue("d_race_lost"));
    WAIT(SC_DIALOGUE_FINISHED, NO_PARAMS);

    /* If lost, start new script instance in parallel and stop this instance immediately */
    /* Use IF_ELSE to ensure we only restart if lost, and stop this instance */
    IF_NOT(SC_RACE_TIME_LE, p_timer(45.0f), SA_START_SCRIPT_PARALLEL, p_script("race"));
    /* Stop this instance if we're in the lost path (condition is false) */
    IF_NOT(SC_RACE_TIME_LE, p_timer(45.0f), SA_STOP_SCRIPT, NO_PARAMS);

    /* Win-fade-unlock sequence (shared for both early and normal win) */
    STEP(SA_FADE_TO_BLACK, NO_PARAMS);
    WAIT_THEN(SC_FADE_FINISHED, NO_PARAMS, SA_SET_SAVE_FLAG, p_flag(GP_UNLOCK_TURBO));
    STEP(SA_PLAY_SOUND, p_sound("rom:/crankhorn_installed.wav64", MIXER_CHANNEL_USER_INTERFACE));
    WAIT_THEN(SC_SOUND_FINISHED, p_sound("rom:/crankhorn_installed.wav64", MIXER_CHANNEL_USER_INTERFACE), SA_FADE_FROM_BLACK, NO_PARAMS);
    WAIT_THEN(SC_FADE_FINISHED, NO_PARAMS, SA_START_DIALOGUE, p_dialogue("d_race_won_01"));
    WAIT(SC_DIALOGUE_FINISHED, NO_PARAMS);

    /* Script stops here after win sequence (only reruns if lost, which stops earlier) */
    STEP(SA_STOP_SCRIPT, NO_PARAMS);

    SCRIPT_END();
}