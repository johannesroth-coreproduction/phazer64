#include "../gameplay_script.h"
#include <stddef.h>

ScriptInstance *script_mine(void)
{
    SCRIPT_BEGIN();

    /* If player already has bullets, stop the script */
    IF(SC_BULLETS_UNLOCKED, NO_PARAMS, SA_STOP_SCRIPT, NO_PARAMS);

    /* PLANET mode: delegate to mine_planet script */
    IF_ELSE(SC_GP_STATE_IS, p_gp_state(PLANET), SA_START_SCRIPT, p_script("mine_planet"), SA_START_SCRIPT, p_script("mine_surface"));

    /* Wait for dialogue to finish */
    WAIT(SC_DIALOGUE_FINISHED, NO_PARAMS);

    SCRIPT_END();
}
