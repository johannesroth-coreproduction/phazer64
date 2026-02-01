#include "../game_objects/gp_state.h"
#include "../game_objects/ufo.h"
#include "../gameplay_script.h"
#include "../math2d.h"
#include "../minimap_marker.h"
#include <stdbool.h>
#include <stddef.h>

/* Callback to check if player has reached the satellite_repair POI */
static int check_poi_reached_callback(void *user_data)
{
    const char *poi_name = (const char *)user_data;
    if (!poi_name)
        return 0;

    const struct entity2D *pMarker = minimap_marker_get_entity_by_name(poi_name);
    if (!pMarker || !entity2d_is_active(pMarker))
        return 0;

    struct vec2 vUfoPos = ufo_get_position();
    float fDistance = vec2_dist(vUfoPos, pMarker->vPos);

    /* Return 1 if within 50 units (reasonable arrival distance) */
    return (fDistance <= 60.0f) ? 1 : 0;
}

ScriptInstance *script_final_00(void)
{
    SCRIPT_BEGIN();

    IF_NOT(SC_RACE_WARMED_UP, NO_PARAMS, SA_WARMUP_RACE_TRACK, p_race_warmup("race", 20, 500.0f, 1));
    STEP(SA_START_SCRIPT_PARALLEL, p_script("race"));
    /* Only spawn rhino if not already spawned */
    IF_NOT(SC_NPC_SPAWNED, p_npc(NPC_TYPE_RHINO), SA_SPAWN_NPC, p_npc(NPC_TYPE_RHINO));
    /* Only execute path if not already active */
    IF_NOT(SC_PATH_ACTIVE, p_path_reached(NPC_TYPE_RHINO), SA_EXECUTE_PATH, p_path_exec("rhino_at_shop", NPC_TYPE_RHINO, NULL, false));
    /* Set markers: always set rhino_shop */
    STEP(SA_SET_MARKER, p_marker("rhino_shop", MARKER_RHINO, false));

    /* Set marker target to satellite_repair poi */
    STEP(SA_SET_MARKER, p_marker("satellite_repair", MARKER_TARGET, true));

    /* Wait until player reaches the POI */
    WAIT(SC_CUSTOM, ((script_param_t){.callback_param = {.callback = (void (*)(void *))check_poi_reached_callback, .user_data = (void *)"satellite_repair"}}));

    STEP(SA_ENABLE_CUTSCENE, NO_PARAMS);
    STEP(SA_FADE_TO_BLACK, NO_PARAMS);

    WAIT(SC_FADE_FINISHED, NO_PARAMS);

    STEP(SA_SPAWN_ASSEMBLE_PIECES, NO_PARAMS);
    STEP(SA_SET_MARKER_TO_PIECE, p_set_marker_to_piece(GP_UNLOCK_PIECE_D, false));
    STEP(SA_SET_MARKER_TO_PIECE, p_set_marker_to_piece(GP_UNLOCK_PIECE_C, false));
    STEP(SA_SET_MARKER_TO_PIECE, p_set_marker_to_piece(GP_UNLOCK_PIECE_B, false));
    STEP(SA_SET_MARKER_TO_PIECE, p_set_marker_to_piece(GP_UNLOCK_PIECE_A, false));

    STEP(SA_FADE_FROM_BLACK, NO_PARAMS);
    STEP(SA_DISABLE_CUTSCENE, NO_PARAMS);

    WAIT_THEN(SC_SATELLITE_REPAIRED, NO_PARAMS, SA_START_DIALOGUE, p_dialogue("d_final_repaired_00"));

    STEP(SA_SET_MARKER, p_marker("terra", MARKER_TARGET, true));

    SCRIPT_END();
}
