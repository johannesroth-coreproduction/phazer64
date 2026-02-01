#pragma once

#include "game_objects/space_objects.h"
#include "math2d.h"
#include <stdbool.h>
#include <stdint.h>

/* Piece direction enum */
typedef enum
{
    PIECE_DIR_NORTH,
    PIECE_DIR_EAST,
    PIECE_DIR_SOUTH,
    PIECE_DIR_WEST,
    PIECE_DIR_COUNT
} ePieceDirection;

/* Initialize satellite pieces system - loads sprites */
void satellite_pieces_init(void);

/* Free satellite pieces resources */
void satellite_pieces_free(void);

/* Refresh piece entities from piece.csv in current folder (called during layer switches) */
void satellite_pieces_refresh(void);

/* Render satellite pieces UI (called from pause menu) */
void satellite_pieces_render_ui(void);

/* Create a new piece entity at the given position */
bool satellite_pieces_create(uint16_t _uUnlockFlag, struct vec2 _vPos, bool _bAssembleMode);

/* Get piece entity by unlock flag (returns NULL if not found, not active, or not loaded) */
const struct entity2D *satellite_pieces_get_entity_by_unlock_flag(uint16_t _uUnlockFlag);

/* Check if satellite is fully repaired (all four pieces snapped) */
bool satellite_pieces_bSatelliteRepaired(void);

/* Render satellite structure at repair POI (missing slots and center piece) */
void satellite_pieces_render_satellite(void);

/* Check collision with center piece (for UFO collision) */
void satellite_pieces_check_center_collision(void);

/* Spawn all four satellite pieces around UFO in assemble mode with velocity away from UFO */
void satellite_pieces_spawn_assemble_pieces(void);

/* Internal helpers for space_objects */
void satellite_piece_update_object(SpaceObject *obj);
void satellite_piece_render_object(SpaceObject *obj, struct vec2i vScreen, float fZoom);
void satellite_piece_collect(SpaceObject *obj);
