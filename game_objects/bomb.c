#include "bomb.h"

#include "../audio.h"
#include "../camera.h"
#include "../entity2d.h"
#include "../frame_time.h"
#include "../math2d.h"
#include "../resource_helper.h"
#include "../tilemap.h"
#include "gp_camera.h"
#include "gp_state.h"
#include "libdragon.h"
#include "rdpq.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "rdpq_tex.h"
#include "space_objects.h"
#include "ufo.h"
#include <math.h>
#include <stdint.h>

/* Bomb settings */
#define BOMB_MAX_RADIUS 125.0f
#define BOMB_GROWTH_SPEED 4.0f /* pixels per frame */
#define BOMB_DAMAGE 5
#define BOMB_START_RADIUS 4.0f
#define BOMB_VISUAL_SCALE_MULTIPLIER 1.5f /* Multiplier to adjust visual size relative to collision radius */
#define BOMB_ALPHA_FADE_START 0.6f        /* Start fading at 75% of max radius (0.0-1.0) */
#define BOMB_FIRING_GLOW_DURATION_MS 500  /* Duration to show weapon glow after bomb spawn */
#define BOMB_COOLDOWN_MS 1000             /* Cooldown delay between bomb executions (1 second) */

/* Assets */
static sprite_t *m_pBombSprite = NULL;
static wav64_t *m_pBombSound = NULL;

/* Bomb state */
static bool m_bActive = false;
static float m_fCurrentRadius = 0.0f;
static struct vec2 m_vCenter = {0.0f, 0.0f};
static bool m_bHasPlayedSound = false;
static uint32_t m_uSpawnTimeMs = 0;
static uint32_t m_uLastTriggerTimeMs = 0; /* Last time bomb was triggered (for cooldown) */

void bomb_free(void)
{
    SAFE_FREE_SPRITE(m_pBombSprite);
    SAFE_CLOSE_WAV64(m_pBombSound);

    m_bActive = false;
    m_fCurrentRadius = 0.0f;
    m_vCenter = vec2_zero();
    m_bHasPlayedSound = false;
    m_uSpawnTimeMs = 0;
    m_uLastTriggerTimeMs = 0;
}

void bomb_init(void)
{
    bomb_free(); /* Ensure clean slate */

    if (!m_pBombSprite)
    {
        m_pBombSprite = sprite_load("rom:/bomb_00.sprite");
    }

    /* Audio - load one-shot bomb sound */
    if (!m_pBombSound)
    {
        m_pBombSound = wav64_load("rom:/bomb.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    }

    m_bActive = false;
    m_fCurrentRadius = 0.0f;
    m_vCenter = vec2_zero();
    m_bHasPlayedSound = false;
    m_uSpawnTimeMs = 0;
    m_uLastTriggerTimeMs = 0;
}

void bomb_update(bool _bFire)
{
    float fFrameMul = frame_time_mul();

    if (!m_bActive)
    {
        /* Check if we should activate */
        if (_bFire)
        {
            /* Check cooldown - don't allow firing if still on cooldown */
            uint32_t uNow = get_ticks_ms();
            if ((uNow - m_uLastTriggerTimeMs) < BOMB_COOLDOWN_MS)
            {
                return; /* Still on cooldown */
            }

            /* Get UFO position and spawn bomb there */
            m_vCenter = ufo_get_position();
            m_fCurrentRadius = BOMB_START_RADIUS;
            m_bActive = true;
            m_bHasPlayedSound = false;
            m_uSpawnTimeMs = get_ticks_ms();
            m_uLastTriggerTimeMs = m_uSpawnTimeMs; /* Record trigger time for cooldown */

            /* Play one-shot sound */
            if (m_pBombSound)
            {
                wav64_play(m_pBombSound, MIXER_CHANNEL_WEAPONS);
            }
            m_bHasPlayedSound = true;
        }
        return;
    }

    /* Update active bomb */
    /* Keep center attached to UFO position */
    m_vCenter = ufo_get_position();

    /* Wrap X position in PLANET mode (same as UFO) */
    if (gp_state_get() == PLANET && g_mainTilemap.bInitialized)
    {
        m_vCenter.fX = tilemap_wrap_world_x(m_vCenter.fX);
    }

    /* Grow the shockwave */
    m_fCurrentRadius += BOMB_GROWTH_SPEED * fFrameMul;

    /* Apply damage to all meteors within current radius (meteors only exist in SPACE) */
    /* Note: Since meteors have 5 HP and bomb deals 5 damage, they'll die on first hit */
    /* so applying damage every frame is acceptable (they'll only take damage once before dying) */
    if (gp_state_get() == SPACE)
    {
        /* Calculate impact direction from UFO (bomb center) outward */
        /* For bomb, we use a normalized direction that will be scaled per-target in damage_in_radius */
        struct vec2 vImpactDir = vec2_make(1.0f, 0.0f); /* Direction doesn't matter, magnitude does */
        vImpactDir = vec2_scale(vImpactDir, IMPACT_STRENGTH_BOMB);
        space_objects_damage_in_radius(m_vCenter, m_fCurrentRadius, BOMB_DAMAGE, vImpactDir);
    }

    /* Deactivate when we reach max radius */
    if (m_fCurrentRadius >= BOMB_MAX_RADIUS)
    {
        m_bActive = false;
        m_fCurrentRadius = 0.0f;
    }
}

void bomb_render(void)
{
    if (!m_bActive || !m_pBombSprite)
        return;

    /* Check if bomb is visible (use wrapped check in PLANET mode) */
    bool bVisible = false;
    if (gp_state_get() == PLANET && g_mainTilemap.bInitialized)
    {
        bVisible = gp_camera_is_point_visible_wrapped(&g_mainCamera, m_vCenter, m_fCurrentRadius);
    }
    else
    {
        bVisible = camera_is_point_visible(&g_mainCamera, m_vCenter, m_fCurrentRadius);
    }

    if (!bVisible)
        return; /* Outside view */

    /* Convert world position to screen (use wrapped version in PLANET mode) */
    struct vec2i vScreen;
    if (gp_state_get() == PLANET && g_mainTilemap.bInitialized)
    {
        gp_camera_world_to_screen_wrapped(&g_mainCamera, m_vCenter, &vScreen);
    }
    else
    {
        camera_world_to_screen(&g_mainCamera, m_vCenter, &vScreen);
    }

    float fZoom = camera_get_zoom(&g_mainCamera);

    /* Calculate progress from start to max radius (0.0 to 1.0) for alpha fade */
    float fProgress = (m_fCurrentRadius - BOMB_START_RADIUS) / (BOMB_MAX_RADIUS - BOMB_START_RADIUS);
    if (fProgress < 0.0f)
        fProgress = 0.0f;
    if (fProgress > 1.0f)
        fProgress = 1.0f;

    /* Calculate alpha with curve: stay opaque for most of duration, then quick smooth fade */
    float fAlpha = 1.0f;
    if (fProgress >= BOMB_ALPHA_FADE_START)
    {
        /* Fade from fadeStart to 1.0, using smooth quadratic curve */
        float fFadeT = (fProgress - BOMB_ALPHA_FADE_START) / (1.0f - BOMB_ALPHA_FADE_START);
        /* Smooth quadratic ease-out: 1 - (1-t)^2 = 2t - t^2, but we want to go from 1 to 0 */
        /* Use 1 - t^2 for smooth fade out */
        fAlpha = 1.0f - (fFadeT * fFadeT);
        if (fAlpha < 0.0f)
            fAlpha = 0.0f;
    }

    /* Calculate scale directly from current radius with multiplier */
    /* Visual diameter = current radius * 2 * multiplier */
    float fVisualDiameter = m_fCurrentRadius * 2.0f * BOMB_VISUAL_SCALE_MULTIPLIER;
    float fSpriteDiameter = (float)(m_pBombSprite->width > m_pBombSprite->height ? m_pBombSprite->width : m_pBombSprite->height);
    float fScale = fVisualDiameter / fSpriteDiameter;

    /* Set up rendering with full multiply blend mode */
    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY_CONST);
    rdpq_mode_dithering(DITHER_NOISE_NOISE);

    /* Set alpha using prim color for multiply blend mode */
    uint8_t uAlpha = (uint8_t)(fAlpha * 255.0f + 0.5f);
    rdpq_set_fog_color(RGBA32(255, 255, 255, uAlpha));
    rdpq_mode_alphacompare(200);

    /* Render with scaling */
    rdpq_blitparms_t parms = {.cx = m_pBombSprite->width / 2, .cy = m_pBombSprite->height / 2, .scale_x = fScale * fZoom, .scale_y = fScale * fZoom, .theta = 0.0f};

    rdpq_sprite_blit(m_pBombSprite, vScreen.iX, vScreen.iY, &parms);
}

bool bomb_is_firing(void)
{
    if (!m_bActive)
        return false;

    uint32_t uNow = get_ticks_ms();
    return (uNow - m_uSpawnTimeMs) < BOMB_FIRING_GLOW_DURATION_MS;
}
