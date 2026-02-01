#include "credits.h"
#include "font_helper.h"
#include "frame_time.h"
#include "joypad.h"
#include "math_helper.h"
#include "stick_normalizer.h"
#include "ui.h"

/* Credits text lines - shared data
 *
 * To create a half-height empty line (50% spacing) between text lines,
 * use a string starting with CREDITS_HALF_HEIGHT_MARKER ('\1').
 * Example: "\1" for a half-height spacer between two text lines.
 * Regular empty strings "" use full spacing (MENU_ITEM_SPACING).
 */
const char *g_credits_text_lines[] = {"Thank you for playing!",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "^07Designed and developed by^00",
                                      "\1",
                                      "Johannes Roth",
                                      "",
                                      "",
                                      "",
                                      "^07with^00",
                                      "\1",
                                      "libdragon",
                                      "",
                                      "",
                                      "",
                                      "^07for^00",
                                      "\1",
                                      "#N64BrewJam 2025-2026",
                                      "",
                                      "",
                                      "",
                                      "^07Supported by^00",
                                      "\1",
                                      "Levi - QA & Intro Voice Over",
                                      "N64Brew Discord Community",
                                      "",
                                      "",
                                      "",
                                      "^07Paid Assets^00",
                                      "\1",
                                      "Helianthus Games - Starfield Assets",
                                      "fliegevogel - Tilesheets",
                                      "SoundSnap - SFX",
                                      "",
                                      "",
                                      "",
                                      "^07Tools^00",
                                      "\1",
                                      "TileEd",
                                      "Aseprite",
                                      "Cursor",
                                      "",
                                      "",
                                      "",
                                      "^07Third Party Libraries^00",
                                      "\1",
                                      "SquirrelNoise5 - Squirrel Eiserloh",
                                      "licensed under CC - BY 3.0(US)",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "www.phazer64.com",
                                      "\1",
                                      "The journey will continue ...",
                                      NULL};

const int g_credits_text_line_count = sizeof(g_credits_text_lines) / sizeof(g_credits_text_lines[0]) - 1; /* Automatically calculated, excludes NULL terminator */

/* Constants */
#define CREDITS_ITEM_SPACING 14
#define CREDITS_SCROLL_SPEED 30.0f      /* Pixels per second */
#define CREDITS_FINAL_BLOCK_LINES 3     /* Number of last lines to hold at center screen */
#define CREDITS_HALF_HEIGHT_MARKER '\1' /* Special character to mark half-height empty lines */
#define CREDITS_HALF_HEIGHT_SIZE 4      // yeah thats not half height, but Buu promised no one judges the source and I am out of time
#define CREDITS_FONT_HEIGHT 8           /* Approximate height of the font (8x8 debug font) */

/* Internal state */
static float s_fScrollOffset = 0.0f;            /* Scroll offset for credits (pixels) */
static float s_fMaxScrollOffset = 0.0f;         /* Maximum scroll offset (calculated on reset) */
static float s_fMinScrollOffset = 0.0f;         /* Minimum scroll offset (calculated on reset) */
static float s_fAbsoluteMaxScrollOffset = 0.0f; /* Absolute max scroll offset (calculated on reset) */
static int s_iStartY = 0;                       /* Start Y position for credits (set during render) */

/* Helper: Get spacing for a line (full or half-height) */
static float credits_get_line_spacing(int _iLineIndex)
{
    if (_iLineIndex >= g_credits_text_line_count || g_credits_text_lines[_iLineIndex] == NULL)
    {
        return CREDITS_ITEM_SPACING;
    }

    const char *pLine = g_credits_text_lines[_iLineIndex];
    /* Check if line is marked as half-height (starts with special marker) */
    if (pLine[0] == CREDITS_HALF_HEIGHT_MARKER)
    {
        return CREDITS_HALF_HEIGHT_SIZE;
    }

    return CREDITS_ITEM_SPACING;
}

/* Helper: Calculate Y position for a line index (accounting for variable spacing) */
static float credits_get_line_y_position(int _iLineIndex, int _iStartY)
{
    float fY = (float)_iStartY;
    for (int i = 0; i < _iLineIndex; i++)
    {
        fY += credits_get_line_spacing(i);
    }
    return fY;
}

/* Helper: Calculate height of the final block */
static float credits_get_final_block_height(int _iStartIndex)
{
    float fHeight = 0.0f;
    for (int i = _iStartIndex; i < g_credits_text_line_count; i++)
    {
        /* If it's the last line, don't add font height to keep it visually centered lower */
        /* This is a heuristic to fix "too high" centering */
        if (i == g_credits_text_line_count - 1)
        {
            fHeight += 0;
        }
        else
        {
            fHeight += credits_get_line_spacing(i);
        }
    }
    return fHeight;
}

/* Reset credits scroll position */
void credits_reset(void)
{
    /* Calculate startY using the same formula as menu.c (SCREEN_H / 2 + MENU_CREDITS_Y_OFFSET) */
    /* MENU_CREDITS_Y_OFFSET is -30, so: SCREEN_H/2 - 30 = 120 - 30 = 90 */
    int iStartY = (SCREEN_H / 2) - 30;

    /* Calculate Y position of the first line in the final block */
    int iFinalBlockStartIndex = g_credits_text_line_count - CREDITS_FINAL_BLOCK_LINES;
    float fFinalBlockStartY = credits_get_line_y_position(iFinalBlockStartIndex, iStartY);

    /* Calculate the center Y of the final block */
    float fFinalBlockHeight = credits_get_final_block_height(iFinalBlockStartIndex);
    /* Center is at start Y + half the block height */
    float fFinalBlockCenterY = fFinalBlockStartY + fFinalBlockHeight * 0.5f;

    /* Calculate maximum scroll offset: scroll until final block is centered at screen center */
    s_fMaxScrollOffset = fFinalBlockCenterY - (SCREEN_H * 0.5f);

    /* Calculate absolute max scroll: ensure the last VISIBLE line BEFORE the final block is fully off-screen */
    /* The line before the final block is at index (iFinalBlockStartIndex - 1) */
    int iLastVisibleLineIndex = iFinalBlockStartIndex - 1;

    /* Search backwards for non-empty line */
    while (iLastVisibleLineIndex >= 0)
    {
        const char *pLine = g_credits_text_lines[iLastVisibleLineIndex];
        if (pLine[0] != '\0' && pLine[0] != CREDITS_HALF_HEIGHT_MARKER)
        {
            break;
        }
        iLastVisibleLineIndex--;
    }

    if (iLastVisibleLineIndex >= 0)
    {
        /* Get Y position of the last visible scrolling line */
        float fLastVisibleLineY = credits_get_line_y_position(iLastVisibleLineIndex, iStartY);

        /* We want this line to be fully off-screen (Y < -spacing) */
        /* Y_screen = Y_world - ScrollOffset */
        /* Y_world - ScrollOffset < -spacing */
        /* ScrollOffset > Y_world + spacing */
        /* Add some extra padding (2x spacing) to be safe */
        float fScrollToClear = fLastVisibleLineY + (CREDITS_ITEM_SPACING * 2.0f);

        /* The absolute max should be at least the freeze point, but allow scrolling further to clear text */
        s_fAbsoluteMaxScrollOffset = (fScrollToClear > s_fMaxScrollOffset) ? fScrollToClear : s_fMaxScrollOffset;
    }
    else
    {
        s_fAbsoluteMaxScrollOffset = s_fMaxScrollOffset;
    }

    /* Start off-screen (at bottom) - set initial offset so first line starts below screen */
    float fFirstLineY = credits_get_line_y_position(0, iStartY);

    /* Set offset such that first line starts at the bottom of the screen with extra padding */
    s_fMinScrollOffset = fFirstLineY - (float)SCREEN_H - (CREDITS_FONT_HEIGHT * 4.0f);
    s_fScrollOffset = s_fMinScrollOffset;
}

/* Update credits scroll based on input and time */
void credits_update(const joypad_inputs_t *_pInputs, bool _bAllowInput)
{
    float fDelta = frame_time_delta_seconds();
    float fScrollSpeed = CREDITS_SCROLL_SPEED;

    /* Check stick Y input for scroll control if allowed */
    if (_bAllowInput)
    {
        /* Get normalized stick Y value (ranges from -85 to +85) */
        float fNormY = (float)stick_normalizer_get_y();
        float fAbsNormY = fabsf(fNormY);

        /* Check if stick is beyond deadzone - use gameplay deadzone for smoother control */
        if (fAbsNormY > STICK_DEADZONE)
        {
            /* Calculate lerp factor from deadzone (0) to full force (1.0) */
            float fLerpFactor = (fAbsNormY - STICK_DEADZONE) / (STICK_NORMALIZED_MAX - STICK_DEADZONE);
            fLerpFactor = clampf_01(fLerpFactor);

            /* Inverted Y-axis: stick up (negative) = fast forward, stick down (positive) = rewind */
            if (fNormY < 0.0f)
            {
                /* Stick up: fast forward (positive scroll speed) */
                fScrollSpeed = CREDITS_SCROLL_SPEED * 3.0f * fLerpFactor;
            }
            else
            {
                /* Stick down: rewind (negative scroll speed) */
                fScrollSpeed = -CREDITS_SCROLL_SPEED * 2.0f * fLerpFactor;
            }
        }
    }

    s_fScrollOffset += fScrollSpeed * fDelta;

    /* Clamp scroll offset to valid range */
    if (s_fScrollOffset < s_fMinScrollOffset)
    {
        s_fScrollOffset = s_fMinScrollOffset;
    }

    /* Clamp upper bound to allow scrolling past max but not indefinitely (prevents long rewind times) */
    if (s_fScrollOffset > s_fAbsoluteMaxScrollOffset)
    {
        s_fScrollOffset = s_fAbsoluteMaxScrollOffset;
    }
}

/* Render scrolling credits */
void credits_render(int _iStartY)
{
    /* Store startY for reset calculations */
    s_iStartY = _iStartY;

    int iFinalBlockStartIndex = g_credits_text_line_count - CREDITS_FINAL_BLOCK_LINES;
    bool bFinalBlockFrozen = (s_fScrollOffset >= s_fMaxScrollOffset);

    for (int i = 0; i < g_credits_text_line_count && g_credits_text_lines[i] != NULL; i++)
    {
        const char *pLine = g_credits_text_lines[i];

        /* Skip rendering if line is empty or is a half-height marker */
        if (pLine[0] == '\0' || pLine[0] == CREDITS_HALF_HEIGHT_MARKER)
        {
            continue;
        }

        float fY;
        int iY;

        /* If this line is in the final block and we've reached max scroll, freeze it at center */
        if (bFinalBlockFrozen && i >= iFinalBlockStartIndex)
        {
            /* Calculate where this line would be at max scroll offset */
            float fLineYAtMax = credits_get_line_y_position(i, _iStartY) - s_fMaxScrollOffset;

            /* Calculate the center Y of the final block at max scroll */
            float fFinalBlockStartY = credits_get_line_y_position(iFinalBlockStartIndex, _iStartY) - s_fMaxScrollOffset;
            float fFinalBlockHeight = credits_get_final_block_height(iFinalBlockStartIndex);
            float fFinalBlockCenterY = fFinalBlockStartY + fFinalBlockHeight * 0.5f;

            /* Offset to center the block at screen center */
            float fCenterOffset = (SCREEN_H * 0.5f) - fFinalBlockCenterY;
            fY = fLineYAtMax + fCenterOffset;
            iY = (int)fY;
        }
        else
        {
            /* Normal scrolling - calculate Y position using variable spacing */
            fY = credits_get_line_y_position(i, _iStartY) - s_fScrollOffset;
            iY = (int)fY;
        }

        /* Only render if on screen */
        if (iY > -CREDITS_ITEM_SPACING && iY < SCREEN_H + CREDITS_ITEM_SPACING)
        {
            rdpq_text_printf(&m_tpCenterHorizontally, FONT_NORMAL, 0, iY, "%s", pLine);
        }
    }
}
