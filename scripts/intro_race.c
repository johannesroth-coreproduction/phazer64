#include "../game_objects/gp_state.h"
#include "../game_objects/npc_handler.h"
#include "../game_objects/race_handler.h"
#include "../gameplay_script.h"
#include "../path_mover.h"
#include <stddef.h>

ScriptInstance *script_intro_race(void)
{
    SCRIPT_BEGIN();

    STEP(SA_SET_MARKER, p_marker("rhino_shop", MARKER_RHINO, true));

    /* Only warmup race if not already warmed up */
    IF_NOT(SC_RACE_WARMED_UP, NO_PARAMS, SA_WARMUP_RACE_TRACK, p_race_warmup("race", 20, 500.0f, 1));

    /* Spawn rhino */
    STEP(SA_SPAWN_NPC, p_npc(NPC_TYPE_RHINO));

    /* Execute path "rhino_at_shop", looping, for rhino (auto-configured by NPC type) */
    STEP(SA_EXECUTE_PATH, p_path_exec("rhino_at_shop", NPC_TYPE_RHINO, NULL, false));

    /* When player is near (80), start dialogue d_intro_race_00 */
    WAIT(SC_UFO_DISTANCE_NPC, p_distance_npc(NPC_TYPE_RHINO, 80.0f));
    STEP(SA_START_DIALOGUE, p_dialogue("d_intro_race_00"));

    /* When dialogue is finished, start race */
    WAIT_THEN(SC_DIALOGUE_FINISHED, NO_PARAMS, SA_START_RACE, NO_PARAMS);

    /* When race is finished, start dialogue d_intro_race_01 */
    WAIT_THEN(SC_RACE_FINISHED, NO_PARAMS, SA_START_DIALOGUE, p_dialogue("d_intro_race_01"));

    /* drop PIECE A, wait for collection */
    WAIT(SC_DIALOGUE_FINISHED, NO_PARAMS);
    STEP(SA_CREATE_PIECE_AT_NPC, p_create_piece_at_npc(NPC_TYPE_RHINO, GP_UNLOCK_PIECE_A));
    STEP(SA_SET_MARKER_TO_PIECE, p_set_marker_to_piece(GP_UNLOCK_PIECE_A, true));

    WAIT(SC_PIECE_OBTAINED, p_piece(GP_UNLOCK_PIECE_A));

    STEP(SA_START_DIALOGUE, p_dialogue("d_intro_race_01_b"));
    /* Set game act to opening */
    WAIT_THEN(SC_DIALOGUE_FINISHED, NO_PARAMS, SA_SET_ACT, p_act(ACT_OPENING));

    /* Save game state */
    STEP(SA_SAVE_GAME, NO_PARAMS);

    /* Start act_master script */
    STEP(SA_START_SCRIPT, p_script("act_master"));

    SCRIPT_END();
}
