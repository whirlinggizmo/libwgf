#include "wgf_font.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fontstash.h"
#include "jetbrains_mono_ascii.h"
#include "wgf_core_part_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl and sokol_fontstash need it first */
#include "text/wgf_gfx_font_priv.h"
#include "util/sokol_gl.h"
#include "util/sokol_fontstash.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"
#include "wgf_render.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */

/* Fonts: a resource loaded on create, its bytes read on a worker and handed to
 * fontstash on the main thread. fontstash can't let go of a font, so a font
 * released for the last time is parked, bytes and all, and a later create of the
 * same file takes it back at once. Cribbed from wgrender's wgr_font and wgr_text. */

#define ATLAS_START 1024
#define ATLAS_MOST 4096 /* 16 MB of 8-bit coverage at most */

typedef struct font_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    int fons_id;
    unsigned char *bytes; /* fontstash reads them for as long as it has the font */
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

/* The fontstash font to draw `font` with: its own when ready, else the default's,
 * else the built-in one. */
static int fons_id_of(wgf_handle_t font)
{
    const font_t *font_ptr = font_of(font != 0 ? font : default_font);
    if (font_ptr != NULL && font_ptr->resource.status == WGF_RESOURCE_STATUS_READY) return font_ptr->fons_id;
    font_ptr = font_of(default_font);
    if (font_ptr != NULL && font_ptr->resource.status == WGF_RESOURCE_STATUS_READY) return font_ptr->fons_id;
    return builtin_id;
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

static const wgf_core_priv_loader_t loader = {"font", prepare, finish, discard, fail, NULL};

/* --- the public API --------------------------------------------------------- */

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

static void init_font(void *record)
{
    ((font_t *)record)->fons_id = FONS_INVALID;
}

/* What a record holds past the header. fontstash keeps a font it was given, so a READY
 * one is parked, for a later create of its path to take back; its bytes are freed
 * only once fontstash is gone. */
static void free_font(wgf_handle_t font, void *record)
{
    font_t *font_ptr = (font_t *)record;
    (void)font;
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
static bool use_font(wgf_handle_t font, float size, float scale, wgf_color_t color)
{
    if (!ensure()) return false;
    fonsClearState(fons);
    fonsSetFont(fons, fons_id_of(font));
    fonsSetSize(fons, (size > 0.0f ? size : WGF_GFX_PRIV_FONT_DEFAULT_SIZE) * scale);
    fonsSetColor(fons, sfons_rgba((uint8_t)wgf_color_get_red(color), (uint8_t)wgf_color_get_green(color),
                                  (uint8_t)wgf_color_get_blue(color), (uint8_t)wgf_color_get_alpha(color)));
    fonsSetAlign(fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    return true;
}

/* Advance width of text[start, end) in the selected font's pixels. */
static float span_width(const char *start, const char *end)
{
    float bounds[4] = {0};
    return end > start ? fonsTextBounds(fons, 0.0f, 0.0f, start, end, bounds) : 0.0f;
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
static int block_lines(const char *text, float max_width, float *widest, float *line_height)
{
    fonsVertMetrics(fons, NULL, NULL, line_height);
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
    lines = block_lines(text, 0.0f, &widest, &line_height);
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
    lines = block_lines(text, wrap_width * scale, &widest, &line_height);
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

typedef struct block_draw_t {
    float left, top, line_height, box_width; /* in the font's (rasterized) pixels */
    wgf_text_halign_t halign;
} block_draw_t;

static void draw_line(const char *start, const char *end, int index, void *user)
{
    const block_draw_t *ctx = (const block_draw_t *)user;
    float x = ctx->left;
    if (end <= start) return;
    if (ctx->halign == WGF_TEXT_HALIGN_CENTER) x += (ctx->box_width - span_width(start, end)) * 0.5f;
    else if (ctx->halign == WGF_TEXT_HALIGN_RIGHT) x += ctx->box_width - span_width(start, end);
    fonsDrawText(fons, x, ctx->top + (float)index * ctx->line_height, start, end);
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
    lines = block_lines(text, wrap_width * scale, &widest, &line_height);
    if (lines == 0) return;
    width = box_width(widest, wrap_width * scale);
    ctx.left = align_offset(width, (int)halign);
    ctx.top = align_offset((float)lines * line_height, (int)valign);
    ctx.line_height = line_height;
    ctx.box_width = width;
    ctx.halign = halign;

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

static void draw_line_quads(const char *start, const char *end, int index, void *user)
{
    const block_draw_t *ctx = (const block_draw_t *)user;
    float x = ctx->left;
    FONStextIter iter;
    FONSquad quad;
    if (end <= start) return;
    if (ctx->halign == WGF_TEXT_HALIGN_CENTER) x += (ctx->box_width - span_width(start, end)) * 0.5f;
    else if (ctx->halign == WGF_TEXT_HALIGN_RIGHT) x += ctx->box_width - span_width(start, end);
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

void wgf_gfx_priv_font_draw_block_3d(wgf_handle_t font, const char *text, float size, wgf_color_t color,
                                    float wrap_width, wgf_text_halign_t halign, wgf_text_valign_t valign,
                                    const float *matrix)
{
    const float scale = size > 0.0f ? WGF_GFX_PRIV_FONT_RASTER_3D / size : 0.0f;
    float widest = 0.0f, line_height = 0.0f, width;
    block_draw_t ctx;
    sg_view atlas;
    sg_sampler sampler;
    sg_shader shader;
    int lines;

    if (text == NULL || scale <= 0.0f || !use_font(font, size, scale, color) ||
        !wgf_gfx_priv_fontstash_render_state(fons, &atlas, &sampler, &shader)) {
        return;
    }
    lines = block_lines(text, wrap_width * scale, &widest, &line_height);
    if (lines == 0) return;
    if (pipeline_3d.id == SG_INVALID_ID || pipeline_3d_shader.id != shader.id) {
        sg_pipeline_desc desc;
        if (pipeline_3d.id != SG_INVALID_ID) sgl_destroy_pipeline(pipeline_3d);
        memset(&desc, 0, sizeof(desc));
        desc.shader = shader;
        desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        desc.depth.write_enabled = false;
        desc.colors[0].blend.enabled = true;
        desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        desc.colors[0].write_mask = SG_COLORMASK_RGBA;
        pipeline_3d = sgl_make_pipeline(&desc);
        pipeline_3d_shader = shader;
    }
    width = box_width(widest, wrap_width * scale);
    ctx.left = align_offset(width, (int)halign);
    ctx.top = align_offset((float)lines * line_height, (int)valign);
    ctx.line_height = line_height;
    ctx.box_width = width;
    ctx.halign = halign;

    sgl_matrix_mode_modelview();
    sgl_push_matrix();
    sgl_load_matrix(matrix);
    sgl_scale(1.0f / scale, 1.0f / scale, 1.0f);
    sgl_push_pipeline();
    sgl_load_pipeline(pipeline_3d);
    sgl_enable_texture();
    sgl_texture(atlas, sampler);
    sgl_begin_triangles();
    sgl_c4b((uint8_t)wgf_color_get_red(color), (uint8_t)wgf_color_get_green(color), (uint8_t)wgf_color_get_blue(color),
            (uint8_t)wgf_color_get_alpha(color));
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
    for (p = 0; p < parked_count; p++) free(parked[p].bytes);
    free(parked);
    parked = NULL;
    parked_count = 0;
    parked_capacity = 0;
    builtin_id = FONS_INVALID;
}

/* ------------------------------------------------------------- the part ---- */

static wgf_core_priv_part_t part = {.name = "text",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_TEXT,
                                    .flush = flush,
                                    .end_frame = end_frame,
                                    .stop = shutdown};

/* Text joins the frame (render/wgf_gfx_part_priv.h), and fontstash is made once gfx
 * runs; false while there is no fontstash. */
static bool ensure(void)
{
    wgf_core_priv_part_install(&part);
    if (fons == NULL && wgf_gfx_priv_render_is_running()) setup();
    return fons != NULL;
}
