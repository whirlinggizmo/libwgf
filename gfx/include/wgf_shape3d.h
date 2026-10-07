#ifndef WGF_SHAPE3D_H
#define WGF_SHAPE3D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_node.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 3D shape: a node drawing a cube, a sphere, a filled rectangle or a circle's outline
 * in its local x-y plane, a line, or a line strip, in one color, unlit, on a stage
 * (wgf_stage.h), as libwgt's (wgrender's shape3d): what debug views and markers are
 * drawn with. In a canvas it draws nothing: 2D shapes are wgf_shape2d.h. Place, turn,
 * scale, and parent it with the node calls. On a stage, shapes are drawn after its
 * opaque models and before its see-through ones, depth tested; a see-through color
 * blends over what is behind it in the order the shapes are found; lines are a pixel
 * wide. */
typedef enum wgf_shape3d_kind_t {
    WGF_SHAPE3D_KIND_NONE = 0, /* nothing yet: draws nothing */
    WGF_SHAPE3D_KIND_CUBE = 1,
    WGF_SHAPE3D_KIND_SPHERE = 2,
    WGF_SHAPE3D_KIND_RECTANGLE = 3,
    WGF_SHAPE3D_KIND_CIRCLE = 4,
    WGF_SHAPE3D_KIND_LINE = 5,
    WGF_SHAPE3D_KIND_LINE_STRIP = 6
} wgf_shape3d_kind_t;

/* A shape with nothing to draw yet, white. 0 when there is no room for another node. */
WGF_API wgf_node_t wgf_shape3d_create(void);

/* What it is, set by the calls below; NONE for a node that isn't a 3D shape. */
WGF_API wgf_shape3d_kind_t wgf_shape3d_get_kind(wgf_node_t shape);

/* About its node's origin: a cube `width` by `height` by `length`; a sphere of
 * `radius`; a filled rectangle `width` by `height`, and a circle's outline of `radius`,
 * in its local x-y plane. False for a size below 0. */
WGF_API bool wgf_shape3d_set_cube(wgf_node_t shape, float width, float height, float length);
WGF_API bool wgf_shape3d_set_sphere(wgf_node_t shape, float radius);
WGF_API bool wgf_shape3d_set_rectangle(wgf_node_t shape, float width, float height);
WGF_API bool wgf_shape3d_set_circle(wgf_node_t shape, float radius);

/* A line from (x0, y0, z0) to (x1, y1, z1), in its own space. */
WGF_API bool wgf_shape3d_set_line(wgf_node_t shape, float x0, float y0, float z0, float x1, float y1, float z1);

/* A line strip, from a caller-owned array of points, x, y, and z in turn (`float_count`
 * floats: three a point), copied: drawn once it has two. False for a count that isn't a
 * multiple of 3, or past 65536 points. get_points fills the caller's array with up to
 * `float_count` floats of them and returns how many it filled. */
WGF_API bool wgf_shape3d_set_line_strip(wgf_node_t shape, const float *points, int float_count);
WGF_API int wgf_shape3d_get_point_count(wgf_node_t shape);
WGF_API int wgf_shape3d_get_points(wgf_node_t shape, float *points, int float_count);

/* What was set: a cube's or rectangle's size (a rectangle's z 0; 0, 0, 0 otherwise), a
 * sphere's or circle's radius (0 otherwise), and a line's ends. */
WGF_API wgf_vec3_t wgf_shape3d_get_size(wgf_node_t shape);
WGF_API float wgf_shape3d_get_radius(wgf_node_t shape);
WGF_API wgf_vec3_t wgf_shape3d_get_line_start(wgf_node_t shape);
WGF_API wgf_vec3_t wgf_shape3d_get_line_end(wgf_node_t shape);

/* Default: white. Alpha below 255 makes it see-through. */
WGF_API bool wgf_shape3d_set_color(wgf_node_t shape, wgf_color_t color);
WGF_API wgf_color_t wgf_shape3d_get_color(wgf_node_t shape);

#ifdef __cplusplus
}
#endif

#endif
