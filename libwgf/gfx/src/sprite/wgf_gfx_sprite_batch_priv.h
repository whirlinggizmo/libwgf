#ifndef WGF_GFX_SPRITE_BATCH_PRIV_H
#define WGF_GFX_SPRITE_BATCH_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "sokol_gfx.h"
#include "wgf_material.h"
#include "wgf_vec3.h"

/* Instanced sprites, libwgt's wgt_gfx_sprite_batch.c (wgrender's wgr_sprite_batch.c):
 * each sprite is one small record, appended in the order it is added, and drawn as
 * instanced quads whose corners the GPU builds (wgf_gfx_sprite.glsl). Consecutive sprites
 * with the same texture, pipeline, and camera share one draw: a batch, which belongs to the
 * 3D stage draw that recorded it, replayed in its place. Without lit sprites (milestone
 * 2.5); 2D sprites are immediate mode's. */

/* One sprite, as the shader reads it (per-instance vertex data). */
typedef struct wgf_gfx_priv_sprite_quad_t {
    float position[3];
    float facing;     /* 0 camera, 1 upright about Y, 2+ its own axes */
    float size[2];    /* world width and height, scale included */
    float pivot[2];   /* 0..1 across the quad, y down */
    float uv[4];      /* u0, v0 (top-left), u1, v1 */
    float right[3];   /* facing 2+: the quad's axes */
    float up[3];
    float alpha;      /* > 0 mask cutoff, < 0 opaque, 0 as is (blended / added) */
    uint8_t color[4]; /* tint, sRGB */
} wgf_gfx_priv_sprite_quad_t;

/* A camera sprites are seen through this frame: its view-projection (OpenGL's clip
 * space), and the billboards' axes. The index to add sprites with; -1 when there is no
 * room. */
int wgf_gfx_priv_sprite_batch_add_camera(const float view_proj[16], wgf_vec3_t right, wgf_vec3_t up,
                                         wgf_vec3_t upright);

/* Record a 3D sprite seen through `camera`, drawn by its alpha mode (the quad's `alpha`
 * says how the shader treats alpha). extend: it may join the batch recorded last, as
 * nothing else was recorded since. The batch it went into; -1 when it was dropped.
 * Between begin_unordered and end_unordered it is held back instead, and -1 returned. */
int wgf_gfx_priv_sprite_batch_add_3d(const wgf_gfx_priv_sprite_quad_t *instance, sg_view view, sg_sampler sampler,
                                     wgf_alpha_mode_t mode, int camera, bool extend);

/* Sprites added between these don't need their order (a 3D stage's opaque, masked, and
 * added ones): end_unordered records them grouped by texture and mode, so 4 textures in
 * any order make 4 draws, and gives the batches they went into, `*count` of them from the
 * one it returns (none: -1). extend: the first may join the batch recorded last. */
void wgf_gfx_priv_sprite_batch_begin_unordered(void);
int wgf_gfx_priv_sprite_batch_end_unordered(bool extend, int *count);

/* Draw batch `batch`, inside the pass. follows: the last thing drawn was the batch before
 * it, so only what differs is applied again. */
void wgf_gfx_priv_sprite_batch_draw(int batch, bool follows);

/* For tests and spritebench: the batches the last ended frame drew, and the sprites in
 * them. */
int wgf_gfx_priv_sprite_batch_get_draw_calls(void);
int wgf_gfx_priv_sprite_batch_get_drawn(void);

#endif
