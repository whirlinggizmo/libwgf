#include "wgf_font.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fontstash.h"
#include "jetbrains_mono_ascii.h"
#include "wgf_core_part_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl and sokol_fontstash need it first */
#include "text/wgf_gfx_font_bitmap_priv.h"
#include "text/wgf_gfx_font_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "util/sokol_gl.h"
#include "util/sokol_fontstash.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_draw.h"
#include "wgf_log.h"
#include "wgf_render.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */

/* Fonts: a resource loaded on create, its bytes read on a worker and handed to
 * fontstash on the main thread. fontstash can't let go of a font, so a font
 * released for the last time is parked, bytes and all, and a later create of the
 * same file takes it back at once. Cribbed from wgrender's wgr_font and wgr_text.
 *
 * Or a bitmap font (a BMFont .fnt, or an image of glyphs): read and its pages decoded on
 * the worker (wgf_gfx_font_bitmap.c), the pages made textures of its own on the main
 * thread, its glyphs drawn as their pages' quads; laid out by the code fontstash's fonts
 * are, through the few calls that ask a font (use_font, span_width, vertical_metrics,
 * and the glyphs' quads). */

#define ATLAS_START 1024
#define ATLAS_MOST 4096 /* 16 MB of 8-bit coverage at most */

typedef struct font_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    int fons_id;
    unsigned char *bytes; /* fontstash reads them for as long as it has the font */
    /* a bitmap font's, once READY: its glyphs, and its pages as textures of its own */
    wgf_gfx_priv_bitmap_t *bitmap;
    wgf_texture_t pages[WGF_GFX_PRIV_BITMAP_PAGES_MAX];
    wgf_texture_filter_t filter; /* its pages' (NEAREST unset), kept from before it is READY */
} font_t;

typedef struct parked_t {
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
    int fons_id;
    unsigned char *bytes;
} parked_t;

typedef struct loaded_t {
    unsigned char *bytes;
    int size;
} loaded_t;

static FONScontext *fons;
static int builtin_id = FONS_INVALID;

static bool ensure(void);
static wgf_handle_t default_font; /* 0: the built-in font */
static bool grow_pending;
static bool full_warned;
static unsigned atlas_version = 1; /* changed whenever glyphs' places in the atlas change (wgf_gfx_font_priv.h) */

static bool pool_ready;
static wgf_core_priv_handle_pool_t font_pool;
static font_t *fonts;
static parked_t *parked;
static int parked_count;
static int parked_capacity;

static font_t *font_of_at(wgf_handle_t font, const char *caller)
{
    uint16_t index;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve_at(&font_pool, font, &index, caller)) return NULL;
    return &fonts[index];
}
#define font_of(...) font_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

/* The font to draw `font` with: its own when ready, else the default's, else NULL, the
 * built-in one. */
static const font_t *drawn_font(wgf_handle_t font)
{
    const font_t *font_ptr = font_of(font != 0 ? font : default_font);
    if (font_ptr != NULL && font_ptr->resource.status == WGF_RESOURCE_STATUS_READY) return font_ptr;
    font_ptr = font_of(default_font);
    if (font_ptr != NULL && font_ptr->resource.status == WGF_RESOURCE_STATUS_READY) return font_ptr;
    return NULL;
}

static bool has_extension(const char *path, const char *extension)
{
    const size_t length = strlen(path), wanted = strlen(extension);
    size_t i;
    if (length < wanted) return false;
    for (i = 0; i < wanted; i++) {
        char c = path[length - wanted + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != extension[i]) return false;
    }
    return true;
}

/* Whether a path names a bitmap font: a BMFont, or an image of glyphs. */
static bool is_fnt_path(const char *path)
{
    return has_extension(path, ".fnt");
}

static bool is_image_path(const char *path)
{
    static const char *const extensions[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif"};
    size_t i;
    for (i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        if (has_extension(path, extensions[i])) return true;
    }
    return false;
}

/* --- the loader ------------------------------------------------------------ */

static uint32_t read_u32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Whether `bytes` are a font whose table directory fits inside them. stb_truetype
 * trusts the offsets a file gives it, and reads past the end of a short or broken
 * one, so nothing reaches it that hasn't passed this. A collection (ttcf) is
 * checked down to its first font, the one fontstash reads. */
static bool is_whole_font(const unsigned char *bytes, int size)
{
    uint32_t offset = 0, tables, i;
    if (size < 12) return false;
    if (read_u32(bytes) == 0x74746366u) { /* "ttcf": the first font's offset */
        if (size < 16) return false;
        offset = read_u32(bytes + 12);
        if (offset > (uint32_t)size - 12) return false;
    }
    switch (read_u32(bytes + offset)) {
        case 0x00010000u: /* TrueType */
        case 0x74727565u: /* "true" */
        case 0x4F54544Fu: /* "OTTO": OpenType with CFF outlines */
        case 0x74797031u: /* "typ1" */
            break;
        default:
            return false;
    }
    tables = ((uint32_t)bytes[offset + 4] << 8) | bytes[offset + 5];
    if ((uint64_t)offset + 12u + 16u * (uint64_t)tables > (uint64_t)size) return false;
    for (i = 0; i < tables; i++) {
        const unsigned char *record = bytes + offset + 12 + 16 * i;
        if ((uint64_t)read_u32(record + 8) + read_u32(record + 12) > (uint64_t)size) return false;
    }
    return true;
}

static void *prepare(const char *path)
{
    loaded_t *loaded = (loaded_t *)calloc(1, sizeof(loaded_t));
    if (loaded == NULL) return NULL;
    if (!wgf_core_priv_fs_read(path, &loaded->bytes, &loaded->size)) {
        free(loaded);
        return NULL;
    }
    if (!is_whole_font(loaded->bytes, loaded->size)) {
        wgf_log_warn("wgf_gfx_font: %s: not a whole TrueType or OpenType font", path);
        wgf_core_priv_fs_read_free(loaded->bytes);
        free(loaded);
        return NULL;
    }
    return loaded;
}

static wgf_core_priv_load_step_t finish(void *prepared, wgf_handle_t resource)
{
    loaded_t *loaded = (loaded_t *)prepared;
    font_t *font_ptr = font_of(resource);
    int id;
    if (font_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (wgf_gfx_priv_render_is_lost()) return WGF_CORE_PRIV_LOAD_WAIT; /* the GPU's context lost: until it is back */
    if (!ensure()) return WGF_CORE_PRIV_LOAD_MORE; /* gfx isn't running yet: wait for it */
    /* libwgf keeps the bytes (freeData 0): fontstash frees them on some failures
       and not others */
    id = fonsAddFontMem(fons, font_ptr->resource.path, loaded->bytes, loaded->size, 0);
    if (id == FONS_INVALID) {
        wgf_log_warn("wgf_gfx_font: %s: not a font it can read", font_ptr->resource.path);
        return WGF_CORE_PRIV_LOAD_FAILED;
    }
    font_ptr->fons_id = id;
    font_ptr->bytes = loaded->bytes;
    loaded->bytes = NULL; /* the font's now */
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void discard(void *prepared)
{
    loaded_t *loaded = (loaded_t *)prepared;
    wgf_core_priv_fs_read_free(loaded->bytes);
    free(loaded);
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"font", prepare, finish, discard, fail, NULL, false, NULL};

/* --- the bitmap fonts' loader ------------------------------------------------- */

/* An image of glyphs is raylib's LoadFont's: glyphs on magenta, from the space on. */
#define IMAGE_KEY 0xFF00FFFFu
#define IMAGE_FIRST 32u

typedef struct bitmap_loaded_t {
    wgf_gfx_priv_bitmap_t *bitmap;
    void *decoded[WGF_GFX_PRIV_BITMAP_PAGES_MAX]; /* its pages, decoded with their mipmaps */
} bitmap_loaded_t;

static void discard_bitmap(void *prepared)
{
    bitmap_loaded_t *loaded = (bitmap_loaded_t *)prepared;
    int i;
    for (i = 0; i < WGF_GFX_PRIV_BITMAP_PAGES_MAX; i++) wgf_gfx_priv_texture_decoded_free(loaded->decoded[i]);
    wgf_gfx_priv_bitmap_free(loaded->bitmap);
    free(loaded);
}

/* An image of glyphs: its key made transparent before its mipmaps are built from it. */
/* What reads a bitmap font's file: set as wgf_gfx_font_bitmap_read.c is linked (a program that
 * enables bitmap fonts); NULL, a bitmap font FAILED. Set before any load, never changed after. */
static const wgf_gfx_priv_bitmap_reader_t *reader;

void wgf_gfx_priv_set_bitmap_reader(const wgf_gfx_priv_bitmap_reader_t *bitmap_reader)
{
    reader = bitmap_reader;
}

bool wgf_font_is_bitmaps_enabled(void)
{
    return reader != NULL;
}

static bool prepare_image(bitmap_loaded_t *loaded, const unsigned char *bytes, int size, const char *path)
{
    void *decoded = wgf_gfx_priv_texture_decode_base(bytes, size, path);
    unsigned char *rgba;
    int width = 0, height = 0;
    if (decoded == NULL) return false;
    loaded->decoded[0] = decoded;
    rgba = wgf_gfx_priv_texture_decoded_base(decoded, &width, &height);
    loaded->bitmap = reader->from_image(rgba, width, height, IMAGE_KEY, IMAGE_FIRST, path);
    if (loaded->bitmap == NULL) return false;
    if (!wgf_gfx_priv_texture_decoded_build_mipmaps(decoded)) {
        wgf_log_warn("wgf_gfx_font: %s: out of memory", path);
        return false;
    }
    return true;
}

/* A .fnt and its pages, each named beside it. */
static bool prepare_fnt(bitmap_loaded_t *loaded, const unsigned char *bytes, int size, const char *path)
{
    int page;
    loaded->bitmap = reader->parse_fnt(bytes, size, path);
    if (loaded->bitmap == NULL) return false;
    for (page = 0; page < loaded->bitmap->page_count; page++) {
        char page_path[WGF_CORE_PRIV_FS_PATH_MAX];
        unsigned char *page_bytes;
        int page_size, width = 0, height = 0;
        if (!wgf_asset_priv_is_relative_uri(loaded->bitmap->pages[page]) ||
            !wgf_asset_priv_join_relative(path, loaded->bitmap->pages[page], page_path, sizeof(page_path))) {
            wgf_log_warn("wgf_gfx_font: %s: page %s isn't a file beside it", path, loaded->bitmap->pages[page]);
            return false;
        }
        if (!wgf_core_priv_fs_read(page_path, &page_bytes, &page_size)) {
            wgf_log_warn("wgf_gfx_font: %s: its page %s couldn't be read", path, page_path);
            return false;
        }
        loaded->decoded[page] = wgf_gfx_priv_texture_decode(page_bytes, page_size, page_path);
        wgf_core_priv_fs_read_free(page_bytes);
        if (loaded->decoded[page] == NULL) return false;
        wgf_gfx_priv_texture_decoded_pixels(loaded->decoded[page], &width, &height);
        wgf_gfx_priv_bitmap_set_page_size(loaded->bitmap, page, width, height);
    }
    return true;
}

static void *prepare_bitmap(const char *path)
{
    bitmap_loaded_t *loaded = (bitmap_loaded_t *)calloc(1, sizeof(bitmap_loaded_t));
    unsigned char *bytes;
    int size;
    bool ok;
    if (loaded == NULL) return NULL;
    if (reader == NULL) {
        wgf_log_error("wgf_font_create: %s: bitmap fonts aren't linked in this program (a game: \"bitmap_fonts\" in "
                      "wgf.json's features; C: wgf_font_enable_bitmaps)",
                      path);
        free(loaded);
        return NULL;
    }
    if (!wgf_core_priv_fs_read(path, &bytes, &size)) {
        free(loaded);
        return NULL;
    }
    ok = is_fnt_path(path) ? prepare_fnt(loaded, bytes, size, path) : prepare_image(loaded, bytes, size, path);
    wgf_core_priv_fs_read_free(bytes);
    if (!ok) {
        discard_bitmap(loaded);
        return NULL;
    }
    return loaded;
}

static wgf_core_priv_load_step_t finish_bitmap(void *prepared, wgf_handle_t resource)
{
    bitmap_loaded_t *loaded = (bitmap_loaded_t *)prepared;
    font_t *font_ptr = font_of(resource);
    wgf_texture_t pages[WGF_GFX_PRIV_BITMAP_PAGES_MAX] = {0};
    int page;
    if (font_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (wgf_gfx_priv_render_is_lost()) return WGF_CORE_PRIV_LOAD_WAIT; /* the GPU's context lost: until it is back */
    if (!ensure()) return WGF_CORE_PRIV_LOAD_MORE; /* gfx isn't running yet: wait for it */
    for (page = 0; page < loaded->bitmap->page_count; page++) {
        pages[page] = wgf_gfx_priv_texture_create_decoded(loaded->decoded[page]); /* it takes the image */
        loaded->decoded[page] = NULL;
        if (pages[page] == 0) {
            wgf_log_warn("wgf_gfx_font: %s: its page %d couldn't be made a texture", font_ptr->resource.path, page);
            for (page = 0; page < loaded->bitmap->page_count; page++) {
                if (pages[page] != 0) wgf_resource_release(pages[page]);
            }
            return WGF_CORE_PRIV_LOAD_FAILED;
        }
        wgf_texture_set_sampling(pages[page], WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, font_ptr->filter);
    }
    font_ptr = font_of(resource); /* making textures made no fonts, but ask again */
    memcpy(font_ptr->pages, pages, sizeof(pages));
    font_ptr->bitmap = loaded->bitmap;
    loaded->bitmap = NULL; /* the font's now */
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static const wgf_core_priv_loader_t bitmap_loader = {"bitmap font", prepare_bitmap, finish_bitmap, discard_bitmap,
                                                     fail, NULL, false, NULL};

/* The GPU's context given back: a bitmap font's file read again (as this run has it) and
 * only its pages made again, each into the texture it made before. */
static wgf_core_priv_load_step_t finish_pages(void *prepared, wgf_handle_t resource)
{
    bitmap_loaded_t *loaded = (bitmap_loaded_t *)prepared;
    const font_t *font_ptr = font_of(resource);
    int page;
    if (font_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (wgf_gfx_priv_render_is_lost()) return WGF_CORE_PRIV_LOAD_WAIT; /* lost again: until it is back */
    for (page = 0; page < loaded->bitmap->page_count && page < WGF_GFX_PRIV_BITMAP_PAGES_MAX; page++) {
        if (font_ptr->pages[page] != 0 && loaded->decoded[page] != NULL) {
            wgf_gfx_priv_texture_refill_decoded(font_ptr->pages[page], loaded->decoded[page]); /* takes it */
        }
        loaded->decoded[page] = NULL;
    }
    wgf_core_priv_resource_loaded(resource, NULL); /* no longer loading again */
    return WGF_CORE_PRIV_LOAD_DONE;
}

static const wgf_core_priv_loader_t pages_loader = {"bitmap font's pages", prepare_bitmap, finish_pages, discard_bitmap,
                                                    fail, NULL, false, NULL};
/* Set by wgf_font_create, the only way a bitmap font comes: text drawn in the built-in font
 * alone links no image decoder. */
static const wgf_core_priv_loader_t *pages_again;

/* --- the public API --------------------------------------------------------- */

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    return is_fnt_path(path) || is_image_path(path) ? &bitmap_loader : &loader;
}

static void init_font(void *record)
{
    ((font_t *)record)->fons_id = FONS_INVALID;
    ((font_t *)record)->filter = WGF_TEXTURE_FILTER_NEAREST;
}

/* What a record holds past the header. fontstash keeps a font it was given, so a READY
 * one is parked, for a later create of its path to take back; its bytes are freed
 * only once fontstash is gone. */
static void free_font(wgf_handle_t font, void *record)
{
    font_t *font_ptr = (font_t *)record;
    int page;
    (void)font;
    if (font_ptr->bitmap != NULL) { /* a bitmap font is its own: let go of whole */
        for (page = 0; page < font_ptr->bitmap->page_count; page++) wgf_resource_release(font_ptr->pages[page]);
        wgf_gfx_priv_bitmap_free(font_ptr->bitmap);
        font_ptr->bitmap = NULL;
        return;
    }
    if (font_ptr->resource.status != WGF_RESOURCE_STATUS_READY || fons == NULL) {
        free(font_ptr->bytes);
        return;
    }
    if (parked_count == parked_capacity) {
        const int capacity = parked_capacity > 0 ? parked_capacity * 2 : 8;
        parked_t *grown = (parked_t *)realloc(parked, sizeof(parked_t) * (size_t)capacity);
        if (grown != NULL) {
            parked = grown;
            parked_capacity = capacity;
        }
    }
    if (parked_count < parked_capacity) {
        snprintf(parked[parked_count].path, sizeof(parked[parked_count].path), "%s", font_ptr->resource.path);
        parked[parked_count].fons_id = font_ptr->fons_id;
        parked[parked_count].bytes = font_ptr->bytes;
        parked_count++;
    }
    /* out of memory to remember it: its bytes go unfreed until exit, since fontstash may still read them */
}

static const wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_font_create",
                                                            .loader = loader_of,
                                                            .init = init_font,
                                                            .free = free_font};

/* --- the public API --------------------------------------------------------- */

wgf_handle_t wgf_font_create(const char *path)
{
    char normalized[WGF_CORE_PRIV_FS_PATH_MAX] = "";
    wgf_handle_t handle;

    pages_again = &pages_loader;
    ensure();
    if (!pool_ready) {
        pool_ready =
            wgf_core_priv_handle_pool_init(&font_pool, WGF_CORE_PRIV_HANDLE_KIND_FONT, (void **)&fonts, sizeof(font_t), 8, 4096);
        if (!pool_ready) return 0;
        wgf_core_priv_resource_register(&font_pool, &resource_kind);
    }
    handle = wgf_core_priv_resource_find(WGF_CORE_PRIV_HANDLE_KIND_FONT, path);
    if (handle != 0) return handle;
    if (wgf_core_priv_fs_normalize_path(path, normalized, sizeof(normalized))) {
        for (int p = 0; p < parked_count; p++) { /* loaded before: take it back */
            if (strcmp(parked[p].path, normalized) != 0) continue;
            handle = wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_FONT);
            if (font_of(handle) == NULL) return 0;
            wgf_core_priv_resource_set_path(handle, normalized);
            font_of(handle)->fons_id = parked[p].fons_id;
            font_of(handle)->bytes = parked[p].bytes;
            parked[p] = parked[--parked_count];
            return handle;
        }
    }
    return wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_FONT, path);
}

void wgf_gfx_priv_font_retain(wgf_handle_t font)
{
    wgf_core_priv_resource_retain(font);
}

bool wgf_font_set_default(wgf_handle_t font)
{
    if (font != 0 && font_of(font) == NULL) return false;
    if (font != 0) wgf_gfx_priv_font_retain(font); /* before the release, in case it is the same */
    if (default_font != 0) wgf_resource_release(default_font);
    default_font = font;
    return true;
}

wgf_handle_t wgf_font_get_default(void)
{
    return default_font;
}

float wgf_font_get_size(wgf_font_t font)
{
    const font_t *font_ptr = font_of(font != 0 ? font : default_font);
    if (font_ptr == NULL || font_ptr->resource.status != WGF_RESOURCE_STATUS_READY || font_ptr->bitmap == NULL)
        return 0.0f;
    return (float)font_ptr->bitmap->line_height;
}

bool wgf_font_set_filter(wgf_font_t font, wgf_texture_filter_t filter)
{
    font_t *font_ptr = font_of(font);
    int page;
    if (font_ptr == NULL || (filter != WGF_TEXTURE_FILTER_LINEAR && filter != WGF_TEXTURE_FILTER_NEAREST) ||
        !(is_fnt_path(font_ptr->resource.path) || is_image_path(font_ptr->resource.path)))
        return false;
    font_ptr->filter = filter;
    if (font_ptr->bitmap != NULL) {
        for (page = 0; page < font_ptr->bitmap->page_count; page++) {
            wgf_texture_set_sampling(font_ptr->pages[page], WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, filter);
        }
    }
    return true;
}

wgf_texture_filter_t wgf_font_get_filter(wgf_font_t font)
{
    const font_t *font_ptr = font_of(font);
    if (font_ptr == NULL || !(is_fnt_path(font_ptr->resource.path) || is_image_path(font_ptr->resource.path)))
        return WGF_TEXTURE_FILTER_LINEAR;
    return font_ptr->filter;
}

/* --- layout ------------------------------------------------------------------ */

/* Ported from wgrender's wgr_text.c: text is laid out at the size it is rasterized
 * at, the logical size times the frame's pixel density, and every measure is
 * divided back by that density, so what is measured is what is drawn. */

/* Framebuffer pixels per logical pixel: glyphs are rasterized at size times this
 * and drawn scaled back, so they land 1:1 on the pixels. */
static float pixel_scale(void)
{
    const float scale = wgf_render_get_dpi_scale();
    return scale > 0.0f ? scale : 1.0f;
}

/* Select `font` at `size` logical pixels (WGF_GFX_PRIV_FONT_DEFAULT_SIZE for 0 or
 * less), rasterized at `scale`, in `color`; false before setup. */
/* Spacing, logical pixels added between letters and lines: immediate mode's (wgf_draw.h),
 * and a text actor's while it lays out (wgf_gfx_priv_font_set_spacing). */
static struct {
    float draw_letter, draw_line;
    bool own;
    float letter, line;
} spacing;

void wgf_draw_set_text_spacing(float letter, float line)
{
    spacing.draw_letter = isfinite(letter) ? letter : 0.0f;
    spacing.draw_line = isfinite(line) ? line : 0.0f;
}

wgf_vec2_t wgf_draw_get_text_spacing(void)
{
    return wgf_vec2_make(spacing.draw_letter, spacing.draw_line);
}

void wgf_gfx_priv_font_set_spacing(bool own, float letter, float line)
{
    spacing.own = own;
    spacing.letter = letter;
    spacing.line = line;
}

static float letter_spacing(void)
{
    return spacing.own ? spacing.letter : spacing.draw_letter;
}

static float line_spacing(void)
{
    return spacing.own ? spacing.line : spacing.draw_line;
}

/* What the layout measures and draws with, chosen by use_font: fontstash's font, or a
 * bitmap font's glyphs (`bitmap` set), `scale` layout pixels a font pixel, `letter`
 * layout pixels between letters. */
static struct {
    const wgf_gfx_priv_bitmap_t *bitmap;
    wgf_texture_t pages[WGF_GFX_PRIV_BITMAP_PAGES_MAX];
    float scale, letter;
    wgf_color_t color;
} sel;

static bool use_font(wgf_handle_t font, float size, float scale, wgf_color_t color)
{
    const font_t *font_ptr;
    if (!ensure()) return false;
    font_ptr = drawn_font(font);
    sel.color = color;
    if (font_ptr != NULL && font_ptr->bitmap != NULL) { /* drawn at its own size times size / its line's height */
        sel.bitmap = font_ptr->bitmap;
        memcpy(sel.pages, font_ptr->pages, sizeof(sel.pages));
        sel.scale = (size > 0.0f ? size : WGF_GFX_PRIV_FONT_DEFAULT_SIZE) * scale / (float)sel.bitmap->line_height;
        sel.letter = letter_spacing() * scale;
        return true;
    }
    sel.bitmap = NULL;
    fonsClearState(fons);
    fonsSetFont(fons, font_ptr != NULL ? font_ptr->fons_id : builtin_id);
    fonsSetSize(fons, (size > 0.0f ? size : WGF_GFX_PRIV_FONT_DEFAULT_SIZE) * scale);
    fonsSetColor(fons, sfons_rgba((uint8_t)wgf_color_get_red(color), (uint8_t)wgf_color_get_green(color),
                                  (uint8_t)wgf_color_get_blue(color), (uint8_t)wgf_color_get_alpha(color)));
    fonsSetAlign(fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    fonsSetSpacing(fons, letter_spacing() * scale);
    return true;
}

/* A bitmap font's glyphs along text[start, end) from the pen at `x`, the line's top at
 * `top`, in the layout's pixels: each glyph on `page` (any, for -1) with a rectangle given
 * to `fn` (x0, y0, x1, y1, then s0, t0, s1, t1 on its page), kerned and spaced between
 * glyphs as fontstash spaces them. Returns the pen's advance. */
typedef void (*glyph_fn)(const float quad[8], void *user);

static float bitmap_run(const char *start, const char *end, float x, float top, int page, glyph_fn fn, void *user)
{
    const wgf_gfx_priv_bitmap_t *bitmap = sel.bitmap;
    const float s = sel.scale;
    const char *at = start;
    uint32_t previous = 0;
    bool first = true;
    float pen = x;
    while (at < end) {
        const uint32_t codepoint = wgf_gfx_priv_bitmap_next_codepoint(&at, end);
        const wgf_gfx_priv_bitmap_glyph_t *glyph = wgf_gfx_priv_bitmap_glyph(bitmap, codepoint);
        if (!first) pen += (float)wgf_gfx_priv_bitmap_kerning(bitmap, previous, codepoint) * s + sel.letter;
        first = false;
        previous = codepoint;
        if (glyph == NULL) continue;
        if (fn != NULL && glyph->width > 0 && glyph->height > 0 && (page < 0 || glyph->page == page)) {
            const float page_width = (float)bitmap->page_width[glyph->page];
            const float page_height = (float)bitmap->page_height[glyph->page];
            float quad[8];
            quad[0] = pen + (float)glyph->x_offset * s;
            quad[1] = top + (float)glyph->y_offset * s;
            quad[2] = quad[0] + (float)glyph->width * s;
            quad[3] = quad[1] + (float)glyph->height * s;
            quad[4] = (float)glyph->x / page_width;
            quad[5] = (float)glyph->y / page_height;
            quad[6] = (float)(glyph->x + glyph->width) / page_width;
            quad[7] = (float)(glyph->y + glyph->height) / page_height;
            fn(quad, user);
        }
        pen += (float)glyph->advance * s;
    }
    return pen - x;
}

/* A glyph's quad as two triangles, in the recording begun. */
static void emit_quad(const float q[8], void *user)
{
    (void)user;
    sgl_v2f_t2f(q[0], q[1], q[4], q[5]);
    sgl_v2f_t2f(q[2], q[1], q[6], q[5]);
    sgl_v2f_t2f(q[2], q[3], q[6], q[7]);
    sgl_v2f_t2f(q[0], q[1], q[4], q[5]);
    sgl_v2f_t2f(q[2], q[3], q[6], q[7]);
    sgl_v2f_t2f(q[0], q[3], q[4], q[7]);
}

/* A bitmap font's page as it is drawn; false while it can't be. */
static bool page_binding(int page, sg_view *view, sg_sampler *sampler)
{
    int width, height;
    bool placeholder;
    return wgf_gfx_priv_texture_get_binding(sel.pages[page], view, sampler, &width, &height, &placeholder) &&
           !placeholder;
}

static void set_color(wgf_color_t color)
{
    sgl_c4b((uint8_t)wgf_color_get_red(color), (uint8_t)wgf_color_get_green(color), (uint8_t)wgf_color_get_blue(color),
            (uint8_t)wgf_color_get_alpha(color));
}

/* A line of a bitmap font's text in 2D, page by page, through the pipeline immediate mode
 * has now (its blend mode). */
static void bitmap_draw_line(const char *start, const char *end, float x, float top)
{
    int page;
    for (page = 0; page < sel.bitmap->page_count; page++) {
        sg_view view;
        sg_sampler sampler;
        if (!page_binding(page, &view, &sampler)) continue;
        sgl_enable_texture();
        sgl_texture(view, sampler);
        sgl_begin_triangles();
        set_color(sel.color);
        bitmap_run(start, end, x, top, page, emit_quad, NULL);
        sgl_end();
        sgl_disable_texture();
    }
}

/* Advance width of text[start, end) in the selected font's pixels. */
static float span_width(const char *start, const char *end)
{
    float bounds[4] = {0};
    if (end <= start) return 0.0f;
    if (sel.bitmap != NULL) return bitmap_run(start, end, 0.0f, 0.0f, -1, NULL, NULL);
    return fonsTextBounds(fons, 0.0f, 0.0f, start, end, bounds);
}

/* The selected font's ascender and descender (below the baseline: negative) and line
 * height, in its pixels, as fontstash gives them. */
static void vertical_metrics(float *ascender, float *descender, float *line_height)
{
    if (sel.bitmap != NULL) {
        if (ascender != NULL) *ascender = (float)sel.bitmap->base * sel.scale;
        if (descender != NULL) *descender = -(float)(sel.bitmap->line_height - sel.bitmap->base) * sel.scale;
        if (line_height != NULL) *line_height = (float)sel.bitmap->line_height * sel.scale;
        return;
    }
    fonsVertMetrics(fons, ascender, descender, line_height);
}

typedef void (*line_fn)(const char *start, const char *end, int index, void *user);

static void emit_line(const char *start, const char *end, int index, float *out_width, line_fn fn, void *user)
{
    const float width = span_width(start, end);
    if (width > *out_width) *out_width = width;
    if (fn != NULL) fn(start, end, index, user);
}

/* Walk the lines of text[text, end), breaking at newlines and, with max_width > 0,
 * before a word that would overflow (a word longer than the box keeps its own line).
 * Everything is in the selected font's pixels. Returns the line count; out_width gets
 * the widest line's width. */
static int walk_lines(const char *text, const char *end, float max_width, float *out_width, line_fn fn, void *user)
{
    const char *line_start = text;
    const char *line_end = text; /* end of what this line holds so far */
    const char *cursor = text;
    int count = 0;

    *out_width = 0.0f;
    if (text >= end) return 0;
    if (max_width <= 0.0f) {
        /* no wrapping: a line is exactly what's between newlines, spaces included, so
           measuring " " gives the space's advance (layout libraries such as Clay add
           that between the words they measure) */
        for (;;) {
            const char *newline = (const char *)memchr(line_start, '\n', (size_t)(end - line_start));
            if (newline == NULL) {
                if (line_start < end || count == 0) emit_line(line_start, end, count++, out_width, fn, user);
                return count;
            }
            emit_line(line_start, newline, count++, out_width, fn, user);
            line_start = newline + 1;
        }
    }
    for (;;) { /* wrapping: spaces at a break belong to neither line's width */
        const char *word_start, *word_end;
        while (cursor < end && (*cursor == ' ' || *cursor == '\t')) cursor++; /* spaces stay with the line before them */
        word_start = cursor;
        while (cursor < end && *cursor != '\n' && *cursor != ' ' && *cursor != '\t') cursor++;
        word_end = cursor;
        if (word_end > word_start) {
            if (line_end > line_start && span_width(line_start, word_end) > max_width) {
                emit_line(line_start, line_end, count++, out_width, fn, user);
                line_start = word_start;
            }
            line_end = word_end;
        }
        if (cursor < end && *cursor == '\n') {
            emit_line(line_start, line_end, count++, out_width, fn, user);
            cursor++;
            line_start = line_end = cursor;
            continue;
        }
        if (cursor >= end) break;
    }
    if (line_end > line_start || count == 0) emit_line(line_start, line_end, count++, out_width, fn, user);
    return count;
}

/* The block `text` makes, in the selected font's pixels: its lines, the widest
 * one's width, and the line height. */
static int block_lines(const char *text, float max_width, float *widest, float *line_height, float scale)
{
    vertical_metrics(NULL, NULL, line_height);
    *line_height += line_spacing() * scale;
    if (*line_height < 0.0f) *line_height = 0.0f;
    return walk_lines(text, text + strlen(text), max_width, widest, NULL, NULL);
}

/* A block's width: a wrapped one is as wide as its wrap width, or its widest line if
 * a word overflows; an unwrapped one as wide as its widest line. */
static float box_width(float widest, float wrap)
{
    return wrap > 0.0f && wrap > widest ? wrap : widest;
}

static float align_offset(float extent, int align) /* 0 start, 1 middle, 2 end */
{
    return align == 1 ? -extent * 0.5f : align == 2 ? -extent : 0.0f;
}

wgf_vec2_t wgf_font_measure(wgf_handle_t font, const char *text, float size)
{
    const float scale = pixel_scale();
    float widest = 0.0f, line_height = 0.0f;
    int lines;
    if (text == NULL || !use_font(font, size, scale, 0xFFFFFFFFu)) return wgf_vec2_make(0.0f, 0.0f);
    lines = block_lines(text, 0.0f, &widest, &line_height, scale);
    return wgf_vec2_make(widest / scale, (float)lines * line_height / scale);
}

/* The block's rectangle about (0, 0), rasterized at `scale` pixels a unit. */
static bool block_bounds_at(wgf_handle_t font, const char *text, float size, float scale, float wrap_width,
                            wgf_text_halign_t halign, wgf_text_valign_t valign, float *left, float *top, float *width,
                            float *height)
{
    float widest = 0.0f, line_height = 0.0f;
    int lines;
    if (text == NULL || !use_font(font, size, scale, 0xFFFFFFFFu)) return false;
    lines = block_lines(text, wrap_width * scale, &widest, &line_height, scale);
    if (lines == 0) return false;
    *width = box_width(widest / scale, wrap_width);
    *height = (float)lines * line_height / scale;
    *left = align_offset(*width, (int)halign);
    *top = align_offset(*height, (int)valign);
    return true;
}

bool wgf_gfx_priv_font_block_bounds(wgf_handle_t font, const char *text, float size, float wrap_width,
                                   wgf_text_halign_t halign, wgf_text_valign_t valign, float *left, float *top,
                                   float *width, float *height)
{
    return block_bounds_at(font, text, size, pixel_scale(), wrap_width, halign, valign, left, top, width, height);
}

bool wgf_gfx_priv_font_block_bounds_3d(wgf_handle_t font, const char *text, float size, float wrap_width,
                                      wgf_text_halign_t halign, wgf_text_valign_t valign, float *left, float *top,
                                      float *width, float *height)
{
    return size > 0.0f && block_bounds_at(font, text, size, WGF_GFX_PRIV_FONT_RASTER_3D / size, wrap_width, halign,
                                          valign, left, top, width, height);
}

typedef struct block_draw_t {
    float left, top, line_height, box_width; /* in the font's (rasterized) pixels */
    wgf_text_halign_t halign;
    int page; /* a bitmap font's page drawn in 3D */
} block_draw_t;

static void draw_line(const char *start, const char *end, int index, void *user)
{
    const block_draw_t *ctx = (const block_draw_t *)user;
    float x = ctx->left;
    if (end <= start) return;
    if (ctx->halign == WGF_TEXT_HALIGN_CENTER) x += (ctx->box_width - span_width(start, end)) * 0.5f;
    else if (ctx->halign == WGF_TEXT_HALIGN_RIGHT) x += ctx->box_width - span_width(start, end);
    if (sel.bitmap != NULL) bitmap_draw_line(start, end, x, ctx->top + (float)index * ctx->line_height);
    else fonsDrawText(fons, x, ctx->top + (float)index * ctx->line_height, start, end);
}

void wgf_gfx_priv_font_draw_block(wgf_handle_t font, const char *text, float size, wgf_color_t color,
                                 float wrap_width, wgf_text_halign_t halign, wgf_text_valign_t valign,
                                 const float *matrix)
{
    const float scale = pixel_scale();
    float widest = 0.0f, line_height = 0.0f, width;
    block_draw_t ctx;
    int lines;

    if (text == NULL || !use_font(font, size, scale, color)) return;
    lines = block_lines(text, wrap_width * scale, &widest, &line_height, scale);
    if (lines == 0) return;
    width = box_width(widest, wrap_width * scale);
    ctx.left = align_offset(width, (int)halign);
    ctx.top = align_offset((float)lines * line_height, (int)valign);
    ctx.line_height = line_height;
    ctx.box_width = width;
    ctx.halign = halign;
    ctx.page = -1;

    /* laid out and drawn in the rasterized pixels, scaled back to logical ones:
       fontstash emits each call's vertices before returning, so they get this matrix */
    sgl_matrix_mode_modelview();
    if (matrix != NULL) sgl_load_matrix(matrix);
    else sgl_load_identity();
    sgl_scale(1.0f / scale, 1.0f / scale, 1.0f);
    walk_lines(text, text + strlen(text), wrap_width * scale, &widest, draw_line, &ctx);
    sgl_load_identity(); /* back as the rest of the frame expects it */
}

/* Text in 3D, libwgt's (wgrender's text3d): each glyph's quad, two triangles, through
 * a depth-tested pipeline on sokol_fontstash's own shader and atlas. */
static sgl_pipeline pipeline_3d;
static sg_shader pipeline_3d_shader;
static sgl_pipeline bitmap_pipeline_3d; /* a bitmap font's pages: sokol_gl's own shader, the same depth test */

static void draw_line_quads(const char *start, const char *end, int index, void *user)
{
    const block_draw_t *ctx = (const block_draw_t *)user;
    float x = ctx->left;
    FONStextIter iter;
    FONSquad quad;
    if (end <= start) return;
    if (ctx->halign == WGF_TEXT_HALIGN_CENTER) x += (ctx->box_width - span_width(start, end)) * 0.5f;
    else if (ctx->halign == WGF_TEXT_HALIGN_RIGHT) x += ctx->box_width - span_width(start, end);
    if (sel.bitmap != NULL) {
        bitmap_run(start, end, x, ctx->top + (float)index * ctx->line_height, ctx->page, emit_quad, NULL);
        return;
    }
    fonsTextIterInit(fons, &iter, x, ctx->top + (float)index * ctx->line_height, start, end);
    while (fonsTextIterNext(fons, &iter, &quad)) {
        sgl_v2f_t2f(quad.x0, quad.y0, quad.s0, quad.t0);
        sgl_v2f_t2f(quad.x1, quad.y0, quad.s1, quad.t0);
        sgl_v2f_t2f(quad.x1, quad.y1, quad.s1, quad.t1);
        sgl_v2f_t2f(quad.x0, quad.y0, quad.s0, quad.t0);
        sgl_v2f_t2f(quad.x1, quad.y1, quad.s1, quad.t1);
        sgl_v2f_t2f(quad.x0, quad.y1, quad.s0, quad.t1);
    }
}

/* The depth-tested pipeline on `shader` (none: sokol_gl's own), blended, not written. */
static sgl_pipeline make_pipeline_3d(sg_shader shader)
{
    sg_pipeline_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.shader = shader;
    desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    desc.depth.write_enabled = false;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].write_mask = SG_COLORMASK_RGBA;
    return sgl_make_pipeline(&desc);
}

/* A bitmap font's block in 3D, page by page, in the recording's modelview. */
static void bitmap_draw_block_3d(const char *text, float wrap_width, block_draw_t *ctx)
{
    float widest = 0.0f;
    int page;
    if (bitmap_pipeline_3d.id == SG_INVALID_ID) {
        sg_shader none;
        memset(&none, 0, sizeof(none));
        bitmap_pipeline_3d = make_pipeline_3d(none);
    }
    sgl_push_pipeline();
    sgl_load_pipeline(bitmap_pipeline_3d);
    for (page = 0; page < sel.bitmap->page_count; page++) {
        sg_view view;
        sg_sampler sampler;
        if (!page_binding(page, &view, &sampler)) continue;
        ctx->page = page;
        sgl_enable_texture();
        sgl_texture(view, sampler);
        sgl_begin_triangles();
        set_color(sel.color);
        walk_lines(text, text + strlen(text), wrap_width, &widest, draw_line_quads, ctx);
        sgl_end();
        sgl_disable_texture();
    }
    sgl_pop_pipeline();
}

void wgf_gfx_priv_font_draw_block_3d(wgf_handle_t font, const char *text, float size, wgf_color_t color,
                                    float wrap_width, wgf_text_halign_t halign, wgf_text_valign_t valign,
                                    const float *matrix)
{
    const float scale = size > 0.0f ? WGF_GFX_PRIV_FONT_RASTER_3D / size : 0.0f;
    float widest = 0.0f, line_height = 0.0f, width;
    block_draw_t ctx;
    sg_view atlas = {SG_INVALID_ID};
    sg_sampler sampler = {SG_INVALID_ID};
    sg_shader shader = {SG_INVALID_ID};
    int lines;

    if (text == NULL || scale <= 0.0f || !use_font(font, size, scale, color)) return;
    if (sel.bitmap == NULL && !wgf_gfx_priv_fontstash_render_state(fons, &atlas, &sampler, &shader)) return;
    lines = block_lines(text, wrap_width * scale, &widest, &line_height, scale);
    if (lines == 0) return;
    width = box_width(widest, wrap_width * scale);
    ctx.left = align_offset(width, (int)halign);
    ctx.top = align_offset((float)lines * line_height, (int)valign);
    ctx.line_height = line_height;
    ctx.box_width = width;
    ctx.halign = halign;
    ctx.page = -1;

    sgl_matrix_mode_modelview();
    sgl_push_matrix();
    sgl_load_matrix(matrix);
    sgl_scale(1.0f / scale, 1.0f / scale, 1.0f);
    if (sel.bitmap != NULL) {
        bitmap_draw_block_3d(text, wrap_width * scale, &ctx);
        sgl_pop_matrix();
        return;
    }
    if (pipeline_3d.id == SG_INVALID_ID || pipeline_3d_shader.id != shader.id) {
        if (pipeline_3d.id != SG_INVALID_ID) sgl_destroy_pipeline(pipeline_3d);
        pipeline_3d = make_pipeline_3d(shader);
        pipeline_3d_shader = shader;
    }
    sgl_push_pipeline();
    sgl_load_pipeline(pipeline_3d);
    sgl_enable_texture();
    sgl_texture(atlas, sampler);
    sgl_begin_triangles();
    set_color(color);
    walk_lines(text, text + strlen(text), wrap_width * scale, &widest, draw_line_quads, &ctx);
    sgl_end();
    sgl_disable_texture();
    sgl_pop_pipeline();
    sgl_pop_matrix();
    /* the glyphs the quads rasterized into fontstash's atlas go to sokol_fontstash's
       texture only when fontstash flushes, which fonsDrawText does and its iterator
       doesn't: an empty string flushes them, drawing nothing (libwgt's fix of wgrender's
       text3d, which showed only in frames that also drew 2D text) */
    fonsDrawText(fons, 0.0f, 0.0f, "", NULL);
}

/* Glyphs for a caller's own meshes (the ui module's documents): a line's metrics, its
 * width, and its glyphs' quads, laid out at the frame's pixel density as the text above is
 * and measured back in logical pixels. */
bool wgf_gfx_priv_font_vertical_metrics(wgf_handle_t font, float size, float *ascender, float *descender,
                                        float *line_height)
{
    const float scale = pixel_scale();
    float a = 0.0f, d = 0.0f, h = 0.0f;
    if (!use_font(font, size, scale, 0xFFFFFFFFu)) return false;
    vertical_metrics(&a, &d, &h);
    *ascender = a / scale;
    *descender = -d / scale;
    *line_height = h / scale;
    return true;
}

float wgf_gfx_priv_font_span_width(wgf_handle_t font, const char *start, const char *end, float size)
{
    const float scale = pixel_scale();
    if (start == NULL || end <= start || !use_font(font, size, scale, 0xFFFFFFFFu)) return 0.0f;
    return span_width(start, end) / scale;
}

typedef struct quads_out_t {
    float *out;
    int count, most;
    float scale;
} quads_out_t;

static void add_quad(const float q[8], void *user)
{
    quads_out_t *quads = (quads_out_t *)user;
    float *o;
    int k;
    if (quads->count >= quads->most) return;
    o = quads->out + 8 * quads->count++;
    for (k = 0; k < 4; k++) o[k] = q[k] / quads->scale;
    for (k = 4; k < 8; k++) o[k] = q[k];
}

int wgf_gfx_priv_font_quads(wgf_handle_t font, const char *start, const char *end, float size, float x,
                            float baseline, int page, float *out, int max_quads)
{
    const float scale = pixel_scale();
    FONStextIter iter;
    FONSquad quad;
    int n = 0;
    if (start == NULL || end <= start || out == NULL || !use_font(font, size, scale, 0xFFFFFFFFu)) return 0;
    if (sel.bitmap != NULL) {
        quads_out_t quads;
        quads.out = out;
        quads.count = 0;
        quads.most = max_quads;
        quads.scale = scale;
        bitmap_run(start, end, x * scale, baseline * scale - (float)sel.bitmap->base * sel.scale, page, add_quad,
                   &quads);
        return quads.count;
    }
    if (page > 0) return 0; /* fontstash's glyphs are all on its atlas, page 0 */
    fonsSetAlign(fons, FONS_ALIGN_LEFT | FONS_ALIGN_BASELINE);
    fonsTextIterInit(fons, &iter, x * scale, baseline * scale, start, end);
    while (n < max_quads && fonsTextIterNext(fons, &iter, &quad)) {
        float *q = out + 8 * n;
        if (quad.x1 <= quad.x0 || quad.y1 <= quad.y0) continue; /* a space */
        q[0] = quad.x0 / scale;
        q[1] = quad.y0 / scale;
        q[2] = quad.x1 / scale;
        q[3] = quad.y1 / scale;
        q[4] = quad.s0;
        q[5] = quad.t0;
        q[6] = quad.s1;
        q[7] = quad.t1;
        n++;
    }
    /* the glyphs rasterized go to sokol_fontstash's texture only when fontstash flushes
       (text in 3D's fix, above): an empty string flushes them, drawing nothing */
    fonsDrawText(fons, 0.0f, 0.0f, "", NULL);
    return n;
}

int wgf_gfx_priv_font_page_count(wgf_handle_t font)
{
    const font_t *font_ptr = drawn_font(font);
    return font_ptr != NULL && font_ptr->bitmap != NULL ? font_ptr->bitmap->page_count : 1;
}

wgf_texture_t wgf_gfx_priv_font_page(wgf_handle_t font, int page)
{
    const font_t *font_ptr = drawn_font(font);
    if (font_ptr == NULL || font_ptr->bitmap == NULL || page < 0 || page >= font_ptr->bitmap->page_count) return 0;
    return font_ptr->pages[page];
}

bool wgf_gfx_priv_font_atlas(sg_view *atlas, sg_sampler *sampler, sgl_pipeline *pipeline)
{
    sg_shader shader;
    return ensure() && wgf_gfx_priv_fontstash_render_state(fons, atlas, sampler, &shader) &&
           wgf_gfx_priv_fontstash_pipeline(fons, pipeline);
}

bool wgf_gfx_priv_font_shader(sg_shader *shader)
{
    sg_view atlas;
    sg_sampler sampler;
    return ensure() && wgf_gfx_priv_fontstash_render_state(fons, &atlas, &sampler, shader);
}

unsigned wgf_gfx_priv_font_atlas_version(void)
{
    return atlas_version;
}

/* A full atlas grows, but never in the middle of a frame: draws recorded earlier
 * refer to the atlas and to coordinates for its size, and growing remakes both. So
 * a full atlas only asks to grow, the glyphs that didn't fit skip this one frame,
 * and it grows once the frame is drawn, the smaller side doubling. */
static void on_fons_error(void *user, int error, int value)
{
    (void)user;
    (void)value;
    if (error == FONS_ATLAS_FULL) grow_pending = true;
    else wgf_log_warn("wgf_gfx_font: fontstash error %d", error);
}

static void setup(void)
{
    sfons_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.width = ATLAS_START;
    desc.height = ATLAS_START;
    fons = sfons_create(&desc);
    if (fons == NULL) {
        wgf_log_error("wgf_gfx_font: no fontstash; no text will draw");
        return;
    }
    fonsSetErrorCallback(fons, on_fons_error, NULL);
    builtin_id = fonsAddFontMem(fons, "builtin", (unsigned char *)jetbrains_mono_ascii_ttf,
                                (int)sizeof(jetbrains_mono_ascii_ttf), 0);
    if (builtin_id == FONS_INVALID) wgf_log_error("wgf_gfx_font: the built-in font didn't load");
    grow_pending = false;
    full_warned = false;
    atlas_version++;
}

static void flush(void)
{
    if (fons != NULL) sfons_flush(fons);
}

static void end_frame(void)
{
    int width = 0, height = 0;
    if (!grow_pending || fons == NULL) return;
    grow_pending = false;
    fonsGetAtlasSize(fons, &width, &height);
    if (width < ATLAS_MOST || height < ATLAS_MOST) {
        const bool grow_width = width <= height && width < ATLAS_MOST;
        const int new_width = grow_width ? width * 2 : width, new_height = grow_width ? height : height * 2;
        if (fonsExpandAtlas(fons, new_width, new_height)) {
            atlas_version++;
            wgf_log_info("wgf_gfx_font: the glyph atlas grew to %dx%d", new_width, new_height);
            return;
        }
    }
    if (!full_warned) {
        wgf_log_warn("wgf_gfx_font: the glyph atlas is full at %dx%d; glyphs that don't fit aren't drawn", width,
                    height);
        full_warned = true;
    }
}

static void shutdown(void)
{
    int p;
    if (default_font != 0) {
        wgf_resource_release(default_font);
        default_font = 0;
    }
    if (fons != NULL) { /* first: nothing reads the fonts' bytes after this */
        sfons_destroy(fons);
        fons = NULL;
    }
    if (pool_ready) { /* fontstash is gone: their bytes are freed, not parked */
        wgf_core_priv_resource_unregister(&font_pool);
        wgf_core_priv_handle_pool_destroy(&font_pool);
        pool_ready = false;
    }
    if (bitmap_pipeline_3d.id != SG_INVALID_ID) sgl_destroy_pipeline(bitmap_pipeline_3d);
    bitmap_pipeline_3d.id = SG_INVALID_ID;
    for (p = 0; p < parked_count; p++) free(parked[p].bytes);
    free(parked);
    parked = NULL;
    parked_count = 0;
    parked_capacity = 0;
    builtin_id = FONS_INVALID;
}

/* ------------------------------------------------------------- the part ---- */

/* The GPU's context given back: fontstash's atlas sent again from its copy on the CPU at the
 * next flush, and each bitmap font's pages read again from its file. */
static void restore(void)
{
    uint16_t i;
    if (fons != NULL) wgf_gfx_priv_fontstash_restore(fons);
    for (i = 1; pool_ready && i < font_pool.capacity; i++) {
        const wgf_font_t font = wgf_core_priv_handle_pool_handle_from_index(&font_pool, i);
        if (font != 0 && fonts[i].bitmap != NULL && pages_again != NULL) {
            wgf_core_priv_resource_load_again(font, pages_again);
        }
    }
}

static wgf_core_priv_part_t part = {.name = "text",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_TEXT,
                                    .flush = flush,
                                    .end_frame = end_frame,
                                    .restore = restore,
                                    .stop = shutdown};

/* Text joins the frame (render/wgf_gfx_part_priv.h), and fontstash is made once gfx
 * runs; false while there is no fontstash. */
static bool ensure(void)
{
    wgf_core_priv_part_install(&part);
    if (fons == NULL && wgf_gfx_priv_render_is_running() && !wgf_gfx_priv_render_is_lost()) setup();
    return fons != NULL;
}
