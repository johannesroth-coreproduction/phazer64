#pragma once

#include <stdbool.h>

#include "../camera.h"

/* Initialize assets used by the laser. */
void laser_init(void);

/* Free laser resources */
void laser_free(void);

/* Update laser state and apply damage to targets. */
void laser_update(bool _bLaserPressed);

/* Render the laser beam from the UFO to the first impact point (if active). */
void laser_render(void);

/* Render the overheat meter UI */
void laser_render_overheat_meter(void);

/* Check if laser is currently firing (button is down) */
bool laser_is_firing(void);

/* Get current overheat level (0.0 to 1.0) */
float laser_get_overheat_level(void);

/* Check if laser is in overheat penalty state */
bool laser_is_overheated(void);