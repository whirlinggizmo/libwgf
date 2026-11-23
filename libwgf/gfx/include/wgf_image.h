#ifndef WGF_IMAGE_H
#define WGF_IMAGE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_resource.h"
#include "wgf_texture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An image on the CPU: RGBA pixels in memory, never on the GPU.
 *
 * Pixels are 8 bits a channel, in rows from the top. You can:
 * - load an image from a file;
 * - generate one: a color, gradients, checks, or noise;
 * - make one from a texture's pixels read back;
 * - read and write it a pixel at a time;
 * - make a texture from it, or save it as a PNG.
 *
 * libwgf has no other pixel formats, and no editing beyond a pixel (drawing, resizing,
 * cropping).
 *
 * An image is a resource (wgf_resource.h), a handle of kind "gfx.image".
 * - One loaded from a path is shared, reference counted, and loaded on create. It is
 *   PENDING at once. The file is found (fetched on the web) and decoded on a worker, and the
 *   image becomes READY or FAILED in a later update.
 * - Creating the same path again gives the same image. A pixel set on it is seen through
 *   every handle of it.
 * - One made in code is the program's own, and READY at once.
 * - Release it with wgf_resource_release.
 * - A getter returns 0 or NULL for an image that isn't READY. */
typedef wgf_handle_t wgf_image_t;

/* Creates an image from a file: PNG, JPEG, BMP, TGA, or GIF (its first frame).
 *
 * The file is decoded on a worker. It isn't loaded again in place:
 * wgf_asset_reload leaves it as it is.
 *
 * Returns 0 only if there is no room for another image. A path or file that can't be loaded
 * gives an image that FAILED (logged once, naming the file). */
WGF_API wgf_image_t wgf_image_create(const char *path);

/* Creates images in code, `width` by `height` pixels, READY at once.
 *
 * Each returns 0 for a size of 0 or less or over 16384, or if there is no memory for it
 * (logged). */

/* Creates an image with every pixel `color`. */
WGF_API wgf_image_t wgf_image_create_color(int width, int height, wgf_color_t color);
/* Creates a gradient from `start` to `end` along a direction.
 *
 * - `angle` is in degrees, clockwise from rightward. 0 runs left to right, and 90 top to
 *   bottom.
 * - The corner furthest back along the direction is exactly `start`.
 * - The corner furthest along it is exactly `end`. */
WGF_API wgf_image_t wgf_image_create_gradient_linear(int width, int height, float angle, wgf_color_t start,
                                                     wgf_color_t end);
/* Creates a gradient out from the middle, in circles.
 *
 * - The radius runs from the middle to the pixels of the nearer sides.
 * - The image is `inner` within `density` of the radius (0 to 1, clamped).
 * - It then blends toward `outer`, reaching it at the radius, and stays `outer` beyond.
 * - So the middle is `inner`, and the corners are `outer`. */
WGF_API wgf_image_t wgf_image_create_gradient_radial(int width, int height, float density, wgf_color_t inner,
                                                     wgf_color_t outer);
/* Creates a gradient as wgf_image_create_gradient_radial does, in squares rather than circles.
 *
 * The image is `inner` within `density` of the way from the middle to the edges. It then
 * blends toward `outer`, and every edge pixel is `outer`. */
WGF_API wgf_image_t wgf_image_create_gradient_square(int width, int height, float density, wgf_color_t inner,
                                                     wgf_color_t outer);
/* Creates a checkerboard of `check_width` by `check_height` pixel squares, with `a` at the
 * top-left. Also returns 0 for a check's side of 0 or less. */
WGF_API wgf_image_t wgf_image_create_checked(int width, int height, int check_width, int check_height, wgf_color_t a,
                                             wgf_color_t b);

/* Creates noise images, in opaque gray.
 *
 * The same numbers give the same pixels on every platform. The noise uses integer arithmetic,
 * hashed from `seed`, and never the C library's rand. */

/* Creates white noise: each pixel is white with the chance `factor` (0 to 1, clamped), and
 * black otherwise. */
WGF_API wgf_image_t wgf_image_create_white_noise(int width, int height, float factor, int seed);
/* Creates Perlin noise in six octaves. Each octave has twice the last one's frequency and half
 * its weight.
 *
 * - `scale` is the number of cells of the first octave across the image.
 * - `offset_x` and `offset_y` shift the noise, in pixels, so neighboring images tile.
 *
 * Also returns 0 for a `scale` of 0 or less, over 4096, or not finite. */
WGF_API wgf_image_t wgf_image_create_perlin_noise(int width, int height, int offset_x, int offset_y, float scale,
                                                  int seed);
/* Creates cells (Worley noise).
 *
 * Each `tile_size` square has a point at random. Each pixel is as bright as it is far from
 * the nearest point, and white a tile's side away.
 *
 * Also returns 0 for a `tile_size` of 0 or less. */
WGF_API wgf_image_t wgf_image_create_cellular(int width, int height, int tile_size, int seed);

/* Creates an image from a copy of a texture's pixels read back (wgf_texture_read, DONE).
 *
 * The image is READY at once. Returns 0 for a read that isn't DONE (logged). */
WGF_API wgf_image_t wgf_image_create_from_read(wgf_texture_read_t read);

/* Returns the image's size in pixels, or 0 until it is READY. */
WGF_API int wgf_image_get_width(wgf_image_t image);
WGF_API int wgf_image_get_height(wgf_image_t image);

/* Returns the pixel at column `x`, row `y`, counted from the top-left.
 *
 * Returns 0 (transparent black) outside the image, or for an image that isn't READY. */
WGF_API wgf_color_t wgf_image_get_pixel(wgf_image_t image, int x, int y);
/* Sets the pixel at column `x`, row `y` to `color`.
 *
 * Returns false outside the image, or for an image that isn't READY. */
WGF_API bool wgf_image_set_pixel(wgf_image_t image, int x, int y, wgf_color_t color);

/* Returns the image's pixels, and their size in bytes.
 *
 * Pixels are RGBA, 8 bits a channel, width * 4 bytes a row, with the top row first. They are
 * valid until the image is released.
 *
 * Returns NULL and 0 for an image that isn't READY. */
WGF_API const unsigned char *wgf_image_get_data(wgf_image_t image);
WGF_API int wgf_image_get_size(wgf_image_t image);

/* Saves the image as a PNG file at `path`, with libwgf's own writer.
 *
 * The path works as for wgf_fs_write: under the root, with its directories made. On the web,
 * the file is kept in the browser's store. The image is encoded on the calling thread.
 *
 * Returns the write's task. Returns 0 for an image that isn't READY, a NULL or refused
 * `path`, or if there is no memory to encode it. */
WGF_API wgf_fs_task_t wgf_image_save(wgf_image_t image, const char *path);

/* Creates a texture in code (as wgf_texture_create_blank) holding the image's pixels as they
 * are now.
 *
 * - The pixels are seen from the next frame drawn.
 * - The texture has no mipmaps.
 * - You can write it again with wgf_texture_write or wgf_texture_write_image. You can't in
 *   the frame it is made, because this is that frame's one write.
 *
 * Returns 0 for an image that isn't READY, or before gfx runs (logged). */
WGF_API wgf_texture_t wgf_texture_create_from_image(wgf_image_t image);
/* Writes a texture made in code whole with the image's pixels, as wgf_texture_write does.
 *
 * Returns false for a texture not made in code, an image that isn't READY or isn't the
 * texture's size, or a second write in a frame. */
WGF_API bool wgf_texture_write_image(wgf_texture_t texture, wgf_image_t image);

#ifdef __cplusplus
}
#endif

#endif
