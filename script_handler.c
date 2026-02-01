#include "script_handler.h"
#include "gameplay_script.h"
#include "libdragon.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Script registration structure */
typedef struct
{
    const char *name;
    ScriptInstance *(*creator)(void);
} script_registry_entry_t;

/* Script registry macro */
#define SCRIPT_REGISTER(_name, _func) {.name = _name, .creator = _func}

/* Include auto-generated script registry */
#include "build/scripts_registry.inc"

/* Active script instances */
typedef struct
{
    ScriptInstance *script;
    const char *name;
} active_script_entry_t;

#define SCRIPT_HANDLER_MAX_ACTIVE SCRIPT_REGISTRY_COUNT

static active_script_entry_t s_activeScripts[SCRIPT_HANDLER_MAX_ACTIVE];
static size_t s_activeScriptCount = 0;
static uint32_t s_scriptGeneration = 0;

#ifdef DEV_BUILD
/* Debug logging state */
static bool s_scriptDebugEnabled = false;
static uint32_t s_scriptDebugFrame = 0;
static uint32_t s_scriptDebugFrameEvent = 0;
#endif

/* Get script instance by name */
static ScriptInstance *script_handler_get_script(const char *name)
{
    if (!name)
        return NULL;

    for (size_t i = 0; i < SCRIPT_REGISTRY_COUNT; i++)
    {
        if (strcmp(s_scriptRegistry[i].name, name) == 0)
        {
            return s_scriptRegistry[i].creator();
        }
    }

    return NULL;
}

#ifdef DEV_BUILD
void script_handler_set_debug(bool enabled)
{
    s_scriptDebugEnabled = enabled;
}

void script_handler_debug_log(const char *script_name, const ScriptInstance *script, const char *stage, const char *fmt, ...)
{
    if (!s_scriptDebugEnabled)
        return;

    char message[192];

    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    uint16_t step = script ? script->current_step : 0;

    debugf("[S%06lu.%02lu] %s #%02u %s%s\n",
           (unsigned long)s_scriptDebugFrame,
           (unsigned long)s_scriptDebugFrameEvent++,
           script_name ? script_name : "?",
           (unsigned int)step,
           stage ? stage : "",
           message);
}
#endif

void script_handler_init(void)
{
    s_activeScriptCount = 0;
    s_scriptGeneration = 0;
}

void script_handler_start(const char *name, bool stop_others)
{
    if (!name)
        return;

    debugf("[SCRIPT] script_handler_start: Starting script '%s'%s\n", name, stop_others ? "" : " (parallel)");

#ifdef DEBUG_SCRIPTS
    if (s_activeScriptCount > 0)
    {
        if (stop_others)
        {
            if (s_scriptDebugEnabled)
            {
                debugf("[WARNING] script_handler_start: Stopping %d active script(s) to start '%s'\n", (int)s_activeScriptCount, name);
            }
        }
        else
        {
            debugf("[SCRIPT] script_handler_start: Starting script '%s' in parallel (active scripts: %d)\n", name, (int)s_activeScriptCount);
        }
    }
#endif

    if (stop_others)
    {
        /* Stop any existing scripts */
        script_handler_stop();
    }

    /* Get and start the new script */
    ScriptInstance *pScript = script_handler_get_script(name);
    if (pScript)
    {
        if (s_activeScriptCount >= SCRIPT_HANDLER_MAX_ACTIVE)
        {
            debugf("[ERROR] script_handler_start: Max active scripts reached (%d)\n", (int)SCRIPT_HANDLER_MAX_ACTIVE);
            script_destroy(pScript);
            return;
        }

        s_activeScripts[s_activeScriptCount].script = pScript;
        s_activeScripts[s_activeScriptCount].name = name;

#ifdef DEV_BUILD
        /* Store debug name inside the instance for more detailed logs */
        pScript->debug_name = name;
#endif

        s_activeScriptCount++;
        script_start(pScript);
    }
    else
    {
        debugf("[ERROR] script_handler_start: Script '%s' not found\n", name);
    }
}

void script_handler_stop(void)
{
    if (s_activeScriptCount == 0)
        return;

    for (size_t i = 0; i < s_activeScriptCount; i++)
    {
        if (s_activeScripts[i].script)
        {
            script_stop(s_activeScripts[i].script);
            script_destroy(s_activeScripts[i].script);
        }
        s_activeScripts[i].script = NULL;
        s_activeScripts[i].name = NULL;
    }

    s_activeScriptCount = 0;
    s_scriptGeneration++;
}

void script_handler_update(void)
{
#ifdef DEV_BUILD
    /* Advance frame counter for script debug logging. This is called once per game frame. */
    if (s_scriptDebugEnabled)
    {
        s_scriptDebugFrame++;
        s_scriptDebugFrameEvent = 0;
    }
#endif

    size_t i = 0;
    while (i < s_activeScriptCount)
    {
        ScriptInstance *pScript = s_activeScripts[i].script;
        if (!pScript)
        {
            if (i + 1 < s_activeScriptCount)
            {
                memmove(&s_activeScripts[i], &s_activeScripts[i + 1], (s_activeScriptCount - i - 1) * sizeof(s_activeScripts[0]));
            }
            s_activeScriptCount--;
            continue;
        }

        if (script_is_active(pScript))
        {
            uint32_t generation_before = s_scriptGeneration;
            script_update(pScript);
            if (generation_before != s_scriptGeneration)
            {
                return;
            }
        }

        if (!script_is_active(pScript))
        {
#ifdef DEV_BUILD
            if (s_scriptDebugEnabled)
            {
                script_handler_debug_log(s_activeScripts[i].name, pScript, "DONE ", "finished");
            }
#endif

            script_stop(pScript);
            script_destroy(pScript);
            if (i + 1 < s_activeScriptCount)
            {
                memmove(&s_activeScripts[i], &s_activeScripts[i + 1], (s_activeScriptCount - i - 1) * sizeof(s_activeScripts[0]));
            }
            s_activeScriptCount--;
            continue;
        }

        i++;
    }
}

bool script_handler_is_active(void)
{
    for (size_t i = 0; i < s_activeScriptCount; i++)
    {
        if (s_activeScripts[i].script && script_is_active(s_activeScripts[i].script))
            return true;
    }
    return false;
}

void script_handler_free(void)
{
    script_handler_stop();
}

uint32_t script_handler_get_generation(void)
{
    return s_scriptGeneration;
}
