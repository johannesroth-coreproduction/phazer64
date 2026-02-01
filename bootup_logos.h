#pragma once

#include <stdbool.h>

/* Initialize bootup logos system */
void bootup_logos_init(void);

/* Update bootup logos (call every frame) */
void bootup_logos_update(void);

/* Render bootup logos (call in render function) */
void bootup_logos_render(void);

/* Check if bootup sequence is complete */
bool bootup_logos_is_done(void);
