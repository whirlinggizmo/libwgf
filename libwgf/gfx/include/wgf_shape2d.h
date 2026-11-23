#ifndef WGF_SHAPE2D_H
#define WGF_SHAPE2D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_actor.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 2D shape: an actor that draws a rectangle, a circle, a line, or a polygon on a 2D stage.
 *
 * A shape is filled or outlined, in one color. Shapes are actors (wgf_actor.h): place, turn,
 * scale, parent, and destroy them with the actor calls. The shape turns and scales with its
 * actor, but an outline's thickness stays in the frame's logical pixels. */

typedef enum wgf_shape2d_kind_t {
    WGF_SHAPE2D_KIND_NONE = 0, /* nothing yet: draws nothing */
    WGF_SHAPE2D_KIND_RECTANGLE = 1,
    WGF_SHAPE2D_KIND_CIRCLE = 2,
    WGF_SHAPE2D_KIND_LINE = 3,
    WGF_SHAPE2D_KIND_POLYGON = 4
} wgf_shape2d_kind_t;

/* Creates a shape with nothing to draw yet, white and filled.
 *
 * Returns 0 if there is no room for another actor. */
WGF_API wgf_actor_t wgf_shape2d_create(void);

/* Returns the shape's kind, set by the calls below, or NONE for a handle that isn't a 2D
 * shape. */
WGF_API wgf_shape2d_kind_t wgf_shape2d_get_kind(wgf_actor_t shape);

/* Makes the shape a rectangle `width` by `height` from its own origin. The origin is its
 * top-left corner, unless a pivot is set.
 *
 * Returns false for a handle that isn't a 2D shape, or a size below 0. */
WGF_API bool wgf_shape2d_set_rectangle(wgf_actor_t shape, float width, float height);

/* Makes the shape a circle of `radius` about its own origin. The origin is its center,
 * unless a pivot is set.
 *
 * Returns false for a handle that isn't a 2D shape, or a `radius` below 0. */
WGF_API bool wgf_shape2d_set_circle(wgf_actor_t shape, float radius);

/* Makes the shape a line from (x0, y0) to (x1, y1), in its own units.
 *
 * The line is as thick as the outline, and a hairline at 0. Its ends are its own, so the
 * pivot doesn't move them.
 *
 * Returns false for a handle that isn't a 2D shape. */
WGF_API bool wgf_shape2d_set_line(wgf_actor_t shape, float x0, float y0, float x1, float y1);

/* Makes the shape a polygon through `points`, in its own units.
 *
 * - `points` holds x and y in turn: `count` floats, so count / 2 points, at most 1024.
 * - The points are copied.
 * - The polygon is filled whether convex or not, or outlined as a closed line.
 * - Its points are its own, so the pivot doesn't move them.
 *
 * Returns false for a handle that isn't a 2D shape, a NULL `points`, an odd `count`, fewer
 * than 3 points, or more than 1024. */
WGF_API bool wgf_shape2d_set_polygon(wgf_actor_t shape, const float *points, int count);

/* Returns what was set.
 *
 * - get_size returns a rectangle's width and height, or 0, 0 for another kind.
 * - get_radius returns a circle's radius, or 0 for another kind.
 * - get_line_start and get_line_end return a line's ends, or 0, 0 for another kind.
 * - get_point_count returns how many points a polygon has, or 0 for another kind.
 * - get_points copies a polygon's points into `out`, x and y in turn, as many as fit in
 *   `count` floats. It returns how many floats it filled, or 0 for anything but a polygon. */
WGF_API wgf_vec2_t wgf_shape2d_get_size(wgf_actor_t shape);
WGF_API float wgf_shape2d_get_radius(wgf_actor_t shape);
WGF_API wgf_vec2_t wgf_shape2d_get_line_start(wgf_actor_t shape);
WGF_API wgf_vec2_t wgf_shape2d_get_line_end(wgf_actor_t shape);
WGF_API int wgf_shape2d_get_point_count(wgf_actor_t shape);
WGF_API int wgf_shape2d_get_points(wgf_actor_t shape, float *out, int count);

/* Sets or gets the pivot: the point of a rectangle or circle at its actor's position, which
 * it turns and scales about.
 *
 * - The pivot is a fraction of the shape: 0, 0 is its top-left, and 1, 1 its bottom-right.
 * - Unset (the default), each shape keeps its own origin: a rectangle's top-left corner, or
 *   a circle's center.
 * - The getter reads an unset pivot back as that fraction: 0, 0 or 0.5, 0.5.
 *
 * The setter returns false for a handle that isn't a 2D shape. */
WGF_API bool wgf_shape2d_set_pivot(wgf_actor_t shape, float x, float y);
WGF_API wgf_vec2_t wgf_shape2d_get_pivot(wgf_actor_t shape);

/* Sets or gets the outline: the shape is drawn as an outline `thickness` logical pixels wide,
 * centered on its edge.
 *
 * - A `thickness` of 0 (the default) fills the shape. A line is then a hairline.
 * - The thickness is clamped to 0 or more.
 *
 * The setter returns false for a handle that isn't a 2D shape. */
WGF_API bool wgf_shape2d_set_outline(wgf_actor_t shape, float thickness);
WGF_API float wgf_shape2d_get_outline(wgf_actor_t shape);

/* Sets or gets the radius a rectangle's corners are rounded to, in its own units (default: 0,
 * square).
 *
 * - The corners are rounded as wgf_draw_rectangle_rounded rounds them, filled or outlined.
 * - The radius is clamped to 0 or more, and drawn at no more than half the shorter side.
 * - It is kept for the other kinds, which it doesn't change.
 *
 * The setter returns false for a handle that isn't a 2D shape. */
WGF_API bool wgf_shape2d_set_corner_radius(wgf_actor_t shape, float radius);
WGF_API float wgf_shape2d_get_corner_radius(wgf_actor_t shape);

/* Sets or gets the shape's color (default: white). The setter returns false for a handle
 * that isn't a 2D shape. */
WGF_API bool wgf_shape2d_set_color(wgf_actor_t shape, wgf_color_t color);
WGF_API wgf_color_t wgf_shape2d_get_color(wgf_actor_t shape);

#ifdef __cplusplus
}
#endif

#endif
