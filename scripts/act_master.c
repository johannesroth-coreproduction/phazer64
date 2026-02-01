#include "../game_objects/gp_state.h"
#include "../gameplay_script.h"
#include "../script_handler.h"
#include <stddef.h>

/* Callback to start script based on act */
static int act_master_callback(void *user_data)
{
    gp_act_t act = gp_state_act_get();
    const char *script_name = NULL;

    switch (act)
    {
    case ACT_INTRO:
        script_name = "intro_sequence";
        break;
    case ACT_INTRO_RACE:
        script_name = "intro_race";
        break;
    case ACT_OPENING:
        script_name = "opening_00";
        break;
    case ACT_MAIN:
        script_name = "main_00";
        break;
    case ACT_FINAL:
        script_name = "final_00";
        break;
    default:
        debugf("Act not handled: %d\n", act);
        return 0; /* Act not handled, don't start script */
    }

    if (script_name)
    {
        script_handler_start(script_name, true);
        return 1; /* Script started */
    }

    return 0;
}

ScriptInstance *script_act_master(void)
{
    SCRIPT_BEGIN();

    /* Check act and execute appropriate script using custom callback */
    WAIT(SC_CUSTOM, ((script_param_t){.callback_param = {.callback = (void (*)(void *))act_master_callback, .user_data = NULL}}));

    SCRIPT_END();
}
