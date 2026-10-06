#ifndef WGF_SPRITE_H
#define WGF_SPRITE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_node.h"
#include "wgf_texture.h"
#include "wgf_vec2.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A sprite: a node showing a texture, or a region of one, in a canvas. It holds a
 * reference to its texture, dropped when it is given another or destroyed. Sprites are
 * nodes (wgf_node.h): place, turn, scale, parent, and destroy them with the node calls.
 * While its texture is PENDING it draws nothing; once that FAILED, the checker. */

/* A sprite of `texture` (0 for none: it draws nothing). 0 when there is no room for
 * another node. */
WGF_API wgf_node_t wgf_sprite_create(wgf_texture_t texture);

/* False for a handle that isn't a sprite, or a texture that isn't one (0 is none). */
WGF_API bool wgf_sprite_set_texture(wgf_node_t sprite, wgf_texture_t texture);
WGF_API wgf_texture_t wgf_sprite_get_texture(wgf_node_t sprite);

/* The region of the texture shown, in its pixels from the top-left; a width or height
 * of 0 or less (the default) shows the whole texture. False for a handle that isn't a
 * sprite. Read back as x, y, width, height. */
WGF_API bool wgf_sprite_set_source(wgf_node_t sprite, float x, float y, float width, float height);
WGF_API wgf_vec4_t wgf_sprite_get_source(wgf_node_t sprite);

/* Its size in its own units; a width or height of 0 or less (the default) is the
 * region's size in pixels. get_size is the size it draws at: what was set, or else
 * the region's (0, 0 while its texture isn't READY and no size is set). False for a
 * handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_size(wgf_node_t sprite, float width, float height);
WGF_API wgf_vec2_t wgf_sprite_get_size(wgf_node_t sprite);

/* The point of the sprite at its node's position, which it turns and scales about, as
 * a fraction of its size: 0.5, 0.5 its center (the default), 0, 0 its top-left. False
 * for a handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_pivot(wgf_node_t sprite, float x, float y);
WGF_API wgf_vec2_t wgf_sprite_get_pivot(wgf_node_t sprite);

/* What its texels are multiplied by; white (the default) leaves them as they are. False
 * for a handle that isn't a sprite. */
WGF_API bool wgf_sprite_set_tint(wgf_node_t sprite, wgf_color_t tint);
WGF_API wgf_color_t wgf_sprite_get_tint(wgf_node_t sprite);

#ifdef __cplusplus
}
#endif

#endif
