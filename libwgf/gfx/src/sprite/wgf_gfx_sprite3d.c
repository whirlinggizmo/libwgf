#include "sprite/wgf_gfx_sprite3d_priv.h"

#include <math.h>
#include <string.h>

#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_sprite.h"

/* Sprites on a 3D stage: libwgt's (gfx/src/sprite/wgt_gfx_sprite.c, "in a scene",
 * wgrender-c's sprite3d), with wgrender-c's facing fix of 2026-09-18 (CAMERA spherical,
 * CAMERA_FIXED_Y cylindrical, neither tilting, and FIXED_Y never collapsing to no width
 * when the camera looks straight down). Linked by the stage3d, which records its sprites
 * into the instanced batch (wgf_gfx_sprite_batch.c): a 2D-only program links none of it. */

wgf_gfx_priv_sprite_view_t wgf_gfx_priv_sprite_view_of(wgf_mat4_t camera_world)
{
    const wgf_vec3_t forward =
        wgf_vec3_normalize(wgf_mat4_transform_direction(camera_world, wgf_vec3_make(0.0f, 0.0f, -1.0f)));
    const wgf_vec3_t camera_up =
        wgf_vec3_normalize(wgf_mat4_transform_direction(camera_world, wgf_vec3_make(0.0f, 1.0f, 0.0f)));
    wgf_gfx_priv_sprite_view_t view;
    wgf_vec3_t horizontal;
    /* spherical: parallel to the view plane, facing the camera whatever its pitch */
    view.right = wgf_vec3_normalize(wgf_vec3_cross(forward, camera_up));
    view.up = wgf_vec3_normalize(wgf_vec3_cross(view.right, forward));
    /* cylindrical: turns about world Y to face the camera and stays upright, so a tree
       doesn't lean back when the camera looks down at it */
    horizontal = wgf_vec3_cross(forward, wgf_vec3_make(0.0f, 1.0f, 0.0f));
    if (wgf_vec3_dot(horizontal, horizontal) < 1e-12f) {
        horizontal = wgf_vec3_cross(forward, camera_up); /* looking straight up or down: the camera's own right */
    }
    view.upright = wgf_vec3_normalize(horizontal);
    return view;
}

/* Its quad's own axes (facing Y_UP and FREE; the billboards' come from the camera). */
static void own_axes(int facing, wgf_mat4_t world, wgf_vec3_t *right, wgf_vec3_t *up)
{
    if (facing == WGF_SPRITE_FACING_FREE) { /* in the actor's local x-y plane, as it is turned */
        *right = wgf_vec3_normalize(wgf_vec3_make(world.m[0], world.m[1], world.m[2]));
        *up = wgf_vec3_normalize(wgf_vec3_make(world.m[4], world.m[5], world.m[6]));
    } else {
        *right = wgf_vec3_make(1.0f, 0.0f, 0.0f);
        *up = wgf_vec3_make(0.0f, 0.0f, -1.0f);
    }
}

void wgf_gfx_priv_facing_axes(int facing, wgf_mat4_t world, const wgf_gfx_priv_sprite_view_t *view,
                              wgf_vec3_t *right, wgf_vec3_t *up)
{
    if (facing == WGF_SPRITE_FACING_CAMERA) {
        *right = view->right;
        *up = view->up;
    } else if (facing == WGF_SPRITE_FACING_CAMERA_FIXED_Y) {
        *right = view->upright;
        *up = wgf_vec3_make(0.0f, 1.0f, 0.0f);
    } else {
        own_axes(facing, world, right, up);
    }
}

/* Its size in the stage: its own (1 by 1 unset), times its actor's scale. */
static void world_size(const wgf_gfx_priv_sprite_t *sprite, wgf_mat4_t world, float *width, float *height)
{
    const wgf_vec3_t scale = wgf_mat4_get_scale(world);
    const bool set = sprite->size[0] > 0.0f && sprite->size[1] > 0.0f;
    *width = (set ? sprite->size[0] : 1.0f) * scale.x;
    *height = (set ? sprite->size[1] : 1.0f) * scale.y;
}

bool wgf_gfx_priv_sprite_quad(const wgf_gfx_priv_actor_t *actor_ptr, wgf_mat4_t world,
                              wgf_gfx_priv_sprite_quad_t *quad, sg_view *view, sg_sampler *sampler)
{
    const wgf_gfx_priv_sprite_t *sprite = &actor_ptr->as.sprite;
    const wgf_vec3_t position = wgf_mat4_get_translation(world);
    int tw, th;
    bool placeholder;

    if (sprite->texture == 0 || !wgf_gfx_priv_texture_get_binding(sprite->texture, view, sampler, &tw, &th, &placeholder)) {
        return false;
    }
    memset(quad, 0, sizeof(*quad));
    quad->position[0] = position.x;
    quad->position[1] = position.y;
    quad->position[2] = position.z;
    quad->facing = (float)sprite->facing;
    world_size(sprite, world, &quad->size[0], &quad->size[1]);
    quad->pivot[0] = sprite->pivot[0];
    quad->pivot[1] = sprite->pivot[1];
    quad->uv[2] = quad->uv[3] = 1.0f;
    if (!placeholder && tw > 0 && th > 0 && sprite->source[2] > 0.0f && sprite->source[3] > 0.0f) {
        quad->uv[0] = sprite->source[0] / (float)tw;
        quad->uv[1] = sprite->source[1] / (float)th;
        quad->uv[2] = (sprite->source[0] + sprite->source[2]) / (float)tw;
        quad->uv[3] = (sprite->source[1] + sprite->source[3]) / (float)th;
    }
    if (sprite->facing != WGF_SPRITE_FACING_CAMERA && sprite->facing != WGF_SPRITE_FACING_CAMERA_FIXED_Y) {
        wgf_vec3_t right, up;
        own_axes(sprite->facing, world, &right, &up);
        quad->right[0] = right.x, quad->right[1] = right.y, quad->right[2] = right.z;
        quad->up[0] = up.x, quad->up[1] = up.y, quad->up[2] = up.z;
    }
    if (sprite->alpha_mode == WGF_ALPHA_MODE_MASK) { /* the shader's: > 0 a cutoff, < 0 opaque, 0 as it is */
        quad->alpha = fmaxf(sprite->alpha_cutoff, 1e-6f);
    } else {
        quad->alpha = sprite->alpha_mode == WGF_ALPHA_MODE_OPAQUE ? -1.0f : 0.0f;
    }
    quad->color[0] = (uint8_t)wgf_color_get_red(sprite->tint);
    quad->color[1] = (uint8_t)wgf_color_get_green(sprite->tint);
    quad->color[2] = (uint8_t)wgf_color_get_blue(sprite->tint);
    quad->color[3] = (uint8_t)wgf_color_get_alpha(sprite->tint);
    return true;
}

float wgf_gfx_priv_sprite_radius(const wgf_gfx_priv_actor_t *actor_ptr, wgf_mat4_t world)
{
    const wgf_gfx_priv_sprite_t *sprite = &actor_ptr->as.sprite;
    float width, height, half_w, half_h, off_x, off_y;
    world_size(sprite, world, &width, &height);
    half_w = 0.5f * width;
    half_h = 0.5f * height;
    off_x = (0.5f - sprite->pivot[0]) * width; /* how far its center sits from its position */
    off_y = (sprite->pivot[1] - 0.5f) * height;
    return sqrtf(half_w * half_w + half_h * half_h) + sqrtf(off_x * off_x + off_y * off_y);
}
