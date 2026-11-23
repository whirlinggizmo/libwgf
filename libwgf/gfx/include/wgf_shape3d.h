#ifndef WGF_SHAPE3D_H
#define WGF_SHAPE3D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_actor.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 3D shape: an actor that draws a simple shape on a 3D stage (wgf_stage3d.h), in one
 * color, unlit.
 *
 * Use shapes to draw debug views and markers. A shape is one of:
 * - a cube or a sphere;
 * - a filled rectangle, or a circle's outline, in its local x-y plane;
 * - a line, or a line strip.
 *
 * On a 2D stage it draws nothing. For 2D shapes, see wgf_shape2d.h. Place, turn, scale, and
 * parent it with the actor calls.
 *
 * On a stage, shapes are drawn after its opaque models and before its see-through ones,
 * depth tested. A see-through color blends over what is behind it, in the order the shapes
 * are found. Lines are a pixel wide. */
typedef enum wgf_shape3d_kind_t {
    WGF_SHAPE3D_KIND_NONE = 0, /* nothing yet: draws nothing */
    WGF_SHAPE3D_KIND_CUBE = 1,
    WGF_SHAPE3D_KIND_SPHERE = 2,
    WGF_SHAPE3D_KIND_RECTANGLE = 3,
    WGF_SHAPE3D_KIND_CIRCLE = 4,
    WGF_SHAPE3D_KIND_LINE = 5,
    WGF_SHAPE3D_KIND_LINE_STRIP = 6
} wgf_shape3d_kind_t;

/* Creates a shape with nothing to draw yet, white. Returns 0 if there is no room for another
 * actor. */
WGF_API wgf_actor_t wgf_shape3d_create(void);

/* Returns the shape's kind, set by the calls below, or NONE for an actor that isn't a 3D
 * shape. */
WGF_API wgf_shape3d_kind_t wgf_shape3d_get_kind(wgf_actor_t shape);

/* Makes the shape a cube, a sphere, a rectangle, or a circle, about its actor's origin.
 *
 * - set_cube makes a cube `width` by `height` by `length`.
 * - set_sphere makes a sphere of `radius`.
 * - set_rectangle makes a filled rectangle `width` by `height`, in its local x-y plane.
 * - set_circle makes a circle's outline of `radius`, in its local x-y plane.
 *
 * Returns false for a size below 0. */
WGF_API bool wgf_shape3d_set_cube(wgf_actor_t shape, float width, float height, float length);
WGF_API bool wgf_shape3d_set_sphere(wgf_actor_t shape, float radius);
WGF_API bool wgf_shape3d_set_rectangle(wgf_actor_t shape, float width, float height);
WGF_API bool wgf_shape3d_set_circle(wgf_actor_t shape, float radius);

/* Makes the shape a line from (x0, y0, z0) to (x1, y1, z1), in its own space. */
WGF_API bool wgf_shape3d_set_line(wgf_actor_t shape, float x0, float y0, float z0, float x1, float y1, float z1);

/* Makes the shape a line strip, or gets its points.
 *
 * - `points` is your array, holding x, y, and z in turn: `float_count` floats, three for
 *   each point. It is copied.
 * - The strip is drawn once it has two points.
 * - get_points fills your array with up to `float_count` floats of the points, and returns
 *   how many it filled.
 *
 * set_line_strip returns false for a `float_count` that isn't a multiple of 3, or past 65536
 * points. */
WGF_API bool wgf_shape3d_set_line_strip(wgf_actor_t shape, const float *points, int float_count);
WGF_API int wgf_shape3d_get_point_count(wgf_actor_t shape);
WGF_API int wgf_shape3d_get_points(wgf_actor_t shape, float *points, int float_count);

/* Returns what was set.
 *
 * - get_size returns a cube's or rectangle's size, with a rectangle's z 0. It returns 0, 0, 0
 *   for another kind.
 * - get_radius returns a sphere's or circle's radius, or 0 for another kind.
 * - get_line_start and get_line_end return a line's ends. */
WGF_API wgf_vec3_t wgf_shape3d_get_size(wgf_actor_t shape);
WGF_API float wgf_shape3d_get_radius(wgf_actor_t shape);
WGF_API wgf_vec3_t wgf_shape3d_get_line_start(wgf_actor_t shape);
WGF_API wgf_vec3_t wgf_shape3d_get_line_end(wgf_actor_t shape);

/* Sets or gets the shape's color (default: white). Alpha below 255 makes it see-through. */
WGF_API bool wgf_shape3d_set_color(wgf_actor_t shape, wgf_color_t color);
WGF_API wgf_color_t wgf_shape3d_get_color(wgf_actor_t shape);

#ifdef __cplusplus
}
#endif

#endif
