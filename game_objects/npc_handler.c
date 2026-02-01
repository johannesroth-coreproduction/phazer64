#include "npc_handler.h"
#include "../dialogue.h"
#include "../game_objects/gp_state.h"
#include "../game_objects/ufo.h"
#include "../minimap_marker.h"
#include "../path_mover.h"
#include "npc_alien.h"
#include <stdlib.h>
#include <string.h>

/* NPC instance data - one per type */
static NpcAlienInstance *s_apNpcInstances[NPC_TYPE_COUNT] = {NULL};

/* Helper: Immediately despawn an NPC instance */
static void npc_handler_despawn_immediate(npc_type_t _type)
{
    if (_type >= NPC_TYPE_COUNT || s_apNpcInstances[_type] == NULL)
        return;

    /* Safeguard: Ensure UFO doesn't hold a reference to the despawned entity */
    const struct entity2D *pEntity = npc_alien_get_entity(s_apNpcInstances[_type]);
    if (pEntity)
    {
        ufo_deselect_entity_lock_and_marker(pEntity);
    }

    npc_alien_destroy(s_apNpcInstances[_type]);
    s_apNpcInstances[_type] = NULL;
}

void npc_handler_init(void)
{
    /* Initialize all NPC instances to NULL */
    memset(s_apNpcInstances, 0, sizeof(s_apNpcInstances));
}

void npc_handler_spawn(npc_type_t _type)
{
    if (_type >= NPC_TYPE_COUNT)
        return;

    /* If already spawned, despawn first (immediate for spawn replacement) */
    if (s_apNpcInstances[_type] != NULL)
    {
        npc_handler_despawn_immediate(_type);
    }

    /* Create NPC instance */
    NpcAlienInstance *_pInstance = npc_alien_create(_type);
    if (!_pInstance)
        return;

    /* Store instance */
    s_apNpcInstances[_type] = _pInstance;
}

void npc_handler_despawn(npc_type_t _type)
{
    /* Immediate despawn (logic handles markForDelete safe cleanup) */
    npc_handler_despawn_immediate(_type);
}

bool npc_handler_is_spawned(npc_type_t _type)
{
    if (_type >= NPC_TYPE_COUNT)
        return false;
    return s_apNpcInstances[_type] != NULL;
}

const struct entity2D *npc_handler_get_entity(npc_type_t _type)
{
    if (_type >= NPC_TYPE_COUNT)
        return NULL;

    NpcAlienInstance *_pInstance = s_apNpcInstances[_type];
    if (!_pInstance)
        return NULL;

    return npc_alien_get_entity(_pInstance);
}

PathInstance **npc_handler_get_path_ptr(npc_type_t _type)
{
    if (_type >= NPC_TYPE_COUNT)
        return NULL;

    NpcAlienInstance *_pInstance = s_apNpcInstances[_type];
    if (!_pInstance)
        return NULL;

    return npc_alien_get_path_ptr(_pInstance);
}

NpcAlienInstance *npc_handler_get_instance(npc_type_t _type)
{
    if (_type >= NPC_TYPE_COUNT)
        return NULL;

    return s_apNpcInstances[_type];
}
