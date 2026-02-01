#include "debug_cheats.h"
#include "font_helper.h"
#include "game_objects/gp_state.h"
#include "rdpq_text.h"
#include "save.h"
#include "ui.h"
#include <stddef.h>
#include <string.h>

static bool s_bActive = false;

/* Edge detection (local to this module, so we don't fight menu navigation state). */
static bool s_prev_d_up = false;
static bool s_prev_d_down = false;
static bool s_prev_d_left = false;
static bool s_prev_d_right = false;
static bool s_prev_c_up = false;
static bool s_prev_c_down = false;
static bool s_prev_c_left = false;
static bool s_prev_r = false;
static bool s_prev_abz_combo = false;

/* “Stacking” unlock progression step per logical group.
 * For a group with N flags, the step cycles as:
 *   0: all OFF
 *   1: flag[0] ON
 *   2: flag[0..1] ON
 *   ...
 *   N: flag[0..N-1] ON (all) */
static int s_step_weapons = 0;  /* bullets/laser/bomb */
static int s_step_movement = 0; /* turbo/tractor beam */
static int s_step_pieces = 0;   /* ship pieces */

#define ARRAY_COUNT(_a) ((int)(sizeof(_a) / sizeof((_a)[0])))

/* Explicit group definitions – keeps things readable and resilient
 * to reordering or inserting new flags in the enum. */
static const uint16_t s_group_weapons[] = {
    GP_UNLOCK_BULLETS_NORMAL,
    GP_UNLOCK_BULLETS_UPGRADED,
    GP_UNLOCK_LASER,
    GP_UNLOCK_BOMB,
};

static const uint16_t s_group_movement[] = {
    GP_UNLOCK_TURBO,
    GP_UNLOCK_TRACTOR_BEAM,
};

static const uint16_t s_group_pieces[] = {
    GP_UNLOCK_PIECE_A,
    GP_UNLOCK_PIECE_B,
    GP_UNLOCK_PIECE_C,
    GP_UNLOCK_PIECE_D,
};

static bool button_pressed_local(bool _bCurrent, bool *_pPrev)
{
    bool bPressed = _bCurrent && !(*_pPrev);
    *_pPrev = _bCurrent;
    return bPressed;
}

static const char *get_unlock_flag_name(uint16_t flag)
{
    switch (flag)
    {
    case GP_UNLOCK_BULLETS_NORMAL:
        return "BULLETS_NORMAL";
    case GP_UNLOCK_BULLETS_UPGRADED:
        return "BULLETS_UPGRADED";
    case GP_UNLOCK_LASER:
        return "LASER";
    case GP_UNLOCK_BOMB:
        return "BOMB";
    case GP_UNLOCK_TURBO:
        return "TURBO";
    case GP_UNLOCK_TRACTOR_BEAM:
        return "TRACTOR_BEAM";
    case GP_UNLOCK_PIECE_A:
        return "PIECE_A";
    case GP_UNLOCK_PIECE_B:
        return "PIECE_B";
    case GP_UNLOCK_PIECE_C:
        return "PIECE_C";
    case GP_UNLOCK_PIECE_D:
        return "PIECE_D";
    case GP_UNLOCK_MINIMAP:
        return "MINIMAP";
    default:
        return "UNKNOWN";
    }
}

static void apply_stacking_group(const uint16_t *_pFlags, int _iCount, int *_pStep)
{
    if (_pFlags == NULL || _pStep == NULL || _iCount <= 0)
        return;

    /* Cycle through 0.._iCount inclusive */
    *_pStep = (*_pStep + 1) % (_iCount + 1);

    /* Enable indices < step; everything after gets cleared. */
    for (int i = 0; i < _iCount; i++)
    {
        bool bEnable = (i < *_pStep);
        gp_state_unlock_set(_pFlags[i], bEnable);
    }
}

static void unlock_all(void)
{
    /* Keep these in display order. */
    uint16_t flags[] = {GP_UNLOCK_BULLETS_NORMAL,
                        GP_UNLOCK_BULLETS_UPGRADED,
                        GP_UNLOCK_LASER,
                        GP_UNLOCK_BOMB,
                        GP_UNLOCK_TURBO,
                        GP_UNLOCK_TRACTOR_BEAM,
                        GP_UNLOCK_PIECE_A,
                        GP_UNLOCK_PIECE_B,
                        GP_UNLOCK_PIECE_C,
                        GP_UNLOCK_PIECE_D,
                        GP_UNLOCK_MINIMAP};
    int iFlagCount = ARRAY_COUNT(flags);
    for (int i = 0; i < iFlagCount; i++)
    {
        gp_state_unlock_set(flags[i], true);
    }

    /* Keep cycler steps in sync (fully enabled based on group sizes). */
    s_step_weapons = ARRAY_COUNT(s_group_weapons);
    s_step_movement = ARRAY_COUNT(s_group_movement);
    s_step_pieces = ARRAY_COUNT(s_group_pieces);
}

void debug_cheats_init(void)
{
    s_bActive = false;
    s_prev_d_up = false;
    s_prev_d_down = false;
    s_prev_d_left = false;
    s_prev_d_right = false;
    s_prev_c_up = false;
    s_prev_c_down = false;
    s_prev_r = false;
    s_prev_abz_combo = false;

    s_step_weapons = 0;
    s_step_movement = 0;
    s_step_pieces = 0;
}

void debug_cheats_toggle(void)
{
    s_bActive = !s_bActive;

    /* Reset edge states so we don't trigger immediately after toggling. */
    s_prev_d_up = false;
    s_prev_d_down = false;
    s_prev_d_left = false;
    s_prev_d_right = false;
    s_prev_c_up = false;
    s_prev_c_down = false;
    s_prev_c_left = false;
    s_prev_r = false;
}

void debug_cheats_set_active(bool _bActive)
{
    s_bActive = _bActive;
}

bool debug_cheats_is_active(void)
{
    return s_bActive;
}

void debug_cheats_update(const joypad_inputs_t *_pInputs)
{
    if (!_pInputs || !s_bActive)
        return;

    /* A+B+Z combo: hard wipe save with no prompt (debug only) */
    bool bAbzCombo = _pInputs->btn.a && _pInputs->btn.b && _pInputs->btn.z;
    if (bAbzCombo && !s_prev_abz_combo)
    {
        save_wipe();
        /* Optionally keep the overlay open so you immediately see state. */
    }
    s_prev_abz_combo = bAbzCombo;

    /* R: unlock everything */
    if (button_pressed_local(_pInputs->btn.r, &s_prev_r))
    {
        unlock_all();
    }

    /* D-UP: cycle act (mod) */
    if (button_pressed_local(_pInputs->btn.d_up, &s_prev_d_up))
    {
        gp_act_t act = gp_state_act_get();
        act = (gp_act_t)(((int)act + 1) % (int)ACT_COUNT);
        gp_state_act_set(act);
    }

    /* D-DOWN: cycle flags (gp_state.h 36-39) stacking */
    if (button_pressed_local(_pInputs->btn.d_down, &s_prev_d_down))
    {
        apply_stacking_group(s_group_weapons, ARRAY_COUNT(s_group_weapons), &s_step_weapons);
    }

    /* D-LEFT: cycle flags (gp_state.h 40-41) stacking */
    if (button_pressed_local(_pInputs->btn.d_left, &s_prev_d_left))
    {
        apply_stacking_group(s_group_movement, ARRAY_COUNT(s_group_movement), &s_step_movement);
    }

    /* D-RIGHT: cycle flags (gp_state.h 42-45) stacking */
    if (button_pressed_local(_pInputs->btn.d_right, &s_prev_d_right))
    {
        apply_stacking_group(s_group_pieces, ARRAY_COUNT(s_group_pieces), &s_step_pieces);
    }

    /* C-UP: increase currency */
    if (button_pressed_local(_pInputs->btn.c_up, &s_prev_c_up))
    {
        uint16_t currency = gp_state_currency_get();
        if (currency < UINT16_MAX - 1)
            currency += 1;
        else
            currency = UINT16_MAX;
        gp_state_currency_set(currency);
    }

    /* C-DOWN: decrease currency */
    if (button_pressed_local(_pInputs->btn.c_down, &s_prev_c_down))
    {
        uint16_t currency = gp_state_currency_get();
        if (currency > 1)
            currency -= 1;
        else
            currency = 0;
        gp_state_currency_set(currency);
    }

    /* C-LEFT: reset currency collection array (all collected currency reset) */
    if (button_pressed_local(_pInputs->btn.c_left, &s_prev_c_left))
    {
        currency_collection_entry_t *pArray = gp_state_get_currency_collection_array();
        if (pArray)
        {
            memset(pArray, 0, sizeof(currency_collection_entry_t) * MAX_CURRENCY_COLLECTION_FOLDERS);
        }
    }
}

void debug_cheats_render(void)
{
    if (!s_bActive)
        return;

    gp_state_persist_t persist;
    gp_state_get_persist(&persist);

    int iX = 10;
    int iY = 10;
    const int iLineHeight = 12;
    const int iColumnWidth = 160;
    const int iSecondColumnX = iX + iColumnWidth;

    const char *stateNames[] = {"SPACE", "PLANET", "SURFACE", "JNR"};
    const char *actNames[] = {"INTRO", "INTRO_RACE", "OPENING", "MAIN", "FINAL"};

    rdpq_text_printf(NULL, FONT_NORMAL, iX, iY, "DEBUG CHEATS (L toggles, R unlocks all)");
    iY += iLineHeight * 2;

    rdpq_text_printf(NULL, FONT_NORMAL, iX, iY, "State: %s", stateNames[persist.uGpStateCurrent < 4 ? persist.uGpStateCurrent : 0]);
    iY += iLineHeight;
    rdpq_text_printf(NULL, FONT_NORMAL, iX, iY, "Act: %s", actNames[persist.uAct < ACT_COUNT ? persist.uAct : 0]);
    iY += iLineHeight;
    rdpq_text_printf(NULL, FONT_NORMAL, iX, iY, "Currency: %u", persist.uCurrency);
    iY += iLineHeight;
    rdpq_text_printf(NULL, FONT_NORMAL, iX, iY, "Pos: %.1f, %.1f", persist.fCurrentPosX, persist.fCurrentPosY);
    iY += iLineHeight;
    rdpq_text_printf(NULL, FONT_NORMAL, iX, iY, "Best Lap: %.2f", persist.fBestLapTime);
    iY += iLineHeight * 2;

    rdpq_text_printf(NULL, FONT_NORMAL, iX, iY, "Unlocks:");
    iY += iLineHeight;

    uint16_t allFlags[] = {GP_UNLOCK_BULLETS_NORMAL,
                           GP_UNLOCK_BULLETS_UPGRADED,
                           GP_UNLOCK_LASER,
                           GP_UNLOCK_BOMB,
                           GP_UNLOCK_TURBO,
                           GP_UNLOCK_TRACTOR_BEAM,
                           GP_UNLOCK_PIECE_A,
                           GP_UNLOCK_PIECE_B,
                           GP_UNLOCK_PIECE_C,
                           GP_UNLOCK_PIECE_D,
                           GP_UNLOCK_MINIMAP};
    int iFlagCount = (int)(sizeof(allFlags) / sizeof(allFlags[0]));
    int iFlagsPerColumn = (iFlagCount + 1) / 2;

    int iLeftColumnStartY = iY;
    for (int i = 0; i < iFlagsPerColumn; i++)
    {
        bool bUnlocked = (persist.uUnlockFlags & allFlags[i]) != 0;
        rdpq_text_printf(NULL, FONT_NORMAL, iX, iY, "  %s: %s", get_unlock_flag_name(allFlags[i]), bUnlocked ? "YES" : "NO");
        iY += iLineHeight;
    }

    iY = iLeftColumnStartY;
    for (int i = iFlagsPerColumn; i < iFlagCount; i++)
    {
        bool bUnlocked = (persist.uUnlockFlags & allFlags[i]) != 0;
        rdpq_text_printf(NULL, FONT_NORMAL, iSecondColumnX, iY, "  %s: %s", get_unlock_flag_name(allFlags[i]), bUnlocked ? "YES" : "NO");
        iY += iLineHeight;
    }

    iY = iLeftColumnStartY + iLineHeight * iFlagsPerColumn + iLineHeight;
    const char *layerNames[] = {"SPACE", "PLANET", "SURFACE", "JNR"};
    for (int i = 0; i < 4; i++)
    {
        if (persist.aLayers[i].folder_name[0] != '\0')
        {
            rdpq_text_printf(NULL, FONT_NORMAL, iX, iY, "%s:", layerNames[i]);
            iY += iLineHeight;
            rdpq_text_printf(NULL, FONT_NORMAL, iX, iY, "  Folder: %s", persist.aLayers[i].folder_name);
            iY += iLineHeight;
            rdpq_text_printf(NULL, FONT_NORMAL, iX, iY, "  Pos: %.1f, %.1f", persist.aLayers[i].saved_position.fX, persist.aLayers[i].saved_position.fY);
            iY += iLineHeight;
        }
    }
}
