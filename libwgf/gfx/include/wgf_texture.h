#ifndef WGF_TEXTURE_H
#define WGF_TEXTURE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A texture: an image on the GPU, loaded from a file.
 *
 * A texture is a resource (wgf_resource.h): shared, reference counted, and loaded on create.
 * - The handle comes back at once, PENDING.
 * - The file is found by the asset layer (fetched on the web), then read and decoded on a
 *   worker thread.
 * - It is uploaded during the runtime's update at the start of a frame.
 * - Creating the same path again gives the same texture, with one more reference.
 *
 * While a texture is PENDING it draws nothing. Once it has FAILED (logged once, naming the
 * file), it draws a magenta and black checker in its place, which is hard to miss. */
typedef wgf_handle_t wgf_texture_t;

/* How a texture is sampled where it is drawn. */
typedef enum wgf_texture_wrap_t {
    WGF_TEXTURE_WRAP_REPEAT = 0, /* tile */
    WGF_TEXTURE_WRAP_CLAMP = 1,  /* stretch the edge texels */
    WGF_TEXTURE_WRAP_MIRROR = 2  /* tile, flipping every other copy */
} wgf_texture_wrap_t;

typedef enum wgf_texture_filter_t {
    WGF_TEXTURE_FILTER_LINEAR = 0,           /* smooth */
    WGF_TEXTURE_FILTER_NEAREST = 1,          /* sharp texels, for pixel art */
    WGF_TEXTURE_FILTER_LINEAR_NO_MIPMAPS = 2 /* smooth, from the full image only, never its mipmaps. For a sheet
                                                of cells drawn smaller than it is, such as a UI skin's states or an
                                                atlas, whose mipmaps would blend each cell's edge with the next. */
} wgf_texture_filter_t;

/* Creates a texture from an image file: PNG, JPEG, BMP, TGA, or GIF (its first frame), or a
 * compressed one.
 *
 * "name.ktx" names a texture compressed for GPUs. tools/compress_textures.py writes these
 * beside name.png.
 * - The first of these that this GPU can sample is loaded: name.bc7.ktx (desktops),
 *   name.astc.ktx (phones), then name.etc2.ktx (older phones).
 * - It is loaded as it is, with no decoding, in a quarter of the memory.
 * - If the GPU can sample none, or the file is missing or can't be used (warned), name.png
 *   is loaded instead.
 * - A texture made before the window opens has no GPU to ask, so it loads name.png.
 * - A variant named outright (name.bc7.ktx) is loaded or FAILED, with no PNG in its place.
 *
 * wgf_resource_get_path gives the file it read.
 *
 * Returns 0 only if there is no room for another texture. A path or file that can't be
 * loaded gives a texture that FAILED. */
WGF_API wgf_texture_t wgf_texture_create(const char *path);

/* Creates a render target: a texture to draw into, `width` by `height` pixels.
 *
 * - You draw into it between wgf_render_begin_target and wgf_render_end_target
 *   (wgf_render.h).
 * - You then use it as any texture: drawn (wgf_draw_texture), on a sprite, or on a material.
 * - It has the screen's pixel format and samples (wgf_window_get_msaa_samples), and a depth buffer of
 *   its own, so whatever draws to the screen draws into it.
 * - It has no mipmaps.
 * - It is made from numbers: READY at once, with no path.
 * - What was drawn into it is gone when the web's GPU context is lost
 *   (wgf_render_is_context_lost). It comes back blank.
 *
 * Returns 0 for a size of 0 or less or over 16384, or before gfx runs (in init or later)
 * (logged). */
WGF_API wgf_texture_t wgf_texture_create_target(int width, int height);

/* Creates a texture in code, `width` by `height` pixels of RGBA, transparent black until
 * written.
 *
 * Use it for a minimap, fog of war, or a picture the program works out. It is made from
 * numbers: READY at once, with no path, and no mipmaps.
 *
 * Returns 0 for a size of 0 or less or over 16384, or before gfx runs (logged). */
WGF_API wgf_texture_t wgf_texture_create_blank(int width, int height);
/* Writes every pixel of a texture made blank, copied from `data`.
 *
 * - `data` is RGBA, 8 bits each, rows from the top: width x height x 4 bytes.
 * - The pixels are seen from the next frame drawn.
 * - A texture takes one write a frame, as the GPU takes one. A second write in a frame is
 *   refused.
 * - On the web, the last write is also kept, a copy the texture's size. The texture then
 *   comes back as it was when the GPU's context does (wgf_render.h).
 * - The frame the context comes back in takes no other write.
 * - While the context is lost, every write is kept and taken.
 *
 * Returns false for a texture not made blank, a `size` that isn't the texture's, a NULL
 * `data`, or a second write in a frame. */
WGF_API bool wgf_texture_write(wgf_texture_t texture, const unsigned char *data, int size);

/* Returns the texture's size in pixels, or 0 until it is READY. */
WGF_API int wgf_texture_get_width(wgf_texture_t texture);
WGF_API int wgf_texture_get_height(wgf_texture_t texture);

/* Sets or gets how the texture is sampled where it is drawn (default: clamp, linear).
 *
 * Where it is drawn smaller than it is, it samples from its mipmaps, built when it loads.
 *
 * The setter returns false for a handle that isn't a texture, or a wrap or filter that
 * isn't one. */
WGF_API bool wgf_texture_set_sampling(wgf_texture_t texture, wgf_texture_wrap_t wrap_u, wgf_texture_wrap_t wrap_v,
                                      wgf_texture_filter_t filter);
WGF_API wgf_texture_wrap_t wgf_texture_get_wrap_u(wgf_texture_t texture);
WGF_API wgf_texture_wrap_t wgf_texture_get_wrap_v(wgf_texture_t texture);
WGF_API wgf_texture_filter_t wgf_texture_get_filter(wgf_texture_t texture);

/* A texture read back: its pixels in memory.
 *
 * Use it for a screenshot of a render target, a tool, a pixel check, or an image
 * (wgf_image_create_from_read).
 *
 * A read is a task, a handle of kind "gfx.texture_read" (CONVENTIONS.md's asynchronous work).
 * - It is PENDING as it is made.
 * - In the runtime's update at the start of the next frame, it becomes DONE. It then holds
 *   what the texture held once the frame the read was asked in was drawn.
 * - It becomes FAILED instead where there is no GPU to read (a headless build), for a
 *   texture that isn't READY by then or is compressed (a .ktx), or with no memory for its
 *   pixels.
 *
 * Reading stalls that update until the GPU has drawn the frame. That is fine for a
 * screenshot, but not for every frame. */
typedef wgf_handle_t wgf_texture_read_t;

typedef enum wgf_texture_read_status_t {
    WGF_TEXTURE_READ_STATUS_NONE = 0, /* not a read */
    WGF_TEXTURE_READ_STATUS_PENDING = 1,
    WGF_TEXTURE_READ_STATUS_DONE = 2,
    WGF_TEXTURE_READ_STATUS_FAILED = 3
} wgf_texture_read_status_t;

/* Creates a read of `texture`.
 *
 * `texture` may be a render target, a texture made in code, or one loaded from a file (its
 * largest level is read). The read holds a reference to the texture until it is read.
 *
 * Returns 0 for a handle that isn't a texture, or if there is no room for another read
 * (logged). */
WGF_API wgf_texture_read_t wgf_texture_read(wgf_texture_t texture);

/* Returns the read's status: PENDING until the next frame's update, then DONE or FAILED.
 *
 * Each read getter returns NONE, 0, or NULL for a handle that isn't a read. */
WGF_API wgf_texture_read_status_t wgf_texture_read_get_status(wgf_texture_read_t read);
/* Returns the texture's size as it was read, or 0 until DONE. */
WGF_API int wgf_texture_read_get_width(wgf_texture_read_t read);
WGF_API int wgf_texture_read_get_height(wgf_texture_read_t read);
/* Returns a DONE read's pixels, and their size in bytes.
 *
 * Pixels are RGBA, 8 bits a channel, width * 4 bytes a row, with the top row first, as an
 * image file stores them. They are valid until the read is destroyed.
 *
 * Returns NULL and 0 for any read that isn't DONE. */
WGF_API const unsigned char *wgf_texture_read_get_data(wgf_texture_read_t read);
WGF_API int wgf_texture_read_get_size(wgf_texture_read_t read);
/* Saves a DONE read's pixels as a PNG file at `path`.
 *
 * With wgf_render_read_screen, this takes a screenshot.
 *
 * - The path works as for wgf_fs_write: under the root, with its directories made. On the
 *   web, the file is kept in the browser's store.
 * - The pixels are encoded on the calling thread. That takes a few hundredths of a second
 *   for a 1280 x 720 frame.
 *
 * Returns the write's task. Returns 0 for a read that isn't DONE, a NULL or refused `path`,
 * or if there is no memory to encode it. */
WGF_API wgf_fs_task_t wgf_texture_read_save(wgf_texture_read_t read, const char *path);
/* Destroys a read. A read still PENDING is dropped, unread.
 *
 * Returns false for a handle that isn't a read. */
WGF_API bool wgf_texture_read_destroy(wgf_texture_read_t read);

#ifdef __cplusplus
}
#endif

#endif
