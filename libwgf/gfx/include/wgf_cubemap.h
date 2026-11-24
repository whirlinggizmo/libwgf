#ifndef WGF_CUBEMAP_H
#define WGF_CUBEMAP_H

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A cubemap: six square images on the GPU, one for each direction (+X, -X, +Y, -Y, +Z, -Z).
 *
 * A custom shader samples a cubemap by a direction, as a samplerCube, for a skybox or a
 * reflection. See wgf_material_set_cubemap (wgf_material.h), and wgf.glsl for how.
 *
 * A cubemap is a resource (wgf_resource.h): reference counted and loaded on create.
 * - The handle comes back at once, PENDING.
 * - Its files are found by the asset layer (fetched on the web). On a worker thread they are
 *   decoded, cut into faces, and given their mipmaps.
 * - It is uploaded during the runtime's update at the start of a frame.
 * - While it is PENDING, a shader samples white.
 * - Once it has FAILED (logged, naming the file and why), a shader samples a magenta and
 *   black checker.
 *
 * Each face is as a camera at the middle sees it, looking down that axis. This is OpenGL's
 * convention:
 * - +X's top is +Y, and its left is +Z.
 * - +Y's top is -Z, and its left is -X.
 * - +Z's top is +Y, and its left is -X.
 * - The negative faces are turned to match.
 *
 * Pixels are RGBA, 8 bits a channel, as stored. A picture's pixels are sRGB, so convert them
 * with wgf_srgb_to_linear in the shader.
 *
 * A cubemap loads again in place (wgf_asset_reload) when its file is saved, or any of its six:
 * - It keeps its handle, and is shown as it was until the new file is in.
 * - One image's layout is read again from its shape, as AUTO reads it.
 * - Six files are all read again, and made a cube once they are all in.
 * - A file that doesn't load, or faces of two sizes, keep what it had, with one error logged. */
typedef wgf_handle_t wgf_cubemap_t;

/* How one image holds the six faces. A face is a square of `size` pixels. */
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

/* Creates a cubemap from the image file at `path`: PNG, JPEG, BMP, TGA, or GIF's first frame.
 *
 * - The faces are laid out by `layout`. With AUTO, they are laid out by the image's shape.
 * - Creating the same path again gives the same cubemap, with one more reference. It keeps
 *   the layout the first create gave. Another layout is warned about.
 * - An image whose shape isn't exactly the layout's (such as a 6 by 1 row of square faces)
 *   FAILS.
 * - AUTO also FAILS for a shape that is none of the layouts.
 *
 * Returns 0 for a `layout` that isn't a layout or is FACES (logged), or if there is no room
 * for another cubemap. */
WGF_API wgf_cubemap_t wgf_cubemap_create(const char *path, wgf_cubemap_layout_t layout);

/* Creates a cubemap from six image files, one for each face.
 *
 * - The faces must be square and all the same size, or the cubemap FAILS (logged, naming
 *   the face).
 * - Each call makes a new cubemap, never shared.
 * - wgf_resource_get_path gives +X's file.
 *
 * Returns 0 for a NULL path, or if there is no room for another cubemap. */
WGF_API wgf_cubemap_t wgf_cubemap_create_faces(const char *positive_x, const char *negative_x,
                                               const char *positive_y, const char *negative_y,
                                               const char *positive_z, const char *negative_z);

/* Returns a face's size in pixels, or 0 until the cubemap is READY. */
WGF_API int wgf_cubemap_get_size(wgf_cubemap_t cubemap);

/* Returns how the cubemap's faces were read.
 *
 * - It is the layout given, or, once READY, the one AUTO found.
 * - It is FACES for a cubemap of six files.
 * - It is AUTO for a handle that isn't a cubemap, or one AUTO hasn't read yet. */
WGF_API wgf_cubemap_layout_t wgf_cubemap_get_layout(wgf_cubemap_t cubemap);

#ifdef __cplusplus
}
#endif

#endif
