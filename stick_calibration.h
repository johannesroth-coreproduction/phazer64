#pragma once

#include "libdragon.h"
#include <stdbool.h>

void stick_calibration_init(void);
void stick_calibration_init_without_menu(void);
void stick_calibration_close(void);
void stick_calibration_update(const joypad_inputs_t *inputs);
void stick_calibration_render(void);
bool stick_calibration_is_active_without_menu(void);
