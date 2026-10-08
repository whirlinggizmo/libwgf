#include "wgf_texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "stb_image.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"

/* Textures: a resource, loaded on create through core's load pipeline. A worker reads
 * and decodes the file (stb_image) and builds its mipmaps; the main thread uploads it.
 * libwgt's, without what milestone 1 doesn't draw: compressed (KTX) textures, render
 * targets, a glTF file's images, and the copy of the alpha picking reads. */

#define CHECKER_SIZE 64
#define CHECKER_SQUARE 8

typedef struct texture_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    int width;
    int height;
    sg_image image;
    sg_view view;
    wgf_texture_wrap_t wrap_u;
    wgf_texture_wrap_t wrap_v;
    wgf_texture_filter_t filter;
} texture_t;

typedef struct decoded_t {
    int width;
    int height;
    int mip_count;
    unsigned char *levels[SG_MAX_MIPMAPS]; /* level 0 is stb_image's, the rest malloc'd */
} decoded_t;

static bool pool_ready;
static wgf_core_priv_handle_pool_t texture_pool;
static texture_t *textures;

/* The built-in placeholder, a texture of gfx's own, drawn for a texture that FAILED:
 * made at setup, never let go of before shutdown. */
static wgf_texture_t placeholder;
static sg_view placeholder_view;
static sg_sampler samplers[3][3][2][2]; /* wrap u, wrap v, filter, mipmaps */

static texture_t *texture_of_at(wgf_texture_t texture, const char *caller)
{
    uint16_t index;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve_at(&texture_pool, texture, &index, caller)) return NULL;
    return &textures[index];
}
#define texture_of(...) texture_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

static wgf_core_priv_resource_kind_t resource_kind; /* defined below, with what it names */

static bool ensure_pool(void)
{
    if (pool_ready) return true;
    pool_ready = wgf_core_priv_handle_pool_init(&texture_pool, WGF_CORE_PRIV_HANDLE_KIND_TEXTURE, (void **)&textures,
                                                sizeof(texture_t), 16, 65535);
    if (pool_ready) wgf_core_priv_resource_register(&texture_pool, &resource_kind);
    return pool_ready;
}

sg_sampler wgf_gfx_priv_texture_sampler(wgf_texture_wrap_t wrap_u, wgf_texture_wrap_t wrap_v,
                                        wgf_texture_filter_t filter, bool mipmaps)
{
    static const sg_wrap wraps[3] = {SG_WRAP_REPEAT, SG_WRAP_CLAMP_TO_EDGE, SG_WRAP_MIRRORED_REPEAT};
    sg_sampler *sampler = &samplers[wrap_u][wrap_v][filter][mipmaps ? 1 : 0];
    if (sampler->id == SG_INVALID_ID) {
        sg_sampler_desc desc;
        const sg_filter f = filter == WGF_TEXTURE_FILTER_NEAREST ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
        memset(&desc, 0, sizeof(desc));
        desc.min_filter = f;
        desc.mag_filter = f;
        desc.mipmap_filter = f;
        desc.max_lod = mipmaps ? 1000.0f : 0.0f; /* every level there is, or the base level only */
        desc.wrap_u = wraps[wrap_u];
        desc.wrap_v = wraps[wrap_v];
        *sampler = sg_make_sampler(&desc);
    }
    return *sampler;
}

/* An RGBA image of `mip_count` levels, each half the last, and a view of it; false,
 * with nothing made, when the GPU refused. */
static bool make_image(int width, int height, int mip_count, unsigned char *const *levels, sg_image *image,
                       sg_view *view)
{
    sg_image_desc image_desc;
    sg_view_desc view_desc;
    int level, w = width, h = height;
    memset(&image_desc, 0, sizeof(image_desc));
    image_desc.width = width;
    image_desc.height = height;
    image_desc.num_mipmaps = mip_count;
    image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    image_desc.label = "wgf-texture";
    for (level = 0; level < mip_count; level++) {
        image_desc.data.mip_levels[level].ptr = levels[level];
        image_desc.data.mip_levels[level].size = (size_t)w * (size_t)h * 4;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    *image = sg_make_image(&image_desc);
    if (sg_query_image_state(*image) != SG_RESOURCESTATE_VALID) {
        sg_destroy_image(*image);
        return false;
    }
    memset(&view_desc, 0, sizeof(view_desc));
    view_desc.texture.image = *image;
    *view = sg_make_view(&view_desc);
    if (sg_query_view_state(*view) != SG_RESOURCESTATE_VALID) {
        sg_destroy_view(*view);
        sg_destroy_image(*image);
        return false;
    }
    return true;
}

/* --- the loader ------------------------------------------------------------ */

static void discard(void *prepared)
{
    decoded_t *decoded = (decoded_t *)prepared;
    int level;
    stbi_image_free(decoded->levels[0]);
    for (level = 1; level < decoded->mip_count; level++) free(decoded->levels[level]);
    free(decoded);
}

/* Fill levels 1.. from level 0. Each level averages 2x2 texels of the previous one (the
 * last row or column repeats for odd sizes), in the stored color space, as libwgt's. */
static bool build_mipmaps(decoded_t *decoded)
{
    int lw = decoded->width, lh = decoded->height;

    decoded->mip_count = 1;
    while ((lw > 1 || lh > 1) && decoded->mip_count < SG_MAX_MIPMAPS) {
        const unsigned char *src = decoded->levels[decoded->mip_count - 1];
        const int sw = lw, sh = lh;
        unsigned char *dst;
        int x, y, c;
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
        dst = (unsigned char *)malloc((size_t)lw * (size_t)lh * 4);
        if (dst == NULL) return false;
        for (y = 0; y < lh; y++) {
            const int y0 = y * 2 < sh ? y * 2 : sh - 1, y1 = y * 2 + 1 < sh ? y * 2 + 1 : sh - 1;
            for (x = 0; x < lw; x++) {
                const int x0 = x * 2 < sw ? x * 2 : sw - 1, x1 = x * 2 + 1 < sw ? x * 2 + 1 : sw - 1;
                for (c = 0; c < 4; c++) {
                    const int sum = src[(y0 * sw + x0) * 4 + c] + src[(y0 * sw + x1) * 4 + c] +
                                    src[(y1 * sw + x0) * 4 + c] + src[(y1 * sw + x1) * 4 + c];
                    dst[(y * lw + x) * 4 + c] = (unsigned char)((sum + 2) / 4);
                }
            }
        }
        decoded->levels[decoded->mip_count++] = dst;
    }
    return true;
}

/* The file decoded to RGBA with its mipmaps, on a worker; NULL (logged) when it can't
 * be read or isn't an image stb_image reads. */
static void *prepare(const char *path)
{
    unsigned char *bytes;
    int size, comp;
    decoded_t *decoded;
    if (!wgf_core_priv_fs_read(path, &bytes, &size)) {
        wgf_log_warn("wgf_gfx_texture: %s: couldn't be read", path);
        return NULL;
    }
    decoded = (decoded_t *)calloc(1, sizeof(decoded_t));
    if (decoded != NULL) {
        decoded->levels[0] = stbi_load_from_memory(bytes, size, &decoded->width, &decoded->height, &comp, 4);
        if (decoded->levels[0] == NULL) {
            wgf_log_warn("wgf_gfx_texture: %s: not an image it can read (%s)", path, stbi_failure_reason());
            free(decoded);
            decoded = NULL;
        } else if (!build_mipmaps(decoded)) {
            wgf_log_warn("wgf_gfx_texture: %s: out of memory", path);
            discard(decoded);
            decoded = NULL;
        }
    }
    wgf_core_priv_fs_read_free(bytes);
    return decoded;
}

static wgf_core_priv_load_step_t finish(void *prepared, wgf_handle_t resource)
{
    const decoded_t *decoded = (const decoded_t *)prepared;
    texture_t *texture_ptr = texture_of(resource);
    if (texture_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (!wgf_gfx_priv_render_is_running()) return WGF_CORE_PRIV_LOAD_MORE; /* no GPU yet: wait for one */
    if (!make_image(decoded->width, decoded->height, decoded->mip_count, decoded->levels, &texture_ptr->image,
                    &texture_ptr->view)) {
        wgf_log_warn("wgf_gfx_texture: %s: the GPU didn't take it (%d by %d)", texture_ptr->resource.found,
                     decoded->width, decoded->height);
        return WGF_CORE_PRIV_LOAD_FAILED;
    }
    texture_ptr->width = decoded->width;
    texture_ptr->height = decoded->height;
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"texture", prepare, finish, discard, fail, NULL};

/* --- an image of another file's (a glTF's) ------------------------------------ */

void *wgf_gfx_priv_texture_decode(const unsigned char *bytes, int size, const char *what)
{
    int comp;
    decoded_t *decoded = (decoded_t *)calloc(1, sizeof(decoded_t));
    if (decoded == NULL) return NULL;
    decoded->levels[0] = stbi_load_from_memory(bytes, size, &decoded->width, &decoded->height, &comp, 4);
    if (decoded->levels[0] == NULL) {
        wgf_log_warn("wgf_gfx_texture: %s: an image it can't read (%s)", what, stbi_failure_reason());
        free(decoded);
        return NULL;
    }
    if (!build_mipmaps(decoded)) {
        wgf_log_warn("wgf_gfx_texture: %s: out of memory", what);
        discard(decoded);
        return NULL;
    }
    return decoded;
}

void wgf_gfx_priv_texture_decoded_free(void *decoded)
{
    if (decoded != NULL) discard(decoded);
}

wgf_texture_t wgf_gfx_priv_texture_create_decoded(void *decoded_data)
{
    decoded_t *decoded = (decoded_t *)decoded_data;
    wgf_texture_t texture = 0;
    texture_t *texture_ptr;
    if (decoded == NULL) return 0;
    if (ensure_pool()) texture = wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_TEXTURE);
    texture_ptr = texture_of(texture);
    if (texture_ptr == NULL || !make_image(decoded->width, decoded->height, decoded->mip_count, decoded->levels,
                                           &texture_ptr->image, &texture_ptr->view)) {
        if (texture != 0) wgf_resource_release(texture);
        discard(decoded);
        return 0;
    }
    texture_ptr->width = decoded->width;
    texture_ptr->height = decoded->height; /* READY as it was added: made from numbers */
    discard(decoded);
    return texture;
}

wgf_texture_t wgf_gfx_priv_texture_get_placeholder(void)
{
    return placeholder;
}

/* --- the public API --------------------------------------------------------- */

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

static void init_texture(void *record)
{
    texture_t *texture_ptr = (texture_t *)record;
    texture_ptr->wrap_u = WGF_TEXTURE_WRAP_CLAMP;
    texture_ptr->wrap_v = WGF_TEXTURE_WRAP_CLAMP;
    texture_ptr->filter = WGF_TEXTURE_FILTER_LINEAR;
}

/* What a record holds past the header: its GPU image once it has one. */
static void free_texture(wgf_handle_t texture, void *record)
{
    texture_t *texture_ptr = (texture_t *)record;
    (void)texture;
    if (texture_ptr->view.id != SG_INVALID_ID) sg_destroy_view(texture_ptr->view);
    if (texture_ptr->image.id != SG_INVALID_ID) sg_destroy_image(texture_ptr->image);
}

/* Its file side (the loader) is set by wgf_texture_create, the only way a texture comes
 * from a file: gfx's start makes the placeholder, so a kind naming the loader from the
 * start would link the image decoder into every program, those that load no image too
 * (libwgt's measurement, its HISTORY's "Release web sizes"). */
static wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_texture_create",
                                                      .init = init_texture,
                                                      .free = free_texture};

wgf_texture_t wgf_texture_create(const char *path)
{
    resource_kind.loader = loader_of;
    return ensure_pool() ? wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEXTURE, path) : 0;
}

void wgf_gfx_priv_texture_retain(wgf_texture_t texture)
{
    if (texture_of(texture) != NULL) wgf_core_priv_resource_retain(texture);
}

int wgf_texture_get_width(wgf_texture_t texture)
{
    const texture_t *texture_ptr = texture_of(texture);
    return texture_ptr != NULL ? texture_ptr->width : 0;
}

int wgf_texture_get_height(wgf_texture_t texture)
{
    const texture_t *texture_ptr = texture_of(texture);
    return texture_ptr != NULL ? texture_ptr->height : 0;
}

bool wgf_texture_set_sampling(wgf_texture_t texture, wgf_texture_wrap_t wrap_u, wgf_texture_wrap_t wrap_v,
                              wgf_texture_filter_t filter)
{
    texture_t *texture_ptr = texture_of(texture);
    if (texture_ptr == NULL || (int)wrap_u < 0 || wrap_u > WGF_TEXTURE_WRAP_MIRROR || (int)wrap_v < 0 ||
        wrap_v > WGF_TEXTURE_WRAP_MIRROR || (int)filter < 0 || filter > WGF_TEXTURE_FILTER_NEAREST) {
        return false;
    }
    texture_ptr->wrap_u = wrap_u;
    texture_ptr->wrap_v = wrap_v;
    texture_ptr->filter = filter;
    return true;
}

wgf_texture_wrap_t wgf_texture_get_wrap_u(wgf_texture_t texture)
{
    const texture_t *texture_ptr = texture_of(texture);
    return texture_ptr != NULL ? texture_ptr->wrap_u : WGF_TEXTURE_WRAP_REPEAT;
}

wgf_texture_wrap_t wgf_texture_get_wrap_v(wgf_texture_t texture)
{
    const texture_t *texture_ptr = texture_of(texture);
    return texture_ptr != NULL ? texture_ptr->wrap_v : WGF_TEXTURE_WRAP_REPEAT;
}

wgf_texture_filter_t wgf_texture_get_filter(wgf_texture_t texture)
{
    const texture_t *texture_ptr = texture_of(texture);
    return texture_ptr != NULL ? texture_ptr->filter : WGF_TEXTURE_FILTER_LINEAR;
}

/* --- for gfx's drawing ------------------------------------------------------ */

bool wgf_gfx_priv_texture_get_binding(wgf_texture_t texture, sg_view *view, sg_sampler *sampler, int *width,
                                      int *height, bool *is_placeholder)
{
    const texture_t *texture_ptr = texture_of(texture);
    if (texture_ptr == NULL || !wgf_gfx_priv_render_is_running()) return false;
    if (texture_ptr->resource.status == WGF_RESOURCE_STATUS_READY) {
        *view = texture_ptr->view;
        *sampler = wgf_gfx_priv_texture_sampler(texture_ptr->wrap_u, texture_ptr->wrap_v, texture_ptr->filter, true);
        *width = texture_ptr->width;
        *height = texture_ptr->height;
        *is_placeholder = false;
        return true;
    }
    if (texture_ptr->resource.status != WGF_RESOURCE_STATUS_FAILED || placeholder_view.id == SG_INVALID_ID) {
        return false; /* still loading: nothing yet */
    }
    *view = placeholder_view;
    *sampler = wgf_gfx_priv_texture_sampler(WGF_TEXTURE_WRAP_REPEAT, WGF_TEXTURE_WRAP_REPEAT,
                                            WGF_TEXTURE_FILTER_NEAREST, false);
    *width = CHECKER_SIZE;
    *height = CHECKER_SIZE;
    *is_placeholder = true;
    return true;
}

void wgf_gfx_priv_texture_setup(void)
{
    static unsigned char checker[CHECKER_SIZE * CHECKER_SIZE * 4];
    unsigned char *const levels[1] = {checker};
    texture_t *texture_ptr;
    int x, y;
    for (y = 0; y < CHECKER_SIZE; y++) {
        for (x = 0; x < CHECKER_SIZE; x++) { /* magenta and near-black squares, as libwgt's */
            const bool magenta = ((x / CHECKER_SQUARE) + (y / CHECKER_SQUARE)) % 2 == 0;
            unsigned char *p = &checker[(y * CHECKER_SIZE + x) * 4];
            p[0] = magenta ? 255 : 20;
            p[1] = 0;
            p[2] = magenta ? 255 : 20;
            p[3] = 255;
        }
    }
    placeholder = ensure_pool() ? wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_TEXTURE) : 0;
    texture_ptr = texture_of(placeholder);
    if (texture_ptr == NULL) {
        wgf_log_error("wgf_gfx_texture: no room for the placeholder");
        placeholder = 0;
        return;
    }
    texture_ptr->resource.permanent = true; /* a built-in: never freed */
    if (!make_image(CHECKER_SIZE, CHECKER_SIZE, 1, levels, &texture_ptr->image, &texture_ptr->view)) {
        wgf_log_error("wgf_gfx_texture: couldn't make the placeholder");
        texture_ptr->resource.status = WGF_RESOURCE_STATUS_FAILED;
        return;
    }
    texture_ptr->width = texture_ptr->height = CHECKER_SIZE;
    placeholder_view = texture_ptr->view;
}

void wgf_gfx_priv_texture_shutdown(void)
{
    int u, v, f, m;
    placeholder = 0; /* freed with the rest */
    placeholder_view.id = SG_INVALID_ID;
    if (pool_ready) {
        wgf_core_priv_resource_unregister(&texture_pool);
        wgf_core_priv_handle_pool_destroy(&texture_pool);
        pool_ready = false;
    }
    for (u = 0; u < 3; u++) {
        for (v = 0; v < 3; v++) {
            for (f = 0; f < 2; f++) {
                for (m = 0; m < 2; m++) {
                    if (samplers[u][v][f][m].id != SG_INVALID_ID) sg_destroy_sampler(samplers[u][v][f][m]);
                    samplers[u][v][f][m].id = SG_INVALID_ID;
                }
            }
        }
    }
}
