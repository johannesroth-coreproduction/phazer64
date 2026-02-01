#pragma once

#include "libdragon.h"
#include <stdbool.h>

/* Initialize weapons system (load sprites, etc.) */
void weapons_init(void);

/* Free weapon resources */
void weapons_free(void);

/* Update weapons logic.
   _bFire: trigger pressed?
   _bCycleLeft/_bCycleRight: cycle weapon input */
void weapons_update(bool _bFire, bool _bCycleLeft, bool _bCycleRight);

/* Render active weapons (bullets, lasers, bombs) */
void weapons_render(void);

/* Render weapon UI (icon, etc.) */
void weapons_render_ui(void);

/* Refresh weapons visuals/state after unlock flag changes (icons, bullets, etc.) */
void weapons_refresh_state(void);
/* Check if currently firing (for UFO glow) */
bool weapons_is_firing(void);

typedef enum
{
    WEAPON_BULLETS,
    WEAPON_LASER,
    WEAPON_BOMB,
    WEAPON_COUNT
} weapon_type_t;

/* Get current weapon type */
weapon_type_t weapons_get_current(void);
/* Set current weapon type if unlocked */
void weapons_set_current(weapon_type_t _eType);

/* Get color associated with current weapon */
color_t weapons_get_current_color(void);

/* Check if any weapons are unlocked */
bool weapons_any_unlocked(void);