#ifndef WGF_GFX_SPRITE_BATCH2D_PRIV_H
#define WGF_GFX_SPRITE_BATCH2D_PRIV_H

/* For tests and the bunnymark: the sprite batches' draws in the last ended frame, and the
 * quads in them (wgf_gfx_sprite_batch2d.c). */
int wgf_gfx_priv_sprite_batch2d_get_draw_calls(void);
int wgf_gfx_priv_sprite_batch2d_get_drawn(void);

#endif
