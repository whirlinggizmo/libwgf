#ifndef WGF_GFX_FONT_PRIV_H
#define WGF_GFX_FONT_PRIV_H

#include <stdbool.h>

#include "sokol_gfx.h"
#include "wgf_handle.h"
#include "wgf_color.h"
#include "wgf_text.h"

/* Fonts and text layout, for gfx's own drawing: fontstash, drawing through the
 * frame's sokol_gl recording. */

/* The size text takes when given one of 0 or less, in logical pixels: wgrender's. */
#define WGF_GFX_PRIV_FONT_DEFAULT_SIZE 16.0f

/* Text is a part (wgf_core_part_priv.h), installed by the first font created
 * or the first text measured or drawn: fontstash and the built-in font are made then,
 * once gfx runs; at the frame's end, before its pass, the glyphs added this frame go
 * into the atlas, and after it a full atlas grows; every font, and fontstash, are freed
 * at gfx's stop. */

/* One more reference to `font`, dropped with wgf_resource_release; nothing for 0
 * or a handle that isn't a font. */
void wgf_gfx_priv_font_retain(wgf_handle_t font);

/* The rectangle `text`'s block covers at `size` logical pixels, laid out as
 * wgf_gfx_priv_font_draw_block lays it out, in logical pixels about (0, 0): as wide
 * as its widest line, or its wrap width if that is more. False before setup, or for
 * no text. */
bool wgf_gfx_priv_font_block_bounds(wgf_handle_t font, const char *text, float size, float wrap_width,
                                   wgf_text_halign_t halign, wgf_text_valign_t valign, float *left, float *top,
                                   float *width, float *height);

/* Draw `text` as a block in `font` at `size` logical pixels, in the frame's
 * recording, through `matrix` (column by column, 16 floats: where the block's local
 * space goes; NULL for none), the block placed about (0, 0) by its alignment and
 * wrapped at `wrap_width` (0: not wrapped). Nothing before setup. */
void wgf_gfx_priv_font_draw_block(wgf_handle_t font, const char *text, float size, wgf_color_t color,
                                 float wrap_width, wgf_text_halign_t halign, wgf_text_valign_t valign,
                                 const float *matrix);

#endif
