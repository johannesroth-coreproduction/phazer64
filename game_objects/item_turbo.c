#include "item_turbo.h"
#include "../audio.h"
#include "../camera.h"
#include "../resource_helper.h"
#include "libdragon.h"
#include "ufo.h"
#include "ufo_turbo.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sprite - loaded once and reused */
static sprite_t *m_pTurboSprite = NULL;
static wav64_t *m_pPickupSound = NULL;

/* Turbo item instances - two separate arrays */
#define MAX_TURBO_ITEMS_STATIC 32
#define MAX_TURBO_ITEMS_DYNAMIC 32
static ItemTurboInstance m_aTurboItemsStatic[MAX_TURBO_ITEMS_STATIC];
static ItemTurboInstance m_aTurboItemsDynamic[MAX_TURBO_ITEMS_DYNAMIC];
static size_t m_iTurboItemCountStatic = 0;
static size_t m_iTurboItemCountDynamic = 0;

/* Spawn order counter for dynamic items (used to find oldest) */
static uint32_t m_uSpawnOrderCounter = 0;

/* Helper: Initialize an item entity at the given position */
static void init_item_entity(ItemTurboInstance *pItem, struct vec2 _vPos, uint32_t uSpawnOrder)
{
    if (!m_pTurboSprite)
    {
        debugf("Turbo sprite not loaded\n");
        return;
    }

    /* Initialize entity from pre-loaded sprite */
    uint16_t uFlags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE | ENTITY_FLAG_COLLIDABLE;
    uint16_t uLayerMask = ENTITY_LAYER_GAMEPLAY;

    entity2d_init_from_sprite(&pItem->entity, _vPos, m_pTurboSprite, uFlags, uLayerMask);
    pItem->uSpawnOrder = uSpawnOrder;
}

/* Helper: Update an array of items (check collisions) */
static void update_items_array(ItemTurboInstance *pItems, size_t iCount, const struct entity2D *pUfoEntity)
{

    for (size_t i = 0; i < iCount; ++i)
    {
        ItemTurboInstance *pItem = &pItems[i];

        if (!entity2d_is_active(&pItem->entity))
            continue;

        if (!entity2d_is_collidable(&pItem->entity))
            continue;

        /* Use collision helper to check collision state */
        CollisionEvents events = entity2d_check_collision_and_update(&pItem->entity, pUfoEntity);

        /* Handle collision events */
        if (events.bOnTriggerEnter)
        {
            /* Turbo: refill fuel to 100, destroy item */
            ufo_turbo_refill();
            entity2d_deactivate(&pItem->entity);
            wav64_play(m_pPickupSound, MIXER_CHANNEL_ITEMS);
        }
    }
}

/* Helper: Render an array of items */
static void render_items_array(const ItemTurboInstance *pItems, size_t iCount)
{
    for (size_t i = 0; i < iCount; ++i)
    {
        const ItemTurboInstance *pItem = &pItems[i];
        const struct entity2D *pEnt = &pItem->entity;
        entity2d_render_simple(pEnt);
    }
}

/* Initialize turbo items (loads sprites) */
void item_turbo_init(void)
{
    /* Load sprite once */
    if (!m_pTurboSprite)
        m_pTurboSprite = sprite_load("rom:/item_turbo_00.sprite");

    /* Load pickup sound */
    if (!m_pPickupSound)
        m_pPickupSound = wav64_load("rom:/item_turbo_pickup.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    item_turbo_reset();
}

/* Reset turbo items (clears all items) */
void item_turbo_reset(void)
{
    m_iTurboItemCountStatic = 0;
    m_iTurboItemCountDynamic = 0;
    m_uSpawnOrderCounter = 0;
}

/* Free turbo items (frees sprites/sounds and clears items) */
void item_turbo_free(void)
{
    item_turbo_reset();

    SAFE_FREE_SPRITE(m_pTurboSprite);
    SAFE_CLOSE_WAV64(m_pPickupSound);
}

/* Add a turbo item at the specified position (static, won't disappear) */
void item_turbo_add(struct vec2 _vPos)
{
    if (m_iTurboItemCountStatic >= MAX_TURBO_ITEMS_STATIC)
    {
        debugf("Static turbo item array full\n");
        return; /* Array full */
    }

    ItemTurboInstance *pItem = &m_aTurboItemsStatic[m_iTurboItemCountStatic];
    init_item_entity(pItem, _vPos, 0); /* Spawn order not used for static items */
    m_iTurboItemCountStatic++;
}

/* Spawn a turbo item during gameplay (dynamic, can be replaced if array is full) */
void item_turbo_spawn(struct vec2 _vPos)
{
    if (!m_pTurboSprite)
    {
        debugf("Turbo sprite not loaded\n");
        return;
    }

    /* First, try to find a free spot (inactive item) */
    size_t iFreeIndex = SIZE_MAX;
    for (size_t i = 0; i < m_iTurboItemCountDynamic; ++i)
    {
        if (!entity2d_is_active(&m_aTurboItemsDynamic[i].entity))
        {
            iFreeIndex = i;
            break;
        }
    }

    /* If no free spot found and array is full, find oldest active item */
    if (iFreeIndex == SIZE_MAX)
    {
        if (m_iTurboItemCountDynamic >= MAX_TURBO_ITEMS_DYNAMIC)
        {
            /* Find oldest active item (lowest spawn order) */
            uint32_t uOldestOrder = UINT32_MAX;
            size_t iOldestIndex = 0;
            bool bFoundActive = false;

            for (size_t i = 0; i < m_iTurboItemCountDynamic; ++i)
            {
                if (entity2d_is_active(&m_aTurboItemsDynamic[i].entity))
                {
                    if (m_aTurboItemsDynamic[i].uSpawnOrder < uOldestOrder)
                    {
                        uOldestOrder = m_aTurboItemsDynamic[i].uSpawnOrder;
                        iOldestIndex = i;
                        bFoundActive = true;
                    }
                }
            }

            if (bFoundActive)
            {
                iFreeIndex = iOldestIndex;
            }
            else
            {
                /* No active items, just use the first slot */
                iFreeIndex = 0;
            }
        }
        else
        {
            /* Array not full, add to end */
            iFreeIndex = m_iTurboItemCountDynamic;
            m_iTurboItemCountDynamic++;
        }
    }

    ItemTurboInstance *pItem = &m_aTurboItemsDynamic[iFreeIndex];
    init_item_entity(pItem, _vPos, m_uSpawnOrderCounter++);
}

/* Update turbo items (check collisions) */
void item_turbo_update(void)
{
    const struct entity2D *pUfoEntity = ufo_get_entity();
    if (!pUfoEntity || !entity2d_is_collidable(pUfoEntity))
        return;

    update_items_array(m_aTurboItemsStatic, m_iTurboItemCountStatic, pUfoEntity);
    update_items_array(m_aTurboItemsDynamic, m_iTurboItemCountDynamic, pUfoEntity);
}

/* Render turbo items */
void item_turbo_render(void)
{
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1); /* draw pixels with alpha >= 1 (colorkey style) */

    render_items_array(m_aTurboItemsStatic, m_iTurboItemCountStatic);
    render_items_array(m_aTurboItemsDynamic, m_iTurboItemCountDynamic);
}
