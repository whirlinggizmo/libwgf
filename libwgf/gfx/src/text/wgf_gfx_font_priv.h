#ifndef WGF_GFX_FONT_PRIV_H
#define WGF_GFX_FONT_PRIV_H

#include <stdbool.h>

#include "sokol_gfx.h"
#include "util/sokol_gl.h"
#include "wgf_handle.h"
#include "wgf_color.h"
#include "wgf_text.h"
#include "wgf_texture.h"

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

/* The same in 3D (wgf_draw_text_3d): rasterized at WGF_GFX_PRIV_FONT_RASTER_3D pixels
 * and scaled to `size` world units a line, its glyphs' quads through the frame's 3D
 * projection and `matrix`, depth tested, not written. Nothing before setup. */
#define WGF_GFX_PRIV_FONT_RASTER_3D 64.0f
/* Its block's bounds as draw_block_3d lays it out, in world units (`size` a line). */
bool wgf_gfx_priv_font_block_bounds_3d(wgf_handle_t font, const char *text, float size, float wrap_width,
                                      wgf_text_halign_t halign, wgf_text_valign_t valign, float *left, float *top,
                                      float *width, float *height);
/* The spacing the layouts after it use: a text actor's own (`own`, `letter` and `line` logical
 * pixels) around its layout and drawing, or immediate mode's again (own false). */
void wgf_gfx_priv_font_set_spacing(bool own, float letter, float line);

void wgf_gfx_priv_font_draw_block_3d(wgf_handle_t font, const char *text, float size, wgf_color_t color,
                                    float wrap_width, wgf_text_halign_t halign, wgf_text_valign_t valign,
                                    const float *matrix);

/* What sokol_fontstash draws with (wgf_gfx_fontstash_impl.c): its atlas, sampler, and
 * shader; false before it has them. */
struct FONScontext;
bool wgf_gfx_priv_fontstash_render_state(struct FONScontext *context, sg_view *atlas, sg_sampler *sampler,
                                         sg_shader *shader);
/* sokol_fontstash's 2D pipeline (blended; the atlas's coverage as alpha); false before it has one. */
bool wgf_gfx_priv_fontstash_pipeline(struct FONScontext *context, sgl_pipeline *pipeline);
/* The GPU's context given back: the atlas sent again from fontstash's copy at the next flush. */
void wgf_gfx_priv_fontstash_restore(struct FONScontext *context);

/* Glyphs for meshes of a caller's own (the ui module's documents), in logical pixels,
 * rasterized at the frame's pixel density as the text drawn here is. `font` 0 is the
 * default font, and a font not READY draws as the default (then the built-in) does.
 * vertical_metrics: `size`'s ascender and descender (both positive, above and below the
 * baseline) and line height; false before setup. span_width: the advance of
 * [start, end), one line. quads: [start, end)'s glyphs from `x` along the baseline at
 * `baseline`, those on `page` (-1: every page), up to `max_quads` of them into `out`, 8 floats each (x0, y0,
 * x1, y1, then s0, t0, s1, t1 on the page, 0 to 1), how many: a glyph rasterized here
 * reaches the atlas's texture at the frame's flush. page_count: how many pages the font's
 * glyphs are drawn from as it is drawn now, 1 for fontstash's (its atlas, page 0); page: a
 * bitmap font's page's texture, 0 for the atlas. */
bool wgf_gfx_priv_font_vertical_metrics(wgf_handle_t font, float size, float *ascender, float *descender,
                                        float *line_height);
float wgf_gfx_priv_font_span_width(wgf_handle_t font, const char *start, const char *end, float size);
int wgf_gfx_priv_font_quads(wgf_handle_t font, const char *start, const char *end, float size, float x,
                            float baseline, int page, float *out, int max_quads);
int wgf_gfx_priv_font_page_count(wgf_handle_t font);
wgf_texture_t wgf_gfx_priv_font_page(wgf_handle_t font, int page);
/* What to draw those quads with: the atlas, its sampler, and sokol_fontstash's 2D
 * pipeline; false before setup. The atlas's view changes as it grows, so ask for every
 * draw. And its version: changed whenever glyphs move in it (it grew, or fontstash was
 * made again), when quads made before are wrong. */
bool wgf_gfx_priv_font_atlas(sg_view *atlas, sg_sampler *sampler, sgl_pipeline *pipeline);
/* sokol_fontstash's shader (the atlas's coverage as alpha), for pipelines of another's
 * own (the documents' blending and clip masks); it changes when fontstash is made again.
 * False before setup. */
bool wgf_gfx_priv_font_shader(sg_shader *shader);
unsigned wgf_gfx_priv_font_atlas_version(void);
/* How many times a font has loaded again in place (wgf_asset_reload): text measured or
 * broken into lines before is wrong for that font, which keeps its handle. */
unsigned wgf_gfx_priv_font_reloads(void);

#endif
