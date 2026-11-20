#ifndef WGF_GFX_FONT_BITMAP_PRIV_H
#define WGF_GFX_FONT_BITMAP_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "wgf_core_load_priv.h"

/* Bitmap fonts, read on any thread: an AngelCode BMFont's .fnt (its text format, or its
 * binary one, version 3) and an image of glyphs on a key color (raylib's LoadFontFromImage).
 * What they read is checked before it is kept, and a file that can't be read as it says
 * is refused, logged naming it. Drawing them is wgf_gfx_font.c's. */

/* The most pages a .fnt may name, and glyphs an image may hold (raylib's). */
#define WGF_GFX_PRIV_BITMAP_PAGES_MAX 16
#define WGF_GFX_PRIV_BITMAP_IMAGE_GLYPHS_MAX 256

/* A glyph: its rectangle on its page, in the page's pixels; where that goes from the pen
 * (x) and the line's top (y); and how far the pen moves after it. */
typedef struct wgf_gfx_priv_bitmap_glyph_t {
    uint32_t codepoint;
    int x, y, width, height;
    int x_offset, y_offset, advance;
    int page;
} wgf_gfx_priv_bitmap_glyph_t;

typedef struct wgf_gfx_priv_bitmap_kerning_t {
    uint32_t first, second;
    int amount;
} wgf_gfx_priv_bitmap_kerning_t;

/* A font's metrics and glyphs, in its own pixels: a line `line_height` high, its baseline
 * `base` below the line's top. The glyphs sorted by codepoint, the pairs by first then
 * second; the pages' file names as the .fnt wrote them. */
typedef struct wgf_gfx_priv_bitmap_t {
    int line_height, base;
    int page_count;
    char *pages[WGF_GFX_PRIV_BITMAP_PAGES_MAX];
    int page_width[WGF_GFX_PRIV_BITMAP_PAGES_MAX], page_height[WGF_GFX_PRIV_BITMAP_PAGES_MAX];
    wgf_gfx_priv_bitmap_glyph_t *glyphs;
    int glyph_count;
    wgf_gfx_priv_bitmap_kerning_t *kernings;
    int kerning_count;
    int fallback; /* the glyph drawn for a codepoint it lacks: '?' when it has one, else -1 */
} wgf_gfx_priv_bitmap_t;

/* A .fnt file's contents (`path` names it in the log): NULL, logged, when it isn't a
 * BMFont this reads -- its XML form, a channel-packed font, a page or number out of range,
 * a glyph twice, no glyphs. Freed with bitmap_free. */
wgf_gfx_priv_bitmap_t *wgf_gfx_priv_bitmap_parse_fnt(const unsigned char *data, int size, const char *path);

/* The glyphs of an image (RGBA, `width` by `height`) as raylib's LoadFontFromImage reads
 * them: rows of glyphs as high as the first, separated and surrounded by `key` (0xRRGGBBAA),
 * the first `first_codepoint`; the key's pixels made transparent in place. Its one page is
 * the image. NULL, logged, when there are none, or more than IMAGE_GLYPHS_MAX. */
wgf_gfx_priv_bitmap_t *wgf_gfx_priv_bitmap_from_image(unsigned char *rgba, int width, int height, uint32_t key,
                                                      uint32_t first_codepoint, const char *path);

/* The page sizes, once its pages are decoded: each glyph's rectangle clipped to its page
 * (a .fnt may place one past its page's edge, which reads as empty there). */
void wgf_gfx_priv_bitmap_set_page_size(wgf_gfx_priv_bitmap_t *font, int page, int width, int height);

/* The glyph to draw `codepoint` with: its own, else the fallback; NULL for neither. */
const wgf_gfx_priv_bitmap_glyph_t *wgf_gfx_priv_bitmap_glyph(const wgf_gfx_priv_bitmap_t *font, uint32_t codepoint);

/* The pen's adjustment between `first` and `second`, in the font's pixels: 0 for no pair. */
int wgf_gfx_priv_bitmap_kerning(const wgf_gfx_priv_bitmap_t *font, uint32_t first, uint32_t second);

void wgf_gfx_priv_bitmap_free(wgf_gfx_priv_bitmap_t *font);

/* The next codepoint of UTF-8 text at *at, before `end`, moving *at past it: an
 * ill-formed sequence is U+FFFD, one byte at a time. */
uint32_t wgf_gfx_priv_bitmap_next_codepoint(const char **at, const char *end);

/* The files a .fnt names, its pages, for the asset part (the lister, registered as the
 * file is linked). */
void wgf_gfx_priv_bitmap_list_pages(const unsigned char *data, int size, wgf_core_priv_load_add_fn add,
                                    void *context);

#endif
