#include "tractor_beam.h"

#include "../audio.h"
#include "../dialogue.h"
#include "../entity2d.h"
#include "../frame_time.h"
#include "../math2d.h"
#include "../math_helper.h"
#include "../minimap.h"
#include "../resource_helper.h"
#include "../save.h"
#include "../ui.h"
#include "gp_state.h"
#include "libdragon.h"
#include "rdpq.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "rdpq_tex.h"
#include "rdpq_tri.h"
#include "ufo.h"
#include <math.h>
#include <stdint.h>

#define TRACTOR_ROTATE_SPEED 0.04f
#define TRACTOR_TURN_SPEED 0.05f
#define TRACTOR_DISTANCE_STEP 3.0f
#define TRACTOR_MIN_DISTANCE 26.0f
#define TRACTOR_MAX_DISTANCE 320.0f

#define TRACTOR_FADEIN_FRAMES 8
#define TRACTOR_ALPHA_FLICKER_STRENGTH 0.3f
#define TRACTOR_TEX_SCROLL_SPEED 1.0f
#define TRACTOR_TEX_WOBBLE_AMPLITUDE 5
#define TRACTOR_TEX_WOBBLE_FREQ 0.25f
#define TRACTOR_TEX_STRETCH_AMPLITUDE 0.5f
#define TRACTOR_TEX_STRETCH_FREQ 0.15f

static wav64_t *m_pTractorLoop = NULL;
static bool m_bActive = false;
static bool m_bPrevBeamPressed = false;
static struct entity2D *m_pGrabbedTarget = NULL;
static sprite_t *m_pTractorBeamSprite = NULL;
static sprite_t *m_pBtnR = NULL;
static sprite_t *m_pTractorBeamLayout = NULL;
static sprite_t *m_pTractorBeamLayoutAb = NULL;
static rdpq_texparms_t m_beamTexParms = {0};
static float m_fBeamTexWidth = 1.0f;  /* cached tex width in pixels */
static float m_fBeamTexHeight = 1.0f; /* cached tex height in pixels */
static float m_fBeamFrames = 0.0f;    /* frames since last activation (for fade/flicker) */
static float m_fBeamScroll = 0.0f;    /* scrolling offset for the beam texture */

// Provide mutable (non-const) pointer to the currently locked target, for beam manipulation
static struct entity2D *tractor_beam_target_mutable(void)
{
    return (struct entity2D *)ufo_get_locked_target();
}

static void tractor_beam_stop_audio(void)
{
    if (mixer_ch_playing(MIXER_CHANNEL_WEAPONS))
        mixer_ch_stop(MIXER_CHANNEL_WEAPONS);
}

static void tractor_beam_release_target(struct entity2D *pTarget)
{
    if (pTarget)
    {
        pTarget->bGrabbed = false;
    }
}

void tractor_beam_disengage(void)
{
    if (m_bActive)
    {
        /* Release the previously grabbed target */
        tractor_beam_release_target(m_pGrabbedTarget);
        m_pGrabbedTarget = NULL;
        tractor_beam_stop_audio();
        m_bActive = false;
        m_fBeamFrames = 0.0f;
        m_fBeamScroll = 0.0f;
    }
}

/* Clamp target distance to valid range. Always call this to ensure distance limits are enforced. */
static void tractor_beam_clamp_distance(struct entity2D *_pTarget, const struct vec2 *_pUfoPos)
{
    struct vec2 vDelta = vec2_sub(_pTarget->vPos, *_pUfoPos);
    float fDist = vec2_mag(vDelta);
    struct vec2 vDir = vec2_zero();

    if (fDist > 1e-6f)
        vDir = vec2_scale(vDelta, 1.0f / fDist);
    else
        vDir = vec2_make(fm_sinf(ufo_get_angle_rad()), -fm_cosf(ufo_get_angle_rad()));

    float fClampedDist = clampf(fDist, TRACTOR_MIN_DISTANCE, TRACTOR_MAX_DISTANCE);

    _pTarget->vPos = vec2_add(*_pUfoPos, vec2_scale(vDir, fClampedDist));
}

/* Check if there's a valid target (locked or potential) within tractor beam range. */
static bool tractor_beam_is_target_in_range(void)
{
    const struct entity2D *pTarget = NULL;

    /* Determine which target to check: locked target or potential target */
    if (ufo_is_target_locked())
    {
        pTarget = ufo_get_locked_target();
    }
    else
    {
        pTarget = ufo_get_potential_target();
    }

    /* If no target available, return false */
    if (!pTarget || !entity2d_is_active(pTarget))
        return false;

    /* Check distance to target */
    struct vec2 vUfoPos = ufo_get_position();
    struct vec2 vDelta = vec2_sub(pTarget->vPos, vUfoPos);
    float fDist = vec2_mag(vDelta);

    return (fDist <= TRACTOR_MAX_DISTANCE);
}

void tractor_beam_init(void)
{
    tractor_beam_free();

    if (!m_pTractorLoop)
    {
        m_pTractorLoop = wav64_load("rom:/tractor_beam.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
        if (m_pTractorLoop)
        {
            wav64_set_loop(m_pTractorLoop, true);
        }
    }

    if (!m_pTractorBeamSprite)
    {
        m_pTractorBeamSprite = sprite_load("rom:/tractor_beam_00.sprite");
        if (m_pTractorBeamSprite)
        {
            m_fBeamTexWidth = (float)m_pTractorBeamSprite->width;
            m_fBeamTexHeight = (float)m_pTractorBeamSprite->height;
            m_beamTexParms = (rdpq_texparms_t){
                .s = {.repeats = REPEAT_INFINITE, .mirror = MIRROR_NONE},
                .t = {.repeats = 1.0f, .mirror = MIRROR_NONE},
            };
        }
    }

    if (!m_pBtnR)
    {
        m_pBtnR = sprite_load("rom:/btn_tractor_beam_00.sprite");
    }

    if (!m_pTractorBeamLayout)
    {
        m_pTractorBeamLayout = sprite_load("rom:/tractor_beam_layout_00.sprite");
    }

    if (!m_pTractorBeamLayoutAb)
    {
        m_pTractorBeamLayoutAb = sprite_load("rom:/tractor_beam_layout_ab_00.sprite");
    }
}

void tractor_beam_free(void)
{
    SAFE_CLOSE_WAV64(m_pTractorLoop);
    SAFE_FREE_SPRITE(m_pTractorBeamSprite);
    SAFE_FREE_SPRITE(m_pBtnR);
    SAFE_FREE_SPRITE(m_pTractorBeamLayout);
    SAFE_FREE_SPRITE(m_pTractorBeamLayoutAb);
    m_bActive = false;
    m_bPrevBeamPressed = false;
}

void tractor_beam_update(bool _bBeamPressed, bool _bTurnCW, bool _bTurnCCW, bool _bRotateCW, bool _bRotateCCW, bool _bExtend, bool _bRetract)
{
    /* Disable tractor beam if not unlocked */
    if (!gp_state_unlock_get(GP_UNLOCK_TRACTOR_BEAM))
    {
        _bBeamPressed = false;
        _bTurnCW = false;
        _bTurnCCW = false;
        _bRotateCW = false;
        _bRotateCCW = false;
        _bExtend = false;
        _bRetract = false;
    }

    /* Disable tractor beam input when gameplay input is blocked (minimap, cutscenes, transitions) */
    if (!gp_state_accepts_input())
    {
        _bBeamPressed = false;
        _bTurnCW = false;
        _bTurnCCW = false;
        _bRotateCW = false;
        _bRotateCCW = false;
        _bExtend = false;
        _bRetract = false;
    }

    float fFrameMul = frame_time_mul();

    struct entity2D *pTarget = NULL;
    if (ufo_is_target_locked())
    {
        pTarget = tractor_beam_target_mutable();
        if (pTarget && !entity2d_is_active(pTarget))
        {
            pTarget = NULL;
        }
    }

    bool bToggleMode = save_get_target_lock_toggle_mode();
    bool bBeamPressedEdge = _bBeamPressed && !m_bPrevBeamPressed;
    m_bPrevBeamPressed = _bBeamPressed;

    bool bShouldActivate = false;
    if (bToggleMode)
    {
        /* Toggle mode: toggle on button press edge */
        if (bBeamPressedEdge && pTarget)
        {
            if (!m_bActive)
            {
                /* Activating: check if target is within range (distance check only on activation) */
                bool bTargetInRange = tractor_beam_is_target_in_range();
                if (bTargetInRange)
                {
                    m_bActive = true;
                }
            }
            else
            {
                /* Deactivating: just toggle off */
                m_bActive = false;
            }
        }
        /* In toggle mode, stay active until toggled off (or target lost) - no distance check during use */
        bShouldActivate = m_bActive && pTarget;
    }
    else
    {
        /* Hold mode */
        if (bBeamPressedEdge && pTarget)
        {
            /* Check distance only on activation (button press edge) */
            bool bTargetInRange = tractor_beam_is_target_in_range();
            if (bTargetInRange)
            {
                m_bActive = true;
            }
        }
        /* Hold mode: stay active while button is held and target exists - no distance check during use */
        bShouldActivate = _bBeamPressed && pTarget && m_bActive;
    }

    if (!bShouldActivate)
    {
        if (m_bActive)
        {
            /* Release the previously grabbed target */
            tractor_beam_release_target(m_pGrabbedTarget);
            m_pGrabbedTarget = NULL;
            tractor_beam_stop_audio();
        }
        m_bActive = false;
        m_fBeamFrames = 0.0f;
        m_fBeamScroll = 0.0f;
        return;
    }

    /* Check if this is the first frame of activation (before setting m_bActive) */
    bool bJustActivated = !m_bActive;
    m_bActive = true;

    if (bJustActivated)
    {
        m_fBeamFrames = 0.0f;
        m_fBeamScroll = 0.0f;
    }

    /* Release old grabbed target if lock changed while beam stays active */
    if (m_pGrabbedTarget && m_pGrabbedTarget != pTarget)
    {
        tractor_beam_release_target(m_pGrabbedTarget);
        m_pGrabbedTarget = NULL;
    }

    /* Mark target as grabbed (ensure it's set every frame) */
    pTarget->bGrabbed = true;
    m_pGrabbedTarget = pTarget;

    /* Match target velocity to the UFO (every frame while active). */
    struct vec2 vUfoPos = ufo_get_position();
    struct vec2 vUfoVel = ufo_get_velocity();
    pTarget->vVel = vUfoVel;

    /* When first activated, clamp distance if target is too far away. */
    if (bJustActivated)
    {
        tractor_beam_clamp_distance(pTarget, &vUfoPos);
    }

    /* Turn target entity (rotate fAngleRad) - old functionality */
    float fTurnDelta = 0.0f;
    if (_bRotateCW)
        fTurnDelta -= TRACTOR_TURN_SPEED;
    if (_bRotateCCW)
        fTurnDelta += TRACTOR_TURN_SPEED;
    if (fabsf(fTurnDelta) > 0.0f)
    {
        fTurnDelta *= fFrameMul;
        pTarget->fAngleRad = angle_wrap_rad(pTarget->fAngleRad + fTurnDelta);
    }

    /* Translate target around the UFO (orbital rotation) - new functionality */
    float fOrbitDelta = 0.0f;
    if (_bTurnCW)
        fOrbitDelta += TRACTOR_ROTATE_SPEED;
    if (_bTurnCCW)
        fOrbitDelta -= TRACTOR_ROTATE_SPEED;
    if (fabsf(fOrbitDelta) > 0.0f)
    {
        fOrbitDelta *= fFrameMul;
        struct vec2 vDelta = vec2_sub(pTarget->vPos, vUfoPos);
        float s = fm_sinf(fOrbitDelta);
        float c = fm_cosf(fOrbitDelta);
        struct vec2 vRot = vec2_make(vDelta.fX * c - vDelta.fY * s, vDelta.fX * s + vDelta.fY * c);
        pTarget->vPos = vec2_add(vUfoPos, vRot);
        /* Clamp distance after rotation to ensure it stays within bounds. */
        tractor_beam_clamp_distance(pTarget, &vUfoPos);
    }

    /* Optional distance adjustment along the line connecting UFO and target. */
    if (_bExtend || _bRetract)
    {
        struct vec2 vDelta = vec2_sub(pTarget->vPos, vUfoPos);
        float fDist = vec2_mag(vDelta);
        struct vec2 vDir = vec2_zero();
        if (fDist > 1e-6f)
            vDir = vec2_scale(vDelta, 1.0f / fDist);
        else
            vDir = vec2_make(fm_sinf(ufo_get_angle_rad()), -fm_cosf(ufo_get_angle_rad()));

        float fNewDist = fDist;
        if (_bExtend)
            fNewDist += TRACTOR_DISTANCE_STEP * fFrameMul;
        if (_bRetract)
            fNewDist -= TRACTOR_DISTANCE_STEP * fFrameMul;

        if (fNewDist < TRACTOR_MIN_DISTANCE)
            fNewDist = TRACTOR_MIN_DISTANCE;
        if (fNewDist > TRACTOR_MAX_DISTANCE)
            fNewDist = TRACTOR_MAX_DISTANCE;

        pTarget->vPos = vec2_add(vUfoPos, vec2_scale(vDir, fNewDist));
    }

    /* Always enforce distance limits to handle:
       - Targets that start beyond max distance when beam is activated
       - Distance drift from velocity matching or other sources
       - Ensure consistency after any position changes */
    tractor_beam_clamp_distance(pTarget, &vUfoPos);

    /* Start or keep playing the loop. */
    if (m_pTractorLoop && !mixer_ch_playing(MIXER_CHANNEL_WEAPONS))
    {
        wav64_play(m_pTractorLoop, MIXER_CHANNEL_WEAPONS);
    }

    /* Advance animation phases while active */
    m_fBeamScroll += TRACTOR_TEX_SCROLL_SPEED * fFrameMul;
    m_fBeamFrames += fFrameMul;
}

void tractor_beam_render(void)
{
    if (!m_bActive)
        return;

    if (!ufo_is_target_locked())
        return;

    const struct entity2D *pTarget = ufo_get_locked_target();
    if (!pTarget || !entity2d_is_active(pTarget))
        return;

    if (!m_pTractorBeamSprite)
        return;

    // always draw the beam, even if sth is out of screen, as it still exists anyway.
    /* Both points are visible while locked, but guard anyway. */
    // if (!camera_is_point_visible(_pCamera, pTarget->vPos, 0.0f))
    //     return;

    struct vec2 vUfoPos = ufo_get_position();
    // if (!camera_is_point_visible(_pCamera, vUfoPos, 0.0f))
    //     return;

    struct vec2i vUfoScreen;
    struct vec2i vTargetScreen;
    camera_world_to_screen(&g_mainCamera, vUfoPos, &vUfoScreen);
    camera_world_to_screen(&g_mainCamera, pTarget->vPos, &vTargetScreen);

    float fDx = (float)(vTargetScreen.iX - vUfoScreen.iX);
    float fDy = (float)(vTargetScreen.iY - vUfoScreen.iY);
    float fLen = sqrtf(fDx * fDx + fDy * fDy);
    if (fLen <= 1e-3f)
        return;

    float fZoom = camera_get_zoom(&g_mainCamera);

    /* Build a textured quad around the segment using a screen-space perpendicular. */
    float fInvLen = 1.0f / fLen;
    float fBeamHalfWidth = m_fBeamTexHeight * 0.5f * fZoom;
    float fOffsetX = -fDy * fInvLen * fBeamHalfWidth;
    float fOffsetY = fDx * fInvLen * fBeamHalfWidth;

    /* Animate the texture slightly to avoid a static look. */
    float fScroll = m_fBeamScroll;
    float fWobble = fm_sinf(m_fBeamFrames * TRACTOR_TEX_WOBBLE_FREQ) * TRACTOR_TEX_WOBBLE_AMPLITUDE;
    float fStretch = 1.0f + TRACTOR_TEX_STRETCH_AMPLITUDE * fm_sinf(m_fBeamFrames * TRACTOR_TEX_STRETCH_FREQ + 1.2f);

    float fS0 = fScroll + fWobble;
    /* rdpq uses texels as units for S/T: advancing S by the texture width wraps once. */
    float fS1 = fS0 + fLen * fStretch;
    float fTTop = 0.0f;
    float fTBottom = m_fBeamTexHeight - 1.0f;

    float v0[5] = {(float)vUfoScreen.iX + fOffsetX, (float)vUfoScreen.iY + fOffsetY, fS0, fTTop, 1.0f};
    float v1[5] = {(float)vUfoScreen.iX - fOffsetX, (float)vUfoScreen.iY - fOffsetY, fS0, fTBottom, 1.0f};
    float v2[5] = {(float)vTargetScreen.iX + fOffsetX, (float)vTargetScreen.iY + fOffsetY, fS1, fTTop, 1.0f};
    float v3[5] = {(float)vTargetScreen.iX - fOffsetX, (float)vTargetScreen.iY - fOffsetY, fS1, fTBottom, 1.0f};

    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY_CONST);
    rdpq_mode_dithering(DITHER_NOISE_NOISE);

    /* Fade-in then flicker the alpha for a livelier beam. */
    float fFadeT = m_fBeamFrames / (float)TRACTOR_FADEIN_FRAMES;
    if (fFadeT > 1.0f)
        fFadeT = 1.0f;
    float fFlickerWave = 0.65f * fm_sinf(m_fBeamFrames * 0.7f) + 0.35f * fm_sinf(m_fBeamFrames * 1.9f + 1.1f);
    float fAlphaNorm = fFadeT + fFadeT * TRACTOR_ALPHA_FLICKER_STRENGTH * fFlickerWave;
    if (fAlphaNorm < 0.0f)
        fAlphaNorm = 0.0f;
    if (fAlphaNorm > 1.0f)
        fAlphaNorm = 1.0f;
    uint8_t uAlpha = (uint8_t)(fAlphaNorm * 255.0f + 0.5f);
    rdpq_set_fog_color(RGBA32(0, 0, 0, uAlpha));
    rdpq_mode_alphacompare(255);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    // rdpq_set_prim_color(RGBA32(0, 255, 0, 220));
    rdpq_sprite_upload(TILE0, m_pTractorBeamSprite, &m_beamTexParms);

    rdpq_triangle(&TRIFMT_TEX, v0, v2, v1);
    rdpq_triangle(&TRIFMT_TEX, v1, v2, v3);
}

bool tractor_beam_is_active(void)
{
    return m_bActive;
}

void tractor_beam_render_ui(void)
{
    /* Hide tractor beam UI when not unlocked */
    if (!gp_state_unlock_get(GP_UNLOCK_TRACTOR_BEAM))
        return;

    /* Don't render tractor beam UI during dialogue */
    if (dialogue_is_active())
        return;

    /* Disable tractor beam UI in minimap mode */
    if (minimap_is_active())
        return;

    /* Draw tractor beam layout at the same position that weapons.c uses for its layout */
    if (m_pTractorBeamLayout && tractor_beam_is_active())
    {
        /* Get position for top-right using UI helper (same calculation as weapons.c) */
        struct vec2i vLayoutPos = ui_get_pos_top_right_sprite(m_pTractorBeamLayout);
        vLayoutPos.iX -= UI_DESIGNER_PADDING * 2 + 5;

        /* Render layout sprite */
        rdpq_set_mode_copy(false);
        rdpq_mode_alphacompare(1); /* draw pixels with alpha >= 1 (colorkey style) */
        rdpq_mode_filter(FILTER_POINT);
        rdpq_sprite_blit(m_pTractorBeamLayout, vLayoutPos.iX, vLayoutPos.iY, NULL);

        /* Draw layout_ab sprite below the current layout */
        if (m_pTractorBeamLayoutAb)
        {
            struct vec2i vLayoutAbPos = vLayoutPos;
            vLayoutAbPos.iY += m_pTractorBeamLayout->height - 7;
            vLayoutAbPos.iX += UI_DESIGNER_PADDING * 2 + 4;
            rdpq_sprite_blit(m_pTractorBeamLayoutAb, vLayoutAbPos.iX, vLayoutAbPos.iY, NULL);
        }
    }

    if (!m_pBtnR)
        return; /* Button sprite not loaded */

    /* Get position for top-right using UI helper */
    struct vec2i vBtnPos = ui_get_pos_top_right_sprite(m_pBtnR);
    vBtnPos.iY += 3; /* N64 layout: move R button slightly down */

    /* Check if button should be greyed out (no target or target out of range)
     * But never grey out while tractor beam is active */
    bool bShouldGreyOut = !tractor_beam_is_active() && !tractor_beam_is_target_in_range();

    /* Render button sprite */
    if (bShouldGreyOut)
    {
        /* Grey out the button using multiply mode */
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT); /* output = TEX0 * PRIM (RGB and A) */
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_alphacompare(1); /* draw pixels with alpha >= 1 (colorkey style) */
        rdpq_mode_filter(FILTER_POINT);
        rdpq_set_prim_color(RGBA32(128, 128, 128, 255)); /* 50% grey for multiply */
        rdpq_sprite_blit(m_pBtnR, vBtnPos.iX, vBtnPos.iY, NULL);
    }
    else
    {
        /* Normal rendering */
        rdpq_set_mode_copy(false);
        rdpq_mode_alphacompare(1); /* draw pixels with alpha >= 1 (colorkey style) */
        rdpq_mode_filter(FILTER_POINT);
        rdpq_sprite_blit(m_pBtnR, vBtnPos.iX, vBtnPos.iY, NULL);
    }
}
