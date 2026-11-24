#include "wgf_cubemap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "material/wgf_gfx_shader_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h"
#include "texture/wgf_gfx_cubemap_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"

/* Cubemaps, raylib's LoadTextureCubemap: six square faces on the GPU, an RGBA8 cube image
 * with its mipmaps, which a custom shader samples as samplerCube. From one image in a layout
 * (a loader a layout, so the worker knows how to cut it: decoded with the texture loader's
 * stb_image, cut into faces, and its mipmaps built there; in a page the browser decodes it,
 * and it is cut in finish once it has), or from six files, each a request
 * of the same resource (decoded, checked square, and its mipmaps built on a worker; kept by
 * the record as each finishes, and the cube made by the last). A part, installed by the first
 * cubemap made, which sets the custom materials' hook for binding one, so a program with none
 * links none of it. Loaded again in place (wgf_asset_reload): one image through the kind's
 * loader, its layout read again from its shape; six files by the kind's reload hook, a request
 * a face again, the cube made again once all six are in and none failed. */

#define MAX_SIZE 8192 /* a face's side, at most: the GPUs libwgf runs on take this */
#define CHECKER 8     /* the FAILED placeholder's face, in texels */

/* A cube's pixels, or one face's: `faces` square faces of `size` a level, each level half the
 * last (at least 1), each holding its faces one after another. */
typedef struct pixels_t {
    int size;
    int faces;
    int mip_count;
    unsigned char *levels[WGF_GFX_PRIV_MAX_MIPMAPS];
    wgf_cubemap_layout_t layout; /* what an image was read as */
} pixels_t;

/* While six files load: each face's pixels as its request finishes, and their files. */
typedef struct faces_t {
    pixels_t *face[6];
    char paths[6][WGF_CORE_PRIV_FS_PATH_MAX];
} faces_t;

typedef struct cubemap_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    sg_image image;
    sg_view view;
    int size;
    wgf_cubemap_layout_t layout; /* asked, then, READY, what it was read as */
    bool made;                   /* its first create has said its layout */
    faces_t *faces;              /* six files: their paths, and their pixels while they load */
    int again;                   /* six files loading again: the requests not yet in */
    bool again_failed;           /* and one of them didn't load: what it had is kept */
} cubemap_t;

static bool pool_ready;
static wgf_core_priv_handle_pool_t pool;
static cubemap_t *cubemaps; /* grown by the pool: don't hold a pointer across a create */
static sg_image checker_image;
static sg_view checker_view;

static cubemap_t *cubemap_of_at(wgf_cubemap_t cubemap, const char *caller)
{
    uint16_t index;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve_at(&pool, cubemap, &index, caller)) return NULL;
    return &cubemaps[index];
}
#define cubemap_of(cubemap) cubemap_of_at((cubemap), WGF_CORE_PRIV_CALLER)

/* ------------------------------------------------------------- layouts ---- */

wgf_cubemap_layout_t wgf_gfx_priv_cubemap_layout_of(int width, int height, wgf_cubemap_layout_t layout, int *size)
{
    wgf_cubemap_layout_t found = WGF_CUBEMAP_LAYOUT_AUTO;
    int face = 0;
    if (width > 0 && height > 0) {
        if (width == height * 6) {
            found = WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL;
            face = height;
        } else if (height == width * 6) {
            found = WGF_CUBEMAP_LAYOUT_LINE_VERTICAL;
            face = width;
        } else if (width % 4 == 0 && width * 3 == height * 4) {
            found = WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE;
            face = width / 4;
        } else if (width % 3 == 0 && width * 4 == height * 3) {
            found = WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR;
            face = width / 3;
        }
    }
    if (layout != WGF_CUBEMAP_LAYOUT_AUTO && layout != found) found = WGF_CUBEMAP_LAYOUT_AUTO;
    *size = found != WGF_CUBEMAP_LAYOUT_AUTO ? face : 0;
    return found;
}

bool wgf_gfx_priv_cubemap_face_cell(wgf_cubemap_layout_t layout, int face, int *column, int *row)
{
    /* raylib's places, +X to -Z */
    static const int three_by_four[6][2] = {{1, 1}, {1, 3}, {1, 0}, {1, 2}, {0, 1}, {2, 1}};
    static const int four_by_three[6][2] = {{2, 1}, {0, 1}, {1, 0}, {1, 2}, {1, 1}, {3, 1}};
    if (face < 0 || face >= 6) return false;
    switch (layout) {
        case WGF_CUBEMAP_LAYOUT_LINE_VERTICAL: *column = 0, *row = face; return true;
        case WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL: *column = face, *row = 0; return true;
        case WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR:
            *column = three_by_four[face][0], *row = three_by_four[face][1];
            return true;
        case WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE:
            *column = four_by_three[face][0], *row = four_by_three[face][1];
            return true;
        default: return false;
    }
}

void wgf_gfx_priv_cubemap_extract(const unsigned char *rgba, int width, wgf_cubemap_layout_t layout, int size,
                                  unsigned char *out)
{
    const size_t row_bytes = (size_t)size * 4;
    int face, column = 0, row = 0, y;
    for (face = 0; face < 6; face++) {
        if (!wgf_gfx_priv_cubemap_face_cell(layout, face, &column, &row)) return;
        for (y = 0; y < size; y++) {
            const size_t from = (((size_t)row * (size_t)size + (size_t)y) * (size_t)width + (size_t)column * size) * 4;
            memcpy(out + ((size_t)face * size + (size_t)y) * row_bytes, rgba + from, row_bytes);
        }
    }
}

/* -------------------------------------------------------------- pixels ---- */

static void pixels_free(pixels_t *pixels)
{
    int m;
    if (pixels == NULL) return;
    for (m = 0; m < pixels->mip_count; m++) free(pixels->levels[m]);
    free(pixels);
}

static size_t level_bytes(const pixels_t *pixels, int level)
{
    const size_t s = (size_t)(pixels->size >> level > 0 ? pixels->size >> level : 1);
    return (size_t)pixels->faces * s * s * 4;
}

/* Levels 1 on, each face's 2x2 texels of the last averaged, in the stored color space, as a
 * texture's mipmaps are. False for no memory. */
static bool build_mipmaps(pixels_t *pixels)
{
    while (pixels->size >> (pixels->mip_count - 1) > 1 && pixels->mip_count < WGF_GFX_PRIV_MAX_MIPMAPS) {
        const int src = pixels->size >> (pixels->mip_count - 1), dst = src / 2;
        const unsigned char *from = pixels->levels[pixels->mip_count - 1];
        unsigned char *to = (unsigned char *)malloc((size_t)pixels->faces * (size_t)dst * (size_t)dst * 4);
        int f, x, y, c;
        if (to == NULL) return false;
        for (f = 0; f < pixels->faces; f++) {
            const unsigned char *s = from + (size_t)f * src * src * 4;
            unsigned char *d = to + (size_t)f * dst * dst * 4;
            for (y = 0; y < dst; y++) {
                for (x = 0; x < dst; x++) {
                    const int x0 = 2 * x, y0 = 2 * y, x1 = x0 + 1 < src ? x0 + 1 : x0, y1 = y0 + 1 < src ? y0 + 1 : y0;
                    for (c = 0; c < 4; c++) {
                        const int sum = s[(y0 * src + x0) * 4 + c] + s[(y0 * src + x1) * 4 + c] +
                                        s[(y1 * src + x0) * 4 + c] + s[(y1 * src + x1) * 4 + c];
                        d[(y * dst + x) * 4 + c] = (unsigned char)((sum + 2) / 4);
                    }
                }
            }
        }
        pixels->levels[pixels->mip_count++] = to;
    }
    return true;
}

/* A file's prepared data: its image while it decodes (the browser's, in a page, waited for in
 * finish), then its pixels, cut into six faces (a whole cube's) or one (a face's file), with
 * their mipmaps, until its finish takes them. */
typedef struct prepared_t {
    void *decoded;
    pixels_t *pixels;
    wgf_cubemap_layout_t layout; /* a whole cube's, as asked; FACES for a face's file */
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
} prepared_t;

/* An image in one of the layouts, decoded (`rgba`, `width` by `height`), cut into faces with
 * their mipmaps; NULL (warned) when its shape isn't the layout's. */
static pixels_t *cut_image(const unsigned char *rgba, int width, int height, const char *path,
                           wgf_cubemap_layout_t layout)
{
    int size = 0;
    pixels_t *pixels;
    wgf_cubemap_layout_t found;
    found = wgf_gfx_priv_cubemap_layout_of(width, height, layout, &size);
    if (found == WGF_CUBEMAP_LAYOUT_AUTO || size > MAX_SIZE) {
        wgf_log_warn("wgf_gfx_cubemap: %s: a %d by %d image isn't %s", path, width, height,
                     size > MAX_SIZE ? "a cubemap of faces 8192 pixels or smaller"
                     : layout == WGF_CUBEMAP_LAYOUT_AUTO
                         ? "a cubemap's layout (6 by 1, 1 by 6, 4 by 3, or 3 by 4 faces)"
                         : "the layout's shape");
        return NULL;
    }
    pixels = (pixels_t *)calloc(1, sizeof(pixels_t));
    if (pixels != NULL) {
        pixels->size = size;
        pixels->faces = 6;
        pixels->layout = found;
        pixels->levels[0] = (unsigned char *)malloc(level_bytes(pixels, 0));
        pixels->mip_count = pixels->levels[0] != NULL ? 1 : 0;
        if (pixels->mip_count == 1) wgf_gfx_priv_cubemap_extract(rgba, width, found, size, pixels->levels[0]);
        if (pixels->mip_count == 0 || !build_mipmaps(pixels)) {
            wgf_log_warn("wgf_gfx_cubemap: %s: out of memory", path);
            pixels_free(pixels);
            pixels = NULL;
        }
    }
    return pixels;
}

/* One face's file, decoded, square, with its mipmaps; NULL (warned) otherwise. */
static pixels_t *cut_face(const unsigned char *rgba, int width, int height, const char *path)
{
    pixels_t *pixels;
    if (width != height || width > MAX_SIZE) {
        wgf_log_warn("wgf_gfx_cubemap: %s: a face is square, 8192 pixels or smaller: this is %d by %d", path, width,
                     height);
        return NULL;
    }
    pixels = (pixels_t *)calloc(1, sizeof(pixels_t));
    if (pixels != NULL) {
        pixels->size = width;
        pixels->faces = 1;
        pixels->layout = WGF_CUBEMAP_LAYOUT_FACES;
        pixels->levels[0] = (unsigned char *)malloc(level_bytes(pixels, 0));
        pixels->mip_count = pixels->levels[0] != NULL ? 1 : 0;
        if (pixels->mip_count == 1) memcpy(pixels->levels[0], rgba, level_bytes(pixels, 0));
        if (pixels->mip_count == 0 || !build_mipmaps(pixels)) {
            wgf_log_warn("wgf_gfx_cubemap: %s: out of memory", path);
            pixels_free(pixels);
            pixels = NULL;
        }
    }
    return pixels;
}

static void discard(void *data)
{
    prepared_t *prepared = (prepared_t *)data;
    if (prepared == NULL) return;
    wgf_gfx_priv_texture_decoded_free(prepared->decoded);
    pixels_free(prepared->pixels);
    free(prepared);
}

/* Its decode done: its pixels cut from it, the image let go of. FAILED (warned) when the
 * decode did, or the image isn't what it should be; PENDING while the browser decodes it. */
static wgf_core_priv_load_step_t cut(prepared_t *prepared)
{
    const unsigned char *rgba;
    int width = 0, height = 0;
    wgf_gfx_priv_decode_state_t state;
    if (prepared->pixels != NULL) return WGF_CORE_PRIV_LOAD_DONE;
    state = wgf_gfx_priv_texture_decoded_wait(prepared->decoded, prepared->path);
    if (state == WGF_GFX_PRIV_DECODE_PENDING) return WGF_CORE_PRIV_LOAD_WAIT;
    rgba = state == WGF_GFX_PRIV_DECODE_DONE ? wgf_gfx_priv_texture_decoded_pixels(prepared->decoded, &width, &height)
                                              : NULL;
    if (rgba != NULL) {
        prepared->pixels = prepared->layout == WGF_CUBEMAP_LAYOUT_FACES
                               ? cut_face(rgba, width, height, prepared->path)
                               : cut_image(rgba, width, height, prepared->path, prepared->layout);
    }
    wgf_gfx_priv_texture_decoded_free(prepared->decoded);
    prepared->decoded = NULL;
    return prepared->pixels != NULL ? WGF_CORE_PRIV_LOAD_DONE : WGF_CORE_PRIV_LOAD_FAILED;
}

/* The file at `path` read and its decode started, on a worker: natively decoded at once and
 * cut there; in a page the browser's decode, waited for and cut in finish. NULL (warned)
 * when it can't be read, decoded, or cut. */
static void *prepare(const char *path, wgf_cubemap_layout_t layout)
{
    unsigned char *bytes;
    int size;
    prepared_t *prepared;
    if (!wgf_core_priv_fs_read(path, &bytes, &size)) {
        wgf_log_warn("wgf_gfx_cubemap: %s: couldn't be read", path);
        return NULL;
    }
    prepared = (prepared_t *)calloc(1, sizeof(prepared_t));
    if (prepared != NULL) {
        prepared->layout = layout;
        snprintf(prepared->path, sizeof(prepared->path), "%s", path);
        prepared->decoded = wgf_gfx_priv_texture_decode_base(bytes, size, path);
    } else {
        wgf_log_warn("wgf_gfx_cubemap: %s: out of memory", path);
    }
    wgf_core_priv_fs_read_free(bytes);
    if (prepared == NULL || prepared->decoded == NULL || cut(prepared) == WGF_CORE_PRIV_LOAD_FAILED) {
        discard(prepared);
        return NULL;
    }
    return prepared;
}

/* ------------------------------------------------------------ the GPU ---- */

/* A cube image of six faces' `pixels` and a view of it; false when the GPU didn't take it. */
static bool make_image(const pixels_t *pixels, const char *label, sg_image *image, sg_view *view)
{
    sg_image_desc desc;
    sg_view_desc view_desc;
    size_t sizes[WGF_GFX_PRIV_MAX_MIPMAPS];
    int m;
    memset(&desc, 0, sizeof(desc));
    desc.type = SG_IMAGETYPE_CUBE;
    desc.width = desc.height = pixels->size;
    desc.num_mipmaps = pixels->mip_count;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.label = label;
    for (m = 0; m < pixels->mip_count; m++) sizes[m] = level_bytes(pixels, m);
    *image = wgf_gfx_priv_render_make_image(&desc, (const void *const *)pixels->levels, sizes);
    if (sg_query_image_state(*image) != SG_RESOURCESTATE_VALID) {
        sg_destroy_image(*image);
        return false;
    }
    memset(&view_desc, 0, sizeof(view_desc));
    view_desc.texture.image = *image;
    *view = sg_make_view(&view_desc);
    return true;
}

/* What a custom material binds for `cubemap` (the hook this part sets): its view READY, a
 * magenta and black checker FAILED; false while PENDING, or for what isn't a cubemap. */
static bool bind(wgf_handle_t cubemap, sg_view *view)
{
    const cubemap_t *cubemap_ptr = cubemap_of(cubemap);
    if (cubemap_ptr == NULL) return false;
    if (cubemap_ptr->resource.status == WGF_RESOURCE_STATUS_READY) {
        *view = cubemap_ptr->view;
        return true;
    }
    if (cubemap_ptr->resource.status != WGF_RESOURCE_STATUS_FAILED) return false;
    if (checker_image.id == SG_INVALID_ID) {
        static unsigned char texels[6 * CHECKER * CHECKER * 4];
        pixels_t checker;
        int i;
        for (i = 0; i < 6 * CHECKER * CHECKER; i++) {
            const int x = i % CHECKER, y = (i / CHECKER) % CHECKER, magenta = ((x / 2) + (y / 2)) % 2 == 0;
            texels[i * 4 + 0] = texels[i * 4 + 2] = magenta ? 255 : 0;
            texels[i * 4 + 1] = 0;
            texels[i * 4 + 3] = 255;
        }
        memset(&checker, 0, sizeof(checker));
        checker.size = CHECKER;
        checker.faces = 6;
        checker.mip_count = 1;
        checker.levels[0] = texels;
        if (!make_image(&checker, "wgf-cubemap-checker", &checker_image, &checker_view)) return false;
    }
    *view = checker_view;
    return true;
}

/* ------------------------------------------------------------- loaders ---- */

/* READY with `pixels`: its cube made, its size and layout kept. */
static wgf_core_priv_load_step_t finish_with(const pixels_t *pixels, wgf_handle_t resource, const char *found)
{
    cubemap_t *cubemap_ptr = cubemap_of(resource);
    sg_image image = {SG_INVALID_ID};
    sg_view view = {SG_INVALID_ID};
    if (cubemap_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (!make_image(pixels, "wgf-cubemap", &image, &view)) {
        wgf_log_warn("wgf_gfx_cubemap: %s: the GPU didn't take it (faces of %d)", cubemap_ptr->resource.found,
                     pixels->size);
        return WGF_CORE_PRIV_LOAD_FAILED;
    }
    wgf_gfx_priv_destroy_later(cubemap_ptr->view, cubemap_ptr->image); /* made again: the old */
    cubemap_ptr->image = image;
    cubemap_ptr->view = view;
    cubemap_ptr->size = pixels->size;
    cubemap_ptr->layout = pixels->layout;
    wgf_core_priv_resource_loaded(resource, found);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static wgf_core_priv_load_step_t finish_image(void *data, wgf_handle_t resource)
{
    prepared_t *prepared = (prepared_t *)data;
    const wgf_core_priv_load_step_t step = cut(prepared); /* in a page, once the browser has decoded it */
    if (step != WGF_CORE_PRIV_LOAD_DONE) return step;
    if (!wgf_gfx_priv_render_is_running()) return WGF_CORE_PRIV_LOAD_MORE; /* no GPU yet: wait for one */
    if (wgf_gfx_priv_render_is_lost()) return WGF_CORE_PRIV_LOAD_WAIT; /* the GPU's context lost: until it is back */
    return finish_with(prepared->pixels, resource, NULL);
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static void *prepare_auto(const char *path)
{
    return prepare(path, WGF_CUBEMAP_LAYOUT_AUTO);
}

static void *prepare_vertical(const char *path)
{
    return prepare(path, WGF_CUBEMAP_LAYOUT_LINE_VERTICAL);
}

static void *prepare_horizontal(const char *path)
{
    return prepare(path, WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL);
}

static void *prepare_three_by_four(const char *path)
{
    return prepare(path, WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR);
}

static void *prepare_four_by_three(const char *path)
{
    return prepare(path, WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE);
}

/* A layout's loader, by the layout (AUTO to CROSS_FOUR_BY_THREE). Loading again in place
 * (wgf_asset_reload) goes through the kind's loader, AUTO's, which reads the layout from the
 * image's shape: the one asked isn't known by the file's path alone. */
static const wgf_core_priv_loader_t image_loaders[5] = {
    {"cubemap", prepare_auto, finish_image, discard, fail, NULL, true, NULL},
    {"cubemap", prepare_vertical, finish_image, discard, fail, NULL, true, NULL},
    {"cubemap", prepare_horizontal, finish_image, discard, fail, NULL, true, NULL},
    {"cubemap", prepare_three_by_four, finish_image, discard, fail, NULL, true, NULL},
    {"cubemap", prepare_four_by_three, finish_image, discard, fail, NULL, true, NULL},
};

static void *prepare_face_file(const char *path)
{
    return prepare(path, WGF_CUBEMAP_LAYOUT_FACES);
}

/* The six faces of `faces`, all in, as one cube's pixels (the faces' levels side by side);
 * false (warned) when their sizes differ, or for no memory. */
static bool join_faces(const faces_t *faces, pixels_t *cube)
{
    int f, m;
    for (f = 1; f < 6; f++) {
        if (faces->face[f]->size != faces->face[0]->size) {
            wgf_log_warn("wgf_gfx_cubemap: %s is %d pixels a side, +X's face (%s) %d: the faces are one size",
                         faces->paths[f], faces->face[f]->size, faces->paths[0], faces->face[0]->size);
            return false;
        }
    }
    memset(cube, 0, sizeof(*cube));
    cube->size = faces->face[0]->size;
    cube->faces = 6;
    cube->layout = WGF_CUBEMAP_LAYOUT_FACES;
    for (m = 0; m < faces->face[0]->mip_count; m++) {
        const size_t bytes = level_bytes(faces->face[0], m);
        cube->levels[m] = (unsigned char *)malloc(bytes * 6);
        if (cube->levels[m] == NULL) {
            for (f = 0; f < m; f++) free(cube->levels[f]);
            wgf_log_warn("wgf_gfx_cubemap: %s: out of memory", faces->paths[0]);
            return false;
        }
        for (f = 0; f < 6; f++) memcpy(cube->levels[m] + bytes * (size_t)f, faces->face[f]->levels[m], bytes);
        cube->mip_count = m + 1;
    }
    return true;
}

/* Six files' pixels let go of, their paths kept (to read again: the GPU's context given back). */
static void free_face_pixels(cubemap_t *cubemap_ptr)
{
    int f;
    if (cubemap_ptr->faces == NULL) return;
    for (f = 0; f < 6; f++) {
        pixels_free(cubemap_ptr->faces->face[f]);
        cubemap_ptr->faces->face[f] = NULL;
    }
}

static void free_faces(cubemap_t *cubemap_ptr)
{
    free_face_pixels(cubemap_ptr);
    free(cubemap_ptr->faces);
    cubemap_ptr->faces = NULL;
}

/* Six files' loading again over, one of them not loaded: what it had kept (said once). */
static void again_failed(wgf_handle_t resource, cubemap_t *cubemap_ptr)
{
    free_face_pixels(cubemap_ptr);
    cubemap_ptr->again_failed = false;
    wgf_core_priv_resource_failed(resource); /* loading again: what was loaded is kept */
}

/* A face's file didn't load: FAILED, or, loading again, what it had kept once the other
 * faces' requests are in. */
static void fail_face(wgf_handle_t resource)
{
    cubemap_t *cubemap_ptr = cubemap_of(resource);
    if (cubemap_ptr == NULL || cubemap_ptr->again == 0) {
        wgf_core_priv_resource_failed(resource);
        return;
    }
    cubemap_ptr->again_failed = true;
    if (--cubemap_ptr->again == 0) again_failed(resource, cubemap_ptr);
}

/* Face `face` of six files in: kept by the record, and the cube made once all six are,
 * FAILED (warned) when their sizes differ. A face of a cubemap that has FAILED is dropped,
 * but while it loads again. Loading again, the cube is made once the six requests are in,
 * none failed; what it had is kept when one did, or when their sizes differ. */
static wgf_core_priv_load_step_t finish_face(void *data, wgf_handle_t resource, int face)
{
    prepared_t *face_ptr = (prepared_t *)data;
    cubemap_t *cubemap_ptr = cubemap_of(resource);
    pixels_t cube;
    wgf_core_priv_load_step_t step;
    char found[WGF_CORE_PRIV_FS_PATH_MAX];
    int f, m, arrived = 0;
    if (cubemap_ptr == NULL || cubemap_ptr->faces == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (cubemap_ptr->resource.status == WGF_RESOURCE_STATUS_FAILED && cubemap_ptr->again == 0) {
        return WGF_CORE_PRIV_LOAD_DONE; /* another face's */
    }
    step = cut(face_ptr); /* in a page, once the browser has decoded it */
    if (step != WGF_CORE_PRIV_LOAD_DONE) return step;
    if (!wgf_gfx_priv_render_is_running()) return WGF_CORE_PRIV_LOAD_MORE; /* no GPU yet: wait for one */
    if (wgf_gfx_priv_render_is_lost()) return WGF_CORE_PRIV_LOAD_WAIT; /* the GPU's context lost: until it is back */
    pixels_free(cubemap_ptr->faces->face[face]);
    cubemap_ptr->faces->face[face] = face_ptr->pixels; /* the record's now */
    face_ptr->pixels = NULL;
    if (cubemap_ptr->again > 0) { /* loading again: the cube made once the six are in */
        if (--cubemap_ptr->again > 0) return WGF_CORE_PRIV_LOAD_DONE;
        if (cubemap_ptr->again_failed) {
            again_failed(resource, cubemap_ptr);
            return WGF_CORE_PRIV_LOAD_DONE;
        }
    }
    for (f = 0; f < 6; f++) arrived += cubemap_ptr->faces->face[f] != NULL ? 1 : 0;
    if (arrived < 6) return WGF_CORE_PRIV_LOAD_DONE;
    if (!join_faces(cubemap_ptr->faces, &cube)) {
        free_face_pixels(cubemap_ptr); /* its paths kept: to load again */
        wgf_core_priv_resource_failed(resource); /* loading again, what was loaded is kept */
        return WGF_CORE_PRIV_LOAD_DONE;
    }
    memcpy(found, cubemap_ptr->faces->paths[0], sizeof(found));
    free_face_pixels(cubemap_ptr);
    step = finish_with(&cube, resource, found);
    for (m = 0; m < cube.mip_count; m++) free(cube.levels[m]);
    return step;
}

static wgf_core_priv_load_step_t finish_px(void *prepared, wgf_handle_t resource)
{
    return finish_face(prepared, resource, 0);
}

static wgf_core_priv_load_step_t finish_nx(void *prepared, wgf_handle_t resource)
{
    return finish_face(prepared, resource, 1);
}

static wgf_core_priv_load_step_t finish_py(void *prepared, wgf_handle_t resource)
{
    return finish_face(prepared, resource, 2);
}

static wgf_core_priv_load_step_t finish_ny(void *prepared, wgf_handle_t resource)
{
    return finish_face(prepared, resource, 3);
}

static wgf_core_priv_load_step_t finish_pz(void *prepared, wgf_handle_t resource)
{
    return finish_face(prepared, resource, 4);
}

static wgf_core_priv_load_step_t finish_nz(void *prepared, wgf_handle_t resource)
{
    return finish_face(prepared, resource, 5);
}

/* A face's loader, by the face: its request's finish says which it is. */
static const wgf_core_priv_loader_t face_loaders[6] = {
    {"cubemap face", prepare_face_file, finish_px, discard, fail_face, NULL, false, NULL},
    {"cubemap face", prepare_face_file, finish_nx, discard, fail_face, NULL, false, NULL},
    {"cubemap face", prepare_face_file, finish_py, discard, fail_face, NULL, false, NULL},
    {"cubemap face", prepare_face_file, finish_ny, discard, fail_face, NULL, false, NULL},
    {"cubemap face", prepare_face_file, finish_pz, discard, fail_face, NULL, false, NULL},
    {"cubemap face", prepare_face_file, finish_nz, discard, fail_face, NULL, false, NULL},
};

/* ----------------------------------------------------------- the part ---- */

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &image_loaders[WGF_CUBEMAP_LAYOUT_AUTO];
}

/* What a record holds past the header: its cube, once READY, and six files' faces. */
static void free_cubemap(wgf_handle_t cubemap, void *record)
{
    cubemap_t *cubemap_ptr = (cubemap_t *)record;
    (void)cubemap;
    wgf_gfx_priv_destroy_later(cubemap_ptr->view, cubemap_ptr->image);
    free_faces(cubemap_ptr);
}

/* The kind's reload hook (wgf_core_resource_priv.h): a cubemap of six files loaded again when
 * `key` is one of them, a request a face, fetched anew, the last making the cube; what it had
 * shown until then, and kept when a face doesn't load. One of one image: the usual, 0. */
static int reload(wgf_handle_t cubemap, void *record, const char *key, bool go)
{
    cubemap_t *cubemap_ptr = (cubemap_t *)record;
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
    int f;
    bool named = false;
    for (f = 0; cubemap_ptr->faces != NULL && f < 6 && !named; f++) {
        named = wgf_core_priv_fs_normalize_path(cubemap_ptr->faces->paths[f], path, sizeof(path)) &&
                strcmp(path, key) == 0;
    }
    if (!named || !go) return named ? 1 : 0;
    free_face_pixels(cubemap_ptr);
    cubemap_ptr->again = 6;
    cubemap_ptr->again_failed = false;
    cubemap_ptr->resource.reloading = true;
    for (f = 0; f < 6; f++) {
        char face[WGF_CORE_PRIV_FS_PATH_MAX];
        snprintf(face, sizeof(face), "%s", cubemap_of(cubemap)->faces->paths[f]);
        if (!wgf_core_priv_load_request_reload(&face_loaders[f], face, cubemap)) {
            wgf_log_warn("wgf_asset_reload: %s: nothing to load it with", face);
            cubemap_ptr = cubemap_of(cubemap); /* the requests made may have moved nothing, but ask again */
            cubemap_ptr->again -= 6 - f;
            cubemap_ptr->again_failed = true;
            if (cubemap_ptr->again == 0) again_failed(cubemap, cubemap_ptr);
            break;
        }
    }
    return 1;
}

static const wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_cubemap_create",
                                                            .loader = loader_of,
                                                            .free = free_cubemap,
                                                            .reload = reload};

static void stop(void)
{
    if (!pool_ready) return;
    wgf_core_priv_resource_unregister(&pool);
    wgf_core_priv_handle_pool_destroy(&pool);
    cubemaps = NULL;
    pool_ready = false;
    if (checker_image.id != SG_INVALID_ID) {
        sg_destroy_view(checker_view);
        sg_destroy_image(checker_image);
    }
    checker_image.id = SG_INVALID_ID;
    checker_view.id = SG_INVALID_ID;
    wgf_gfx_priv_cubemap_bind = NULL;
}

/* The GPU's context given back: every cube READY read again from its file or files (as
 * this run has them), blank meanwhile; its new cube made before the old goes. */
static void restore(void)
{
    uint16_t i;
    int f;
    for (i = 1; pool_ready && i < pool.capacity; i++) {
        const wgf_cubemap_t cubemap = wgf_core_priv_handle_pool_handle_from_index(&pool, i);
        cubemap_t *cubemap_ptr = &cubemaps[i];
        if (cubemap == 0 || cubemap_ptr->resource.status != WGF_RESOURCE_STATUS_READY) continue;
        if (cubemap_ptr->faces == NULL) {
            const int layout = (int)cubemap_ptr->layout;
            if (layout >= WGF_CUBEMAP_LAYOUT_AUTO && layout <= WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE) {
                wgf_core_priv_resource_load_again(cubemap, &image_loaders[layout]);
            }
            continue;
        }
        for (f = 0; f < 6; f++) { /* a request a face, the last making the cube */
            if (!wgf_core_priv_load_request(&face_loaders[f], cubemaps[i].faces->paths[f], cubemap)) break;
        }
    }
}

static wgf_core_priv_part_t part = {.name = "cubemaps",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_CUBEMAPS,
                                    .restore = restore,
                                    .stop = stop};

/* The pool and the part, at the first create; false when there is no room. */
static bool ensure_part(void)
{
    if (!pool_ready) {
        pool_ready = wgf_core_priv_handle_pool_init(&pool, WGF_CORE_PRIV_HANDLE_KIND_CUBEMAP, (void **)&cubemaps,
                                                    sizeof(cubemap_t), 8, 4096);
        if (!pool_ready) return false;
        wgf_core_priv_resource_register(&pool, &resource_kind);
    }
    wgf_core_priv_part_install(&part);
    wgf_gfx_priv_cubemap_bind = bind;
    wgf_asset_priv_install();
    return true;
}

/* ---------------------------------------------------------- public API ---- */

wgf_cubemap_t wgf_cubemap_create(const char *path, wgf_cubemap_layout_t layout)
{
    wgf_cubemap_t cubemap;
    cubemap_t *cubemap_ptr;
    if ((int)layout < WGF_CUBEMAP_LAYOUT_AUTO || layout > WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE) {
        wgf_log_error("wgf_cubemap_create: %d isn't a layout of one image (six files: wgf_cubemap_create_faces)",
                      (int)layout);
        return 0;
    }
    if (!ensure_part()) return 0;
    cubemap = wgf_core_priv_resource_create_with(WGF_CORE_PRIV_HANDLE_KIND_CUBEMAP, path, &image_loaders[layout]);
    cubemap_ptr = cubemap_of(cubemap);
    if (cubemap_ptr == NULL) return cubemap;
    if (!cubemap_ptr->made) {
        cubemap_ptr->made = true;
        cubemap_ptr->layout = layout;
    } else if (layout != WGF_CUBEMAP_LAYOUT_AUTO && cubemap_ptr->layout != layout) {
        wgf_log_warn("wgf_cubemap_create: %s was made with another layout, which it keeps", path != NULL ? path : "");
    }
    return cubemap;
}

wgf_cubemap_t wgf_cubemap_create_faces(const char *positive_x, const char *negative_x, const char *positive_y,
                                       const char *negative_y, const char *positive_z, const char *negative_z)
{
    const char *const paths[6] = {positive_x, negative_x, positive_y, negative_y, positive_z, negative_z};
    wgf_core_priv_resource_t *resource;
    cubemap_t *cubemap_ptr;
    wgf_cubemap_t cubemap;
    faces_t *faces;
    int f;
    for (f = 0; f < 6; f++) {
        if (paths[f] == NULL) {
            wgf_log_error("wgf_cubemap_create_faces: needs six paths (face %d is NULL)", f);
            return 0;
        }
    }
    if (!ensure_part()) return 0;
    faces = (faces_t *)calloc(1, sizeof(faces_t));
    cubemap = faces != NULL ? wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_CUBEMAP) : 0;
    cubemap_ptr = cubemap_of(cubemap);
    if (cubemap_ptr == NULL) {
        free(faces);
        return 0;
    }
    for (f = 0; f < 6; f++) snprintf(faces->paths[f], sizeof(faces->paths[f]), "%s", paths[f]);
    cubemap_ptr->faces = faces;
    cubemap_ptr->made = true;
    cubemap_ptr->layout = WGF_CUBEMAP_LAYOUT_FACES;
    resource = &cubemap_ptr->resource;
    resource->status = WGF_RESOURCE_STATUS_PENDING; /* made from numbers, READY, until its files are in */
    for (f = 0; f < 6; f++) {
        if (!wgf_core_priv_load_request(&face_loaders[f], paths[f], cubemap)) {
            wgf_log_warn("wgf_cubemap_create_faces: %s: nothing to load it with (is core running?)", paths[f]);
            wgf_core_priv_load_cancel(cubemap);
            cubemap_ptr = cubemap_of(cubemap);
            if (cubemap_ptr != NULL) cubemap_ptr->resource.status = WGF_RESOURCE_STATUS_FAILED;
            break;
        }
    }
    return cubemap;
}

int wgf_cubemap_get_size(wgf_cubemap_t cubemap)
{
    const cubemap_t *cubemap_ptr = cubemap_of(cubemap);
    return cubemap_ptr != NULL && cubemap_ptr->resource.status == WGF_RESOURCE_STATUS_READY ? cubemap_ptr->size : 0;
}

wgf_cubemap_layout_t wgf_cubemap_get_layout(wgf_cubemap_t cubemap)
{
    const cubemap_t *cubemap_ptr = cubemap_of(cubemap);
    return cubemap_ptr != NULL ? cubemap_ptr->layout : WGF_CUBEMAP_LAYOUT_AUTO;
}
