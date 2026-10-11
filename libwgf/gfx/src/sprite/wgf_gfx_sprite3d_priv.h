#ifndef WGF_GFX_SPRITE3D_PRIV_H
#define WGF_GFX_SPRITE3D_PRIV_H

#include <stdbool.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "sprite/wgf_gfx_sprite_batch_priv.h"
#include "wgf_mat4.h"
#include "wgf_vec3.h"

/* Sprites on a 3D stage (wgf_gfx_sprite3d.c): libwgt's sprite geometry in a scene
 * (wgrender-c's sprite3d), what a stage3d's draw records into the sprite batch. */

/* The billboards' axes for a camera placed by `camera_world`. */
wgf_gfx_priv_sprite_view_t wgf_gfx_priv_sprite_view_of(wgf_mat4_t camera_world);

/* A sprite actor (`actor_ptr`) placed by `world` as the batch takes it, and its texture's
 * binding; false while it has nothing to draw (no texture, or its texture PENDING). */
bool wgf_gfx_priv_sprite_quad(const wgf_gfx_priv_actor_t *actor_ptr, wgf_mat4_t world,
                              wgf_gfx_priv_sprite_quad_t *quad, sg_view *view, sg_sampler *sampler);

/* The axes a thing facing `facing` (a wgf_sprite_facing_t) at `world` lies along, seen
 * with `view`: libwgt's, for text in 3D, which the CPU places (a sprite's camera facings
 * are its shader's). */
void wgf_gfx_priv_facing_axes(int facing, wgf_mat4_t world, const wgf_gfx_priv_sprite_view_t *view,
                              wgf_vec3_t *right, wgf_vec3_t *up);

/* How far its quad can reach from its position, whichever way it faces: culling's. */
float wgf_gfx_priv_sprite_radius(const wgf_gfx_priv_actor_t *actor_ptr, wgf_mat4_t world);

#endif
