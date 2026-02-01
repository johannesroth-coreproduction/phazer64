#include "../game_objects/npc_alien.h"
#include "../game_objects/npc_handler.h"
#include "../game_objects/ufo.h"
#include "../gameplay_script.h"
#include "../path_mover.h"
#include <stddef.h>

ScriptInstance *script_terra_land(void)
{
    SCRIPT_BEGIN();

    /* Check if satellite is repaired */
    /* If condition is false, play terra_00 dialogue and then script will end */
    IF_ELSE(SC_SATELLITE_REPAIRED, NO_PARAMS, SA_START_DIALOGUE, p_dialogue("d_terra_01"), SA_START_DIALOGUE, p_dialogue("d_terra_00"));
    WAIT(SC_DIALOGUE_FINISHED, NO_PARAMS);
    IF_NOT(SC_SATELLITE_REPAIRED, NO_PARAMS, SA_STOP_SCRIPT, NO_PARAMS);

    // FINISH GAME - SATELLITE REPAIRED
    STEP(SA_ENABLE_CUTSCENE, NO_PARAMS);

    STEP(SA_START_ANIM, p_anim(SPACE, PLANET));
    WAIT(SC_ANIM_FINISHED, NO_PARAMS);

    /* Spawn NPC alien */
    STEP(SA_SPAWN_NPC, p_npc(NPC_TYPE_ALIEN));

    /* Execute path green_alien_approach */
    STEP(SA_EXECUTE_PATH, p_path_exec("green_alien_approach", NPC_TYPE_ALIEN, NULL, false));

    /* Wait for path to be reached */
    WAIT(SC_NPC_TARGET_REACHED, p_path_reached(NPC_TYPE_ALIEN));

    /* Free the path */
    STEP(SA_FREE_PATH, p_path_reached(NPC_TYPE_ALIEN));

    STEP(SA_START_ANIM, p_anim(PLANET, SPACE));
    WAIT(SC_ANIM_FINISHED, NO_PARAMS);
    STEP(SA_END_ANIM, p_anim(PLANET, SPACE));

    // we stay in cutscene mode STEP(SA_DISABLE_CUTSCENE, NO_PARAMS);

    /* Play terra_01_b dialogue */
    STEP(SA_START_DIALOGUE, p_dialogue("d_terra_01_b"));

    /* Wait for terra_01_b dialogue to finish, fade, finish game */
    WAIT_THEN(SC_DIALOGUE_FINISHED, NO_PARAMS, SA_FADE_TO_BLACK, NO_PARAMS);
    WAIT(SC_FADE_FINISHED, NO_PARAMS);
    WAIT(SC_TIMER, p_timer(1.5f));
    STEP(SA_FINISH_GAME, NO_PARAMS);

    SCRIPT_END();
}
