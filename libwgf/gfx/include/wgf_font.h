#ifndef WGF_FONT_H
#define WGF_FONT_H

#include <stdbool.h>

#include "wgf_handle.h"
#include "wgf_api.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A font, a resource: a handle of kind "gfx.font". 0 is the default font. */
typedef wgf_handle_t wgf_font_t;

/* A font: a TrueType or OpenType typeface, or a bitmap font, loaded from a file. It is a
 * resource (wgf_resource.h): shared, reference counted, and loaded on create. Sizes are
 * chosen where text is drawn, not here.
 *
 * Font 0 means the default font, everywhere a font is taken. It starts as the
 * built-in font -- JetBrains Mono, printable ASCII only -- so text works with no
 * font file at all. A font not yet ready, or failed, draws as the default font. */

/* A font from a file under fs's root: a .ttf or .otf; or a bitmap font, raylib's two:
 * an AngelCode BMFont .fnt (its text format, or its binary one, version 3; not its XML),
 * its pages the images it names beside it, its kerning pairs honored; or an image of
 * glyphs (.png, .jpg, .jpeg, .bmp, .tga, .gif), raylib's LoadFont of one: rows of glyphs
 * as high as the first, each bordered by magenta (255, 0, 255), from the space (32) on, at
 * most 256, each as wide as its cell, magenta made transparent. A bitmap font's glyphs
 * are drawn from its pages in their colors times the text's color (white glyphs take the
 * color; colored ones keep theirs under white), at wgf_font_get_size scaled to the size
 * asked, through its filter (wgf_font_set_filter). A channel-packed .fnt, a page or
 * number out of range, a character twice, or no glyphs FAILED. 0 only when there is no
 * room for another font; one that can't be loaded FAILED. A font loaded before, and
 * released since, comes back READY at once. */
WGF_API wgf_font_t wgf_font_create(const char *path);

/* A bitmap font's own size: the height of its line in its pixels (an image's: its
 * glyphs' height), the size its text is drawn texel for texel at (times the frame's DPI
 * scale); a whole multiple of it stays sharp under NEAREST. 0 for a TrueType font, one
 * not READY, or a handle that isn't a font; font 0 is the default's. */
WGF_API float wgf_font_get_size(wgf_font_t font);

/* How a bitmap font's pages are sampled when its text is drawn at a size not its own:
 * NEAREST (unset), its texels kept sharp, for pixel fonts; LINEAR, smoothed. Kept from
 * before it is READY. False for a font that isn't a bitmap font (by its file's
 * extension), font 0, or a filter that isn't one. The getter: LINEAR for those. */
WGF_API bool wgf_font_set_filter(wgf_font_t font, wgf_texture_filter_t filter);
WGF_API wgf_texture_filter_t wgf_font_get_filter(wgf_font_t font);

/* The font that font 0 means. It holds a reference to `font`; 0 goes back to the
 * built-in font. For text beyond ASCII, set a font that has it. False for a handle
 * that isn't a font. */
WGF_API bool wgf_font_set_default(wgf_font_t font);
WGF_API wgf_font_t wgf_font_get_default(void);

/* How much room `text` (UTF-8) takes at `size` logical pixels (16 for 0 or less; a
 * bitmap font's line is `size` high), measured at the frame's pixel density as it is
 * drawn: the widest line, and the lines' height together; a bitmap font's advances,
 * kerning, and letter spacing as its glyphs are placed. Newlines break lines; nothing wraps. A line keeps its
 * spaces, so " " measures a space's advance. 0, 0 for empty text, or before gfx is
 * set up. */
WGF_API wgf_vec2_t wgf_font_measure(wgf_font_t font, const char *text, float size);

#ifdef __cplusplus
}
#endif

#endif
