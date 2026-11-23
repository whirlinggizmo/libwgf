#ifndef WGF_PHYSICS3D_PRIV_H
#define WGF_PHYSICS3D_PRIV_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_color.h"
#include "wgf_mat4.h"
#include "wgf_physics3d_jolt_priv.h"

/* Characters (WGF_BODY3D_TYPE_CHARACTER), reached by physics3d through the hooks their file sets
 * (wgf_physics3d_character.c, linked by wgf_physics3d_enable_characters), so a program that
 * makes none links none of Jolt's CharacterVirtual: the Jolt layer's character calls
 * (wgf_physics3d_jolt_priv.h). NULL until set; a character type set without them is refused. */
typedef struct wgf_physics3d_priv_characters_t {
    int (*create)(const wgf_physics3d_priv_body_desc_t *desc, float max_slope);
    void (*destroy)(int character);
    void (*update)(int character, const float velocity[3], float step, float dt, float position[3],
                   float out_velocity[3]);
    void (*set_pose)(int character, const float position[3], const float rotation[4]);
    bool (*ground)(int character, float normal[3]);
} wgf_physics3d_priv_characters_t;
void wgf_physics3d_priv_set_characters(const wgf_physics3d_priv_characters_t *characters);

/* Vehicles (WGF_COMPONENT_VEHICLE), reached by physics3d through the hooks their file sets
 * (wgf_physics3d_vehicle.c, linked by wgf_physics3d_enable_vehicles), so a program that makes none
 * links none of Jolt's VehicleConstraint: their component made as physics starts (at once, when
 * linked after), let go of as it stops; before each step each made where it changed and given its
 * driver's input, after it its wheels set; a body made again (its vehicle with it); and the debug
 * view's wheels, drawn with its ring. NULL until set; a vehicle component is refused without. */
typedef struct wgf_physics3d_priv_vehicles_t {
    void (*start)(void);
    void (*stop)(void);
    void (*before_step)(void);
    void (*after_step)(void);
    void (*body_unmade)(wgf_actor_t actor);
    void (*draw)(wgf_color_t color, void (*ring)(wgf_mat4_t m, float radius, int axis, float offset, wgf_color_t c));
} wgf_physics3d_priv_vehicles_t;
void wgf_physics3d_priv_set_vehicles(const wgf_physics3d_priv_vehicles_t *vehicles);

/* What the vehicles take from physics3d: physics started (as a body's setter starts it),
 * whether it is; a body's Jolt id, whether it waits to be made again, and its type (false: the
 * actor has no body); and a scene line's value read as its keys say. */
bool wgf_physics3d_priv_start(void);
bool wgf_physics3d_priv_is_started(void);
bool wgf_physics3d_priv_body_info(wgf_actor_t actor, uint32_t *id, bool *dirty, int *type);
typedef struct wgf_physics3d_priv_line_key_t {
    const char *key;
    int count; /* numbers it takes; 0 a word or names; -1 a list of 1 to 8 */
} wgf_physics3d_priv_line_key_t;
int wgf_physics3d_priv_read_value(const char *line, const wgf_physics3d_priv_line_key_t *keys, int key_count,
                                  const char *key, const char *value, double *n);

#endif
