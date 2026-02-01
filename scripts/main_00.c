#include "../audio.h"
#include "../game_objects/gp_state.h"
#include "../game_objects/npc_alien.h"
#include "../gameplay_script.h"
#include <stddef.h>

ScriptInstance *script_main_00(void)
{
    SCRIPT_BEGIN();

    STEP(SA_CLEAR_MARKER, p_marker("gold_mine", MARKER_TARGET, false));
    IF_NOT(SC_RACE_WARMED_UP, NO_PARAMS, SA_WARMUP_RACE_TRACK, p_race_warmup("race", 20, 500.0f, 1));
    STEP(SA_START_SCRIPT_PARALLEL, p_script("race"));
    /* Only spawn rhino if not already spawned */
    IF_NOT(SC_NPC_SPAWNED, p_npc(NPC_TYPE_RHINO), SA_SPAWN_NPC, p_npc(NPC_TYPE_RHINO));
    /* Only execute path if not already active */
    IF_NOT(SC_PATH_ACTIVE, p_path_reached(NPC_TYPE_RHINO), SA_EXECUTE_PATH, p_path_exec("rhino_at_shop", NPC_TYPE_RHINO, NULL, false));
    /* Set markers: always set rhino_shop */
    STEP(SA_SET_MARKER, p_marker("rhino_shop", MARKER_RHINO, false));

    /* Create PIECE D at POI "piece_d" */
    STEP(SA_CREATE_PIECE_AT_POI, p_create_piece_at_poi("piece_d", GP_UNLOCK_PIECE_D));
    STEP(SA_SET_MARKER_TO_PIECE, p_set_marker_to_piece(GP_UNLOCK_PIECE_D, true));

    /* Create PIECE C at POI "piece_c" */
    STEP(SA_CREATE_PIECE_AT_POI, p_create_piece_at_poi("piece_c", GP_UNLOCK_PIECE_C));
    STEP(SA_SET_MARKER_TO_PIECE, p_set_marker_to_piece(GP_UNLOCK_PIECE_C, true));

    /* Wait until PIECE C is collected */
    WAIT(SC_PIECE_OBTAINED, p_piece(GP_UNLOCK_PIECE_C));
    /* Wait until PIECE D is collected */
    WAIT(SC_PIECE_OBTAINED, p_piece(GP_UNLOCK_PIECE_D));

    STEP(SA_SET_MARKER, p_marker("rhino_shop", MARKER_RHINO, true));

    // player needs to be near
    WAIT(SC_UFO_DISTANCE_NPC, p_distance_npc(NPC_TYPE_RHINO, 100.0f));

    /* Play dialogue main_pieces_collected_00 */
    STEP(SA_START_DIALOGUE, p_dialogue("d_main_pieces_collected_00"));

    /* Wait for dialogue to finish */
    WAIT(SC_DIALOGUE_FINISHED, NO_PARAMS);

    /* Unlock tractor beam flag in gp state */
    STEP(SA_FADE_TO_BLACK, NO_PARAMS);
    WAIT_THEN(SC_FADE_FINISHED, NO_PARAMS, SA_SET_SAVE_FLAG, p_flag(GP_UNLOCK_TRACTOR_BEAM));
    STEP(SA_PLAY_SOUND, p_sound("rom:/crankhorn_installed.wav64", MIXER_CHANNEL_USER_INTERFACE));
    WAIT_THEN(SC_SOUND_FINISHED, p_sound("rom:/crankhorn_installed.wav64", MIXER_CHANNEL_USER_INTERFACE), SA_FADE_FROM_BLACK, NO_PARAMS);
    WAIT(SC_FADE_FINISHED, NO_PARAMS);

    STEP(SA_START_DIALOGUE, p_dialogue("d_main_pieces_collected_01"));

    /* Set act to final */
    WAIT_THEN(SC_DIALOGUE_FINISHED, NO_PARAMS, SA_SET_ACT, p_act(ACT_FINAL));

    /* Save game state */
    STEP(SA_SAVE_GAME, NO_PARAMS);

    /* Start act_master script */
    STEP(SA_START_SCRIPT, p_script("act_master"));

    SCRIPT_END();
}
