#include "weapons.h"

#include "../camera.h"
#include "../dialogue.h"
#include "../minimap.h"
#include "../palette.h"
#include "../resource_helper.h"
#include "../ui.h"
#include "bomb.h"
#include "bullets.h"
#include "gp_state.h"
#include "laser.h"
#include "libdragon.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "tractor_beam.h"
#include <stdbool.h>

/* Current active weapon */
static weapon_type_t m_eCurrentWeapon = WEAPON_BULLETS;

/* Helper: return true if a specific weapon type is unlocked via gp_state flags */
static bool weapon_is_unlocked(weapon_type_t _eType)
{
    switch (_eType)
    {
    case WEAPON_BULLETS:
        /* Bullets are available if either normal or upgraded flag is set */
        return gp_state_unlock_get(GP_UNLOCK_BULLETS_NORMAL) || gp_state_unlock_get(GP_UNLOCK_BULLETS_UPGRADED);
    case WEAPON_LASER:
        return gp_state_unlock_get(GP_UNLOCK_LASER);
    case WEAPON_BOMB:
        return gp_state_unlock_get(GP_UNLOCK_BOMB);
    default:
        return false;
    }
}

/* Return true if any weapon is currently unlocked */
bool weapons_any_unlocked(void)
{
    return weapon_is_unlocked(WEAPON_BULLETS) || weapon_is_unlocked(WEAPON_LASER) || weapon_is_unlocked(WEAPON_BOMB);
}

/* Helper: count how many weapons are currently unlocked */
static int weapons_count_unlocked(void)
{
    int iCount = 0;
    if (weapon_is_unlocked(WEAPON_BULLETS))
        iCount++;
    if (weapon_is_unlocked(WEAPON_LASER))
        iCount++;
    if (weapon_is_unlocked(WEAPON_BOMB))
        iCount++;
    return iCount;
}

/* Helper: find first unlocked weapon, or WEAPON_BULLETS as fallback */
static weapon_type_t weapons_find_first_unlocked(void)
{
    for (int i = 0; i < (int)WEAPON_COUNT; ++i)
    {
        weapon_type_t eType = (weapon_type_t)i;
        if (weapon_is_unlocked(eType))
            return eType;
    }

    /* Nothing unlocked yet: keep default (will be effectively disabled by unlock checks) */
    return WEAPON_BULLETS;
}

/* Helper: cycle to next/previous unlocked weapon, starting from current */
static weapon_type_t weapons_cycle_unlocked(weapon_type_t _eStart, int _iDirection)
{
    /* Direction must be +1 (next) or -1 (previous) */
    if (_iDirection == 0)
        return _eStart;

    int iStart = (int)_eStart;
    int iCurrent = iStart;

    /* At most WEAPON_COUNT steps to avoid infinite loops */
    for (int step = 0; step < (int)WEAPON_COUNT; ++step)
    {
        iCurrent += _iDirection;
        if (iCurrent < 0)
            iCurrent = (int)WEAPON_COUNT - 1;
        else if (iCurrent >= (int)WEAPON_COUNT)
            iCurrent = 0;

        weapon_type_t eCandidate = (weapon_type_t)iCurrent;
        if (weapon_is_unlocked(eCandidate))
            return eCandidate;
    }

    /* If we didn't find anything, stay on original */
    return _eStart;
}

/* Weapon icon sprites */
static sprite_t *m_aWeaponIcons[WEAPON_COUNT] = {NULL, NULL, NULL};

/* Button interface sprites */
static sprite_t *m_pBtnInterface = NULL;
static sprite_t *m_pBtnBClear = NULL;

void weapons_init(void)
{
    weapons_free(); /* Ensure clean slate */

    bullets_init();
    laser_init();
    bomb_init();
    /* Initialize current weapon to first unlocked, or default bullets */
    m_eCurrentWeapon = weapons_find_first_unlocked();

    /* Load weapon icon sprites */
    const char *pBulletsIconPath = gp_state_unlock_get(GP_UNLOCK_BULLETS_UPGRADED) ? "rom:/bullets_upgraded_icon_00.sprite" : "rom:/bullets_icon_00.sprite";
    m_aWeaponIcons[WEAPON_BULLETS] = sprite_load(pBulletsIconPath);
    m_aWeaponIcons[WEAPON_LASER] = sprite_load("rom:/laser_icon_00.sprite");
    m_aWeaponIcons[WEAPON_BOMB] = sprite_load("rom:/bomb_icon_00.sprite");

    /* Load button interface sprites */
    m_pBtnInterface = sprite_load("rom:/btn_interface.sprite");
    m_pBtnBClear = sprite_load("rom:/btn_b_clear_00.sprite");
}

/* Refresh weapons state after unlock flags change.
 * This keeps current weapon selection valid and updates any visuals that
 * depend on progression (icons, bullet sprites, etc.). */
void weapons_refresh_state(void)
{
    /* Ensure current weapon is still valid; if not, snap to first unlocked. */
    if (!weapon_is_unlocked(m_eCurrentWeapon))
    {
        m_eCurrentWeapon = weapons_find_first_unlocked();
    }

    /* Refresh bullets icon sprite based on upgrade progression */
    const char *pBulletsIconPath = gp_state_unlock_get(GP_UNLOCK_BULLETS_UPGRADED) ? "rom:/bullets_upgraded_icon_00.sprite" : "rom:/bullets_icon_00.sprite";

    SAFE_FREE_SPRITE(m_aWeaponIcons[WEAPON_BULLETS]);
    m_aWeaponIcons[WEAPON_BULLETS] = sprite_load(pBulletsIconPath);

    /* Allow bullets module to refresh any internal visuals (sprite, etc.) */
    bullets_refresh_state();
}

void weapons_free(void)
{
    for (int i = 0; i < WEAPON_COUNT; i++)
    {
        SAFE_FREE_SPRITE(m_aWeaponIcons[i]);
    }

    SAFE_FREE_SPRITE(m_pBtnInterface);
    SAFE_FREE_SPRITE(m_pBtnBClear);

    /* Also free sub-systems */
    bullets_free();
    laser_free();
    bomb_free();
}

void weapons_update(bool _bFire, bool _bCycleLeft, bool _bCycleRight)
{
    /* Cache expensive state checks to avoid calling them multiple times */
    bool bAcceptsInput = gp_state_accepts_input();
    bool bTractorBeamActive = tractor_beam_is_active();

    /* If no weapons are unlocked at all, ignore inputs but keep updating subsystems
     * so any lingering projectiles finish their behavior gracefully. */
    bool bAnyUnlocked = weapons_any_unlocked();
    if (!bAnyUnlocked)
    {
        _bFire = false;
        _bCycleLeft = false;
        _bCycleRight = false;
    }

    /* Disable weapons input when gameplay input is blocked (minimap, cutscenes, transitions) */
    if (!bAcceptsInput)
    {
        _bFire = false;
        _bCycleLeft = false;
        _bCycleRight = false;
    }

    /* Ensure current weapon is valid/unlocked when states change mid-game */
    /* Only check if we know weapons are unlocked (avoid redundant checks) */
    if (bAnyUnlocked && !weapon_is_unlocked(m_eCurrentWeapon))
    {
        m_eCurrentWeapon = weapons_find_first_unlocked();
    }

    if (!bTractorBeamActive)
    {
        if (_bCycleLeft)
        {
            /* Cycle to previous unlocked weapon */
            m_eCurrentWeapon = weapons_cycle_unlocked(m_eCurrentWeapon, -1);
        }
        else if (_bCycleRight)
        {
            /* Cycle to next unlocked weapon */
            m_eCurrentWeapon = weapons_cycle_unlocked(m_eCurrentWeapon, +1);
        }
    }

    /* Prevent shooting while tractor beam is active or during state transitions (UFO + fade) */
    bool bCanFire = _bFire && !bTractorBeamActive && bAcceptsInput;

    /* Update ALL weapons every frame, but only send input to the active one.
     * This ensures bullets keep flying, bombs finish exploding, etc. */
    bullets_update((m_eCurrentWeapon == WEAPON_BULLETS) ? bCanFire : false);
    laser_update((m_eCurrentWeapon == WEAPON_LASER) ? bCanFire : false);
    bomb_update((m_eCurrentWeapon == WEAPON_BOMB) ? bCanFire : false);
}

void weapons_render(void)
{
    /* Render ALL weapons so bullets keep flying, bombs finish exploding, etc. */
    bullets_render();
    laser_render();
    bomb_render();
}

weapon_type_t weapons_get_current(void)
{
    return m_eCurrentWeapon;
}

void weapons_set_current(weapon_type_t _eType)
{
    if (weapon_is_unlocked(_eType))
    {
        m_eCurrentWeapon = _eType;
    }
    else
    {
        m_eCurrentWeapon = weapons_find_first_unlocked();
    }
}

color_t weapons_get_current_color(void)
{
    color_t uColor;
    switch (m_eCurrentWeapon)
    {
    case WEAPON_BULLETS:
        /* Use orange glow for non-upgraded bullets, red when upgraded */
        if (gp_state_unlock_get(GP_UNLOCK_BULLETS_UPGRADED))
            uColor = palette_get_cga_color(CGA_LIGHT_RED);
        else
            uColor = palette_get_cga_color(CGA_YELLOW);
        uColor.a = 96;
        return uColor;
    case WEAPON_LASER:
        uColor = palette_get_cga_color(CGA_LIGHT_CYAN);
        uColor.a = 172;
        return uColor;
    case WEAPON_BOMB:
        uColor = palette_get_cga_color(CGA_LIGHT_MAGENTA);
        uColor.a = 128;
        return uColor;
    default:
        return palette_get_cga_color(CGA_WHITE);
    }
}

bool weapons_is_firing(void)
{
    switch (m_eCurrentWeapon)
    {
    case WEAPON_BULLETS:
        return bullets_is_firing();
    case WEAPON_LASER:
        return laser_is_firing();
    case WEAPON_BOMB:
        return bomb_is_firing();
    default:
        return false;
    }
}

void weapons_render_ui(void)
{
    /* Hide weapons UI entirely when no weapon unlock is active */
    if (!weapons_any_unlocked())
        return;

    /* Don't render weapons UI during dialogue */
    if (dialogue_is_active())
        return;

    /* Disable weapons UI in minimap mode */
    if (minimap_is_active())
        return;

    /* Don't render weapons UI when tractor beam is active */
    if (tractor_beam_is_active())
        return;

    /* Get the icon sprite for the current weapon */
    sprite_t *pIcon = m_aWeaponIcons[m_eCurrentWeapon];
    if (!pIcon)
        return; /* Icon not loaded */

    /* Determine which button sprite to use based on number of unlocked weapons */
    int iUnlockedCount = weapons_count_unlocked();
    bool bMultipleWeapons = (iUnlockedCount > 1);
    sprite_t *pBtnSprite = bMultipleWeapons ? m_pBtnInterface : m_pBtnBClear;

    if (!pBtnSprite)
        return; /* Button sprite not loaded */

    /* Get position for top-right using UI helper */
    struct vec2i vBtnPos = ui_get_pos_top_right_sprite(pBtnSprite);
    /* Shift left a bit only when turbo UI is present, to keep spacing balanced */
    if (gp_state_unlock_get(GP_UNLOCK_TURBO))
    {
        vBtnPos.iX -= UI_DESIGNER_PADDING / 2; // extra for n64 layout feel
    }
    /* If tractor beam UI is unlocked, shift weapons UI further left to make room */
    if (gp_state_unlock_get(GP_UNLOCK_TRACTOR_BEAM))
    {
        vBtnPos.iX -= 23;
    }

    /* Render button sprite first */
    rdpq_set_mode_copy(false);
    rdpq_mode_alphacompare(1); /* draw pixels with alpha >= 1 (colorkey style) */
    rdpq_mode_filter(FILTER_POINT);
    rdpq_sprite_blit(pBtnSprite, vBtnPos.iX, vBtnPos.iY, NULL);

    /* Render the icon with offset based on button type */
    struct vec2i vIconPos;
    if (bMultipleWeapons)
    {
        /* btn_interface: offset (18, 3) */
        vIconPos.iX = vBtnPos.iX + 18;
        vIconPos.iY = vBtnPos.iY + 3;
    }
    else
    {
        /* btn_b_clear_00: offset (6, 5) */
        vIconPos.iX = vBtnPos.iX + 3;
        vIconPos.iY = vBtnPos.iY + 3;
    }
    rdpq_sprite_blit(pIcon, vIconPos.iX, vIconPos.iY, NULL);

    /* Render laser overheat meter if laser is selected */
    if (m_eCurrentWeapon == WEAPON_LASER)
    {
        laser_render_overheat_meter();
    }
}
