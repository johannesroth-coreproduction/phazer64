#include "minimap.h"
#include "audio.h"
#include "camera.h"
#include "dialogue.h"
#include "display.h"
#include "font_helper.h"
#include "frame_time.h"
#include "game_objects/gp_state.h"
#include "game_objects/race_handler.h"
#include "game_objects/tractor_beam.h"
#include "game_objects/ufo.h"
#include "libdragon.h"
#include "math_helper.h"
#include "minimap_marker.h"
#include "palette.h"
#include "rdpq.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "resource_helper.h"
#include "stick_normalizer.h"
#include "ui.h"
#include <limits.h>
#include <math.h>

#define MINIMAP_UI_TEXT_TARGET "TARGET"
#define MINIMAP_UI_TEXT_PIN "PIN"
#define MINIMAP_UI_TEXT_CLEAR "CLEAR"

typedef enum
{
    MINIMAP_STATE_INACTIVE,
    MINIMAP_STATE_ZOOMING_IN,  /* Transitioning to minimap */
    MINIMAP_STATE_ACTIVE,      /* Fully active */
    MINIMAP_STATE_ZOOMING_OUT, /* Transitioning back to normal */
} minimap_state_t;

static minimap_state_t m_state = MINIMAP_STATE_INACTIVE;
static float m_fAnimTimer = 0.0f;
static float m_fCurrentCloseDuration = 0.0f;
static struct vec2 m_vCameraTranslation = {0.0f, 0.0f};
static struct vec2 m_vCloseStartTranslation = {0.0f, 0.0f}; /* Translation at start of closing */
static float m_fBgFadeTimer = 0.0f;                         /* Timer for background fade in/out */

/* UI sprites */
static sprite_t *m_pBtnCUp = NULL;
static sprite_t *m_pBtnCDown = NULL;
static sprite_t *m_pHudMinimapIcon = NULL;
static sprite_t *m_pHudCrosshair = NULL;
static sprite_t *m_pBtnA = NULL;
static sprite_t *m_pBtnR = NULL;

/* Sound effects */
static wav64_t *m_pSfxOpen = NULL;
static wav64_t *m_pSfxPin = NULL;
static wav64_t *m_pSfxClear = NULL;
static wav64_t *m_pSfxClose = NULL;

/* Cached text widths */
static float s_fWaypointTextWidth = 0.0f;
static float s_fPinTextWidth = 0.0f;
static float s_fClearTargetTextWidth = 0.0f;

void minimap_init(void)
{
    m_state = MINIMAP_STATE_INACTIVE;
    m_fAnimTimer = 0.0f;
    m_fCurrentCloseDuration = 0.0f;
    m_vCameraTranslation = vec2_zero();
    m_vCloseStartTranslation = vec2_zero();
    m_fBgFadeTimer = 0.0f;

    /* Load UI sprites */
    if (!m_pBtnCUp)
        m_pBtnCUp = sprite_load("rom:/btn_c_up_00.sprite");
    if (!m_pBtnCDown)
        m_pBtnCDown = sprite_load("rom:/btn_c_down_00.sprite");
    if (!m_pHudMinimapIcon)
        m_pHudMinimapIcon = sprite_load("rom:/hud_minimap_icon_00.sprite");
    if (!m_pHudCrosshair)
        m_pHudCrosshair = sprite_load("rom:/hud_crosshair_00.sprite");
    if (!m_pBtnA)
        m_pBtnA = sprite_load("rom:/btn_a_00.sprite");
    if (!m_pBtnR)
        m_pBtnR = sprite_load("rom:/btn_r_00.sprite");

    /* Load sound effects */
    if (!m_pSfxOpen)
        m_pSfxOpen = wav64_load("rom:/minimap_open.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    if (!m_pSfxPin)
        m_pSfxPin = wav64_load("rom:/minimap_pin.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    if (!m_pSfxClear)
        m_pSfxClear = wav64_load("rom:/minimap_clear.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    if (!m_pSfxClose)
        m_pSfxClose = wav64_load("rom:/minimap_close.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    /* Cache text widths (only once) */
    if (s_fWaypointTextWidth == 0.0f)
    {
        s_fWaypointTextWidth = font_helper_get_text_width(FONT_NORMAL, MINIMAP_UI_TEXT_TARGET);
        s_fPinTextWidth = font_helper_get_text_width(FONT_NORMAL, MINIMAP_UI_TEXT_PIN);
        s_fClearTargetTextWidth = font_helper_get_text_width(FONT_NORMAL, MINIMAP_UI_TEXT_CLEAR);
    }

    /* Initialize minimap markers */
    minimap_marker_init();
}

/* Ease-out cubic: starts fast, slows down at end */
static float ease_out_cubic(float t)
{
    float fInvT = t - 1.0f;
    return fInvT * fInvT * fInvT + 1.0f;
}

/* Ease-in cubic: slow start, fast end */
static float ease_in_cubic(float t)
{
    return t * t * t;
}

void minimap_update(bool _bCUp, bool _bCDown, bool _bActivateMarkerBtn, bool _bClearMarkerBtn, int _iStickX, int _iStickY)
{
    /* Early out if minimap is not unlocked */
    if (!gp_state_unlock_get(GP_UNLOCK_MINIMAP))
        return;

    float fDt = frame_time_delta_seconds();

    /* Check if race is active - if so, force minimap to close and prevent activation */
    if (race_handler_is_race_active())
    {
        if (m_state != MINIMAP_STATE_INACTIVE)
        {
            minimap_init();
        }
        return;
    }

    /* Only handle state changes if we are in SPACE */
    if (gp_state_get() != SPACE)
    {
        /* Force reset if we somehow leave SPACE while active */
        if (m_state != MINIMAP_STATE_INACTIVE)
        {
            minimap_init();
        }
        return;
    }

    switch (m_state)
    {
    case MINIMAP_STATE_INACTIVE:
        /* Update piece markers only (fast path) so UFO next-target indicator works correctly */
        minimap_marker_update(true);

        /* Can only activate if:
           1. C-Up is pressed
           2. State transition is NOT running (gp_state_accepts_input)
           3. Tractor beam is NOT active (prevent input conflict)
           4. Race is NOT active (checked above)
        */
        if (_bCUp && gp_state_accepts_input() && !tractor_beam_is_active())
        {
            m_state = MINIMAP_STATE_ZOOMING_IN;
            m_fAnimTimer = 0.0f;
            m_vCameraTranslation = vec2_zero();
            /* Play open sound */
            if (m_pSfxOpen)
                wav64_play(m_pSfxOpen, MIXER_CHANNEL_USER_INTERFACE);
        }
        break;

    case MINIMAP_STATE_ZOOMING_IN:
        m_fAnimTimer += fDt;
        if (m_fAnimTimer >= MINIMAP_OPEN_TIME)
        {
            m_fAnimTimer = MINIMAP_OPEN_TIME;
            m_state = MINIMAP_STATE_ACTIVE;
            m_fBgFadeTimer = 0.0f;
            /* Request terra-pos and update terra marker when minimap opens (ensures planets_init has run) */
            minimap_marker_update_terra();
            /* Clean up stale PIN markers when minimap opens */
            minimap_marker_cleanup_stale_pin();
        }
        break;

    case MINIMAP_STATE_ACTIVE:
        /* Update fade in timer */
        m_fBgFadeTimer += fDt;
        if (m_fBgFadeTimer > MINIMAP_BG_FADE_IN_TIME)
            m_fBgFadeTimer = MINIMAP_BG_FADE_IN_TIME;

        /* Update all markers (minimap is active, so update everything) */
        minimap_marker_update(false);

        /* Handle clear marker button: clear UFO next target and cleanup stale PIN markers */
        if (_bClearMarkerBtn)
        {
            ufo_set_next_target(NULL);
            minimap_marker_cleanup_stale_pin();
            /* Play clear sound */
            if (m_pSfxClear)
                wav64_play(m_pSfxClear, MIXER_CHANNEL_USER_INTERFACE);
        }

        /* Handle activate marker button: set marker as UFO target */
        if (_bActivateMarkerBtn)
        {
            /* Check for marker at screen center using screen-space collision */
            struct vec2i vScreenCenter = {SCREEN_W / 2, SCREEN_H / 2};
            const struct entity2D *pMarker = minimap_marker_get_at_screen_point(vScreenCenter);
            if (pMarker)
            {
                ufo_set_next_target(pMarker);
                /* Play pin sound */
                if (m_pSfxPin)
                    wav64_play(m_pSfxPin, MIXER_CHANNEL_USER_INTERFACE);
            }
            else
            {
                /* No marker at center - create PIN marker at screen center world position */
                struct vec2 vWorldPos;
                camera_screen_to_world(&g_mainCamera, vScreenCenter, &vWorldPos);
                const struct entity2D *pPinMarker = minimap_marker_set_at_pos(vWorldPos, MARKER_PIN);
                if (pPinMarker)
                {
                    ufo_set_next_target(pPinMarker);
                    /* Play pin sound */
                    if (m_pSfxPin)
                        wav64_play(m_pSfxPin, MIXER_CHANNEL_USER_INTERFACE);
                }
            }

            /* Clear any existing PIN markers before setting a new target */
            minimap_marker_cleanup_stale_pin();
        }

        if (_bCDown)
        {
            /* Check if camera has traveled too far - if so, teleport back first */
            float fDistance = vec2_mag(m_vCameraTranslation);
            if (fDistance > MINIMAP_MAX_TRAVEL_BACK_DISTANCE)
            {
                debugf("Teleporting camera back to UFO position immediately\n");
                /* Teleport camera back to UFO position immediately */
                m_vCameraTranslation = vec2_zero();
                m_vCloseStartTranslation = vec2_zero();
            }
            else
            {
                /* Store starting translation for interpolation */
                m_vCloseStartTranslation = m_vCameraTranslation;
            }

            m_state = MINIMAP_STATE_ZOOMING_OUT;
            m_fAnimTimer = 0.0f;
            /* Start fade out when zoom back in begins */
            m_fBgFadeTimer = MINIMAP_BG_FADE_OUT_TIME; /* Start from max, fade to 0 */
            /* Play close sound */
            if (m_pSfxClose)
                wav64_play(m_pSfxClose, MIXER_CHANNEL_USER_INTERFACE);

            /* Calculate dynamic close duration based on distance */
            fDistance = vec2_mag(m_vCameraTranslation);
            /* Duration = Distance / Speed */
            m_fCurrentCloseDuration = fDistance / MINIMAP_CLOSE_MAX_SPEED;

            /* Clamp to minimum duration for smoothness */
            if (m_fCurrentCloseDuration < MINIMAP_CLOSE_TIME_MIN)
                m_fCurrentCloseDuration = MINIMAP_CLOSE_TIME_MIN;
        }
        else
        {
            /* Process stick input for camera movement */
            struct vec2 vStickInput = vec2_make((float)_iStickX, (float)-_iStickY);
            float fStickMagnitude = vec2_mag(vStickInput);
            float fStickForce = 0.0f;

            if (fStickMagnitude >= STICK_DEADZONE)
            {
                float fEffectiveMagnitude = fStickMagnitude - STICK_DEADZONE;
                fStickForce = clampf_01(fEffectiveMagnitude / (STICK_MAX_MAGNITUDE - STICK_DEADZONE));
            }

            if (fStickForce > 0.0f)
            {
                float fSpeed = MINIMAP_CAMERA_SPEED_MIN + fStickForce * (MINIMAP_CAMERA_SPEED_MAX - MINIMAP_CAMERA_SPEED_MIN);
                struct vec2 vDir = vec2_scale(vStickInput, 1.0f / fStickMagnitude);
                m_vCameraTranslation = vec2_add(m_vCameraTranslation, vec2_scale(vDir, fSpeed * fDt));
            }
        }
        break;

    case MINIMAP_STATE_ZOOMING_OUT:
        m_fAnimTimer += fDt;

        m_fBgFadeTimer -= fDt;
        if (m_fBgFadeTimer < 0.0f)
            m_fBgFadeTimer = 0.0f;

        float t = (m_fCurrentCloseDuration > 0.0f) ? clampf_01(m_fAnimTimer / m_fCurrentCloseDuration) : 1.0f;

        /* Interpolate translation back to zero using Ease-Out to make it quick initially */
        float fMoveT = ease_out_cubic(t);
        m_vCameraTranslation = vec2_mix(m_vCloseStartTranslation, vec2_zero(), fMoveT);

        if (m_fAnimTimer >= m_fCurrentCloseDuration)
        {
            m_state = MINIMAP_STATE_INACTIVE;
            m_fAnimTimer = 0.0f;
            m_fBgFadeTimer = 0.0f;
            m_vCameraTranslation = vec2_zero();
        }
        break;
    }
}

bool minimap_is_active(void)
{
    return m_state != MINIMAP_STATE_INACTIVE;
}

float minimap_get_zoom_progress(void)
{
    switch (m_state)
    {
    case MINIMAP_STATE_INACTIVE:
        return 0.0f;
    case MINIMAP_STATE_ACTIVE:
        return 1.0f;
    case MINIMAP_STATE_ZOOMING_IN:
        return ease_out_cubic(clampf_01(m_fAnimTimer / MINIMAP_OPEN_TIME));
    case MINIMAP_STATE_ZOOMING_OUT:
    {
        float t = (m_fCurrentCloseDuration > 0.0f) ? clampf_01(m_fAnimTimer / m_fCurrentCloseDuration) : 1.0f;
        /* Use previous calculation but snap to 0.0 (zoom 1.0) when t >= 0.95 */
        if (t >= 0.95f)
            return 0.0f;
        return 1.0f - ease_in_cubic(t);
    }
    default:
        return 0.0f;
    }
}

struct vec2 minimap_get_camera_translation(void)
{
    return m_vCameraTranslation;
}

/* Helper: Calculate background fade alpha based on current state */
static uint8_t minimap_get_bg_alpha(void)
{
    if (!minimap_is_active() || m_state == MINIMAP_STATE_ZOOMING_IN)
        return 0;

    float fFadeProgress;
    if (m_state == MINIMAP_STATE_ACTIVE)
    {
        /* Fade in: use separate timer for smooth fade-in after zoom completes */
        fFadeProgress = clampf_01(m_fBgFadeTimer / MINIMAP_BG_FADE_IN_TIME);
    }
    else /* MINIMAP_STATE_ZOOMING_OUT */
    {
        /* Fade out: use zoom progress so it syncs with zoom animation */
        /* Zoom progress goes from 1.0 (minimap view) to 0.0 (normal view) */
        /* We want alpha to match: start at 1.0 (visible) and fade to 0.0 (invisible) */
        float fZoomProgress = minimap_get_zoom_progress();
        fFadeProgress = fZoomProgress;
    }
    return (uint8_t)(fFadeProgress * 255.0f);
}

/* Helper: Set up RDP mode for alpha-blended rendering */
static void minimap_setup_rdp_alpha(uint8_t uAlpha)
{
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    if (uAlpha < 255)
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
}

void minimap_render_ui(void)
{
    /* Early out if minimap is not unlocked */
    if (!gp_state_unlock_get(GP_UNLOCK_MINIMAP))
        return;

    /* Don't render UI during dialogue */
    if (dialogue_is_active())
        return;

    /* Don't render UI if race is active */
    if (race_handler_is_race_active())
        return;

    /* Set up copy mode, no transparency, alphacompare 1 */
    rdpq_set_mode_copy(false);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_POINT);

    if (m_state == MINIMAP_STATE_ACTIVE)
    {
        if (m_pBtnCDown)
        {
            struct vec2i vPos = ui_get_pos_top_left_sprite(m_pBtnCDown);
            vPos.iX += 2;
            vPos.iY += 2;
            rdpq_sprite_blit(m_pBtnCDown, vPos.iX, vPos.iY, NULL);
        }
    }
    else if (m_pBtnCUp && m_pHudMinimapIcon)
    {
        struct vec2i vTopLeft = ui_get_pos_top_left_sprite(m_pBtnCUp);
        vTopLeft.iX += 2;
        vTopLeft.iY += 2;
        rdpq_sprite_blit(m_pBtnCUp, vTopLeft.iX, vTopLeft.iY, NULL);
        rdpq_sprite_blit(m_pHudMinimapIcon, vTopLeft.iX + m_pBtnCUp->width + MINIMAP_UI_BUTTON_ICON_PADDING, vTopLeft.iY - 2, NULL);
    }
}

void minimap_render_bg(void)
{
    /* Early out if minimap is not unlocked */
    if (!gp_state_unlock_get(GP_UNLOCK_MINIMAP))
        return;

    uint8_t uAlpha = minimap_get_bg_alpha();
    if (uAlpha == 0)
        return;

    /* Calculate grid area using SCREEN_W/H, accounting for overscan padding and border */
    int iPadding = ui_get_overscan_padding();
    int iLeft = iPadding + MINIMAP_BG_BORDER_THICKNESS;
    int iTop = iPadding + MINIMAP_BG_BORDER_THICKNESS;
    int iRight = SCREEN_W - iPadding - MINIMAP_BG_BORDER_THICKNESS;
    int iBottom = SCREEN_H - iPadding - MINIMAP_BG_BORDER_THICKNESS;

    /* Set up RDP mode for alpha blending */
    minimap_setup_rdp_alpha(uAlpha);

    /* Render darker green grid with alpha - screen-space with camera offset */
    /* CGA_GREEN is RGBA32(0, 170, 0, 255) */
    rdpq_set_prim_color(RGBA32(0, 64, 0, uAlpha));

    /* Map alpha (0-255) to line length: 0 = 0%, 128 = 50%, 255 = 100% */
    /* During ZOOMING_OUT, keep lines at full length (only fade alpha) */
    float fLengthFactor = (m_state == MINIMAP_STATE_ZOOMING_OUT) ? 1.0f : ((float)uAlpha / 255.0f);
    int iWidth = iRight - iLeft;
    int iHeight = iBottom - iTop;
    int iMaxHeight = iTop + (int)(iHeight * fLengthFactor);

    /* Calculate camera offset in screen-space (modulo grid step for wrapping) */
    /* Use minimap zoom level to keep grid stable during zoom transitions */
    const camera2D *pCamera = &g_mainCamera;
    float fZoom = MINIMAP_ZOOM_LEVEL; /* Use fixed minimap zoom for stable grid */
    float fCamScreenOffsetX = -pCamera->vPos.fX * fZoom;
    float fCamScreenOffsetY = -pCamera->vPos.fY * fZoom;

    /* Calculate grid offset (modulo grid step, ensure positive) */
    float fGridOffsetX = fmodf(fCamScreenOffsetX, (float)MINIMAP_BG_GRID_STEP_X);
    float fGridOffsetY = fmodf(fCamScreenOffsetY, (float)MINIMAP_BG_GRID_STEP_Y);
    if (fGridOffsetX < 0.0f)
        fGridOffsetX += (float)MINIMAP_BG_GRID_STEP_X;
    if (fGridOffsetY < 0.0f)
        fGridOffsetY += (float)MINIMAP_BG_GRID_STEP_Y;

    /* Draw vertical grid lines (screen-space with offset, animated length from top) */
    for (int iX = iLeft + (int)fGridOffsetX; iX < iRight; iX += MINIMAP_BG_GRID_STEP_X)
    {
        if (iMaxHeight > iTop)
            rdpq_fill_rectangle(iX, iTop, iX + MINIMAP_BG_GRID_LINE_THICKNESS, iMaxHeight);
    }

    /* Draw horizontal grid lines (screen-space with offset, animated length from left) */
    int iMaxWidth = iLeft + (int)(iWidth * fLengthFactor);
    for (int iY = iTop + (int)fGridOffsetY; iY < iBottom; iY += MINIMAP_BG_GRID_STEP_Y)
    {
        if (iMaxWidth > iLeft)
            rdpq_fill_rectangle(iLeft, iY, iMaxWidth, iY + MINIMAP_BG_GRID_LINE_THICKNESS);
    }
}

void minimap_render_fg(void)
{
    /* Early out if minimap is not unlocked */
    if (!gp_state_unlock_get(GP_UNLOCK_MINIMAP))
        return;

    uint8_t uAlpha = minimap_get_bg_alpha();
    if (uAlpha == 0)
        return;

    /* Calculate border area using SCREEN_W/H, accounting for overscan padding */
    int iPadding = ui_get_overscan_padding();
    int iLeft = iPadding + 4; // HACK as pixels are missing on screen and VI solution didnt work?
    int iTop = iPadding;
    int iRight = SCREEN_W - iPadding - 3; // HACK as pixels are missing on screen and VI solution didnt work?
    int iBottom = SCREEN_H - iPadding;

    /* Set up RDP mode for alpha blending */
    minimap_setup_rdp_alpha(uAlpha);

    /* Render light green border with alpha - animate line lengths based on alpha */
    rdpq_set_prim_color(RGBA32(0, 170, 0, uAlpha));
    /* During ZOOMING_OUT, keep lines at full length (only fade alpha) */
    float fLengthFactor = (m_state == MINIMAP_STATE_ZOOMING_OUT) ? 1.0f : ((float)uAlpha / 255.0f);
    int iWidth = iRight - iLeft;
    int iHeight = iBottom - iTop;
    int iEndX = iLeft + (int)(iWidth * fLengthFactor);
    int iEndY = iTop + (int)(iHeight * fLengthFactor);

    if (iEndX > iLeft)
    {
        rdpq_fill_rectangle(iLeft, iTop, iEndX, iTop + MINIMAP_BG_BORDER_THICKNESS);       /* Top */
        rdpq_fill_rectangle(iLeft, iBottom - MINIMAP_BG_BORDER_THICKNESS, iEndX, iBottom); /* Bottom */
    }
    if (iEndY > iTop)
    {
        rdpq_fill_rectangle(iLeft, iTop, iLeft + MINIMAP_BG_BORDER_THICKNESS, iEndY);   /* Left */
        rdpq_fill_rectangle(iRight - MINIMAP_BG_BORDER_THICKNESS, iTop, iRight, iEndY); /* Right */
    }

    /* Render markers */
    minimap_marker_render();

    rdpq_set_mode_copy(false);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_POINT);

    /* Render crosshair centered on screen */
    if (m_pHudCrosshair)
    {
        struct vec2i vCrosshairPos = {(SCREEN_W - m_pHudCrosshair->width) / 2, (SCREEN_H - m_pHudCrosshair->height) / 2};
        rdpq_sprite_blit(m_pHudCrosshair, vCrosshairPos.iX, vCrosshairPos.iY, NULL);
    }

    // no button prompt ui during zooming in/out
    if (m_state == MINIMAP_STATE_ZOOMING_IN || m_state == MINIMAP_STATE_ZOOMING_OUT)
        return;

    /* Check if a marker can be selected at screen center */
    struct vec2i vScreenCenter = {SCREEN_W / 2, SCREEN_H / 2};
    const struct entity2D *pMarkerAtCenter = minimap_marker_get_at_screen_point(vScreenCenter);
    const struct entity2D *pCurrentTarget = ufo_get_next_target();

    /* Determine which prompts to show */
    /* Show waypoint prompt if: (1) there's a marker at center that's not current target, OR (2) no marker at center (can PIN) */
    bool bShowWaypoint = ((pMarkerAtCenter && pMarkerAtCenter != pCurrentTarget) || !pMarkerAtCenter) && m_pBtnA;
    bool bShowClearTarget = (pCurrentTarget && m_pBtnR);

    if (!bShowWaypoint && !bShowClearTarget)
        return;

    /* Always use A button height for consistent Y positioning */
    int iMaxButtonHeight = m_pBtnA ? m_pBtnA->height : (m_pBtnR ? m_pBtnR->height : 0);

    /* Calculate total width - use PIN text width if no marker at center, TARGET if marker at center */
    float fTotalWidth = 0.0f;
    bool bShowPinText = !pMarkerAtCenter;
    float fWaypointTextWidth = bShowPinText ? s_fPinTextWidth : s_fWaypointTextWidth;
    if (bShowWaypoint)
        fTotalWidth += (float)m_pBtnA->width + UI_DESIGNER_PADDING + fWaypointTextWidth;
    if (bShowClearTarget)
    {
        if (bShowWaypoint)
            fTotalWidth += UI_DESIGNER_PADDING * 2;
        fTotalWidth += (float)m_pBtnR->width + UI_DESIGNER_PADDING + s_fClearTargetTextWidth;
    }

    /* Calculate positions */
    int iStartX = (SCREEN_W / 2) - (int)(fTotalWidth / 2.0f);
    int iBaseY = SCREEN_H - iPadding - 2 * UI_DESIGNER_PADDING - iMaxButtonHeight;
    int iWaypointButtonX = 0, iWaypointButtonY = 0, iWaypointTextX = 0, iWaypointTextY = 0;
    int iClearTargetButtonX = 0, iClearTargetButtonY = 0, iClearTargetTextX = 0, iClearTargetTextY = 0;

    /* Render all button sprites */
    if (bShowWaypoint)
    {
        iWaypointButtonY = iBaseY + (iMaxButtonHeight - m_pBtnA->height) / 2;
        iWaypointButtonX = iStartX;
        rdpq_sprite_blit(m_pBtnA, iWaypointButtonX, iWaypointButtonY, NULL);
        iWaypointTextX = iWaypointButtonX + m_pBtnA->width + UI_DESIGNER_PADDING;
        iWaypointTextY = iWaypointButtonY + (m_pBtnA->height / 2) + UI_FONT_Y_OFFSET - 4;
        iStartX += m_pBtnA->width + UI_DESIGNER_PADDING + (int)fWaypointTextWidth + UI_DESIGNER_PADDING * 2;
    }

    if (bShowClearTarget)
    {
        iClearTargetButtonY = iBaseY + (iMaxButtonHeight - m_pBtnR->height) / 2;
        iClearTargetButtonX = iStartX;
        rdpq_sprite_blit(m_pBtnR, iClearTargetButtonX, iClearTargetButtonY, NULL);
        iClearTargetTextX = iClearTargetButtonX + m_pBtnR->width + UI_DESIGNER_PADDING;
        iClearTargetTextY = iClearTargetButtonY + (m_pBtnR->height / 2) + UI_FONT_Y_OFFSET - 4;
    }

    /* Render all text */
    if (bShowWaypoint)
    {
        if (bShowPinText)
            rdpq_text_printf(NULL, FONT_NORMAL, iWaypointTextX, iWaypointTextY, MINIMAP_UI_TEXT_PIN);
        else
            rdpq_text_printf(NULL, FONT_NORMAL, iWaypointTextX, iWaypointTextY, MINIMAP_UI_TEXT_TARGET);
    }
    if (bShowClearTarget)
        rdpq_text_printf(NULL, FONT_NORMAL, iClearTargetTextX, iClearTargetTextY, MINIMAP_UI_TEXT_CLEAR);
}
