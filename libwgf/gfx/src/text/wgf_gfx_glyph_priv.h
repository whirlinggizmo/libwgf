#ifndef WGF_GFX_GLYPH_PRIV_H
#define WGF_GFX_GLYPH_PRIV_H

/* Who draws a TrueType or OpenType font's glyphs. Natively, and in a headless web build,
 * stb_truetype, from the font's outlines, and a font is ready as its bytes are
 * (wgf_gfx_glyph_stb.c). In a page the browser (wgf_gfx_glyph_web.c): the font's bytes made a
 * FontFace, loaded in a later update, and each glyph drawn with fillText on a canvas, its alpha
 * the coverage; what places a glyph (the metrics, the advances, a TrueType glyph's box) is still
 * read from the font's tables by stb_truetype, so text is laid out as natively, and only the
 * pixels are the browser's (text/web/stb_truetype.h, where fontstash's calls are turned). helix's
 * page rasterizer. A face is named by its font's bytes, which fontstash keeps. */

#include <stdbool.h>

typedef enum wgf_gfx_priv_face_state_t {
    WGF_GFX_PRIV_FACE_PENDING = 0,
    WGF_GFX_PRIV_FACE_READY = 1,
    WGF_GFX_PRIV_FACE_FAILED = 2
} wgf_gfx_priv_face_state_t;

/* The face of the font in `bytes`, started when first asked for, on the main thread: READY once
 * its glyphs can be drawn (natively at once), FAILED for a font the browser refuses. A font is
 * measured and laid out without it, from its tables; a glyph asked for while its face is PENDING
 * is blank, and take_missed says when to make such glyphs again. */
wgf_gfx_priv_face_state_t wgf_gfx_priv_face_wait(const unsigned char *bytes, int size);
/* A face let go of, before its bytes are freed; nothing for bytes with none. */
void wgf_gfx_priv_face_close(const unsigned char *bytes);
/* Every face let go of (fontstash gone). */
void wgf_gfx_priv_face_close_all(void);
/* Whether a face is still PENDING. */
bool wgf_gfx_priv_face_any_pending(void);
/* True, once, when glyphs were made blank for a face that was PENDING and none is now: what
 * holds glyphs (fontstash's atlas) then forgets them, and they are drawn at their next use. */
bool wgf_gfx_priv_face_take_missed(void);

/* In a page only, for text/web/stb_truetype.h. The box of `codepoint` drawn `em` pixels high with
 * its pen at the origin, y down, out to whole pixels: left, top, right, bottom. */
void wgf_gfx_priv_glyph_measure(const unsigned char *bytes, int codepoint, float em, int box[4]);
/* `codepoint` drawn with its pen at (x, y), its coverage written to the `width` by `height`
 * pixels at `out`, rows `stride` apart; none (zeros) for a face that isn't READY. */
void wgf_gfx_priv_glyph_draw(const unsigned char *bytes, int codepoint, float em, int x, int y, unsigned char *out,
                             int width, int height, int stride);

#endif
