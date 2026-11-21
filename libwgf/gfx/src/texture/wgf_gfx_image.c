#include "wgf_image.h"

#include <stdlib.h>
#include <string.h>

#include "texture/wgf_gfx_decode_priv.h"
#include "texture/wgf_gfx_image_priv.h"
#include "texture/wgf_gfx_png_priv.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"

/* Images on the CPU, raylib's Image: a resource of a pool of its own ("gfx.image"),
 * pixels in memory and nothing on the GPU, so they load and are made headless too. One
 * from a file is decoded as a texture's is (wgf_gfx_decode_priv.h: on a worker natively, by the
 * browser in a page), and moved into
 * its record on the main thread; one made in code is READY as it is added. A part of its
 * own, installed by the first image, so a program that makes none links none of it; the
 * generators (wgf_gfx_image_gen.c) and the texture calls (wgf_gfx_image_texture.c) are
 * files of their own, linked only where called. */

typedef struct image_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    int width;
    int height;
    unsigned char *pixels; /* width * height * 4, RGBA, the top row first; malloc'd, or stb_image's */
} image_t;

typedef struct decoded_t {
    int width;
    int height;
    unsigned char *pixels; /* malloc'd */
    wgf_gfx_priv_decode_t *pending; /* a file's decode under way (the browser's); NULL once its pixels are in */
} decoded_t;

static struct {
    bool ready;
    bool installed;
    wgf_core_priv_handle_pool_t pool;
    image_t *images;
} im;

static image_t *image_of_at(wgf_image_t image, const char *caller)
{
    uint16_t index;
    if (!im.ready || !wgf_core_priv_handle_pool_resolve_at(&im.pool, image, &index, caller)) return NULL;
    return &im.images[index];
}
#define image_of(...) image_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

/* A READY image's record, or NULL. */
static image_t *ready_of_at(wgf_image_t image, const char *caller)
{
    image_t *image_ptr = image_of_at(image, caller);
    return image_ptr != NULL && image_ptr->resource.status == WGF_RESOURCE_STATUS_READY && image_ptr->pixels != NULL
               ? image_ptr
               : NULL;
}
#define ready_of(...) ready_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

/* --- the loader ------------------------------------------------------------ */

static void discard(void *prepared)
{
    decoded_t *decoded = (decoded_t *)prepared;
    if (decoded == NULL) return;
    wgf_gfx_priv_decode_cancel(decoded->pending);
    free(decoded->pixels);
    free(decoded);
}

/* A decode done: its pixels taken. False (logged) when it FAILED, or is larger than an image
 * may be. */
static bool complete(decoded_t *decoded, const char *what)
{
    wgf_gfx_priv_decode_t *pending = decoded->pending;
    decoded->pending = NULL;
    decoded->pixels = wgf_gfx_priv_decode_take(pending, &decoded->width, &decoded->height);
    if (decoded->pixels == NULL) return false;
    if (decoded->width > WGF_GFX_PRIV_IMAGE_MAX_SIDE || decoded->height > WGF_GFX_PRIV_IMAGE_MAX_SIDE) {
        wgf_log_warn("wgf_image: %s: %d by %d, over %d a side", what, decoded->width, decoded->height,
                     WGF_GFX_PRIV_IMAGE_MAX_SIDE);
        return false;
    }
    return true;
}

/* The file decoded to RGBA: on a worker natively; in a page its decode started, the browser's,
 * and waited for in finish. NULL (logged) when it can't be read, isn't an image the decoder
 * reads, or is larger than an image may be. */
static void *prepare(const char *path)
{
    unsigned char *bytes;
    int size;
    decoded_t *decoded;
    if (!wgf_core_priv_fs_read(path, &bytes, &size)) {
        wgf_log_warn("wgf_image: %s: couldn't be read", path);
        return NULL;
    }
    decoded = (decoded_t *)calloc(1, sizeof(decoded_t));
    if (decoded != NULL) {
        decoded->pending = wgf_gfx_priv_decode_start(bytes, size, path);
        if (decoded->pending == NULL || (wgf_gfx_priv_decode_poll(decoded->pending) != WGF_GFX_PRIV_DECODE_PENDING &&
                                         !complete(decoded, path))) {
            discard(decoded);
            decoded = NULL;
        }
    }
    wgf_core_priv_fs_read_free(bytes);
    return decoded;
}

/* The decoded pixels moved into the image, READY: in a page once the browser's decode is done. */
static wgf_core_priv_load_step_t finish(void *prepared, wgf_handle_t resource)
{
    decoded_t *decoded = (decoded_t *)prepared;
    image_t *image_ptr = image_of(resource);
    if (image_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (decoded->pending != NULL) {
        if (wgf_gfx_priv_decode_poll(decoded->pending) == WGF_GFX_PRIV_DECODE_PENDING) return WGF_CORE_PRIV_LOAD_WAIT;
        if (!complete(decoded, image_ptr->resource.path)) return WGF_CORE_PRIV_LOAD_FAILED;
    }
    image_ptr->width = decoded->width;
    image_ptr->height = decoded->height;
    image_ptr->pixels = decoded->pixels;
    decoded->pixels = NULL; /* the image's now: discard frees the rest */
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"image", prepare, finish, discard, fail, NULL, false, NULL};

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

static void free_image(wgf_handle_t image, void *record)
{
    image_t *image_ptr = (image_t *)record;
    (void)image;
    free(image_ptr->pixels); /* a file's, the decoder's malloc, or one made in code */
    image_ptr->pixels = NULL;
}

static const wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_image_create",
                                                            .loader = loader_of,
                                                            .free = free_image};

/* --- the part --------------------------------------------------------------- */

static void stop(void)
{
    if (im.ready) {
        wgf_core_priv_resource_unregister(&im.pool); /* each image freed */
        wgf_core_priv_handle_pool_destroy(&im.pool);
    }
    memset(&im, 0, sizeof(im));
}

static wgf_core_priv_part_t part = {.name = "images",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_IMAGES,
                                    .stop = stop};

static bool ensure_started(void)
{
    if (im.ready) return true;
    im.ready = wgf_core_priv_handle_pool_init(&im.pool, WGF_CORE_PRIV_HANDLE_KIND_IMAGE, (void **)&im.images,
                                              sizeof(image_t), 8, 65535);
    if (!im.ready) return false;
    wgf_core_priv_resource_register(&im.pool, &resource_kind);
    if (!im.installed) {
        wgf_core_priv_part_install(&part);
        im.installed = true;
    }
    return true;
}

wgf_image_t wgf_gfx_priv_image_add(int width, int height, unsigned char **pixels, const char *caller)
{
    wgf_image_t image;
    image_t *image_ptr;
    unsigned char *bytes;
    *pixels = NULL;
    if (width <= 0 || height <= 0 || width > WGF_GFX_PRIV_IMAGE_MAX_SIDE || height > WGF_GFX_PRIV_IMAGE_MAX_SIDE) {
        wgf_log_error("%s: a size of %dx%d", caller, width, height);
        return 0;
    }
    if (!ensure_started()) return 0;
    bytes = (unsigned char *)calloc((size_t)width * (size_t)height, 4);
    if (bytes == NULL) {
        wgf_log_error("%s: no memory for %dx%d pixels", caller, width, height);
        return 0;
    }
    image = wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_IMAGE);
    image_ptr = image_of(image);
    if (image_ptr == NULL) {
        wgf_log_error("%s: no room for another image", caller);
        free(bytes);
        return 0;
    }
    image_ptr->width = width;
    image_ptr->height = height; /* READY as it was added: made from numbers */
    image_ptr->pixels = bytes;
    *pixels = bytes;
    return image;
}

/* --- the public API --------------------------------------------------------- */

wgf_image_t wgf_image_create(const char *path)
{
    return ensure_started() ? wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_IMAGE, path) : 0;
}

wgf_image_t wgf_image_create_color(int width, int height, wgf_color_t color)
{
    unsigned char *pixels;
    const wgf_image_t image = wgf_gfx_priv_image_add(width, height, &pixels, "wgf_image_create_color");
    const unsigned char rgba[4] = {(unsigned char)(color >> 24), (unsigned char)(color >> 16),
                                   (unsigned char)(color >> 8), (unsigned char)color};
    size_t i;
    if (image == 0) return 0;
    for (i = 0; i < (size_t)width * (size_t)height; i++) memcpy(pixels + i * 4, rgba, 4);
    return image;
}

int wgf_image_get_width(wgf_image_t image)
{
    const image_t *image_ptr = ready_of(image);
    return image_ptr != NULL ? image_ptr->width : 0;
}

int wgf_image_get_height(wgf_image_t image)
{
    const image_t *image_ptr = ready_of(image);
    return image_ptr != NULL ? image_ptr->height : 0;
}

wgf_color_t wgf_image_get_pixel(wgf_image_t image, int x, int y)
{
    const image_t *image_ptr = ready_of(image);
    const unsigned char *p;
    if (image_ptr == NULL || x < 0 || y < 0 || x >= image_ptr->width || y >= image_ptr->height) return 0;
    p = image_ptr->pixels + ((size_t)y * (size_t)image_ptr->width + (size_t)x) * 4;
    return ((wgf_color_t)p[0] << 24) | ((wgf_color_t)p[1] << 16) | ((wgf_color_t)p[2] << 8) | (wgf_color_t)p[3];
}

bool wgf_image_set_pixel(wgf_image_t image, int x, int y, wgf_color_t color)
{
    image_t *image_ptr = ready_of(image);
    unsigned char *p;
    if (image_ptr == NULL || x < 0 || y < 0 || x >= image_ptr->width || y >= image_ptr->height) return false;
    p = image_ptr->pixels + ((size_t)y * (size_t)image_ptr->width + (size_t)x) * 4;
    p[0] = (unsigned char)(color >> 24);
    p[1] = (unsigned char)(color >> 16);
    p[2] = (unsigned char)(color >> 8);
    p[3] = (unsigned char)color;
    return true;
}

const unsigned char *wgf_image_get_data(wgf_image_t image)
{
    const image_t *image_ptr = ready_of(image);
    return image_ptr != NULL ? image_ptr->pixels : NULL;
}

int wgf_image_get_size(wgf_image_t image)
{
    const image_t *image_ptr = ready_of(image);
    return image_ptr != NULL ? image_ptr->width * image_ptr->height * 4 : 0;
}

wgf_fs_task_t wgf_image_save(wgf_image_t image, const char *path)
{
    const image_t *image_ptr = ready_of(image);
    unsigned char *png;
    int size = 0;
    wgf_fs_task_t task;
    if (image_ptr == NULL || path == NULL) return 0;
    png = wgf_gfx_priv_png_encode(image_ptr->pixels, image_ptr->width, image_ptr->height, &size);
    if (png == NULL) {
        wgf_log_warn("wgf_image_save: no memory to encode %s", path);
        return 0;
    }
    task = wgf_fs_write(path, png, size); /* copied before it returns */
    free(png);
    return task;
}
