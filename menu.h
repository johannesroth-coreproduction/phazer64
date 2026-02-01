#pragma once

#include "libdragon.h"
#include <stdbool.h>

/* Menu system states */
typedef enum
{
    MENU_STATE_START_SCREEN,            /* Initial start screen with "PUSH START" */
    MENU_STATE_MAIN_MENU,               /* Main menu (NEW GAME/CONTINUE, SETTINGS, NEWSLETTER) */
    MENU_STATE_MAIN_MENU_FADE_TO_BLACK, /* Fade to black before intro (new game) or before continuing */
    MENU_STATE_MAIN_MENU_INTRO,         /* Intro slideshow before starting game */
    MENU_STATE_SETTINGS,                /* Settings menu */
    MENU_STATE_DELETE_CONFIRM,          /* Delete save confirmation */
    MENU_STATE_NEWSLETTER,              /* Newsletter screen */
    MENU_STATE_CREDITS,                 /* Credits screen */
    MENU_STATE_PAUSE,                   /* Pause menu (during gameplay) */
    MENU_STATE_PAUSE_SETTINGS,          /* Settings menu from pause */
    MENU_STATE_PAUSE_SAVE_CONFIRM,      /* Save confirmation from pause */
    MENU_STATE_PAUSE_EXIT_RACE_CONFIRM, /* Exit race confirmation from pause */
    MENU_STATE_PAL60_CONFIRM,           /* PAL60 activation confirmation */
    MENU_STATE_CALIBRATION,             /* Stick calibration screen */
    MENU_STATE_UPGRADE_SHOP,            /* Upgrade shop screen */
    MENU_STATE_TRANSITION_OUT,          /* Transitioning out of menu (to game) */
    MENU_STATE_TRANSITION_IN,           /* Transitioning into menu (from game) */
} eMenuState;

/* Menu result - what the menu system wants to do */
typedef enum
{
    MENU_RESULT_NONE,          /* Menu is still active */
    MENU_RESULT_START_GAME,    /* Start new game */
    MENU_RESULT_CONTINUE_GAME, /* Continue from save */
    MENU_RESULT_EXIT,          /* Exit menu (unpause game) */
} eMenuResult;

/* Initialize menu system - call once at startup */
void menu_init(void);

/* Update menu system - call each frame */
/* Returns MENU_RESULT_START_GAME or MENU_RESULT_CONTINUE_GAME when game should start */
/* _pInputs: Joypad inputs (already polled) */
eMenuResult menu_update(const joypad_inputs_t *_pInputs);

/* Render menu system - call each frame */
void menu_render(void);

/* Get current menu state (for debugging) */
eMenuState menu_get_state(void);

/* Set menu state (for pause menu integration later) */
void menu_set_state(eMenuState _eState);