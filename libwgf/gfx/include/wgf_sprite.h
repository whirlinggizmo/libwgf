#ifndef WGF_SPRITE_H
#define WGF_SPRITE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_material.h"
#include "wgf_actor.h"
#include "wgf_texture.h"
#include "wgf_vec2.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A sprite: an actor showing a texture, or a region of one, on a 2D stage or a 3D one. It
 * holds a reference to its texture, dropped when it is given another or destroyed. Sprites
 * are actors (wgf_actor.h): place, turn, scale, parent, and destroy them with the actor
 * calls. While its texture is PENDING it draws nothing; once that FAILED, the checker.
 *
 * On a 2D stage it is drawn in the stage's units through immediate mode. On a 3D stage it
 * is a quad in the stage's units (its size; 0, the default, is 1 by 1), facing as its
 * facing says, scaled by its actor, culled by the box its quad can reach, drawn as
 * libwgt's instanced sprites: OPAQUE and MASK ones with the opaque models, alike ones one
 * draw; BLEND ones among the see-through models, back to front; ADD ones after them. It
 * casts no shadow, and is unlit. */

/* A sprite of `texture` (0 for none: it draws nothing). 0 when there is no room for
 * another actor. */
WGF_API wgf_actor_t wgf_sprite_create(wgf_texture_t texture);

/* False for a handle that isn't a sprite, or a texture that isn't one (0 is none). */
WGF_API bool wgf_sprite_set_texture(wgf_actor_t sprite, wgf_texture_t texture);
WGF_API wgf_texture_t wgf_sprite_get_texture(wgf_actor_t sprite);

/* The region of the texture shown, in its pixels from the top-left; a width or height
 * of 0 or less (the default) shows the whole texture. False for a handle that isn't a
 * sprite. Read back as x, y, width, height. */
WGF_API bool wgf_sprite_set_source(wgf_actor_t sprite, float x, float y, float width, float height);
WGF_API wgf_vec4_t wgf_sprite_get_source(wgf_actor_t sprite);

/* Its size in its own units; a width or height of 0 or less (the default) is the
 * region's size in pixels on a 2D stage (1 by 1 on a 3D one). get_size is the size it draws at: what was set, or else
 * the region's (0, 0 while its texture isn't READY and no size is set). False for a
 * handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_size(wgf_actor_t sprite, float width, float height);
WGF_API wgf_vec2_t wgf_sprite_get_size(wgf_actor_t sprite);

/* The point of the sprite at its actor's position, which it turns and scales about, as
 * a fraction of its size: 0.5, 0.5 its center (the default), 0, 0 its top-left. False
 * for a handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_pivot(wgf_actor_t sprite, float x, float y);
WGF_API wgf_vec2_t wgf_sprite_get_pivot(wgf_actor_t sprite);

/* What its texels are multiplied by; white (the default) leaves them as they are. False
 * for a handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_tint(wgf_actor_t sprite, wgf_color_t tint);
WGF_API wgf_color_t wgf_sprite_get_tint(wgf_actor_t sprite);

/* How it uses its texels' alpha (wgf_material.h's modes; default BLEND), and the cutoff
 * MASK cuts out below (0..1, clamped; default 0.5): OPAQUE draws no blending, ADD adds. On
 * a 2D stage MASK draws blended (immediate mode cuts out nothing). False for a handle that
 * isn't a sprite, or a mode that isn't one. */
WGF_API bool wgf_sprite_set_alpha_mode(wgf_actor_t sprite, wgf_alpha_mode_t mode, float cutoff);
WGF_API wgf_alpha_mode_t wgf_sprite_get_alpha_mode(wgf_actor_t sprite);
WGF_API float wgf_sprite_get_alpha_cutoff(wgf_actor_t sprite);

/* How a sprite on a 3D stage faces, libwgt's and wgrender-c's (on a 2D stage it lies in
 * the stage, turned by its actor): */
typedef enum wgf_sprite_facing_t {
    WGF_SPRITE_FACING_CAMERA = 0,         /* the camera, parallel to its view: a coin, a glow */
    WGF_SPRITE_FACING_CAMERA_FIXED_Y = 1, /* the camera, turning about Y and staying upright: a tree */
    WGF_SPRITE_FACING_Y_UP = 2,           /* flat on the ground, facing up: a decal, a shadow */
    WGF_SPRITE_FACING_FREE = 3            /* as its actor is turned, in its x-y plane: a sign */
} wgf_sprite_facing_t;
/* Default CAMERA. False for a handle that isn't a sprite, or a facing that isn't one. */
WGF_API bool wgf_sprite_set_facing(wgf_actor_t sprite, wgf_sprite_facing_t facing);
WGF_API wgf_sprite_facing_t wgf_sprite_get_facing(wgf_actor_t sprite);

#ifdef __cplusplus
}
#endif

#endif
