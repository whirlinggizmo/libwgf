#ifndef WGF_SPRITE_BATCH_H
#define WGF_SPRITE_BATCH_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_material.h"
#include "wgf_texture.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A sprite batch: one actor drawing many quads of one texture on a 2D stage, in one draw,
 * for what is too many to be an actor each (bullets, a crowd, a bunnymark's bunnies). Each
 * quad has a position, a size, a turn, a region of the texture, and a tint, which the
 * program writes in bulk, as arrays, whenever they change; the batch draws them all each
 * frame, in their order, through its actor (place, turn, scale, and parent it with the actor
 * calls: a quad's position is in the actor's units). It holds a reference to its texture.
 * While the texture is PENDING it draws nothing; once that FAILED, the checker over every
 * quad. A batch on a 3D stage draws nothing.
 *
 * A quad made by set_count is at 0, 0, its size and region 0 (the region's size, and the
 * whole texture), unturned, and white, until written. */

/* A batch of `texture` with no quads (0 for no texture: it draws nothing). 0 when there is
 * no room for another actor. */
WGF_API wgf_actor_t wgf_sprite_batch_create(wgf_texture_t texture);

/* False for a handle that isn't a batch, or a texture that isn't one (0 is none). */
WGF_API bool wgf_sprite_batch_set_texture(wgf_actor_t batch, wgf_texture_t texture);
WGF_API wgf_texture_t wgf_sprite_batch_get_texture(wgf_actor_t batch);

/* How many quads it draws: more are made as above, fewer drop those past the count. False
 * for a handle that isn't a batch, a count below 0 or over 16777216, or no memory for it. */
WGF_API bool wgf_sprite_batch_set_count(wgf_actor_t batch, int count);
WGF_API int wgf_sprite_batch_get_count(wgf_actor_t batch);

/* The quads' positions from quad `first` on, x and y in turn (`count` floats, count / 2
 * quads): where each one's pivot goes, in the batch's units. Quads past the batch's count
 * are left out; returns how many quads it wrote (0 for a handle that isn't a batch, a
 * `first` outside the batch, or an odd count). The getter reads them back into `out`, as
 * many as fit in `count` floats, returning how many floats it filled. */
WGF_API int wgf_sprite_batch_set_positions(wgf_actor_t batch, int first, const float *positions, int count);
WGF_API int wgf_sprite_batch_get_positions(wgf_actor_t batch, int first, float *out, int count);

/* The quads' sizes, width and height in turn, as positions are; a width or height of 0 is
 * the region's size in texels, and a negative one mirrors the quad across that axis, about
 * its pivot (a tile flipped). */
WGF_API int wgf_sprite_batch_set_sizes(wgf_actor_t batch, int first, const float *sizes, int count);
WGF_API int wgf_sprite_batch_get_sizes(wgf_actor_t batch, int first, float *out, int count);

/* The quads' turns, in radians about their pivots, one a quad (`count` floats, count
 * quads), clockwise on a 2D stage as an actor's turn is. */
WGF_API int wgf_sprite_batch_set_rotations(wgf_actor_t batch, int first, const float *radians, int count);
WGF_API int wgf_sprite_batch_get_rotations(wgf_actor_t batch, int first, float *out, int count);

/* The quads' regions of the texture, x, y, width, and height in its pixels from the
 * top-left, four floats a quad (count / 4 quads; a count not a multiple of 4 writes none),
 * each rounded to a whole pixel and clamped to 0..32767; a width or height of 0 shows the
 * whole texture. */
WGF_API int wgf_sprite_batch_set_regions(wgf_actor_t batch, int first, const float *regions, int count);
WGF_API int wgf_sprite_batch_get_regions(wgf_actor_t batch, int first, float *out, int count);

/* The quads' tints, one a quad (count quads), each a wgf_color_t (0xRRGGBBAA) as an int. */
WGF_API int wgf_sprite_batch_set_tints(wgf_actor_t batch, int first, const int *tints, int count);
WGF_API int wgf_sprite_batch_get_tints(wgf_actor_t batch, int first, int *out, int count);

/* The point of every quad at its position, which it turns about, as a fraction of its size:
 * 0.5, 0.5 its center (the default), 0, 0 its top-left. False for a handle that isn't a
 * batch. */
WGF_API bool wgf_sprite_batch_set_pivot(wgf_actor_t batch, float x, float y);
WGF_API wgf_vec2_t wgf_sprite_batch_get_pivot(wgf_actor_t batch);

/* How its quads use their texels' alpha (wgf_material.h's modes; default BLEND), and the
 * cutoff MASK cuts out below (0..1, clamped; default 0.5): OPAQUE draws no blending, MASK
 * cuts out and draws the rest opaque, ADD adds. False for a handle that isn't a batch, or a
 * mode that isn't one. */
WGF_API bool wgf_sprite_batch_set_alpha_mode(wgf_actor_t batch, wgf_alpha_mode_t mode, float cutoff);
WGF_API wgf_alpha_mode_t wgf_sprite_batch_get_alpha_mode(wgf_actor_t batch);
WGF_API float wgf_sprite_batch_get_alpha_cutoff(wgf_actor_t batch);

#ifdef __cplusplus
}
#endif

#endif
