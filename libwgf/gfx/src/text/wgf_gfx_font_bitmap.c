#include "text/wgf_gfx_font_bitmap_priv.h"

#include <stdlib.h>

/* Bitmap fonts, as drawn and measured (wgf_gfx_font_bitmap_priv.h): a glyph and a kerning
 * looked up, the text's codepoints, a font let go of. What reads a font's file is a file of
 * its own (wgf_gfx_font_bitmap_read.c), linked only by a program that enables bitmap fonts. */

/* ---- using one ------------------------------------------------------------------------ */

void wgf_gfx_priv_bitmap_set_page_size(wgf_gfx_priv_bitmap_t *font, int page, int width, int height)
{
    int i;
    if (font == NULL || page < 0 || page >= font->page_count) return;
    font->page_width[page] = width;
    font->page_height[page] = height;
    for (i = 0; i < font->glyph_count; i++) {
        wgf_gfx_priv_bitmap_glyph_t *g = &font->glyphs[i];
        if (g->page != page) continue;
        if (g->x >= width || g->y >= height) {
            g->width = 0;
            g->height = 0;
            continue;
        }
        if (g->width > width - g->x) g->width = width - g->x;
        if (g->height > height - g->y) g->height = height - g->y;
    }
}

const wgf_gfx_priv_bitmap_glyph_t *wgf_gfx_priv_bitmap_glyph(const wgf_gfx_priv_bitmap_t *font, uint32_t codepoint)
{
    int low = 0, high;
    if (font == NULL) return NULL;
    high = font->glyph_count - 1;
    while (low <= high) {
        const int mid = low + (high - low) / 2;
        const uint32_t at = font->glyphs[mid].codepoint;
        if (at == codepoint) return &font->glyphs[mid];
        if (at < codepoint) low = mid + 1;
        else high = mid - 1;
    }
    return font->fallback >= 0 ? &font->glyphs[font->fallback] : NULL;
}

int wgf_gfx_priv_bitmap_kerning(const wgf_gfx_priv_bitmap_t *font, uint32_t first, uint32_t second)
{
    int low = 0, high;
    if (font == NULL) return 0;
    high = font->kerning_count - 1;
    while (low <= high) {
        const int mid = low + (high - low) / 2;
        const wgf_gfx_priv_bitmap_kerning_t *k = &font->kernings[mid];
        if (k->first == first && k->second == second) return k->amount;
        if (k->first < first || (k->first == first && k->second < second)) low = mid + 1;
        else high = mid - 1;
    }
    return 0;
}

void wgf_gfx_priv_bitmap_free(wgf_gfx_priv_bitmap_t *font)
{
    int i;
    if (font == NULL) return;
    for (i = 0; i < WGF_GFX_PRIV_BITMAP_PAGES_MAX; i++) free(font->pages[i]);
    free(font->glyphs);
    free(font->kernings);
    free(font);
}

uint32_t wgf_gfx_priv_bitmap_next_codepoint(const char **at, const char *end)
{
    const unsigned char *p = (const unsigned char *)*at;
    const unsigned char first = p[0];
    int length, i;
    uint32_t codepoint;
    if (first < 0x80) {
        *at += 1;
        return first;
    }
    if (first >= 0xC2 && first <= 0xDF) {
        length = 2;
        codepoint = first & 0x1Fu;
    } else if (first >= 0xE0 && first <= 0xEF) {
        length = 3;
        codepoint = first & 0x0Fu;
    } else if (first >= 0xF0 && first <= 0xF4) {
        length = 4;
        codepoint = first & 0x07u;
    } else {
        *at += 1;
        return 0xFFFDu;
    }
    if (end - *at < length) {
        *at += 1;
        return 0xFFFDu;
    }
    for (i = 1; i < length; i++) {
        if ((p[i] & 0xC0u) != 0x80u) {
            *at += 1;
            return 0xFFFDu;
        }
        codepoint = (codepoint << 6) | (p[i] & 0x3Fu);
    }
    /* overlong forms, surrogates, and past U+10FFFF */
    if ((length == 3 && codepoint < 0x800u) || (length == 4 && (codepoint < 0x10000u || codepoint > 0x10FFFFu)) ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
        *at += 1;
        return 0xFFFDu;
    }
    *at += length;
    return codepoint;
}
