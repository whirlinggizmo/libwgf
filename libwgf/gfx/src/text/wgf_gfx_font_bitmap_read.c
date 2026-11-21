#include "text/wgf_gfx_font_bitmap_priv.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_core_load_priv.h"
#include "wgf_font.h"
#include "wgf_log.h"

/* Bitmap fonts' files, read on a worker (wgf_gfx_font_bitmap_priv.h), a file of its own so a
 * program that never enables bitmap fonts links none of it. BMFont's formats are
 * AngelCode's (angelcode.com/products/bmfont/doc/file_format.html); the image of glyphs is
 * raylib's LoadFontFromImage, as helix-raylib reads it: a pixel past the image is the key. */

/* A reading's warning, unless it is quiet (the lister's: the loader says why). */
#define WARN(r, ...)                                                                                                   \
    do {                                                                                                               \
        if (!(r)->quiet) wgf_log_warn(__VA_ARGS__);                                                                    \
    } while (0)

#define NUMBER_MOST 65535
#define OFFSET_MOST 4096
#define GLYPHS_MOST (1 << 20)
#define PAGE_NAME_MAX 512

/* ---- what a file gives, gathered ------------------------------------------------------ */

typedef struct reading_t {
    const char *path;
    wgf_gfx_priv_bitmap_t *font;
    int glyph_capacity, kerning_capacity;
    bool has_common;
    bool quiet;
} reading_t;

static bool add_glyph(reading_t *r, const wgf_gfx_priv_bitmap_glyph_t *glyph)
{
    wgf_gfx_priv_bitmap_t *font = r->font;
    if (font->glyph_count == GLYPHS_MOST) return false;
    if (font->glyph_count == r->glyph_capacity) {
        const int capacity = r->glyph_capacity > 0 ? r->glyph_capacity * 2 : 128;
        wgf_gfx_priv_bitmap_glyph_t *grown =
            (wgf_gfx_priv_bitmap_glyph_t *)realloc(font->glyphs, sizeof(*grown) * (size_t)capacity);
        if (grown == NULL) return false;
        font->glyphs = grown;
        r->glyph_capacity = capacity;
    }
    font->glyphs[font->glyph_count++] = *glyph;
    return true;
}

static bool add_kerning(reading_t *r, uint32_t first, uint32_t second, int amount)
{
    wgf_gfx_priv_bitmap_t *font = r->font;
    if (font->kerning_count == GLYPHS_MOST) return false;
    if (font->kerning_count == r->kerning_capacity) {
        const int capacity = r->kerning_capacity > 0 ? r->kerning_capacity * 2 : 64;
        wgf_gfx_priv_bitmap_kerning_t *grown =
            (wgf_gfx_priv_bitmap_kerning_t *)realloc(font->kernings, sizeof(*grown) * (size_t)capacity);
        if (grown == NULL) return false;
        font->kernings = grown;
        r->kerning_capacity = capacity;
    }
    font->kernings[font->kerning_count].first = first;
    font->kernings[font->kerning_count].second = second;
    font->kernings[font->kerning_count].amount = amount;
    font->kerning_count++;
    return true;
}

static int compare_glyphs(const void *a, const void *b)
{
    const uint32_t x = ((const wgf_gfx_priv_bitmap_glyph_t *)a)->codepoint;
    const uint32_t y = ((const wgf_gfx_priv_bitmap_glyph_t *)b)->codepoint;
    return x < y ? -1 : x > y ? 1 : 0;
}

static int compare_kernings(const void *a, const void *b)
{
    const wgf_gfx_priv_bitmap_kerning_t *x = (const wgf_gfx_priv_bitmap_kerning_t *)a;
    const wgf_gfx_priv_bitmap_kerning_t *y = (const wgf_gfx_priv_bitmap_kerning_t *)b;
    if (x->first != y->first) return x->first < y->first ? -1 : 1;
    return x->second < y->second ? -1 : x->second > y->second ? 1 : 0;
}

/* Sorted, checked whole: every page named, a glyph and a pair once each, a glyph at all. */
static bool finish_reading(reading_t *r)
{
    wgf_gfx_priv_bitmap_t *font = r->font;
    int i;
    if (!r->has_common) {
        WARN(r, "wgf_gfx_font: %s: no common line (its line height): not a BMFont", r->path);
        return false;
    }
    for (i = 0; i < font->page_count; i++) {
        if (font->pages[i] == NULL) {
            WARN(r, "wgf_gfx_font: %s: page %d isn't named", r->path, i);
            return false;
        }
    }
    if (font->glyph_count == 0) {
        WARN(r, "wgf_gfx_font: %s: no glyphs", r->path);
        return false;
    }
    qsort(font->glyphs, (size_t)font->glyph_count, sizeof(font->glyphs[0]), compare_glyphs);
    for (i = 1; i < font->glyph_count; i++) {
        if (font->glyphs[i].codepoint == font->glyphs[i - 1].codepoint) {
            WARN(r, "wgf_gfx_font: %s: character %u given twice", r->path, (unsigned)font->glyphs[i].codepoint);
            return false;
        }
    }
    if (font->kerning_count > 0) {
        qsort(font->kernings, (size_t)font->kerning_count, sizeof(font->kernings[0]), compare_kernings);
        for (i = 1; i < font->kerning_count; i++) {
            if (font->kernings[i].first == font->kernings[i - 1].first &&
                font->kernings[i].second == font->kernings[i - 1].second) {
                WARN(r, "wgf_gfx_font: %s: kerning pair %u, %u given twice", r->path,
                             (unsigned)font->kernings[i].first, (unsigned)font->kernings[i].second);
                return false;
            }
        }
    }
    font->fallback = -1;
    {
        const wgf_gfx_priv_bitmap_glyph_t *question = wgf_gfx_priv_bitmap_glyph(font, '?');
        if (question != NULL) font->fallback = (int)(question - font->glyphs);
    }
    return true;
}

static bool check_range(const reading_t *r, const char *what, long value, long low, long high)
{
    if (value >= low && value <= high) return true;
    WARN(r, "wgf_gfx_font: %s: %s %ld is out of range (%ld to %ld)", r->path, what, value, low, high);
    return false;
}

/* The common block's: a line's height and baseline, the pages, not packed into channels. */
static bool set_common(reading_t *r, long line_height, long base, long pages, bool packed)
{
    if (r->has_common) {
        WARN(r, "wgf_gfx_font: %s: its common line twice", r->path);
        return false;
    }
    if (packed) {
        WARN(r, "wgf_gfx_font: %s: glyphs packed into color channels aren't read", r->path);
        return false;
    }
    if (!check_range(r, "lineHeight", line_height, 1, OFFSET_MOST) || !check_range(r, "base", base, 0, OFFSET_MOST) ||
        !check_range(r, "pages", pages, 1, WGF_GFX_PRIV_BITMAP_PAGES_MAX))
        return false;
    r->font->line_height = (int)line_height;
    r->font->base = (int)base;
    r->font->page_count = (int)pages;
    r->has_common = true;
    return true;
}

static bool set_page(reading_t *r, long id, const char *name, size_t length)
{
    char *copy;
    if (!r->has_common) {
        WARN(r, "wgf_gfx_font: %s: a page before its common line", r->path);
        return false;
    }
    if (!check_range(r, "page id", id, 0, r->font->page_count - 1)) return false;
    if (length == 0 || length >= PAGE_NAME_MAX || memchr(name, '\0', length) != NULL) {
        WARN(r, "wgf_gfx_font: %s: page %ld's file name is empty or too long", r->path, id);
        return false;
    }
    if (r->font->pages[id] != NULL) {
        WARN(r, "wgf_gfx_font: %s: page %ld named twice", r->path, id);
        return false;
    }
    copy = (char *)malloc(length + 1);
    if (copy == NULL) return false;
    memcpy(copy, name, length);
    copy[length] = '\0';
    r->font->pages[id] = copy;
    return true;
}

static bool set_glyph(reading_t *r, long id, long x, long y, long width, long height, long x_offset, long y_offset,
                      long advance, long page)
{
    wgf_gfx_priv_bitmap_glyph_t glyph;
    if (!r->has_common) {
        WARN(r, "wgf_gfx_font: %s: a character before its common line", r->path);
        return false;
    }
    if (!check_range(r, "char id", id, 0, 0x10FFFF) || !check_range(r, "x", x, 0, NUMBER_MOST) ||
        !check_range(r, "y", y, 0, NUMBER_MOST) || !check_range(r, "width", width, 0, OFFSET_MOST) ||
        !check_range(r, "height", height, 0, OFFSET_MOST) ||
        !check_range(r, "xoffset", x_offset, -OFFSET_MOST, OFFSET_MOST) ||
        !check_range(r, "yoffset", y_offset, -OFFSET_MOST, OFFSET_MOST) ||
        !check_range(r, "xadvance", advance, -OFFSET_MOST, OFFSET_MOST) ||
        !check_range(r, "page", page, 0, r->font->page_count - 1))
        return false;
    glyph.codepoint = (uint32_t)id;
    glyph.x = (int)x;
    glyph.y = (int)y;
    glyph.width = (int)width;
    glyph.height = (int)height;
    glyph.x_offset = (int)x_offset;
    glyph.y_offset = (int)y_offset;
    glyph.advance = (int)advance;
    glyph.page = (int)page;
    if (!add_glyph(r, &glyph)) {
        WARN(r, "wgf_gfx_font: %s: too many characters", r->path);
        return false;
    }
    return true;
}

static bool set_kerning(reading_t *r, long first, long second, long amount)
{
    if (!check_range(r, "kerning first", first, 0, 0x10FFFF) || !check_range(r, "kerning second", second, 0, 0x10FFFF) ||
        !check_range(r, "kerning amount", amount, -OFFSET_MOST, OFFSET_MOST))
        return false;
    if (!add_kerning(r, (uint32_t)first, (uint32_t)second, (int)amount)) {
        WARN(r, "wgf_gfx_font: %s: too many kerning pairs", r->path);
        return false;
    }
    return true;
}

/* ---- the text format ------------------------------------------------------------------ */

/* A line's tag and its key=value pairs, a value quoted or not; up to KEYS_MOST of them. */
#define KEYS_MOST 24
typedef struct span_t {
    const char *at;
    size_t length;
} span_t;

typedef struct line_t {
    span_t tag;
    span_t keys[KEYS_MOST], values[KEYS_MOST];
    int count;
} line_t;

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static bool split_line(const char *at, const char *end, line_t *line)
{
    memset(line, 0, sizeof(*line));
    while (at < end && is_space(*at)) at++;
    line->tag.at = at;
    while (at < end && !is_space(*at)) at++;
    line->tag.length = (size_t)(at - line->tag.at);
    for (;;) {
        span_t key, value;
        while (at < end && is_space(*at)) at++;
        if (at >= end) return true;
        key.at = at;
        while (at < end && *at != '=' && !is_space(*at)) at++;
        key.length = (size_t)(at - key.at);
        if (at >= end || *at != '=') { /* a word without a value: kept with an empty one */
            value.at = at;
            value.length = 0;
        } else if (++at < end && *at == '"') {
            value.at = ++at;
            while (at < end && *at != '"') at++;
            if (at >= end) return false; /* a quote never closed */
            value.length = (size_t)(at - value.at);
            at++;
        } else {
            value.at = at;
            while (at < end && !is_space(*at)) at++;
            value.length = (size_t)(at - value.at);
        }
        if (line->count == KEYS_MOST) return false;
        line->keys[line->count] = key;
        line->values[line->count] = value;
        line->count++;
    }
}

static bool span_is(span_t span, const char *word)
{
    return span.length == strlen(word) && memcmp(span.at, word, span.length) == 0;
}

static const span_t *value_of(const line_t *line, const char *key)
{
    int i;
    for (i = 0; i < line->count; i++) {
        if (span_is(line->keys[i], key)) return &line->values[i];
    }
    return NULL;
}

/* A key's whole number, *out; `fallback` when it isn't there. False, logged, for one that
 * isn't a number. */
static bool number_of(const reading_t *r, const line_t *line, const char *key, long fallback, long *out)
{
    const span_t *value = value_of(line, key);
    long n = 0;
    size_t i = 0;
    bool negative = false;
    if (value == NULL) {
        *out = fallback;
        return true;
    }
    if (i < value->length && (value->at[i] == '-' || value->at[i] == '+')) negative = value->at[i++] == '-';
    if (i == value->length) {
        WARN(r, "wgf_gfx_font: %s: %s has no number", r->path, key);
        return false;
    }
    for (; i < value->length; i++) {
        if (value->at[i] < '0' || value->at[i] > '9' || n > 100000000L) {
            WARN(r, "wgf_gfx_font: %s: %s=%.*s isn't a number it takes", r->path, key, (int)value->length,
                         value->at);
            return false;
        }
        n = n * 10 + (value->at[i] - '0');
    }
    *out = negative ? -n : n;
    return true;
}

static bool read_line(reading_t *r, const line_t *line)
{
    long a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0, i = 0;
    if (span_is(line->tag, "common")) {
        return number_of(r, line, "lineHeight", 0, &a) && number_of(r, line, "base", 0, &b) &&
               number_of(r, line, "pages", 1, &c) && number_of(r, line, "packed", 0, &d) &&
               set_common(r, a, b, c, d != 0);
    }
    if (span_is(line->tag, "page")) {
        const span_t *file = value_of(line, "file");
        if (file == NULL) {
            WARN(r, "wgf_gfx_font: %s: a page without a file", r->path);
            return false;
        }
        return number_of(r, line, "id", -1, &a) && set_page(r, a, file->at, file->length);
    }
    if (span_is(line->tag, "char")) {
        return number_of(r, line, "id", -1, &a) && number_of(r, line, "x", 0, &b) && number_of(r, line, "y", 0, &c) &&
               number_of(r, line, "width", 0, &d) && number_of(r, line, "height", 0, &e) &&
               number_of(r, line, "xoffset", 0, &f) && number_of(r, line, "yoffset", 0, &g) &&
               number_of(r, line, "xadvance", 0, &h) && number_of(r, line, "page", 0, &i) &&
               set_glyph(r, a, b, c, d, e, f, g, h, i);
    }
    if (span_is(line->tag, "kerning")) {
        return number_of(r, line, "first", -1, &a) && number_of(r, line, "second", -1, &b) &&
               number_of(r, line, "amount", 0, &c) && set_kerning(r, a, b, c);
    }
    return true; /* info, chars, kernings, and what a later version adds: nothing this needs */
}

static bool read_text(reading_t *r, const char *at, const char *end)
{
    int number = 0;
    while (at < end) {
        const char *newline = (const char *)memchr(at, '\n', (size_t)(end - at));
        const char *line_end = newline != NULL ? newline : end;
        line_t line;
        number++;
        if (!split_line(at, line_end, &line)) {
            WARN(r, "wgf_gfx_font: %s: line %d can't be read", r->path, number);
            return false;
        }
        if (line.tag.length > 0 && !read_line(r, &line)) return false;
        at = line_end + (newline != NULL ? 1 : 0);
    }
    return finish_reading(r);
}

/* ---- the binary format (version 3) ---------------------------------------------------- */

static unsigned u16_at(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static long s16_at(const unsigned char *p)
{
    const unsigned u = u16_at(p);
    return u >= 0x8000u ? (long)u - 0x10000L : (long)u;
}

static uint32_t u32_at(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool read_binary(reading_t *r, const unsigned char *data, int size)
{
    int at = 4;
    if (data[3] != 3) {
        WARN(r, "wgf_gfx_font: %s: binary BMFont version %d; this reads version 3", r->path, data[3]);
        return false;
    }
    while (at < size) {
        int type, length, i;
        const unsigned char *block;
        if (size - at < 5) break;
        type = data[at];
        length = (int)(u32_at(data + at + 1) & 0x7FFFFFFFu);
        if (u32_at(data + at + 1) > 0x7FFFFFFFu || length > size - at - 5) {
            WARN(r, "wgf_gfx_font: %s: block %d runs past the file's end", r->path, type);
            return false;
        }
        block = data + at + 5;
        switch (type) {
            case 2: /* common */
                if (length < 15) break;
                if (!set_common(r, (long)u16_at(block), (long)u16_at(block + 2), (long)u16_at(block + 8),
                                (block[10] & 0x80u) != 0))
                    return false;
                break;
            case 3: { /* pages: names of one length, each ended by a 0 */
                int page = 0, start = 0;
                for (i = 0; i < length; i++) {
                    if (block[i] != 0) continue;
                    if (!set_page(r, page++, (const char *)block + start, (size_t)(i - start))) return false;
                    start = i + 1;
                }
                if (start != length) {
                    WARN(r, "wgf_gfx_font: %s: a page's name isn't ended", r->path);
                    return false;
                }
                break;
            }
            case 4: /* chars, 20 bytes each */
                if (length % 20 != 0) {
                    WARN(r, "wgf_gfx_font: %s: its characters' block isn't whole", r->path);
                    return false;
                }
                for (i = 0; i < length; i += 20) {
                    const unsigned char *c = block + i;
                    if (!set_glyph(r, (long)u32_at(c) > 0x10FFFFL ? -1 : (long)u32_at(c), (long)u16_at(c + 4),
                                   (long)u16_at(c + 6), (long)u16_at(c + 8), (long)u16_at(c + 10), s16_at(c + 12),
                                   s16_at(c + 14), s16_at(c + 16), (long)c[18]))
                        return false;
                }
                break;
            case 5: /* kerning pairs, 10 bytes each */
                if (length % 10 != 0) {
                    WARN(r, "wgf_gfx_font: %s: its kerning block isn't whole", r->path);
                    return false;
                }
                for (i = 0; i < length; i += 10) {
                    const unsigned char *k = block + i;
                    if (!set_kerning(r, (long)u32_at(k) > 0x10FFFFL ? -1 : (long)u32_at(k),
                                     (long)u32_at(k + 4) > 0x10FFFFL ? -1 : (long)u32_at(k + 4), s16_at(k + 8)))
                        return false;
                }
                break;
            default: /* info, and what a later version adds */
                break;
        }
        at += 5 + length;
    }
    if (at != size) {
        WARN(r, "wgf_gfx_font: %s: a block cut short", r->path);
        return false;
    }
    return finish_reading(r);
}

static wgf_gfx_priv_bitmap_t *parse_fnt(const unsigned char *data, int size, const char *path, bool quiet)
{
    reading_t r;
    const char *text = (const char *)data, *end;
    bool ok;
    if (data == NULL || size <= 0) {
        if (!quiet) wgf_log_warn("wgf_gfx_font: %s: empty", path);
        return NULL;
    }
    memset(&r, 0, sizeof(r));
    r.path = path;
    r.quiet = quiet;
    r.font = (wgf_gfx_priv_bitmap_t *)calloc(1, sizeof(wgf_gfx_priv_bitmap_t));
    if (r.font == NULL) return NULL;
    end = text + size;
    if (size >= 4 && memcmp(data, "BMF", 3) == 0) {
        ok = read_binary(&r, data, size);
    } else {
        if (size >= 3 && memcmp(data, "\xEF\xBB\xBF", 3) == 0) text += 3; /* a byte order mark */
        while (text < end && (is_space(*text) || *text == '\n')) text++;
        if (text < end && *text == '<') {
            WARN(&r, "wgf_gfx_font: %s: BMFont's XML form isn't read; save it as text or binary", path);
            ok = false;
        } else {
            ok = read_text(&r, text, end);
        }
    }
    if (!ok) {
        wgf_gfx_priv_bitmap_free(r.font);
        return NULL;
    }
    return r.font;
}

wgf_gfx_priv_bitmap_t *wgf_gfx_priv_bitmap_parse_fnt(const unsigned char *data, int size, const char *path)
{
    return parse_fnt(data, size, path, false);
}

/* ---- an image of glyphs --------------------------------------------------------------- */

static bool is_key(const unsigned char *rgba, int width, int height, int x, int y, const unsigned char key[4])
{
    const unsigned char *p;
    if (x < 0 || y < 0 || x >= width || y >= height) return true;
    p = rgba + ((size_t)y * (size_t)width + (size_t)x) * 4;
    return p[0] == key[0] && p[1] == key[1] && p[2] == key[2] && p[3] == key[3];
}

wgf_gfx_priv_bitmap_t *wgf_gfx_priv_bitmap_from_image(unsigned char *rgba, int width, int height, uint32_t key_color,
                                                      uint32_t first_codepoint, const char *path)
{
    const unsigned char key[4] = {(unsigned char)(key_color >> 24), (unsigned char)(key_color >> 16),
                                  (unsigned char)(key_color >> 8), (unsigned char)key_color};
    reading_t r;
    int x = 0, y, char_spacing, line_spacing, char_height = 0, line;
    size_t i, count;
    memset(&r, 0, sizeof(r));
    r.path = path;
    if (rgba == NULL || width <= 0 || height <= 0) return NULL;
    /* the first pixel that isn't the key: the spacing left of and above every glyph */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width && is_key(rgba, width, height, x, y, key); x++) {
        }
        if (x < width) break;
    }
    if (x == 0 || y == 0 || y == height) {
        wgf_log_warn("wgf_gfx_font: %s: no glyphs inside a border of its key color (magenta)", path);
        return NULL;
    }
    char_spacing = x;
    line_spacing = y;
    while (!is_key(rgba, width, height, char_spacing, line_spacing + char_height, key)) char_height++;
    r.font = (wgf_gfx_priv_bitmap_t *)calloc(1, sizeof(wgf_gfx_priv_bitmap_t));
    if (r.font == NULL) return NULL;
    if (!set_common(&r, char_height, char_height, 1, false) || !set_page(&r, 0, "image", 5)) {
        wgf_gfx_priv_bitmap_free(r.font);
        return NULL;
    }
    for (line = 0; line_spacing + line * (char_height + line_spacing) < height; line++) {
        const int row = line_spacing + line * (char_height + line_spacing);
        int at = char_spacing;
        while (at < width && !is_key(rgba, width, height, at, row, key)) {
            int char_width = 0;
            while (!is_key(rgba, width, height, at + char_width, row, key)) char_width++;
            if (r.font->glyph_count == WGF_GFX_PRIV_BITMAP_IMAGE_GLYPHS_MAX ||
                !set_glyph(&r, (long)first_codepoint + r.font->glyph_count, at, row, char_width, char_height, 0, 0,
                           char_width, 0)) {
                wgf_log_warn("wgf_gfx_font: %s: more than %d glyphs, or one too big", path,
                             WGF_GFX_PRIV_BITMAP_IMAGE_GLYPHS_MAX);
                wgf_gfx_priv_bitmap_free(r.font);
                return NULL;
            }
            at += char_width + char_spacing;
        }
    }
    if (!finish_reading(&r)) {
        wgf_gfx_priv_bitmap_free(r.font);
        return NULL;
    }
    count = (size_t)width * (size_t)height;
    for (i = 0; i < count; i++) { /* the key made transparent, so a filtered edge doesn't pick it up */
        unsigned char *p = rgba + i * 4;
        if (p[0] == key[0] && p[1] == key[1] && p[2] == key[2] && p[3] == key[3]) memset(p, 0, 4);
    }
    wgf_gfx_priv_bitmap_set_page_size(r.font, 0, width, height);
    return r.font;
}

/* ---- the files a .fnt names, for the web ---------------------------------------------- */

void wgf_gfx_priv_bitmap_list_pages(const unsigned char *data, int size, wgf_core_priv_load_add_fn add,
                                    void *context)
{
    wgf_gfx_priv_bitmap_t *font = parse_fnt(data, size, "", true);
    int i;
    if (font == NULL) return; /* its loader says why */
    for (i = 0; i < font->page_count; i++) add(font->pages[i], NULL, true, context);
    wgf_gfx_priv_bitmap_free(font);
}

/* What the font loader reads a bitmap font's file with. */
static const wgf_gfx_priv_bitmap_reader_t reader = {wgf_gfx_priv_bitmap_parse_fnt, wgf_gfx_priv_bitmap_from_image};

/* Linked (wgf_font_enable_bitmaps, or a game's "bitmap_fonts" feature): the font loader's
 * reader set, and the lister that names a .fnt's pages, from the program's start. */
WGF_CORE_PRIV_ON_LINK(register_bitmap_fonts)
{
    wgf_core_priv_load_set_lister(".fnt", wgf_gfx_priv_bitmap_list_pages);
    wgf_gfx_priv_set_bitmap_reader(&reader);
}

/* ---- linked on purpose ---------------------------------------------------------------- */

bool wgf_font_enable_bitmaps(void)
{
    wgf_gfx_priv_set_bitmap_reader(&reader);
    return true;
}
