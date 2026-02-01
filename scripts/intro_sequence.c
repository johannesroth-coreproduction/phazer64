#include "../game_objects/npc_alien.h"
#include "../game_objects/npc_handler.h"
#include "../game_objects/ufo.h"
#include "../gameplay_script.h"
#include "../path_mover.h"
#include <stddef.h>

ScriptInstance *script_intro_sequence(void)
{
    SCRIPT_BEGIN();

    /* Set UFO spawn position from space folder's logic.csv and reset camera/starfield */
    STEP(SA_SET_SPAWN, p_spawn("space"));

    /* Spawn NPC at the start */
    STEP(SA_SPAWN_NPC, p_npc(NPC_TYPE_ALIEN));
    STEP(SA_SPAWN_NPC, p_npc(NPC_TYPE_RHINO));

    /* Start rhino idle path immediately (auto-configured by NPC type) */
    STEP(SA_EXECUTE_PATH, p_path_exec("rhino_idle", NPC_TYPE_RHINO, NULL, false));

    /* Trigger UFO launch animation (as if coming from planet, but we started in space) */
    STEP(SA_START_ANIM, p_anim(PLANET, SPACE));

    /* Enable cutscene mode */
    STEP(SA_ENABLE_CUTSCENE, NO_PARAMS);

    /* Wait for fade done (if any) */
    WAIT(SC_FADE_FINISHED, NO_PARAMS);

    /* Wait for launch animation, then end it */
    WAIT_THEN(SC_ANIM_FINISHED, NO_PARAMS, SA_END_ANIM, p_anim(PLANET, SPACE));

    /* Execute approach path (load, configure, start) - auto-configured by NPC type */
    STEP(SA_EXECUTE_PATH, p_path_exec("green_alien_approach", NPC_TYPE_ALIEN, NULL, false));

    /* Wait for path to be reached, then free it and start dialogue */
    WAIT_THEN(SC_NPC_TARGET_REACHED, p_path_reached(NPC_TYPE_ALIEN), SA_FREE_PATH, p_path_reached(NPC_TYPE_ALIEN));

    STEP(SA_START_DIALOGUE, p_dialogue("d_intro_00"));

    /* Execute main path (load, configure, start), then set target - auto-configured by NPC type */
    WAIT_THEN(SC_DIALOGUE_FINISHED, NO_PARAMS, SA_EXECUTE_PATH, p_path_exec("green_alien_to_rhino", NPC_TYPE_ALIEN, NULL, true));

    /* Set target to alien entity (retrieved at execution time) */
    STEP(SA_SET_TARGET_NPC, p_npc(NPC_TYPE_ALIEN));

    STEP(SA_DISABLE_CUTSCENE, NO_PARAMS);

    /* Wait for path to be reached, then check if player is close */
    WAIT(SC_NPC_TARGET_REACHED, p_path_reached(NPC_TYPE_ALIEN));

    STEP(SA_FREE_PATH, p_path_reached(NPC_TYPE_ALIEN));

    /* Wait for player to be close to alien NPC (distance 80) */
    WAIT(SC_UFO_DISTANCE_NPC, p_distance_npc(NPC_TYPE_ALIEN, 80.0f));

    STEP(SA_ENABLE_CUTSCENE, NO_PARAMS);

    STEP(SA_SET_TARGET, p_entity(NULL));

    /* Start dialogue when player is close */
    STEP(SA_START_DIALOGUE, p_dialogue("d_intro_01"));

    /* Fade to black before calibration */
    WAIT_THEN(SC_DIALOGUE_FINISHED, NO_PARAMS, SA_FADE_TO_BLACK, NO_PARAMS);

    WAIT_THEN(SC_FADE_FINISHED, NO_PARAMS, SA_OPEN_CALIBRATION, NO_PARAMS);

    /* Wait for fade to black to finish, then fade from black */
    STEP(SA_FADE_FROM_BLACK, NO_PARAMS);

    /* Wait for fade from black to finish */
    WAIT(SC_FADE_FINISHED, NO_PARAMS);

    /* Wait 1 second */
    WAIT(SC_TIMER, p_timer(1.0f));

    /* Start dialogue d_intro_02 */
    STEP(SA_START_DIALOGUE, p_dialogue("d_intro_02"));

    WAIT_THEN(SC_DIALOGUE_FINISHED, NO_PARAMS, SA_FADE_TO_BLACK, NO_PARAMS);

    WAIT_THEN(SC_FADE_FINISHED, NO_PARAMS, SA_CLOSE_CALIBRATION, NO_PARAMS);

    /* Wait for fade to black to finish (TRIGGERED BY CALIBRATION SCREEN!), then fade from black */
    STEP(SA_FADE_FROM_BLACK, NO_PARAMS);

    /* Wait for fade from black to finish, then start dialogue d_intro_02_b */
    WAIT_THEN(SC_FADE_FINISHED, NO_PARAMS, SA_START_DIALOGUE, p_dialogue("d_intro_02_b"));

    STEP(SA_DISABLE_CUTSCENE, NO_PARAMS);

    /* Free rhino path at the end (in case it's still active) */
    WAIT_THEN(SC_DIALOGUE_FINISHED, NO_PARAMS, SA_FREE_PATH, p_path_reached(NPC_TYPE_RHINO));

    /* Set rhino direct target to rhino_leave POI */
    STEP(SA_SET_NPC_DIRECT_TARGET, p_npc_direct_target(NPC_TYPE_RHINO, "rhino_leave", false));

    /* Wait 1 second */
    WAIT(SC_TIMER, p_timer(1.0f));

    /* Start dialogue d_intro_03 */
    STEP(SA_START_DIALOGUE, p_dialogue("d_intro_03"));

    WAIT_THEN(SC_DIALOGUE_FINISHED, NO_PARAMS, SA_SET_SAVE_FLAG, p_flag(GP_UNLOCK_MINIMAP));

    /* Only warmup race if not already warmed up */
    IF_NOT(SC_RACE_WARMED_UP, NO_PARAMS, SA_WARMUP_RACE_TRACK, p_race_warmup("race", 20, 500.0f, 1));

    /* Despawn rhino */
    STEP(SA_DESPAWN_NPC, p_npc(NPC_TYPE_RHINO));

    /* Set green_alien direct target to green_alien_leave POI */
    STEP(SA_SET_NPC_DIRECT_TARGET, p_npc_direct_target(NPC_TYPE_ALIEN, "green_alien_leave", false));

    STEP(SA_SET_MARKER, p_marker("rhino_shop", MARKER_RHINO, true));

    /* Set game act to intro_race */
    STEP(SA_SET_ACT, p_act(ACT_INTRO_RACE));

    /* Save game state */
    STEP(SA_SAVE_GAME, NO_PARAMS);

    /* Wait 2 seconds */
    WAIT(SC_TIMER, p_timer(2.0f));

    /* Wait for target to be reached */
    WAIT(SC_NPC_TARGET_REACHED, p_path_reached(NPC_TYPE_ALIEN));

    /* Despawn alien */
    STEP(SA_DESPAWN_NPC, p_npc(NPC_TYPE_ALIEN));

    /* Start act_master script */
    STEP(SA_START_SCRIPT, p_script("act_master"));

    SCRIPT_END();
}
