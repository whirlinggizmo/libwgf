#ifndef WGF_TEXTURE_H
#define WGF_TEXTURE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A texture: an image on the GPU, loaded from a file. It is a resource
 * (wgf_resource.h): shared, reference counted, and loaded on create -- the handle comes
 * back at once, PENDING, the file is found (the asset layer: fetched on the web), read
 * and decoded on a worker thread, and uploaded during the runtime's update at the start
 * of a frame. Creating the same path again gives the same texture, with one more
 * reference. While a texture is PENDING it draws nothing; once it has FAILED (logged
 * once, naming the file) it draws a magenta and black checker in its place, hard to
 * miss. */
typedef wgf_handle_t wgf_texture_t;

/* How a texture is sampled where it is drawn. */
typedef enum wgf_texture_wrap_t {
    WGF_TEXTURE_WRAP_REPEAT = 0, /* tile */
    WGF_TEXTURE_WRAP_CLAMP = 1,  /* stretch the edge texels */
    WGF_TEXTURE_WRAP_MIRROR = 2  /* tile, flipping every other copy */
} wgf_texture_wrap_t;

typedef enum wgf_texture_filter_t {
    WGF_TEXTURE_FILTER_LINEAR = 0, /* smooth */
    WGF_TEXTURE_FILTER_NEAREST = 1 /* sharp texels, for pixel art */
} wgf_texture_filter_t;

/* A texture from an image file: PNG, JPEG, BMP, TGA, or GIF (its first frame). 0 only
 * when there is no room for another texture; a path or file that can't be loaded gives
 * a texture that FAILED. wgf_resource_get_path gives the file it read. */
WGF_API wgf_texture_t wgf_texture_create(const char *path);

/* Its size in pixels; 0 until it is READY. */
WGF_API int wgf_texture_get_width(wgf_texture_t texture);
WGF_API int wgf_texture_get_height(wgf_texture_t texture);

/* How it is sampled where it is drawn, from its mipmaps where it is drawn smaller than
 * it is (built when it loads). Default: clamp, linear. False for a handle that isn't a
 * texture, or a wrap or filter that isn't one. */
WGF_API bool wgf_texture_set_sampling(wgf_texture_t texture, wgf_texture_wrap_t wrap_u, wgf_texture_wrap_t wrap_v,
                                      wgf_texture_filter_t filter);
WGF_API wgf_texture_wrap_t wgf_texture_get_wrap_u(wgf_texture_t texture);
WGF_API wgf_texture_wrap_t wgf_texture_get_wrap_v(wgf_texture_t texture);
WGF_API wgf_texture_filter_t wgf_texture_get_filter(wgf_texture_t texture);

#ifdef __cplusplus
}
#endif

#endif
