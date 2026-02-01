#pragma once

#include "libdragon.h"
#include <stdbool.h>

void finish_slideshow_init(void);
void finish_slideshow_close(void);
void finish_slideshow_update(const joypad_inputs_t *inputs);
void finish_slideshow_render(void);
bool finish_slideshow_is_active(void);
