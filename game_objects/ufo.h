#pragma once

#include "../camera.h"
#include "../entity2d.h"
#include "../math2d.h"
#include "../tilemap.h"
#include "gp_state.h"
#include "libdragon.h"

/* Initialization: loads spawn position from CSV if in SPACE state. */
void ufo_init(void);

/* Free resources (sprites, sounds) */
void ufo_free(void);

/* Set UFO position directly. */
void ufo_set_position(struct vec2 _vPos);

/* Set UFO position from a folder's logic.csv file (loads "spawn,x,y" entry). */
void ufo_set_position_from_data(const char *_pFolderName);

/* Start landing/launching animation between game states. */
void ufo_start_transition_animation(gp_state_t _StateFrom, gp_state_t _StateTo);
/* End landing/launch animation early and resume normal rendering/control. */
void ufo_end_transition_animation(gp_state_t _TargetState);

/* True only while animation is progressing, false once in hold. */
bool ufo_is_transition_playing(void);

/* Play door sound effect. */
void ufo_play_door(bool _bOpening);

/* Force update of shadow position (useful when initializing state) */
void ufo_recover_surface_position_mode(void);
void ufo_recover_planet_position_mode(void);

/* Update: physics, input, animation. */
void ufo_update(bool _bTurboPressed, bool _bTargetLockPressed, bool _bTractorBeamPressed, int _iStickX, int _iStickY);

/* Render UFO and related effects (thrusters, shadows). */
void ufo_render(void);

/* Render target lock indicators. */
void ufo_render_target_lock(void);

/* Getters */
struct vec2 ufo_get_position(void);
struct vec2 ufo_get_velocity(void);
float ufo_get_angle_rad(void);
float ufo_get_speed(void);
float ufo_get_stick_force(void);
int ufo_get_stick_angle(void);
float ufo_get_thrust(void);
const struct entity2D *ufo_get_entity(void);
struct vec2 ufo_get_shadow_position(void);
bool ufo_can_land(void);
void ufo_set_velocity(struct vec2 _vVel);
void ufo_set_angle_rad(float _fAngleRad);

/* Target Lock */
bool ufo_is_target_locked(void);
const struct entity2D *ufo_get_locked_target(void);
const struct entity2D *ufo_get_potential_target(void);

/* Next Target Indicator */
void ufo_set_next_target(const struct entity2D *_pEntity);
const struct entity2D *ufo_get_next_target(void);
/* Notify UFO that an entity is being destroyed so it can clear references (target lock, next target) */
void ufo_deselect_entity_lock_and_marker(const struct entity2D *_pEntity);

/* Bounce effect (from obstacle) */
void ufo_apply_bounce_effect(uint32_t _uDurationMs);
bool ufo_is_bouncing(void);
