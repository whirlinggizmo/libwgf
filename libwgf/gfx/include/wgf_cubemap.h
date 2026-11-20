#ifndef WGF_CUBEMAP_H
#define WGF_CUBEMAP_H

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A cubemap: six square images, one a direction (+X, -X, +Y, -Y, +Z, -Z), on the GPU,
 * raylib's LoadTextureCubemap. A custom shader samples one by a direction, as samplerCube
 * (wgf_material_set_cubemap, wgf_material.h; wgf.glsl says how): a skybox, a reflection.
 *
 * It is a resource (wgf_resource.h): reference counted and loaded on create -- the handle
 * comes back at once, PENDING; its files are found (the asset layer: fetched on the web),
 * decoded, cut into faces, and given their mipmaps on a worker thread, and uploaded during
 * the runtime's update at the start of a frame. While it is PENDING a shader samples white;
 * once it has FAILED (logged, naming the file and why) a magenta and black checker.
 *
 * Each face is as a camera at the middle sees it looking down that axis, OpenGL's (and
 * raylib's) convention: +X's top is +Y and its left +Z; +Y's top is -Z and its left -X;
 * +Z's top is +Y and its left -X; the negative faces are turned to match. Pixels are RGBA, 8
 * bits a channel, as stored (sRGB, for a picture; wgf_srgb_to_linear in the shader). */
typedef wgf_handle_t wgf_cubemap_t;

/* How one image holds the six faces, raylib's CUBEMAP_LAYOUT_*. A face is a square of
 * `size` pixels. */
typedef enum wgf_cubemap_layout_t {
    WGF_CUBEMAP_LAYOUT_AUTO = 0,                /* told by the image's shape: 1 by 6, 6 by 1, 3 by 4, 4 by 3 */
    WGF_CUBEMAP_LAYOUT_LINE_VERTICAL = 1,       /* a column, +X, -X, +Y, -Y, +Z, -Z from the top */
    WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL = 2,     /* a row, in that order from the left */
    WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR = 3, /* 3 wide, 4 high: +Y, +X, -Y, -X down the middle column, +Z left of
                                                   +X, -Z right of it */
    WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE = 4, /* 4 wide, 3 high: -X, +Z, +X, -Z across the middle row, +Y above +Z,
                                                   -Y below it */
    WGF_CUBEMAP_LAYOUT_FACES = 5                /* six files, a face each (wgf_cubemap_create_faces) */
} wgf_cubemap_layout_t;

/* The cubemap in the image file at `path` (PNG, JPEG, BMP, TGA, or GIF's first frame), its
 * faces laid out by `layout`, or, AUTO, by its shape. Creating the same path again gives the
 * same cubemap, with one more reference, laid out as the first create said (another layout
 * is warned of). An image whose shape isn't the layout's, exactly (a 6 by 1 row of square
 * faces, ...), FAILS; so does AUTO for a shape that is none of them. 0 for a `layout` that
 * isn't one or is FACES (logged), or no room for another. */
WGF_API wgf_cubemap_t wgf_cubemap_create(const char *path, wgf_cubemap_layout_t layout);

/* The cubemap of six image files, a face each, square and all the same size, or it FAILS
 * (logged, naming the face). Made anew by each call, never shared; wgf_resource_get_path
 * gives +X's file. 0 for a NULL path, or no room for another. */
WGF_API wgf_cubemap_t wgf_cubemap_create_faces(const char *positive_x, const char *negative_x,
                                               const char *positive_y, const char *negative_y,
                                               const char *positive_z, const char *negative_z);

/* A face's size in pixels; 0 until it is READY. */
WGF_API int wgf_cubemap_get_size(wgf_cubemap_t cubemap);

/* How its faces were read: the layout given, or once READY, the one AUTO found; FACES for
 * six files. AUTO for a handle that isn't a cubemap, or one AUTO hasn't read yet. */
WGF_API wgf_cubemap_layout_t wgf_cubemap_get_layout(wgf_cubemap_t cubemap);

#ifdef __cplusplus
}
#endif

#endif
