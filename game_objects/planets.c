#include "planets.h"
#include "../camera.h"
#include "../csv_helper.h"
#include "../font_helper.h"
#include "../math_helper.h"
#include "../minimap.h"
#include "../resource_helper.h"
#include "../string_helper.h"
#include "../triggers.h"
#include "../ui.h"
#include "gp_state.h"
#include "libdragon.h"
#include "sprite.h"
#include "ufo.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Planet instances - dynamically allocated */
#define MAX_PLANETS 64
#define MIN_PLANET_SCALE 0.5f /* Minimum planet scale when at minimap zoom level */
static PlanetInstance *m_aPlanets = NULL;
static size_t m_iPlanetCount = 0;
static size_t m_iPlanetCapacity = 0;

/* Decorative objects - simple entity2d array */
#define MAX_DECO 128
static struct entity2D *m_aDeco = NULL;
static size_t m_iDecoCount = 0;
static size_t m_iDecoCapacity = 0;

/* Trigger collection for planets */
static trigger_collection_t m_planetTriggers;
static bool m_bIntroTriggerFilterApplied = false;

/* Terra position storage */
static struct vec2 m_vTerraPos = {0.0f, 0.0f};
static bool m_bTerraPosValid = false;

/* Parse a single CSV line: name,x,y,texture or ,x,y,texture (no name)
 * Uses triggers.c helper for name,x,y parsing */
static bool parse_entity_line(const char *_pLine, char *_pOutName, size_t _uNameSize, struct vec2 *_pOutPos, char *_pOutTexture, size_t _uTextureSize)
{
    if (!_pLine || !_pOutName || !_pOutPos || !_pOutTexture)
        return false;

    char szLineCopy[512];
    if (!csv_helper_copy_line_for_tokenizing(_pLine, szLineCopy, sizeof(szLineCopy)))
        return false;

    /* Parse optional name,x,y using common helper */
    if (!csv_helper_parse_optional_name_xy(szLineCopy, _pOutName, _uNameSize, _pOutPos))
        return false;

    /* Parse texture (fourth token) */
    char *pToken = strtok(NULL, ",");
    if (!pToken)
        return false;

    if (!csv_helper_copy_string_safe(pToken, _pOutTexture, _uTextureSize))
        return false;

    return true;
}

/* Load entities from CSV file (shared helper for planets and deco) */
static size_t load_entities_from_csv(const char *_pCsvPath, bool _bRequireName)
{
    FILE *pFile = fopen(_pCsvPath, "r");
    if (!pFile)
    {
        if (_bRequireName)
            debugf("Failed to open CSV file: %s\n", _pCsvPath);
        else
            debugf("Deco CSV file not found (optional): %s\n", _pCsvPath);
        return 0;
    }

    char szLine[512];
    size_t uLineNum = 0;
    size_t uLoadedCount = 0;

    /* Read and parse each line */
    while (true)
    {
        bool bTruncated = false;
        if (!csv_helper_fgets_checked(szLine, sizeof(szLine), pFile, &bTruncated))
            break;

        if (bTruncated)
        {
            debugf("CSV line too long (buffer %u) in %s at line %u\n", (unsigned)sizeof(szLine), _pCsvPath, (unsigned)uLineNum);
            continue;
        }

        csv_helper_strip_eol(szLine);

        /* Skip empty lines */
        if (szLine[0] == '\0')
            continue;

        uLineNum++;

        /* Parse line */
        char szName[64];
        struct vec2 vPos;
        char szTexture[128];

        if (!parse_entity_line(szLine, szName, sizeof(szName), &vPos, szTexture, sizeof(szTexture)))
        {
            debugf("Failed to parse line %u: %s\n", (unsigned)uLineNum, szLine);
            continue;
        }

        /* Validate name requirement */
        if (_bRequireName && szName[0] == '\0')
        {
            debugf("Line %u missing required name: %s\n", (unsigned)uLineNum, szLine);
            continue;
        }

        /* Store terra position if this is the terra planet */
        if (_bRequireName && strcmp(szName, PLANET_HOME) == 0)
        {
            m_vTerraPos = vPos;
            m_bTerraPosValid = true;
        }

        /* Build sprite path: rom:/space/<texture>.sprite */
        char szSpritePath[256];
        snprintf(szSpritePath, sizeof(szSpritePath), "rom:/space/%s.sprite", szTexture);

        /* Load sprite */
        sprite_t *pSprite = sprite_load(szSpritePath);
        if (!pSprite)
        {
            debugf("Failed to load sprite: %s\n", szSpritePath);
            continue;
        }

        /* Initialize entity based on type */
        if (_bRequireName)
        {
            /* Planet: check capacity */
            if (m_iPlanetCount >= m_iPlanetCapacity)
            {
                debugf("Planet array full, skipping remaining planets\n");
                sprite_free(pSprite);
                break;
            }

            PlanetInstance *pPlanet = &m_aPlanets[m_iPlanetCount];
            memset(pPlanet, 0, sizeof(*pPlanet));

            uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
            uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;

            entity2d_init_from_sprite(&pPlanet->entity, vPos, pSprite, uFlags, uLayerMask);
            if (!csv_helper_copy_string_safe(szName, pPlanet->szName, sizeof(pPlanet->szName)))
            {
                debugf("Failed to copy planet name\n");
                sprite_free(pSprite);
                continue;
            }

            pPlanet->entity.iCollisionRadius *= 0.7f;
            m_iPlanetCount++;
        }
        else
        {
            /* Deco: check capacity */
            if (m_iDecoCount >= m_iDecoCapacity)
            {
                debugf("Decorative object array full, skipping remaining objects\n");
                sprite_free(pSprite);
                break;
            }

            struct entity2D *pDeco = &m_aDeco[m_iDecoCount];
            memset(pDeco, 0, sizeof(*pDeco));

            uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE;
            uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;

            entity2d_init_from_sprite(pDeco, vPos, pSprite, uFlags, uLayerMask);
            m_iDecoCount++;
        }

        uLoadedCount++;
    }

    fclose(pFile);
    return uLoadedCount;
}

/* Initialize planets (load from CSV) */
void planets_init(void)
{
    /* Always free first to ensure clean state */
    planets_free();

    /* Allocate initial capacity for planets */
    m_iPlanetCapacity = MAX_PLANETS;
    m_aPlanets = (PlanetInstance *)malloc(sizeof(PlanetInstance) * m_iPlanetCapacity);
    if (!m_aPlanets)
    {
        debugf("Failed to allocate memory for planets\n");
        return;
    }

    /* Load planets from CSV (requires names) */
    const char *pCsvPath = "rom:/space/planet.csv";
    load_entities_from_csv(pCsvPath, true);

    /* Initialize trigger collection for the loaded planets */
    trigger_collection_init(&m_planetTriggers);

    /* Add triggers for all loaded planets (planets always have names) */
    for (size_t i = 0; i < m_iPlanetCount; ++i)
    {
        PlanetInstance *pPlanet = &m_aPlanets[i];

        if (m_planetTriggers.uCount < m_planetTriggers.uCapacity)
        {
            trigger_t *pTrigger = &m_planetTriggers.pTriggers[m_planetTriggers.uCount];
            memset(pTrigger, 0, sizeof(*pTrigger));

            /* Store original name (for data loading) */
            if (!csv_helper_copy_string_safe(pPlanet->szName, pTrigger->szName, sizeof(pTrigger->szName)))
                continue;

            /* Create display name using centralized formatting */
            string_helper_nice_location_name(pPlanet->szName, pTrigger->szDisplayName, sizeof(pTrigger->szDisplayName));

            pTrigger->eShape = TRIGGER_SHAPE_CIRCLE;
            pTrigger->eType = TRIGGER_TYPE_LOAD;
            pTrigger->vPos = pPlanet->entity.vPos; /* center position */
            pTrigger->shapeData.circle.fRadius = (float)pPlanet->entity.iCollisionRadius;
            pTrigger->bActive = true;

            m_planetTriggers.uCount++;
        }
    }

    /* Allocate initial capacity for decorative objects */
    m_iDecoCapacity = MAX_DECO;
    m_aDeco = (struct entity2D *)malloc(sizeof(struct entity2D) * m_iDecoCapacity);
    if (!m_aDeco)
    {
        debugf("Failed to allocate memory for decorative objects\n");
        return;
    }

    /* Load decorative objects from CSV (no names required) */
    const char *pDecoCsvPath = "rom:/space/deco.csv";
    load_entities_from_csv(pDecoCsvPath, false);
}

void planets_free(void)
{
    if (m_aPlanets)
    {
        /* Free existing sprites */
        for (size_t i = 0; i < m_iPlanetCount; ++i)
        {
            SAFE_FREE_SPRITE(m_aPlanets[i].entity.pSprite);
        }
        free(m_aPlanets);
        m_aPlanets = NULL;
    }
    m_iPlanetCount = 0;
    m_iPlanetCapacity = 0;

    if (m_aDeco)
    {
        /* Free decorative object sprites */
        for (size_t i = 0; i < m_iDecoCount; ++i)
        {
            SAFE_FREE_SPRITE(m_aDeco[i].pSprite);
        }
        free(m_aDeco);
        m_aDeco = NULL;
    }
    m_iDecoCount = 0;
    m_iDecoCapacity = 0;

    /* Free trigger collection */
    trigger_collection_free(&m_planetTriggers);

    /* Reset terra position */
    m_vTerraPos = vec2_zero();
    m_bTerraPosValid = false;
}

/* Update planets (check collisions using trigger system) */
void planets_update(void)
{
    const struct entity2D *pUfoEntity = ufo_get_entity();
    if (!pUfoEntity || !entity2d_is_collidable(pUfoEntity))
        return;

    /* During ACT_INTRO, only keep the Terra planet trigger active.
       Apply the (potentially expensive) strcmp-based filtering only once
       when entering ACT_INTRO, and restore all triggers when leaving it. */
    gp_act_t eAct = gp_state_act_get();
    if (eAct == ACT_INTRO)
    {
        if (!m_bIntroTriggerFilterApplied)
        {
            for (size_t i = 0; i < m_planetTriggers.uCount; ++i)
            {
                trigger_t *pTrigger = &m_planetTriggers.pTriggers[i];
                bool bIsTerra = (strcmp(pTrigger->szName, PLANET_HOME) == 0);
                pTrigger->bActive = bIsTerra;
            }
            m_bIntroTriggerFilterApplied = true;
        }
    }
    else if (m_bIntroTriggerFilterApplied)
    {
        /* Leaving ACT_INTRO: reactivate all planet triggers. */
        for (size_t i = 0; i < m_planetTriggers.uCount; ++i)
        {
            m_planetTriggers.pTriggers[i].bActive = true;
        }
        m_bIntroTriggerFilterApplied = false;
    }

    /* Update trigger collision state */
    trigger_collection_update_with_entity(&m_planetTriggers, pUfoEntity);
}

/* Helper: Render entity with scale rules (used for both planets and deco objects)
 * Returns true if rendered, false if skipped (not visible, no sprite, etc.)
 * Outputs screen position for caller use (e.g., planet name rendering) */
static bool render_entity_with_scale(const struct entity2D *_pEnt, bool _bMinimapActive, float _fZoom, struct vec2i *_pOutScreenPos)
{
    if (!_pEnt)
        return false;

    /* Visibility check */
    struct vec2i vScreenPos;
    if (_bMinimapActive)
    {
        camera_world_to_screen(&g_mainCamera, _pEnt->vPos, &vScreenPos);
        float fMargin = 200.0f; /* Large margin for minimap mode */
        if (!camera_is_screen_point_visible(&g_mainCamera, vScreenPos, fMargin))
            return false;
    }
    else
    {
        if (!entity2d_is_visible(_pEnt))
            return false;
        if (!camera_entity_world_to_screen(&g_mainCamera, _pEnt, &vScreenPos))
            return false;
    }

    if (!_pEnt->pSprite)
        return false;

    /* Render with clamped zoom */
    rdpq_blitparms_t parms = {.cx = _pEnt->vHalf.iX, .cy = _pEnt->vHalf.iY, .scale_x = _fZoom, .scale_y = _fZoom, .theta = 0.0f};
    rdpq_sprite_blit(_pEnt->pSprite, vScreenPos.iX, vScreenPos.iY, &parms);

    /* Output values for caller */
    if (_pOutScreenPos)
        *_pOutScreenPos = vScreenPos;

    return true;
}

/* Render planets */
void planets_render(void)
{
    bool bMinimapActive = minimap_is_active();

    /* Calculate zoom scale globally once per frame */
    float fCameraZoom = camera_get_zoom(&g_mainCamera);
    float fZoomRange = 1.0f - MINIMAP_ZOOM_LEVEL;
    float fGlobalZoom = MIN_PLANET_SCALE + (1.0f - MIN_PLANET_SCALE) * (fCameraZoom - MINIMAP_ZOOM_LEVEL) / fZoomRange;
    fGlobalZoom = clampf(fGlobalZoom, MIN_PLANET_SCALE, 1.0f);

    /* Setup RDP mode once for all planets/deco */
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1); /* draw pixels with alpha >= 1 (colorkey style) */

    /* Set filter based on zoom */
    if (fGlobalZoom != 1.0f)
        rdpq_mode_filter(FILTER_BILINEAR);
    else
        rdpq_mode_filter(FILTER_POINT);

    /* Render decorative objects first (background layer) */
    for (size_t i = 0; i < m_iDecoCount; ++i)
    {
        render_entity_with_scale(&m_aDeco[i], bMinimapActive, fGlobalZoom, NULL);
    }

    /* Render planets */
    struct vec2i aPlanetScreenPos[MAX_PLANETS];
    bool aPlanetVisible[MAX_PLANETS];

    for (size_t i = 0; i < m_iPlanetCount; ++i)
    {
        const PlanetInstance *pPlanet = &m_aPlanets[i];
        const struct entity2D *pEnt = &pPlanet->entity;

        aPlanetVisible[i] = render_entity_with_scale(pEnt, bMinimapActive, fGlobalZoom, &aPlanetScreenPos[i]);
    }

    /* Render planet names below sprite when minimap is active */
    if (bMinimapActive)
    {
        for (size_t i = 0; i < m_iPlanetCount; ++i)
        {
            if (!aPlanetVisible[i])
                continue;

            const PlanetInstance *pPlanet = &m_aPlanets[i];
            const struct entity2D *pEnt = &pPlanet->entity;
            struct vec2i vScreenPos = aPlanetScreenPos[i];

            char szDisplayName[64];
            if (string_helper_nice_location_name(pPlanet->szName, szDisplayName, sizeof(szDisplayName)))
            {
                float fTextWidth = font_helper_get_text_width(FONT_NORMAL, szDisplayName);
                float fScaledPadding = (UI_DESIGNER_PADDING / 2.0f) * fGlobalZoom;
                int iTextX = (int)(vScreenPos.iX - fTextWidth / 2.0f);
                int iTextY = vScreenPos.iY + (int)((float)pEnt->vHalf.iY * fGlobalZoom) + (int)fScaledPadding + UI_FONT_Y_OFFSET;
                rdpq_text_printf(NULL, FONT_NORMAL, iTextX, iTextY, "%s", szDisplayName);
            }
        }
    }
}

/* Get the display name of the currently selected planet (via trigger enter).
 * Returns NULL if no planet is currently selected, otherwise returns the trigger's display name.
 */
const char *planets_get_selected_display_name(void)
{
    return trigger_collection_get_selected_display_name(&m_planetTriggers);
}

/* Get the data name of the currently selected planet (original name for loading).
 * Returns NULL if no planet is currently selected, otherwise returns the original name.
 */
const char *planets_get_selected_data_name(void)
{
    return trigger_collection_get_selected_data_name(&m_planetTriggers);
}

/* Get the entity of the currently selected planet (via trigger enter).
 * Returns NULL if no planet is currently selected, otherwise returns the planet entity.
 * Uses pointer comparison instead of string comparison for efficiency.
 */
const struct entity2D *planets_get_selected_entity(void)
{
    const trigger_t *pSelectedTrigger = trigger_collection_get_selected(&m_planetTriggers);
    if (!pSelectedTrigger)
        return NULL;

    /* Find the planet instance that matches the selected trigger by comparing positions */
    /* Since triggers are created in the same order as planets, we can use index */
    for (size_t i = 0; i < m_iPlanetCount && i < m_planetTriggers.uCount; ++i)
    {
        const trigger_t *pTrigger = &m_planetTriggers.pTriggers[i];
        if (pTrigger == pSelectedTrigger)
        {
            return &m_aPlanets[i].entity;
        }
    }

    debugf("ATTENTION: STRING COMPARISON FALLBACK CALLED\n");

    /* Fallback: find by name if index doesn't match (shouldn't happen) */
    for (size_t i = 0; i < m_iPlanetCount; ++i)
    {
        if (strcmp(m_aPlanets[i].szName, pSelectedTrigger->szName) == 0)
        {
            return &m_aPlanets[i].entity;
        }
    }

    return NULL;
}

/* Get terra position (returns true if valid, false otherwise) */
bool planets_get_terra_pos(struct vec2 *_pOutPos)
{
    if (!_pOutPos || !m_bTerraPosValid)
        return false;
    *_pOutPos = m_vTerraPos;
    return true;
}
