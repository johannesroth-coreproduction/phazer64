#include "laser.h"

#include "../audio.h"
#include "../camera.h"
#include "../entity2d.h"
#include "../frame_time.h"
#include "../math2d.h"
#include "../meter_renderer.h"
#include "../resource_helper.h"
#include "../tilemap.h"
#include "../ui.h"
#include "gp_camera.h"
#include "gp_state.h"
#include "libdragon.h"
#include "rdpq.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "rdpq_tex.h"
#include "rdpq_tri.h"
#include "space_objects.h"
#include "ufo.h"
#include <math.h>
#include <stdint.h>

#define LASER_SPAWN_OFFSET 8.0f
#define LASER_MAX_RANGE 320.0f
#define LASER_DAMAGE_INTERVAL_MS 5 // near instantanious destruction
#define LASER_DAMAGE_AMOUNT 2
#define LASER_FADEIN_FRAMES 4
#define LASER_ALPHA_FLICKER_STRENGTH 0.2f

/* Overheat system parameters */
#define LASER_OVERHEAT_HEAT_RATE 0.5f        /* Heat gain per frame when firing */
#define LASER_OVERHEAT_COOLDOWN_RATE 0.3f    /* Heat loss per frame when not firing */
#define LASER_OVERHEAT_MAX 100.0f            /* Maximum heat level */
#define LASER_OVERHEAT_PENALTY_DELAY_MS 1000 /* Delay before cooldown starts after overheat (ms) */

static sprite_t *m_pLaserBeamSprite = NULL;
static rdpq_texparms_t m_beamTexParms = {0};
static float m_fBeamTexWidth = 1.0f;  /* cached tex width in pixels */
static float m_fBeamTexHeight = 1.0f; /* cached tex height in pixels */
static float m_fBeamFrames = 0.0f;    /* frames since last activation (for fade/flicker) */
static bool m_bActive = false;
static struct vec2 m_vHitPoint = {0.0f, 0.0f};
static bool m_bHasHit = false;
static uint32_t m_uLastDamageMs = 0;
static SpaceObject *m_pCurrentTarget = NULL;
static wav64_t *m_pLaserLoop = NULL;

/* Overheat system state */
static float m_fOverheatLevel = 0.0f;          /* Current heat level (0.0 to LASER_OVERHEAT_MAX) */
static bool m_bOverheated = false;             /* True when in penalty state */
static uint32_t m_uOverheatPenaltyStartMs = 0; /* Time when overheat penalty started */

static void laser_stop_audio(void)
{
    if (mixer_ch_playing(MIXER_CHANNEL_WEAPONS))
        mixer_ch_stop(MIXER_CHANNEL_WEAPONS);
}

void laser_free(void)
{
    /* Stop audio if playing */
    laser_stop_audio();

    SAFE_FREE_SPRITE(m_pLaserBeamSprite);
    SAFE_CLOSE_WAV64(m_pLaserLoop);

    m_bActive = false;
    m_fBeamFrames = 0.0f;
    m_bHasHit = false;
    m_pCurrentTarget = NULL;

    /* Reset overheat state */
    m_fOverheatLevel = 0.0f;
    m_bOverheated = false;
    m_uOverheatPenaltyStartMs = 0;

    /* Release shared meter resources (balanced with laser_init) */
    meter_renderer_free();
}

void laser_init(void)
{

    laser_free(); /* Ensure clean slate */

    /* Initialize shared meter resources for laser overheat UI */
    meter_renderer_init();

    if (!m_pLaserBeamSprite)
    {
        /* Try to load laser beam sprite - fallback to tractor beam if not found */
        m_pLaserBeamSprite = sprite_load("rom:/laser_beam_00.sprite");

        if (m_pLaserBeamSprite)
        {
            m_fBeamTexWidth = (float)m_pLaserBeamSprite->width;
            m_fBeamTexHeight = (float)m_pLaserBeamSprite->height;
            m_beamTexParms = (rdpq_texparms_t){
                .s = {.repeats = REPEAT_INFINITE, .mirror = MIRROR_NONE},
                .t = {.repeats = 1.0f, .mirror = MIRROR_NONE},
            };
        }
    }

    /* Audio - load looping laser sound */
    if (!m_pLaserLoop)
    {
        m_pLaserLoop = wav64_load("rom:/laser_beam.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
        if (m_pLaserLoop)
        {
            wav64_set_loop(m_pLaserLoop, true);
        }
    }
}

void laser_update(bool _bLaserPressed)
{
    float fFrameMul = frame_time_mul();
    uint32_t uNow = get_ticks_ms();

    /* Update overheat system */
    if (m_bOverheated)
    {
        /* In penalty state - check if penalty delay has passed */
        if (uNow - m_uOverheatPenaltyStartMs >= LASER_OVERHEAT_PENALTY_DELAY_MS)
        {
            /* Start cooling down */
            m_fOverheatLevel -= LASER_OVERHEAT_COOLDOWN_RATE * fFrameMul;
            if (m_fOverheatLevel <= 0.0f)
            {
                m_fOverheatLevel = 0.0f;
                m_bOverheated = false;
            }
        }
        /* else: still in penalty delay, no cooldown yet */

        /* Force laser inactive during overheat */
        _bLaserPressed = false;
    }
    else
    {
        /* Not overheated - normal operation */
        if (_bLaserPressed)
        {
            /* Heating up */
            m_fOverheatLevel += LASER_OVERHEAT_HEAT_RATE * fFrameMul;
            if (m_fOverheatLevel >= LASER_OVERHEAT_MAX)
            {
                m_fOverheatLevel = LASER_OVERHEAT_MAX;
                m_bOverheated = true;
                m_uOverheatPenaltyStartMs = uNow;
                _bLaserPressed = false; /* Immediately stop firing */
            }
        }
        else
        {
            /* Cooling down immediately when not firing */
            m_fOverheatLevel -= LASER_OVERHEAT_COOLDOWN_RATE * fFrameMul;
            if (m_fOverheatLevel < 0.0f)
            {
                m_fOverheatLevel = 0.0f;
            }
        }
    }

    if (!_bLaserPressed)
    {
        if (m_bActive)
            laser_stop_audio();
        m_bActive = false;
        m_fBeamFrames = 0.0f;
        m_bHasHit = false;
        m_pCurrentTarget = NULL;
        return;
    }

    bool bJustActivated = !m_bActive;
    m_bActive = true;
    if (bJustActivated)
    {
        m_fBeamFrames = 0.0f;
        m_uLastDamageMs = get_ticks_ms();
        m_bHasHit = false;
        m_pCurrentTarget = NULL;
        /* Start or keep playing the loop. */
        if (m_pLaserLoop)
        {
            wav64_play(m_pLaserLoop, MIXER_CHANNEL_WEAPONS);
        }
    }

    /* Get UFO position and look direction */
    struct vec2 vUfoPos = ufo_get_position();
    float fAngleRad = ufo_get_angle_rad();
    float fDirX = fm_sinf(fAngleRad);
    float fDirY = -fm_cosf(fAngleRad);
    struct vec2 vDir = vec2_make(fDirX, fDirY);

    /* Calculate laser start position (UFO + offset) */
    struct vec2 vStart = vec2_add(vUfoPos, vec2_scale(vDir, LASER_SPAWN_OFFSET));

    /* Calculate end position (start + direction * max range) */
    struct vec2 vEnd = vec2_add(vStart, vec2_scale(vDir, LASER_MAX_RANGE));

    /* Wrap positions in PLANET mode (same as UFO) */
    if (gp_state_get() == PLANET && g_mainTilemap.bInitialized)
    {
        vStart.fX = tilemap_wrap_world_x(vStart.fX);
        vEnd.fX = tilemap_wrap_world_x(vEnd.fX);
    }

    /* Find first hit along the line (meteors only exist in SPACE) */
    SpaceObject *pNewTarget = NULL;
    struct vec2 vHitPoint = vEnd;
    bool bHit = false;
    if (gp_state_get() == SPACE)
    {
        bHit = space_objects_check_laser_collision(vStart, vEnd, &vHitPoint, &pNewTarget);
    }

    /* Reset damage timer if target changed */
    if (pNewTarget != m_pCurrentTarget)
    {
        m_uLastDamageMs = get_ticks_ms();
    }
    m_pCurrentTarget = pNewTarget;

    m_bHasHit = bHit;
    if (bHit)
    {
        m_vHitPoint = vHitPoint;
    }
    else
    {
        m_vHitPoint = vEnd;
    }

    /* Apply damage every 200ms if hitting a target (meteors only exist in SPACE) */
    if (gp_state_get() == SPACE)
    {
        const struct entity2D *pTargetEntity = m_pCurrentTarget ? &m_pCurrentTarget->entity : NULL;
        if (m_pCurrentTarget && pTargetEntity && entity2d_is_active(pTargetEntity))
        {
            uint32_t uNow = get_ticks_ms();
            if (uNow - m_uLastDamageMs >= LASER_DAMAGE_INTERVAL_MS)
            {
                /* Calculate impact direction from UFO to target */
                struct vec2 vDelta = vec2_sub(pTargetEntity->vPos, vUfoPos);
                struct vec2 vImpactDir = vec2_normalize(vDelta);
                vImpactDir = vec2_scale(vImpactDir, IMPACT_STRENGTH_LASER);

                /* Apply 1 damage to the target meteor */
                space_object_apply_damage(m_pCurrentTarget, LASER_DAMAGE_AMOUNT, vImpactDir);
                m_uLastDamageMs = uNow;
            }
        }
    }

    /* Advance animation phases while active */
    m_fBeamFrames += fFrameMul;
}

void laser_render(void)
{
    if (!m_bActive)
        return;

    if (!m_pLaserBeamSprite)
        return;

    struct vec2 vUfoPos = ufo_get_position();
    float fAngleRad = ufo_get_angle_rad();
    float fDirX = fm_sinf(fAngleRad);
    float fDirY = -fm_cosf(fAngleRad);
    struct vec2 vDir = vec2_make(fDirX, fDirY);
    struct vec2 vStart = vec2_add(vUfoPos, vec2_scale(vDir, LASER_SPAWN_OFFSET));
    struct vec2 vEnd = m_vHitPoint;

    /* Wrap positions in PLANET mode (same as UFO) */
    bool bWrappingMode = (gp_state_get() == PLANET && g_mainTilemap.bInitialized);
    if (bWrappingMode)
    {
        vStart.fX = tilemap_wrap_world_x(vStart.fX);
        vEnd.fX = tilemap_wrap_world_x(vEnd.fX);
    }

    struct vec2i vStartScreen;
    struct vec2i vEndScreen;
    if (bWrappingMode)
    {
        gp_camera_world_to_screen_wrapped(&g_mainCamera, vStart, &vStartScreen);
        gp_camera_world_to_screen_wrapped(&g_mainCamera, vEnd, &vEndScreen);
    }
    else
    {
        camera_world_to_screen(&g_mainCamera, vStart, &vStartScreen);
        camera_world_to_screen(&g_mainCamera, vEnd, &vEndScreen);
    }

    float fDx = (float)(vEndScreen.iX - vStartScreen.iX);
    float fDy = (float)(vEndScreen.iY - vStartScreen.iY);
    float fLen = sqrtf(fDx * fDx + fDy * fDy);
    if (fLen <= 1e-3f)
        return;

    float fZoom = camera_get_zoom(&g_mainCamera);

    /* Build a textured quad around the segment using a screen-space perpendicular. */
    float fInvLen = 1.0f / fLen;
    float fBeamHalfWidth = m_fBeamTexHeight * 0.5f * fZoom;
    float fOffsetX = -fDy * fInvLen * fBeamHalfWidth;
    float fOffsetY = fDx * fInvLen * fBeamHalfWidth;

    /* Texture coordinates - no scrolling */
    float fS0 = 0.0f;
    /* rdpq uses texels as units for S/T: advancing S by the texture width wraps once. */
    float fS1 = fLen;
    float fTTop = 0.0f;
    float fTBottom = m_fBeamTexHeight - 1.0f;

    float v0[5] = {(float)vStartScreen.iX + fOffsetX, (float)vStartScreen.iY + fOffsetY, fS0, fTTop, 1.0f};
    float v1[5] = {(float)vStartScreen.iX - fOffsetX, (float)vStartScreen.iY - fOffsetY, fS0, fTBottom, 1.0f};
    float v2[5] = {(float)vEndScreen.iX + fOffsetX, (float)vEndScreen.iY + fOffsetY, fS1, fTTop, 1.0f};
    float v3[5] = {(float)vEndScreen.iX - fOffsetX, (float)vEndScreen.iY - fOffsetY, fS1, fTBottom, 1.0f};

    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY_CONST);
    rdpq_mode_dithering(DITHER_NOISE_NOISE);

    /* Fade-in then flicker the alpha for a livelier beam. */
    float fFadeT = m_fBeamFrames / (float)LASER_FADEIN_FRAMES;
    if (fFadeT > 1.0f)
        fFadeT = 1.0f;
    float fFlickerWave = 0.65f * fm_sinf(m_fBeamFrames * 0.7f) + 0.35f * fm_sinf(m_fBeamFrames * 1.9f + 1.1f);
    float fAlphaNorm = fFadeT + fFadeT * LASER_ALPHA_FLICKER_STRENGTH * fFlickerWave;
    if (fAlphaNorm < 0.0f)
        fAlphaNorm = 0.0f;
    if (fAlphaNorm > 1.0f)
        fAlphaNorm = 1.0f;
    uint8_t uAlpha = (uint8_t)(fAlphaNorm * 255.0f + 0.5f);
    rdpq_set_fog_color(RGBA32(0, 0, 0, uAlpha));
    rdpq_mode_alphacompare(255);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_sprite_upload(TILE0, m_pLaserBeamSprite, &m_beamTexParms);

    rdpq_triangle(&TRIFMT_TEX, v0, v2, v1);
    rdpq_triangle(&TRIFMT_TEX, v1, v2, v3);
}

bool laser_is_firing(void)
{
    return m_bActive;
}

float laser_get_overheat_level(void)
{
    return m_fOverheatLevel / LASER_OVERHEAT_MAX;
}

bool laser_is_overheated(void)
{
    return m_bOverheated;
}

void laser_render_overheat_meter(void)
{
    float fFillRatio = m_fOverheatLevel / LASER_OVERHEAT_MAX;
    if (fFillRatio < 0.0f)
        fFillRatio = 0.0f;
    if (fFillRatio > 1.0f)
        fFillRatio = 1.0f;
    /* Position meter at lower-left of the screen using UI helpers */
    struct vec2i vMeterSize = meter_renderer_get_frame_size();
    struct vec2i vMeterPos = ui_get_pos_bottom_left(vMeterSize.iX, vMeterSize.iY);

    /* Color: Blue when normal, Red when in penalty state */
    color_t uColor;
    if (m_bOverheated)
    {
        uColor = RGBA32(255, 32, 32, 255); /* Red */
    }
    else
    {
        uColor = RGBA32(32, 128, 255, 255); /* Blue */
    }

    meter_renderer_render(vMeterPos, fFillRatio, uColor);
}
