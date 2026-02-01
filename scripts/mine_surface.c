#include "../gameplay_script.h"
#include <stddef.h>

ScriptInstance *script_mine_surface(void)
{
    SCRIPT_BEGIN();

    IF_ELSE(SC_GP_STATE_WAS, p_gp_state(JNR), SA_SKIP, NO_PARAMS, SA_STOP_SCRIPT, NO_PARAMS);

    IF(SC_CURRENCY_GE, p_currency_threshold(1), SA_START_DIALOGUE, p_dialogue("d_mine_surf_00"));
    IF(SC_CURRENCY_LE, p_currency_threshold(0), SA_START_DIALOGUE, p_dialogue("d_mine_surf_00_b"));

    WAIT(SC_DIALOGUE_FINISHED, NO_PARAMS);

    SCRIPT_END();
}
