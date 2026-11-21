#ifndef WGF_PHYSICS3D_PRIV_H
#define WGF_PHYSICS3D_PRIV_H

#include <stdbool.h>

#include "wgf_physics3d_jolt_priv.h"

/* Characters (WGF_BODY_TYPE_CHARACTER), reached by physics3d through the hooks their file sets
 * (wgf_physics3d_character.c, linked by wgf_physics_enable_characters), so a program that
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

#endif
