#include "../gameplay_script.h"
#include <stddef.h>

ScriptInstance *script_mine_planet(void)
{
    SCRIPT_BEGIN();

    IF_ELSE(SC_CURRENCY_LE, p_currency_threshold(0), SA_START_DIALOGUE, p_dialogue("d_mine_00"), SA_START_DIALOGUE, p_dialogue("d_mine_01"));

    WAIT(SC_DIALOGUE_FINISHED, NO_PARAMS);

    SCRIPT_END();
}
