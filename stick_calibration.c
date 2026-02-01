#include "stick_calibration.h"
#include "audio.h"
#include "font_helper.h"
#include "frame_time.h"
#include "rdpq_mode.h"
#include "resource_helper.h"
#include "save.h"
#include "stick_normalizer.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>

#define CALIB_STICK_RANGE ((float)STICK_NORMALIZED_MAX)

/* Tuning Constants */
#define CALIB_STICK_ROT_INTENSITY 0.0055f   /* Radians per stick unit (approx) */
#define CALIB_STICK_SCALE_Y_INTENSITY 0.32f /* Y-axis scale multiplier for stick */
#define CALIB_STICK_SCALE_Y_UP 0.7f         /* Additional multiplier for stick Y-scale when going up */
#define CALIB_KNOB_SCALE_MIN 0.85f
#define CALIB_KNOB_SCALE_MAX 1.1f
#define CALIB_KNOB_TRANS_MULT_X 0.25f   /* Multiplier for Knob X translation */
#define CALIB_KNOB_TRANS_MULT_Y 0.1f    /* Multiplier for Knob Y translation (down) */
#define CALIB_KNOB_TRANS_MULT_Y_UP 0.3f /* Additional multiplier for Knob Y translation (up) */
#define CALIB_KNOB_TRANS_Y_FROM_X 0.08f /* Y offset based on X rotation for arc following */

/* Design Positions (320x240 space) */
#define CALIB_STICK_POS_X 167
#define CALIB_STICK_POS_Y 188
#define CALIB_STICK_ANCHOR_X 7
#define CALIB_STICK_ANCHOR_Y 44

#define CALIB_KNOB_POS_X 164
#define CALIB_KNOB_POS_Y 103
#define CALIB_KNOB_ANCHOR_X 11
#define CALIB_KNOB_ANCHOR_Y 12

#define CALIB_OVERLAY_POS_X 144
#define CALIB_OVERLAY_POS_Y 137

#define CALIB_TEXT_Y 200

/* Assets */
static sprite_t *s_pBgSprite = NULL;
static sprite_t *s_pStickSprite = NULL;
static sprite_t *s_pKnobSprite = NULL;
static sprite_t *s_pOverlaySprite = NULL;
static wav64_t *s_pSfxStickMovement = NULL;

/* State */
static int8_t s_iMinX = 0;
static int8_t s_iMaxX = 0;
static int8_t s_iMinY = 0;
static int8_t s_iMaxY = 0;

static int8_t s_iCurrentX = 0;
static int8_t s_iCurrentY = 0;
static int8_t s_iPrevX = 0;
static int8_t s_iPrevY = 0;

/* Sound state */
static float s_fNoMovementTimer = 0.0f;
#define STICK_CALIB_NO_MOVEMENT_STOP_DELAY 0.3f /* Seconds of no movement before stopping sound */
#define STICK_CALIB_MOVEMENT_THRESHOLD_SQ 4     /* Movement magnitude squared threshold (2 units) */

/* Flag to track if calibration is active without menu integration */
static bool s_bActiveWithoutMenu = false;

void stick_calibration_init(void)
{
    s_pBgSprite = sprite_load("rom:/screen_calibration_00.sprite");
    s_pStickSprite = sprite_load("rom:/screen_calibration_stick_00.sprite");
    s_pKnobSprite = sprite_load("rom:/screen_calibration_knob_00.sprite");
    s_pOverlaySprite = sprite_load("rom:/screen_calibration_stick_overlay_00.sprite");

    /* Load sound effect */
    s_pSfxStickMovement = wav64_load("rom:/calib_screen_stick_movement.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    if (s_pSfxStickMovement)
    {
        wav64_set_loop(s_pSfxStickMovement, true);
    }

    s_iMinX = 0;
    s_iMaxX = 0;
    s_iMinY = 0;
    s_iMaxY = 0;
    s_iCurrentX = 0;
    s_iCurrentY = 0;
    s_iPrevX = 0;
    s_iPrevY = 0;
    s_fNoMovementTimer = 0.0f;
    s_bActiveWithoutMenu = false;
}

void stick_calibration_init_without_menu(void)
{
    stick_calibration_init();
    s_bActiveWithoutMenu = true;
}

void stick_calibration_close(void)
{
    /* Clamp calibration values to safe minimums to prevent save corruption */
    /* If user didn't move stick enough, use defaults instead */
    int8_t minX = (s_iMinX < -STICK_CALIBRATION_MIN_THRESHOLD) ? s_iMinX : -STICK_NORMALIZED_MAX;
    int8_t maxX = (s_iMaxX > STICK_CALIBRATION_MIN_THRESHOLD) ? s_iMaxX : STICK_NORMALIZED_MAX;
    int8_t minY = (s_iMinY < -STICK_CALIBRATION_MIN_THRESHOLD) ? s_iMinY : -STICK_NORMALIZED_MAX;
    int8_t maxY = (s_iMaxY > STICK_CALIBRATION_MIN_THRESHOLD) ? s_iMaxY : STICK_NORMALIZED_MAX;

    /* Save calibration values before closing */
    save_set_stick_calibration(minX, maxX, minY, maxY);
    save_write();

    /* Update normalizer with new calibration immediately */
    stick_normalizer_set_calibration(minX, maxX, minY, maxY);

    SAFE_FREE_SPRITE(s_pBgSprite);
    SAFE_FREE_SPRITE(s_pStickSprite);
    SAFE_FREE_SPRITE(s_pKnobSprite);
    SAFE_FREE_SPRITE(s_pOverlaySprite);
    SAFE_CLOSE_WAV64(s_pSfxStickMovement);

    s_bActiveWithoutMenu = false;
}

void stick_calibration_update(const joypad_inputs_t *inputs)
{
    float fDelta = frame_time_delta_seconds();

    s_iCurrentX = inputs->stick_x;
    s_iCurrentY = inputs->stick_y;

    /* Detect stick movement (input change) */
    int8_t iDeltaX = s_iCurrentX - s_iPrevX;
    int8_t iDeltaY = s_iCurrentY - s_iPrevY;

    /* Calculate movement magnitude to determine if movement is significant */
    int iMovementMagSq = iDeltaX * iDeltaX + iDeltaY * iDeltaY;
    bool bSignificantMovement = (iMovementMagSq > STICK_CALIB_MOVEMENT_THRESHOLD_SQ);

    /* Handle sound playback based on stick movement */
    if (s_pSfxStickMovement)
    {
        if (bSignificantMovement)
        {
            /* Stick moved significantly: reset no-movement timer and ensure sound is playing */
            s_fNoMovementTimer = 0.0f;
            if (!mixer_ch_playing(MIXER_CHANNEL_EXPLOSIONS))
            {
                /* Start playing sound if not already playing */
                wav64_play(s_pSfxStickMovement, MIXER_CHANNEL_EXPLOSIONS);
            }
        }
        else
        {
            /* Stick not moved or moved very little: increment no-movement timer */
            s_fNoMovementTimer += fDelta;

            /* Stop sound only after delay period */
            if (s_fNoMovementTimer >= STICK_CALIB_NO_MOVEMENT_STOP_DELAY)
            {
                if (mixer_ch_playing(MIXER_CHANNEL_EXPLOSIONS))
                {
                    mixer_ch_stop(MIXER_CHANNEL_EXPLOSIONS);
                }
            }
        }
    }

    /* Update previous values */
    s_iPrevX = s_iCurrentX;
    s_iPrevY = s_iCurrentY;

    if (s_iCurrentX < s_iMinX)
        s_iMinX = s_iCurrentX;
    if (s_iCurrentX > s_iMaxX)
        s_iMaxX = s_iCurrentX;
    if (s_iCurrentY < s_iMinY)
        s_iMinY = s_iCurrentY;
    if (s_iCurrentY > s_iMaxY)
        s_iMaxY = s_iCurrentY;
}

static void render_sprite_fullscreen(sprite_t *pSprite)
{
    if (pSprite)
    {
        struct vec2i vSafeSize = ui_get_safe_area_size();
        float fScaleW = (float)vSafeSize.iX / (float)pSprite->width;
        float fScaleH = (float)vSafeSize.iY / (float)pSprite->height;
        float fScale = (fScaleW < fScaleH) ? fScaleW : fScaleH;

        int iScaledW = (int)(pSprite->width * fScale);
        int iScaledH = (int)(pSprite->height * fScale);
        int iSpriteX = (SCREEN_W - iScaledW) / 2;
        int iSpriteY = (SCREEN_H - iScaledH) / 2;

        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_sprite_blit(pSprite, iSpriteX, iSpriteY, &(rdpq_blitparms_t){.scale_x = fScale, .scale_y = fScale});
    }
}

/* Helper to map 320x240 design coordinates to screen coordinates accounting for scaling/centering */
static void get_screen_pos(float designX, float designY, float *screenX, float *screenY, float *scale)
{
    /* Calculate scale factor similar to fullscreen sprite */
    struct vec2i vSafeSize = ui_get_safe_area_size();

    /* Assuming background is 320x240 or we treat 320x240 as base design resolution */
    float fDesignW = SCREEN_W;
    float fDesignH = SCREEN_H;

    float fScaleW = (float)vSafeSize.iX / fDesignW;
    float fScaleH = (float)vSafeSize.iY / fDesignH;
    float fScale = (fScaleW < fScaleH) ? fScaleW : fScaleH;

    *scale = fScale;

    /* Calculate offset to center the design area */
    float fScaledW = fDesignW * fScale;
    float fScaledH = fDesignH * fScale;
    float fOffsetX = (SCREEN_W - fScaledW) / 2.0f;
    float fOffsetY = (SCREEN_H - fScaledH) / 2.0f;

    *screenX = fOffsetX + (designX * fScale);
    *screenY = fOffsetY + (designY * fScale);
}

void stick_calibration_render(void)
{
    ui_draw_darkening_overlay_alpha(0); // required for overscan
    /* Render background */
    render_sprite_fullscreen(s_pBgSprite);

    /* Common filter mode */
    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_alphacompare(1);

    float fScreenX, fScreenY, fGlobalScale;

    /* --- Render Stick --- */
    if (s_pStickSprite)
    {
        get_screen_pos(CALIB_STICK_POS_X, CALIB_STICK_POS_Y, &fScreenX, &fScreenY, &fGlobalScale);

        float fRotation = -((float)s_iCurrentX * CALIB_STICK_ROT_INTENSITY); /* Rotate based on X */

        /* Calculate Y-axis scale based on stick Y position */
        /* Y+ (down) -> extend, Y- (up) -> shrink */
        float fStickNormY = (float)s_iCurrentY / CALIB_STICK_RANGE;
        if (fStickNormY < -1.0f)
            fStickNormY = -1.0f;
        if (fStickNormY > 1.0f)
            fStickNormY = 1.0f;

        /* Apply asymmetric Y-scale intensity */
        float fScaleIntensity;
        if (fStickNormY > 0.0f)
        {
            /* Going down (Y+) - apply additional reduction */
            fScaleIntensity = CALIB_STICK_SCALE_Y_INTENSITY * CALIB_STICK_SCALE_Y_UP;
        }
        else
        {
            /* Going up (Y-) - normal intensity */
            fScaleIntensity = CALIB_STICK_SCALE_Y_INTENSITY;
        }

        /* Reduce Y-scale effect based on X rotation (even steeper falloff) */
        float fStickNormX = (float)s_iCurrentX / CALIB_STICK_RANGE;
        if (fStickNormX < -1.0f)
            fStickNormX = -1.0f;
        if (fStickNormX > 1.0f)
            fStickNormX = 1.0f;

        float fAbsNormX = fabsf(fStickNormX);
        /* Septic (7th power) falloff for very steep curve */
        float fScaleReduction = 1.0f - (fAbsNormX * fAbsNormX * fAbsNormX * fAbsNormX * fAbsNormX * fAbsNormX * fAbsNormX);

        float fStickScaleY = fGlobalScale * (1.0f + (fStickNormY * fScaleIntensity * fScaleReduction));

        rdpq_blitparms_t stickParams = {
            .scale_x = fGlobalScale,
            .scale_y = fStickScaleY,
            .theta = fRotation,
            .cx = CALIB_STICK_ANCHOR_X,
            .cy = CALIB_STICK_ANCHOR_Y,
        };

        /* Calculate draw position (Top-Left) so that Anchor aligns with Screen Point */
        float fDrawX = fScreenX - (CALIB_STICK_ANCHOR_X * fGlobalScale);
        float fDrawY = fScreenY - (CALIB_STICK_ANCHOR_Y * fGlobalScale);

        rdpq_sprite_blit(s_pStickSprite, fDrawX, fDrawY, &stickParams);
    }

    /* --- Render Knob --- */
    if (s_pKnobSprite)
    {
        /* Calculate knob offset and scale logic */
        /* Y input affects Y pos and Scale */
        /* Map Stick Y (-80 to 80 approx) to Scale Min/Max */
        /* Normalizing stick Y roughly -80..80 to 0..1 range? Or just linear map? */
        /* Let's assume standard stick range -85 to 85 is typical full range. */

        float fStickNormY = (float)s_iCurrentY / CALIB_STICK_RANGE;
        if (fStickNormY < -1.0f)
            fStickNormY = -1.0f;
        if (fStickNormY > 1.0f)
            fStickNormY = 1.0f;

        /* Map -1..1 to ScaleMin..ScaleMax */
        /* Let's assume: Up (negative Y) -> Smaller scale? Down (positive Y) -> Larger scale? */

        float fKnobScaleBase = (CALIB_KNOB_SCALE_MIN + CALIB_KNOB_SCALE_MAX) * 0.5f;
        float fKnobScaleRange = (CALIB_KNOB_SCALE_MAX - CALIB_KNOB_SCALE_MIN) * 0.5f;
        float fKnobLocalScale = fKnobScaleBase + (-fStickNormY * fKnobScaleRange);

        /* Apply translation */
        float fTransX = (float)s_iCurrentX * CALIB_KNOB_TRANS_MULT_X;

        /* Asymmetric Y translation: less movement when going up (Y-) */
        float fTransY;
        if (s_iCurrentY > 0)
        {
            /* Going down (Y+) - apply additional reduction */
            fTransY = -((float)s_iCurrentY * CALIB_KNOB_TRANS_MULT_Y * CALIB_KNOB_TRANS_MULT_Y_UP);
        }
        else
        {
            /* Going up (Y-) - normal translation */
            fTransY = -((float)s_iCurrentY * CALIB_KNOB_TRANS_MULT_Y);
        }

        /* Add Y offset based on X to follow stick rotation arc */
        float fTransY_fromX = fabsf((float)s_iCurrentX) * CALIB_KNOB_TRANS_Y_FROM_X;
        fTransY += fTransY_fromX;

        get_screen_pos(CALIB_KNOB_POS_X + fTransX, CALIB_KNOB_POS_Y + fTransY, &fScreenX, &fScreenY, &fGlobalScale);

        /* Combine global scale (screen fit) with knob local scale */
        float fFinalScale = fGlobalScale * fKnobLocalScale;

        rdpq_blitparms_t knobParams = {
            .scale_x = fFinalScale,
            .scale_y = fFinalScale,
            .cx = CALIB_KNOB_ANCHOR_X,
            .cy = CALIB_KNOB_ANCHOR_Y,
        };

        /* Calculate draw position (Top-Left) so that Anchor aligns with Screen Point */
        float fKnobDrawX = fScreenX - (CALIB_KNOB_ANCHOR_X * fGlobalScale);
        float fKnobDrawY = fScreenY - (CALIB_KNOB_ANCHOR_Y * fGlobalScale);

        rdpq_sprite_blit(s_pKnobSprite, fKnobDrawX, fKnobDrawY, &knobParams);
    }

    /* --- Render Overlay --- */
    if (s_pOverlaySprite)
    {
        get_screen_pos(CALIB_OVERLAY_POS_X, CALIB_OVERLAY_POS_Y, &fScreenX, &fScreenY, &fGlobalScale);

        rdpq_blitparms_t overlayParams = {
            .scale_x = fGlobalScale,
            .scale_y = fGlobalScale,
        };

        rdpq_sprite_blit(s_pOverlaySprite, fScreenX, fScreenY, &overlayParams);
    }

    /* --- Render Instruction Text (when opened from menu) --- */
    if (!s_bActiveWithoutMenu)
    {
        /* Calculate position in upper third of screen, accounting for overscan */
        /* Upper third would be around SCREEN_H / 3, but we need to use design coordinates */
        /* Assuming 320x240 design space, upper third is around 80 */
        float fInstructionY = 40.0f;
        get_screen_pos(0.0f, fInstructionY, &fScreenX, &fScreenY, &fGlobalScale);
        int iInstructionY = (int)fScreenY;

        rdpq_textparms_t tpInstruction = m_tpCenterHorizontally;
        tpInstruction.style_id = FONT_STYLE_GREEN;

        rdpq_text_printf(&tpInstruction, FONT_NORMAL, 2, iInstructionY, "3x rotate joystick in a full circle.");
        rdpq_text_printf(&tpInstruction, FONT_NORMAL, 2, iInstructionY + 12, "Then press START.");
    }

    /* --- Render Stats --- */
    /* Calculate text position accounting for overscan */
    get_screen_pos(0.0f, CALIB_TEXT_Y, &fScreenX, &fScreenY, &fGlobalScale);
    int iTextY = (int)fScreenY;

    char szStatsX[64];
    char szStatsY[64];
    snprintf(szStatsX, sizeof(szStatsX), "X:%02d (%02d|%02d)", abs(s_iCurrentX), abs(s_iMinX), abs(s_iMaxX));
    snprintf(szStatsY, sizeof(szStatsY), "Y:%02d (%02d|%02d)", abs(s_iCurrentY), abs(s_iMinY), abs(s_iMaxY));

    rdpq_textparms_t tpGreen = m_tpCenterHorizontally;
    tpGreen.style_id = FONT_STYLE_GREEN;

    rdpq_text_printf(&tpGreen, FONT_NORMAL, 2, iTextY, "%s", szStatsX);
    rdpq_text_printf(&tpGreen, FONT_NORMAL, 2, iTextY + 12, "%s", szStatsY);
}

bool stick_calibration_is_active_without_menu(void)
{
    return s_bActiveWithoutMenu;
}
