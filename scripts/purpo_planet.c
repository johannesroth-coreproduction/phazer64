#include "../frame_time.h"
#include "../game_objects/gp_state.h"
#include "../gameplay_script.h"
#include <stddef.h>

ScriptInstance *script_purpo_planet(void)
{
    SCRIPT_BEGIN();

    // only continue if coming from space
    IF_NOT(SC_GP_STATE_WAS, p_gp_state(SPACE), SA_STOP_SCRIPT, NO_PARAMS);

    /* If not all currency has been collected, start dialogue d_statue_curious */
    IF_NOT(SC_CURRENCY_ALL_COLLECTED, NO_PARAMS, SA_START_DIALOGUE, p_dialogue("d_statue_curious"));

    SCRIPT_END();
}
