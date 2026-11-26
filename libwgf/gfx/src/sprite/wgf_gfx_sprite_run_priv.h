#ifndef WGF_GFX_SPRITE_RUN_PRIV_H
#define WGF_GFX_SPRITE_RUN_PRIV_H

#include <stdint.h>

#include "sokol_gfx.h"
#include "wgf_mat4.h"

/* 2D sprites drawn in runs (wgf_gfx_sprite_run.c): a sprite on a 2D stage is a record
 * added to the run its stage is drawing, one instanced draw among the frame's immediate
 * mode (a command), not immediate mode's own quad. A run goes on while nothing else is
 * drawn between its sprites, they share an alpha mode, and it has room for their texture
 * (8 to a run), so a stage of sprites over a few textures is a draw or a few, in the order
 * drawn. */

/* A sprite's quad: `rect` x0, y0, x1, y1 in its own units, through `placed` (into the
 * frame's logical pixels) and the frame's 2D projection; `uv` u0, v0, u1, v1; its tint,
 * sRGB as RRGGBBAA; its alpha mode as immediate mode draws it (wgf_alpha_mode_t: MASK as
 * BLEND); its texture's view and sampler. Dropped out of memory. */
void wgf_gfx_priv_sprite_run_add(const wgf_mat4_t *placed, const float rect[4], const float uv[4], uint32_t tint,
                                 int mode, sg_view view, sg_sampler sampler);

/* For tests and the bunnymark: the runs drawn in the last ended frame, and the sprites in
 * them. */
int wgf_gfx_priv_sprite_run_get_draw_calls(void);
int wgf_gfx_priv_sprite_run_get_drawn(void);

#endif
