/* What fontstash includes as stb_truetype.h in a page (this folder is ahead of deps/stb there,
 * and nowhere else): stb_truetype as it comes, then fontstash's three calls that draw a glyph
 * turned to the browser (text/wgf_gfx_glyph_priv.h), so the vendored files stay as they are and
 * stb_truetype's outline code (its rasterizer, its CFF charstrings) is linked into no page. Its
 * table reading stays: metrics, advances, kerning, which glyph a codepoint is, and a TrueType
 * glyph's box, so a glyph is placed and sized as natively. */
#ifndef WGF_GFX_STB_TRUETYPE_WEB_H
#define WGF_GFX_STB_TRUETYPE_WEB_H

#include_next "stb_truetype.h"

#include "text/wgf_gfx_glyph_priv.h"

/* fontstash asks which glyph a codepoint is just before it boxes and draws it, by its glyph;
 * the browser draws text, so the codepoint asked for last is kept for the two calls after. */
static const stbtt_fontinfo *wgf_glyph_asked_font;
static int wgf_glyph_asked_codepoint, wgf_glyph_asked_glyph;

static int wgf_glyph_index(const stbtt_fontinfo *info, int codepoint)
{
    const int glyph = (stbtt_FindGlyphIndex)(info, codepoint); /* in parentheses: stb_truetype's own */
    wgf_glyph_asked_font = info;
    wgf_glyph_asked_codepoint = codepoint;
    wgf_glyph_asked_glyph = glyph;
    return glyph;
}

static float wgf_glyph_em(const stbtt_fontinfo *info, float scale)
{
    return scale * (float)ttUSHORT(info->data + info->head + 18); /* unitsPerEm */
}

/* stb_truetype's box for a TrueType glyph (its outline's, from the glyf table, scaled, out to
 * whole pixels, y down); for one with CFF outlines, what the canvas measures. Empty for a glyph
 * the font hasn't (stb_truetype draws its .notdef, a page nothing) or one with no outline. */
static void wgf_glyph_box(const stbtt_fontinfo *info, int glyph, float scale_x, float scale_y, int *x0, int *y0,
                          int *x1, int *y1)
{
    *x0 = *y0 = *x1 = *y1 = 0;
    if (glyph == 0 || info != wgf_glyph_asked_font || glyph != wgf_glyph_asked_glyph) return;
    if (info->cff.size == 0) {
        const int g = stbtt__GetGlyfOffset(info, glyph);
        if (g < 0) return;
        *x0 = STBTT_ifloor((float)ttSHORT(info->data + g + 2) * scale_x);
        *y0 = STBTT_ifloor((float)-ttSHORT(info->data + g + 8) * scale_y);
        *x1 = STBTT_iceil((float)ttSHORT(info->data + g + 6) * scale_x);
        *y1 = STBTT_iceil((float)-ttSHORT(info->data + g + 4) * scale_y);
    } else {
        int box[4];
        wgf_gfx_priv_glyph_measure(info->data, wgf_glyph_asked_codepoint, wgf_glyph_em(info, scale_y), box);
        *x0 = box[0];
        *y0 = box[1];
        *x1 = box[2];
        *y1 = box[3];
    }
}

static void wgf_glyph_draw(const stbtt_fontinfo *info, unsigned char *out, int width, int height, int stride,
                           float scale_x, float scale_y, int glyph)
{
    int x0, y0, x1, y1;
    wgf_glyph_box(info, glyph, scale_x, scale_y, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0) return;
    wgf_gfx_priv_glyph_draw(info->data, wgf_glyph_asked_codepoint, wgf_glyph_em(info, scale_y), -x0, -y0, out, width,
                            height, stride);
}

#define stbtt_FindGlyphIndex(info, codepoint) wgf_glyph_index(info, codepoint)
#define stbtt_GetGlyphBitmapBox(info, glyph, scale_x, scale_y, x0, y0, x1, y1) \
    wgf_glyph_box(info, glyph, scale_x, scale_y, x0, y0, x1, y1)
#define stbtt_MakeGlyphBitmap(info, out, width, height, stride, scale_x, scale_y, glyph) \
    wgf_glyph_draw(info, out, width, height, stride, scale_x, scale_y, glyph)

#endif
