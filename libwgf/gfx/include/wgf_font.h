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

/* A font: a resource, a handle of kind "gfx.font". 0 is the default font. */
typedef wgf_handle_t wgf_font_t;

/* A font: a TrueType or OpenType typeface, or a bitmap font, loaded from a file.
 *
 * A font is a resource (wgf_resource.h): shared, reference counted, and loaded on create.
 * Sizes are chosen where text is drawn, not here.
 *
 * Font 0 means the default font, everywhere a font is taken. It starts as the built-in
 * font, JetBrains Mono with printable ASCII only, so text works with no font file at all.
 * A font not yet ready, or one that failed, draws as the default font. */

/* Creates a font from a file under fs's root.
 *
 * The file is one of these:
 * - a .ttf or .otf;
 * - an AngelCode BMFont .fnt, in its text format or its binary one (version 3), but not its
 *   XML. Its pages are the images it names beside it, and its kerning pairs are honored;
 * - an image of glyphs (.png, .jpg, .jpeg, .bmp, .tga, .gif). The glyphs are in rows as
 *   high as the first, each bordered by magenta (255, 0, 255) and as wide as its cell. They
 *   run from the space (32) on, at most 256. The magenta is made transparent.
 *
 * A bitmap font's glyphs are drawn from its pages:
 * - in their colors times the text's color, so white glyphs take the color, and colored
 *   ones keep theirs under white;
 * - at wgf_font_get_size, scaled to the size asked;
 * - through its filter (wgf_font_set_filter).
 *
 * A font loaded before, and released since, comes back READY at once.
 *
 * Returns 0 only if there is no room for another font. A font that can't be loaded FAILED.
 * A bitmap font also FAILED for a channel-packed .fnt, a page or number out of range, a
 * character given twice, or no glyphs. */
WGF_API wgf_font_t wgf_font_create(const char *path);

/* Enables bitmap fonts (a .fnt, or an image of glyphs: see wgf_font_create), or says whether
 * they are enabled.
 *
 * Bitmap fonts are linked on purpose. This call links the code that reads their files, so a
 * program with TrueType fonts alone carries none of it. A game enables them with
 * "bitmap_fonts" in wgf.json's features.
 *
 * Call it before the first bitmap font is created. Without it, a bitmap font FAILED (logged). */
WGF_API bool wgf_font_enable_bitmaps(void);
WGF_API bool wgf_font_is_bitmaps_enabled(void);

/* Returns a bitmap font's own size: the height of its line, in its pixels.
 *
 * - For an image of glyphs, it is the glyphs' height.
 * - It is the size the font's text is drawn at texel for texel, times the frame's DPI scale.
 * - A whole multiple of it stays sharp under NEAREST.
 * - Font 0 gives the default font's size.
 *
 * Returns 0 for a TrueType font, a font that isn't READY, or a handle that isn't a font. */
WGF_API float wgf_font_get_size(wgf_font_t font);

/* Sets or gets how a bitmap font's pages are sampled when its text is drawn at a size not
 * its own.
 *
 * - NEAREST (the default, when unset) keeps its texels sharp, for pixel fonts.
 * - LINEAR smooths them.
 *
 * A filter set before the font is READY is kept.
 *
 * The setter returns false for a font that isn't a bitmap font (by its file's extension),
 * font 0, or a filter that isn't one. The getter returns LINEAR for those. */
WGF_API bool wgf_font_set_filter(wgf_font_t font, wgf_texture_filter_t filter);
WGF_API wgf_texture_filter_t wgf_font_get_filter(wgf_font_t font);

/* Sets or gets the font that font 0 means.
 *
 * The default holds a reference to `font`. Setting 0 goes back to the built-in font. For
 * text beyond ASCII, set a font that has it.
 *
 * The setter returns false for a handle that isn't a font. */
WGF_API bool wgf_font_set_default(wgf_font_t font);
WGF_API wgf_font_t wgf_font_get_default(void);

/* Returns how much room UTF-8 `text` takes at `size` logical pixels: the widest line, and
 * the lines' height together.
 *
 * - A `size` of 0 or less uses 16. A bitmap font's line is `size` high.
 * - The text is measured at the frame's pixel density, as it is drawn.
 * - A bitmap font is measured with its advances, kerning, and letter spacing, as its glyphs
 *   are placed.
 * - Newlines break lines, and nothing wraps.
 * - A line keeps its spaces, so " " measures a space's advance.
 *
 * Returns 0, 0 for empty text, or before gfx is set up. */
WGF_API wgf_vec2_t wgf_font_measure(wgf_font_t font, const char *text, float size);

#ifdef __cplusplus
}
#endif

#endif
