#ifndef WGF_GFX_SPRITE3D_PRIV_H
#define WGF_GFX_SPRITE3D_PRIV_H

#include <stdbool.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "sprite/wgf_gfx_sprite_batch_priv.h"
#include "wgf_mat4.h"
#include "wgf_vec3.h"

/* Sprites on a 3D stage (wgf_gfx_sprite3d.c): libwgt's sprite geometry in a scene
 * (wgrender-c's sprite3d), what a stage3d's draw records into the sprite batch. */

/* The billboards' axes for a camera placed by `camera_world`: spherical (facing CAMERA,
 * parallel to the view plane) and cylindrical (CAMERA_FIXED_Y: turning about world Y,
 * staying upright; the camera's own right when it looks straight up or down). */
typedef struct wgf_gfx_priv_sprite_view_t {
    wgf_vec3_t right, up, upright;
} wgf_gfx_priv_sprite_view_t;
wgf_gfx_priv_sprite_view_t wgf_gfx_priv_sprite_view_of(wgf_mat4_t camera_world);

/* A sprite actor (`actor_ptr`) placed by `world` as the batch takes it, and its texture's
 * binding; false while it has nothing to draw (no texture, or its texture PENDING). */
bool wgf_gfx_priv_sprite_quad(const wgf_gfx_priv_actor_t *actor_ptr, wgf_mat4_t world,
                              wgf_gfx_priv_sprite_quad_t *quad, sg_view *view, sg_sampler *sampler);

/* How far its quad can reach from its position, whichever way it faces: culling's. */
float wgf_gfx_priv_sprite_radius(const wgf_gfx_priv_actor_t *actor_ptr, wgf_mat4_t world);

#endif
