#pragma once

#include "../entity2d.h"
#include "../path_mover.h"
#include "space_objects.h"

/* Aliasing NpcAlienInstance to SpaceObject for compatibility */
typedef struct SpaceObject NpcAlienInstance;

/* NPC Type Enum */
typedef enum
{
    NPC_TYPE_ALIEN,
    NPC_TYPE_RHINO,
    NPC_TYPE_COUNT
} npc_type_t;

/* Create NPC alien instance for given type via space_objects */
NpcAlienInstance *npc_alien_create(npc_type_t type);

/* Destroy NPC alien instance */
void npc_alien_destroy(NpcAlienInstance *pInstance);

/* Update: rotation, path control, state tracking */
/* Called by space_objects_update */
void npc_alien_update_object(SpaceObject *pInstance);

/* Render alien and thrusters */
/* Called by space_objects_render */
void npc_alien_render_object(SpaceObject *pInstance, struct vec2i vScreen, float fZoom);

/* Legacy update wrapper (deprecated/redirects) */
/* Removed */

/* Get entity2d pointer */
const struct entity2D *npc_alien_get_entity(NpcAlienInstance *pInstance);

/* Get path pointer address (for script user_data) */
PathInstance **npc_alien_get_path_ptr(NpcAlienInstance *pInstance);

/* Configure path mover based on NPC type (automatically called by scripts) */
void npc_alien_configure_path_by_type(PathInstance *pPath, npc_type_t type);

/* Set path (overrides direct target behavior until a direct target is set) */
void npc_alien_set_path(NpcAlienInstance *pInstance, PathInstance *pPath, bool bPositionEntity, bool bWaitForPlayer);

/* Set direct target position (overrides path behavior until a path is set) */
void npc_alien_set_direct_target(NpcAlienInstance *pInstance, struct vec2 vTarget, bool bWaitForPlayer);

/* Get reached target flag (true when path/direct target is finished AND NPC is close to target) */
bool npc_alien_get_reached_target(NpcAlienInstance *pInstance);

/* Reset reached target flag (used when setting new path/target) */
void npc_alien_reset_reached_target(NpcAlienInstance *pInstance);
