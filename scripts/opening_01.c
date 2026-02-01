#include "../game_objects/gp_state.h"
#include "../gameplay_script.h"
#include <stddef.h>

ScriptInstance *script_opening_01(void)
{
    SCRIPT_BEGIN();
    STEP(SA_SET_MARKER_TO_PIECE, p_set_marker_to_piece(GP_UNLOCK_PIECE_B, true)); // new goal
    WAIT_THEN(SC_PIECE_OBTAINED, p_piece(GP_UNLOCK_PIECE_B), SA_SET_MARKER, p_marker("rhino_shop", MARKER_RHINO, true));

    WAIT(SC_UFO_DISTANCE_NPC, p_distance_npc(NPC_TYPE_RHINO, 100.0f));
    STEP(SA_START_DIALOGUE, p_dialogue("d_opening_02"));
    WAIT_THEN(SC_DIALOGUE_FINISHED, NO_PARAMS, SA_SET_ACT, p_act(ACT_MAIN));
    STEP(SA_SAVE_GAME, NO_PARAMS);
    STEP(SA_START_SCRIPT, p_script("act_master"));

    SCRIPT_END();
}
