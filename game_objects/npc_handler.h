#pragma once

#include "../entity2d.h"
#include "npc_alien.h"

/* Initialize handler system */
void npc_handler_init(void);

/* Spawn NPC of given type (creates instance and script) */
void npc_handler_spawn(npc_type_t type);

/* Despawn NPC of given type */
void npc_handler_despawn(npc_type_t type);

/* Check if NPC type is spawned */
bool npc_handler_is_spawned(npc_type_t type);

/* Get entity pointer for spawned NPC (NULL if not spawned) */
const struct entity2D *npc_handler_get_entity(npc_type_t type);

/* Get path pointer for spawned NPC (NULL if not spawned) */
PathInstance **npc_handler_get_path_ptr(npc_type_t type);

/* Get NPC instance pointer for spawned NPC (NULL if not spawned) */
NpcAlienInstance *npc_handler_get_instance(npc_type_t type);
