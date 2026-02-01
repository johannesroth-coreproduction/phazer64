#pragma once

#include "../camera.h"
#include "../entity2d.h"
#include "../math2d.h"

/* Planet name constants */
#define PLANET_HOME "terra"

/* Planet instance, embedding entity2D */
typedef struct PlanetInstance
{
    struct entity2D entity; /* shared header: position, extents, flags, layer, sprite */
    char szName[64];        /* planet name from CSV */
} PlanetInstance;

/* Initialization: loads planets from CSV file "rom:/space/planet.csv" */
void planets_init(void);

/* Free planet resources */
void planets_free(void);

/* Per-frame logic update (checks collisions) */
void planets_update(void);

/* Rendering */
void planets_render(void);

/* Get the display name of the currently selected planet (via trigger enter).
 * Returns NULL if no planet is currently selected, otherwise returns the cached uppercase name.
 */
const char *planets_get_selected_display_name(void);

/* Get the data name of the currently selected planet (original name for loading).
 * Returns NULL if no planet is currently selected, otherwise returns the original name.
 */
const char *planets_get_selected_data_name(void);

/* Get the entity of the currently selected planet (via trigger enter).
 * Returns NULL if no planet is currently selected, otherwise returns the planet entity.
 */
const struct entity2D *planets_get_selected_entity(void);

/* Get terra position (returns true if valid, false otherwise) */
bool planets_get_terra_pos(struct vec2 *_pOutPos);