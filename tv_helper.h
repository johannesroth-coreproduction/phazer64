#pragma once

#include <stdbool.h>

/* Activate PAL60 mode (call vi_set_timing_preset and vi_debug_dump) */
void tv_activate_pal60(void);

/* Revert to standard PAL mode */
void tv_revert_to_pal50(void);
