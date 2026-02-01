#include "../audio.h"
#include "../game_objects/gp_state.h"
#include "../gameplay_script.h"
#include <stddef.h>

ScriptInstance *script_opening_00(void)
{
    SCRIPT_BEGIN();

    // OPENING SETUP
    IF_NOT(SC_RACE_WARMED_UP, NO_PARAMS, SA_WARMUP_RACE_TRACK, p_race_warmup("race", 20, 500.0f, 1));

    /* Only spawn rhino if not already spawned */
    IF_NOT(SC_NPC_SPAWNED, p_npc(NPC_TYPE_RHINO), SA_SPAWN_NPC, p_npc(NPC_TYPE_RHINO));

    /* Only execute path if not already active */
    IF_NOT(SC_PATH_ACTIVE, p_path_reached(NPC_TYPE_RHINO), SA_EXECUTE_PATH, p_path_exec("rhino_at_shop", NPC_TYPE_RHINO, NULL, false));

    /* Set markers: always set rhino_shop and piece_b, conditionally set gold_mine only if currency <= 0 */
    STEP(SA_SET_MARKER, p_marker("rhino_shop", MARKER_RHINO, true));
    STEP(SA_SET_MARKER_TO_PIECE, p_set_marker_to_piece(GP_UNLOCK_PIECE_B, false));
    IF(SC_CURRENCY_LE, p_currency_threshold(0), SA_SET_MARKER, p_marker("gold_mine", MARKER_TARGET, true));

    /* run this script if we have weapons */
    IF_ELSE(SC_BULLETS_UNLOCKED, NO_PARAMS, SA_START_SCRIPT, p_script("opening_01"), SA_SKIP, NO_PARAMS);

    /* part of the script thats runs if we have NO WEAPONS */
    IF(SC_CURRENCY_LE, p_currency_threshold(0), SA_STOP_SCRIPT, NO_PARAMS);

    // ... and ONE NUGGET
    STEP(SA_CLEAR_MARKER, p_marker("gold_mine", MARKER_TARGET, false));
    WAIT(SC_UFO_DISTANCE_NPC, p_distance_npc(NPC_TYPE_RHINO, 100.0f));
    STEP(SA_START_DIALOGUE, p_dialogue("d_opening_00"));

    WAIT(SC_DIALOGUE_FINISHED, NO_PARAMS);

    STEP(SA_FADE_TO_BLACK, NO_PARAMS);
    WAIT(SC_FADE_FINISHED, NO_PARAMS);
    STEP(SA_CHANGE_CURRENCY, p_currency_delta(-1));
    STEP(SA_SET_SAVE_FLAG, p_flag(GP_UNLOCK_BULLETS_NORMAL));
    STEP(SA_PLAY_SOUND, p_sound("rom:/crankhorn_installed.wav64", MIXER_CHANNEL_USER_INTERFACE));
    WAIT_THEN(SC_SOUND_FINISHED, p_sound("rom:/crankhorn_installed.wav64", MIXER_CHANNEL_USER_INTERFACE), SA_FADE_FROM_BLACK, NO_PARAMS);

    WAIT_THEN(SC_FADE_FINISHED, NO_PARAMS, SA_START_DIALOGUE, p_dialogue("d_opening_01"));
    /* Save game state */
    STEP(SA_SAVE_GAME, NO_PARAMS);

    STEP(SA_START_SCRIPT, p_script("opening_01"));

    SCRIPT_END();
}
