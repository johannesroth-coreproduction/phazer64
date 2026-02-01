#include "bullets.h"
#include "../audio.h"
#include "../camera.h"
#include "../entity2d.h" /* Use entity2d */
#include "../frame_time.h"
#include "../resource_helper.h"
#include "../tilemap.h"
#include "gp_camera.h"
#include "gp_state.h"
#include "libdragon.h"
#include "space_objects.h"
#include "ufo.h"
#include <math.h>
#include <stdlib.h>

/* Bullet pool settings */
#define BULLET_POOL_SIZE 16
#define BULLET_DESPAWN_MARGIN 64.0f

/* Gameplay settings */
#define BULLET_DAMAGE_NORMAL 1
#define BULLET_DAMAGE_UPGRADED 3
#define BULLET_SPAWN_OFFSET 8.0f
#define BULLET_COOLDOWN_MASH_MS 100
#define BULLET_COOLDOWN_HOLD_MS 250
#define BULLET_SPEED 6.0f
#define BULLET_FIRING_GLOW_DURATION_MS 50 /* Duration to show weapon glow after bullet spawn */
#define BULLET_MAX_LIFETIME_MS 5000       /* Max lifetime in milliseconds (5 seconds) */
#define BULLET_POLAR_MARGIN_TILES 8       /* Margin in tiles beyond map height for Y-boundary check in PLANET mode */

/* Assets */
static sprite_t *m_spriteBullet = NULL;
static wav64_t *m_sfxBullets[5] = {NULL, NULL, NULL, NULL, NULL}; // bullet_1 to bullet_5
static audio_sound_group_t m_soundGroupBullets;

/* Pool - now directly using entity2D */
static struct entity2D m_aBullets[BULLET_POOL_SIZE];
static uint32_t m_aBulletSpawnTimes[BULLET_POOL_SIZE]; /* Track spawn time for each bullet (0 = inactive) */
static int m_iNextBulletIndex = 0;

/* Shooting state */
static uint32_t m_uNextShotMs = 0;
static uint32_t m_uLastShotMs = 0;
static bool m_bWasShootDown = false;
static bool m_bHasShot = false;

/* Free bullets resources */
void bullets_free(void)
{
    SAFE_FREE_SPRITE(m_spriteBullet);

    /* Free sounds via sound group */
    audio_sound_group_free(&m_soundGroupBullets);
    /* m_sfxBullets pointers are cleared by audio_sound_group_free */

    /* Reset pool and state */
    for (int i = 0; i < BULLET_POOL_SIZE; ++i)
    {
        entity2d_deactivate(&m_aBullets[i]);
        m_aBulletSpawnTimes[i] = 0;
    }
    m_iNextBulletIndex = 0;
    m_uNextShotMs = 0;
    m_uLastShotMs = 0;
    m_bWasShootDown = false;
    m_bHasShot = false;
}

/* Refresh bullet visuals after progression (e.g. upgrade) changes.
 * Keeps existing bullets alive while updating their sprite pointer. */
void bullets_refresh_state(void)
{
    /* Choose bullet sprite based on upgrade progression */
    const char *pBulletSpritePath = gp_state_unlock_get(GP_UNLOCK_BULLETS_UPGRADED) ? "rom:/bullet_upgraded_00.sprite" : "rom:/bullet_00.sprite";

    SAFE_FREE_SPRITE(m_spriteBullet);
    m_spriteBullet = sprite_load(pBulletSpritePath);

    /* Update active bullets to use the new sprite */
    if (m_spriteBullet)
    {
        for (int i = 0; i < BULLET_POOL_SIZE; ++i)
        {
            struct entity2D *pBullet = &m_aBullets[i];
            if (!entity2d_is_active(pBullet))
                continue;

            pBullet->pSprite = m_spriteBullet;
            /* Half-extents are safe to refresh from sprite size */
            pBullet->vHalf.iX = m_spriteBullet->width / 2;
            pBullet->vHalf.iY = m_spriteBullet->height / 2;
        }
    }
}

/* Initialize bullets module (load assets) */
void bullets_init(void)
{

    bullets_free();

    /* Choose bullet sprite based on upgrade progression */
    const char *pBulletSpritePath = gp_state_unlock_get(GP_UNLOCK_BULLETS_UPGRADED) ? "rom:/bullet_upgraded_00.sprite" : "rom:/bullet_00.sprite";

    if (!m_spriteBullet)
        m_spriteBullet = sprite_load(pBulletSpritePath);

    /* Audio - load all bullet sound variants */
    const char *bullet_sounds[] = {"rom:/bullet_00.wav64", "rom:/bullet_01.wav64", "rom:/bullet_02.wav64", "rom:/bullet_03.wav64", "rom:/bullet_04.wav64"};

    audio_sound_group_init(&m_soundGroupBullets, bullet_sounds, MIXER_CHANNEL_WEAPONS, m_sfxBullets);

    /* Clear pool */
    for (int i = 0; i < BULLET_POOL_SIZE; ++i)
    {
        /* Deactivate entity */
        entity2d_deactivate(&m_aBullets[i]);
        m_aBulletSpawnTimes[i] = 0;
    }
    m_iNextBulletIndex = 0;
    m_uNextShotMs = 0;
    m_uLastShotMs = 0;
    m_bWasShootDown = false;
    m_bHasShot = false;
}

static void bullets_spawn(struct vec2 _vStartPos, float _fAngleRad, struct vec2 _vInheritedVel)
{
    /* Reuse oldest logic: m_iNextBulletIndex is a ring buffer index.
     * We just overwrite whatever is there.
     */
    struct entity2D *pBullet = &m_aBullets[m_iNextBulletIndex];

    /* Move to next index for next spawn */
    m_iNextBulletIndex = (m_iNextBulletIndex + 1) % BULLET_POOL_SIZE;

    /* Initialize entity */
    uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
    uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY; /* Adjust layer as needed */

    entity2d_init_from_sprite(pBullet, _vStartPos, m_spriteBullet, uFlags, uLayerMask);
    pBullet->fAngleRad = _fAngleRad;
    pBullet->iCollisionRadius = 3;

    /* Configurable bullet speed */
    float fBulletSpeed = BULLET_SPEED;

    /* Calculate Direction from Angle */
    float fDirX = fm_sinf(_fAngleRad);
    float fDirY = -fm_cosf(_fAngleRad);
    struct vec2 vDir = vec2_make(fDirX, fDirY);

    /* Velocity = Inherited + Direction * Speed */
    struct vec2 vShotVel = vec2_scale(vDir, fBulletSpeed);
    pBullet->vVel = vec2_add(_vInheritedVel, vShotVel);

    /* Record spawn time for lifetime tracking (use the bullet index before incrementing) */
    int iBulletIndex = (m_iNextBulletIndex == 0) ? (BULLET_POOL_SIZE - 1) : (m_iNextBulletIndex - 1);
    m_aBulletSpawnTimes[iBulletIndex] = get_ticks_ms();

    /* Play random bullet sound */
    audio_sound_group_play_random(&m_soundGroupBullets, true);
}

void bullets_update(bool _bShootDown)
{
    float fFrameMul = frame_time_mul();

    /* ---------------------------------------------------------------------
     * Input / Shooting Logic
     * --------------------------------------------------------------------- */
    uint32_t uNow = get_ticks_ms();
    bool bShoot = false;
    bool bRisingEdge = _bShootDown && !m_bWasShootDown;

    /* On a fresh press, switch to mash cadence (can be faster than hold).
     * The first ever shot is allowed immediately. */
    if (bRisingEdge)
    {
        if (!m_bHasShot)
            m_uNextShotMs = uNow;
        else
            m_uNextShotMs = m_uLastShotMs + BULLET_COOLDOWN_MASH_MS;
    }

    /* Fire only while the button is held and the cooldown has elapsed */
    if (_bShootDown && uNow >= m_uNextShotMs)
    {
        bShoot = true;
    }

    if (bShoot)
    {
        struct vec2 vUfoPos = ufo_get_position();
        struct vec2 vUfoVel = ufo_get_velocity();
        float fAngleRad = ufo_get_angle_rad();

        /* Calculate spawn position: Center + Offset * LookDir */
        float fDirX = fm_sinf(fAngleRad);
        float fDirY = -fm_cosf(fAngleRad);
        struct vec2 vLookDir = vec2_make(fDirX, fDirY);

        struct vec2 vSpawnPos = vec2_add(vUfoPos, vec2_scale(vLookDir, BULLET_SPAWN_OFFSET));

        bullets_spawn(vSpawnPos, fAngleRad, vUfoVel);

        m_bHasShot = true;
        m_uLastShotMs = uNow;
        m_uNextShotMs = uNow + BULLET_COOLDOWN_HOLD_MS;
    }

    m_bWasShootDown = _bShootDown;

    /* ---------------------------------------------------------------------
     * Update Bullets
     * --------------------------------------------------------------------- */
    gp_state_t eState = gp_state_get();
    bool bIsPlanetMode = (eState == PLANET && g_mainTilemap.bInitialized);
    bool bIsSpaceMode = (eState == SPACE);
    uint32_t uCurrentTime = get_ticks_ms();
    /* Select damage based on bullet upgrade progression */
    int iBulletDamage = gp_state_unlock_get(GP_UNLOCK_BULLETS_UPGRADED) ? BULLET_DAMAGE_UPGRADED : BULLET_DAMAGE_NORMAL;

    for (int i = 0; i < BULLET_POOL_SIZE; ++i)
    {
        struct entity2D *pBullet = &m_aBullets[i];
        if (!entity2d_is_active(pBullet))
            continue;

        /* Move */
        pBullet->vPos = vec2_add(pBullet->vPos, vec2_scale(pBullet->vVel, fFrameMul));

        /* Wrap X position in PLANET mode */
        if (bIsPlanetMode)
            pBullet->vPos.fX = tilemap_wrap_world_x(pBullet->vPos.fX);

        /* Despawn checks - optimized order for early exit */

        /* 1. Visibility check (SPACE mode only) - most likely to despawn */
        if (!bIsPlanetMode && !camera_is_point_visible(&g_mainCamera, pBullet->vPos, BULLET_DESPAWN_MARGIN))
        {
            entity2d_deactivate(pBullet);
            m_aBulletSpawnTimes[i] = 0;
            continue;
        }

        /* 2. Collision vs meteors (SPACE mode only) */
        if (bIsSpaceMode && space_objects_check_bullet_collision(pBullet, iBulletDamage))
        {
            entity2d_deactivate(pBullet);
            m_aBulletSpawnTimes[i] = 0;
            continue;
        }

        /* 3. Lifetime check - despawn if exceeded max lifetime */
        if (m_aBulletSpawnTimes[i] != 0 && (uCurrentTime - m_aBulletSpawnTimes[i]) >= BULLET_MAX_LIFETIME_MS)
        {
            entity2d_deactivate(pBullet);
            m_aBulletSpawnTimes[i] = 0;
            continue;
        }

        /* 4. Y-boundary check (PLANET mode only) - despawn if beyond polar regions */
        if (bIsPlanetMode && g_mainTilemap.uWorldHeightTiles > 0)
        {
            float fWorldH = (float)g_mainTilemap.uWorldHeightTiles * (float)TILE_SIZE;
            float fMarginPx = (float)BULLET_POLAR_MARGIN_TILES * (float)TILE_SIZE;
            float fMinY = -fMarginPx;
            float fMaxY = fWorldH + fMarginPx;

            if (pBullet->vPos.fY < fMinY || pBullet->vPos.fY > fMaxY)
            {
                entity2d_deactivate(pBullet);
                m_aBulletSpawnTimes[i] = 0;
                continue;
            }
        }
    }
}

void bullets_render(void)
{
    if (!m_spriteBullet)
        return;

    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_BILINEAR);

    float fZoom = camera_get_zoom(&g_mainCamera);
    bool bWrappingMode = (gp_state_get() == PLANET && g_mainTilemap.bInitialized);

    for (int i = 0; i < BULLET_POOL_SIZE; ++i)
    {
        struct entity2D *pBullet = &m_aBullets[i];
        if (!entity2d_is_active(pBullet))
            continue;

        /* Use wrapped rendering in PLANET mode */
        struct vec2i vScreen;
        bool bVisible = false;
        if (bWrappingMode)
        {
            bVisible = gp_camera_entity_world_to_screen_wrapped(&g_mainCamera, pBullet, &vScreen);
        }
        else
        {
            if (!camera_entity_world_to_screen(&g_mainCamera, pBullet, &vScreen))
                continue;
            bVisible = true;
        }

        if (!bVisible)
            continue;

        /* Render with rotation + zoom */
        rdpq_blitparms_t parms = {.cx = pBullet->vHalf.iX, .cy = pBullet->vHalf.iY, .scale_x = fZoom, .scale_y = fZoom, .theta = pBullet->fAngleRad};
        rdpq_sprite_blit(pBullet->pSprite, vScreen.iX, vScreen.iY, &parms);
    }
}

bool bullets_is_firing(void)
{
    if (!m_bHasShot)
        return false;

    uint32_t uNow = get_ticks_ms();
    return (uNow - m_uLastShotMs) < BULLET_FIRING_GLOW_DURATION_MS;
}
