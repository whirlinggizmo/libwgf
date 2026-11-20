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

/* An image on the CPU: RGBA pixels, 8 bits a channel, rows from the top, in memory, never
 * on the GPU -- raylib's Image. Loaded from a file, generated (a color, gradients, checks,
 * noise), made from a texture's pixels read back, read and written a pixel at a time, made
 * a texture, or saved as a PNG. Pixel formats and editing beyond a pixel (drawing,
 * resizing, cropping) are not libwgf's.
 *
 * An image is a resource (wgf_resource.h): one loaded from a path is shared, reference
 * counted, and loaded on create (PENDING at once, the file found -- fetched on the web --
 * and decoded on a worker, then READY or FAILED in a later update; creating the same path
 * again gives the same image, so a pixel set on it is seen through every handle of it). One
 * made in code is the program's own, READY at once. Released with wgf_resource_release. A
 * getter answers 0 or NULL for an image that isn't READY. A handle of kind "gfx.image". */
typedef wgf_handle_t wgf_image_t;

/* An image from a file: PNG, JPEG, BMP, TGA, or GIF (its first frame), decoded with
 * stb_image on a worker. 0 only when there is no room for another image; a path or file
 * that can't be loaded gives an image that FAILED (logged once, naming the file). Not
 * loaded again in place (wgf_asset_reload leaves it). */
WGF_API wgf_image_t wgf_image_create(const char *path);

/* Images made in code, `width` by `height` pixels, READY at once. Each is 0 for a size of
 * 0 or less or over 16384, or no memory for it, each logged. */

/* Every pixel `color` (raylib's GenImageColor). */
WGF_API wgf_image_t wgf_image_create_color(int width, int height, wgf_color_t color);
/* A gradient from `start` to `end` along a direction `angle` degrees clockwise from
 * rightward (0 runs left to right, 90 top to bottom): the corner furthest back along it is
 * `start` exactly, the one furthest along it `end` (raylib's GenImageGradientLinear, its
 * angle measured from rightward). */
WGF_API wgf_image_t wgf_image_create_gradient_linear(int width, int height, float angle, wgf_color_t start,
                                                     wgf_color_t end);
/* A gradient out from the middle, `inner` within `density` (0 to 1, clamped) of the
 * radius -- from the middle to the nearer sides' pixels -- then toward `outer` at the
 * radius and beyond: the middle `inner`, the corners `outer` (raylib's
 * GenImageGradientRadial). */
WGF_API wgf_image_t wgf_image_create_gradient_radial(int width, int height, float density, wgf_color_t inner,
                                                     wgf_color_t outer);
/* As radial, in squares rather than circles: `inner` within `density` of the way from the
 * middle to the edges, then toward `outer`, every edge pixel `outer` (raylib's
 * GenImageGradientSquare). */
WGF_API wgf_image_t wgf_image_create_gradient_square(int width, int height, float density, wgf_color_t inner,
                                                     wgf_color_t outer);
/* A checkerboard of `check_width` by `check_height` pixel squares, `a` at the top-left (raylib's
 * GenImageChecked). 0 too for a check's side of 0 or less. */
WGF_API wgf_image_t wgf_image_create_checked(int width, int height, int check_width, int check_height, wgf_color_t a,
                                             wgf_color_t b);

/* Noise, the same pixels for the same numbers on every platform (integer arithmetic,
 * hashed from `seed`; never the C library's rand), opaque gray. */

/* Each pixel white with the chance `factor` (0 to 1, clamped), else black (raylib's
 * GenImageWhiteNoise). */
WGF_API wgf_image_t wgf_image_create_white_noise(int width, int height, float factor, int seed);
/* Perlin noise in six octaves (each twice the last's frequency and half its weight),
 * `scale` cells of the first octave across the image (raylib's GenImagePerlinNoise),
 * shifted by `offset_x` and `offset_y` pixels so neighboring images tile; 0 too for a scale
 * of 0 or less, over 4096, or not finite. */
WGF_API wgf_image_t wgf_image_create_perlin_noise(int width, int height, int offset_x, int offset_y, float scale,
                                                  int seed);
/* Cells (Worley noise): a point at random in each `tile_size` square, each pixel as bright
 * as it is far from the nearest point, white a tile's side away (raylib's
 * GenImageCellular). 0 too for a tile's side of 0 or less. */
WGF_API wgf_image_t wgf_image_create_cellular(int width, int height, int tile_size, int seed);

/* A copy of a texture's pixels read back (wgf_texture_read, DONE): raylib's
 * LoadImageFromTexture. READY at once; 0 for a read that isn't DONE (logged). */
WGF_API wgf_image_t wgf_image_create_from_read(wgf_texture_read_t read);

/* Its size in pixels; 0 until it is READY. */
WGF_API int wgf_image_get_width(wgf_image_t image);
WGF_API int wgf_image_get_height(wgf_image_t image);

/* The pixel at column `x`, row `y` from the top-left (raylib's GetImageColor); 0 (transparent
 * black) outside it or for an image that isn't READY. */
WGF_API wgf_color_t wgf_image_get_pixel(wgf_image_t image, int x, int y);
/* That pixel set to `color` (raylib's ImageDrawPixel). Refused, false, outside it or for an
 * image that isn't READY. */
WGF_API bool wgf_image_set_pixel(wgf_image_t image, int x, int y, wgf_color_t color);

/* Its pixels: RGBA, 8 bits a channel, width * 4 bytes a row, the top row first; valid until
 * it is released. NULL and 0 for an image that isn't READY. */
WGF_API const unsigned char *wgf_image_get_data(wgf_image_t image);
WGF_API int wgf_image_get_size(wgf_image_t image);

/* Saved as a PNG file at `path` (as wgf_fs_write: under the root, its directories made; on
 * the web, kept in the browser's store), libwgf's own writer. The write's task, or 0 for an
 * image that isn't READY, a NULL or refused path, or no memory to encode it. Encoded on the
 * calling thread. */
WGF_API wgf_fs_task_t wgf_image_save(wgf_image_t image, const char *path);

/* A texture made in code (as wgf_texture_create_blank) holding the image's pixels as they
 * are now, seen from the next frame drawn (raylib's LoadTextureFromImage): no mipmaps,
 * written again by wgf_texture_write or wgf_texture_write_image -- though not in the frame
 * it is made, whose one write this is. 0 for an image that isn't READY, or before gfx
 * runs, each logged. */
WGF_API wgf_texture_t wgf_texture_create_from_image(wgf_image_t image);
/* A texture made in code written whole with the image's pixels (raylib's UpdateTexture),
 * as wgf_texture_write. False for a texture not made in code, an image that isn't READY
 * or isn't the texture's size, and a second write in a frame. */
WGF_API bool wgf_texture_write_image(wgf_texture_t texture, wgf_image_t image);

#ifdef __cplusplus
}
#endif

#endif
