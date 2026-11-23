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

/* A sprite batch: one actor that draws many quads of one texture on a 2D stage, in one draw.
 *
 * Use a batch when there are too many quads to make each an actor: bullets, a crowd, or a
 * bunnymark's bunnies. Each quad has a position, a size, a turn, a region of the texture, and
 * a tint. You write them in bulk, as arrays, whenever they change. The batch draws them all
 * each frame, in their order.
 *
 * A batch is an actor (wgf_actor.h): place, turn, scale, and parent it with the actor calls.
 * A quad's position is in the actor's units. The batch holds a reference to its texture.
 *
 * - While the texture is PENDING, the batch draws nothing.
 * - Once the texture has FAILED, it draws the placeholder checker over every quad.
 * - A batch on a 3D stage draws nothing.
 *
 * A quad made by set_count starts like this, until written:
 * - at 0, 0;
 * - with size 0 (the region's size) and region 0 (the whole texture);
 * - unturned, and white. */

/* Creates a batch of `texture` with no quads. With 0 for no texture, it draws nothing.
 *
 * Returns 0 if there is no room for another actor. */
WGF_API wgf_actor_t wgf_sprite_batch_create(wgf_texture_t texture);

/* Sets or gets the batch's texture (0 is none). The setter returns false for a handle that
 * isn't a batch, or a `texture` that isn't a texture. */
WGF_API bool wgf_sprite_batch_set_texture(wgf_actor_t batch, wgf_texture_t texture);
WGF_API wgf_texture_t wgf_sprite_batch_get_texture(wgf_actor_t batch);

/* Sets or gets how many quads the batch draws. A larger count makes new quads, set as above.
 * A smaller count drops the quads past it.
 *
 * The setter returns false for a handle that isn't a batch, a `count` below 0 or over
 * 16777216, or no memory for it. */
WGF_API bool wgf_sprite_batch_set_count(wgf_actor_t batch, int count);
WGF_API int wgf_sprite_batch_get_count(wgf_actor_t batch);

/* Sets or gets the quads' positions, from quad `first` on: where each quad's pivot goes, in
 * the batch's units.
 *
 * `positions` holds x and y in turn: `count` floats, for count / 2 quads. Quads past the
 * batch's count are left out. The setter returns how many quads it wrote. It returns 0 for a
 * handle that isn't a batch, a `first` outside the batch, or an odd `count`.
 *
 * The getter reads the positions back into `out`, as many as fit in `count` floats. It
 * returns how many floats it filled. */
WGF_API int wgf_sprite_batch_set_positions(wgf_actor_t batch, int first, const float *positions, int count);
WGF_API int wgf_sprite_batch_get_positions(wgf_actor_t batch, int first, float *out, int count);

/* Sets or gets the quads' sizes, as width and height in turn, in the same way as positions.
 *
 * - A width or height of 0 uses the region's size in texels.
 * - A negative width or height mirrors the quad across that axis, about its pivot: a tile
 *   flipped. */
WGF_API int wgf_sprite_batch_set_sizes(wgf_actor_t batch, int first, const float *sizes, int count);
WGF_API int wgf_sprite_batch_get_sizes(wgf_actor_t batch, int first, float *out, int count);

/* Sets or gets the quads' turns about their pivots, in radians. There is one float a quad
 * (`count` floats, for `count` quads). On a 2D stage they turn clockwise, as an actor's turn
 * does. */
WGF_API int wgf_sprite_batch_set_rotations(wgf_actor_t batch, int first, const float *radians, int count);
WGF_API int wgf_sprite_batch_get_rotations(wgf_actor_t batch, int first, float *out, int count);

/* Sets or gets the quads' regions of the texture: x, y, width, and height, in texture pixels
 * from the top-left.
 *
 * - There are four floats a quad, for count / 4 quads. A `count` that isn't a multiple of 4
 *   writes none.
 * - Each value is rounded to a whole pixel and clamped to 0..32767.
 * - A width or height of 0 shows the whole texture. */
WGF_API int wgf_sprite_batch_set_regions(wgf_actor_t batch, int first, const float *regions, int count);
WGF_API int wgf_sprite_batch_get_regions(wgf_actor_t batch, int first, float *out, int count);

/* Sets or gets the quads' tints, one a quad (`count` quads). Each is a wgf_color_t
 * (0xRRGGBBAA) as an int. */
WGF_API int wgf_sprite_batch_set_tints(wgf_actor_t batch, int first, const int *tints, int count);
WGF_API int wgf_sprite_batch_get_tints(wgf_actor_t batch, int first, int *out, int count);

/* Sets or gets the pivot of every quad: the point placed at its position, which it turns
 * about. It is a fraction of the quad's size.
 *
 * - 0.5, 0.5 is its center (default).
 * - 0, 0 is its top-left.
 *
 * The setter returns false for a handle that isn't a batch. */
WGF_API bool wgf_sprite_batch_set_pivot(wgf_actor_t batch, float x, float y);
WGF_API wgf_vec2_t wgf_sprite_batch_get_pivot(wgf_actor_t batch);

/* Sets or gets how the batch's quads use their texels' alpha, one of wgf_material.h's modes
 * (default: BLEND), and the cutoff MASK cuts out below (default: 0.5, clamped to 0..1).
 *
 * - OPAQUE draws without blending.
 * - MASK cuts out below the cutoff and draws the rest opaque.
 * - ADD adds its color.
 *
 * The setter returns false for a handle that isn't a batch, or a `mode` that isn't an alpha
 * mode. */
WGF_API bool wgf_sprite_batch_set_alpha_mode(wgf_actor_t batch, wgf_alpha_mode_t mode, float cutoff);
WGF_API wgf_alpha_mode_t wgf_sprite_batch_get_alpha_mode(wgf_actor_t batch);
WGF_API float wgf_sprite_batch_get_alpha_cutoff(wgf_actor_t batch);

#ifdef __cplusplus
}
#endif

#endif
