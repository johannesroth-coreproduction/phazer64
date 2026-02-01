#pragma once

#include "gameplay_script.h"
#include <stdbool.h>
#include <string.h>

/* Initialize script handler system */
void script_handler_init(void);

/* Start a script by name (stop_others=true stops all other scripts) */
void script_handler_start(const char *name, bool stop_others);

/* Stop all active scripts */
void script_handler_stop(void);

/* Update active scripts (call once per frame) */
void script_handler_update(void);

/* Check if any script is currently active */
bool script_handler_is_active(void);

/* Free script handler resources */
void script_handler_free(void);

/* Monotonic counter incremented when active scripts are invalidated */
uint32_t script_handler_get_generation(void);

#ifdef DEV_BUILD
/* Enable or disable detailed script debug logging at runtime */
void script_handler_set_debug(bool enabled);

/* Internal helper used by gameplay_script to emit structured debug logs */
void script_handler_debug_log(const char *script_name, const ScriptInstance *script, const char *stage, const char *fmt, ...);
#endif
