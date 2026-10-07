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

/* A 2D shape: an actor drawing a rectangle, a circle, a line, or a polygon, filled or
 * outlined, in one color, on a 2D stage. Shapes are actors (wgf_actor.h): place, turn,
 * scale, parent, and destroy them with the actor calls; the shape turns and scales with
 * its actor, and an outline's thickness stays in the frame's logical pixels. */

typedef enum wgf_shape2d_kind_t {
    WGF_SHAPE2D_KIND_NONE = 0, /* nothing yet: draws nothing */
    WGF_SHAPE2D_KIND_RECTANGLE = 1,
    WGF_SHAPE2D_KIND_CIRCLE = 2,
    WGF_SHAPE2D_KIND_LINE = 3,
    WGF_SHAPE2D_KIND_POLYGON = 4
} wgf_shape2d_kind_t;

/* A shape with nothing to draw yet, white, filled. 0 when there is no room for another
 * actor. */
WGF_API wgf_actor_t wgf_shape2d_create(void);

/* What it is, set by the calls below; NONE for a handle that isn't a 2D shape. */
WGF_API wgf_shape2d_kind_t wgf_shape2d_get_kind(wgf_actor_t shape);

/* A rectangle `width` by `height` from its own origin (its top-left corner, unless a
 * pivot is set). False for a handle that isn't a 2D shape, or a size below 0. */
WGF_API bool wgf_shape2d_set_rectangle(wgf_actor_t shape, float width, float height);

/* A circle of `radius` about its own origin (its center, unless a pivot is set). False
 * for a handle that isn't a 2D shape, or a radius below 0. */
WGF_API bool wgf_shape2d_set_circle(wgf_actor_t shape, float radius);

/* A line from (x0, y0) to (x1, y1) in its own units, as thick as the outline (a hairline
 * at 0). Its ends are its own, so the pivot doesn't move them. False for a handle that
 * isn't a 2D shape. */
WGF_API bool wgf_shape2d_set_line(wgf_actor_t shape, float x0, float y0, float x1, float y1);

/* A polygon through `points` in its own units, x and y in turn (count floats: count / 2
 * points, at most 1024), copied; filled whether convex or not, or outlined, closed. Its
 * points are its own, so the pivot doesn't move them. False for a handle that isn't a
 * 2D shape, NULL, an odd count, fewer than 3 points, or more than 1024. */
WGF_API bool wgf_shape2d_set_polygon(wgf_actor_t shape, const float *points, int count);

/* What was set: a rectangle's width and height (0, 0 otherwise), a circle's radius (0
 * otherwise), a line's ends, and a polygon's points: into `out`, x and y in turn, as
 * many as fit in `count` floats, returning how many floats it filled (0 for anything
 * but a polygon). */
WGF_API wgf_vec2_t wgf_shape2d_get_size(wgf_actor_t shape);
WGF_API float wgf_shape2d_get_radius(wgf_actor_t shape);
WGF_API wgf_vec2_t wgf_shape2d_get_line_start(wgf_actor_t shape);
WGF_API wgf_vec2_t wgf_shape2d_get_line_end(wgf_actor_t shape);
WGF_API int wgf_shape2d_get_point_count(wgf_actor_t shape);
WGF_API int wgf_shape2d_get_points(wgf_actor_t shape, float *out, int count);

/* The point of a rectangle or circle at its actor's position, which it turns and scales
 * about, as a fraction of it: 0, 0 its top-left, 1, 1 its bottom-right. Unset (the
 * default), each keeps its own origin -- a rectangle's top-left corner, a circle's
 * center -- read back as that fraction (0, 0 or 0.5, 0.5). False for a handle that
 * isn't a 2D shape. */
WGF_API bool wgf_shape2d_set_pivot(wgf_actor_t shape, float x, float y);
WGF_API wgf_vec2_t wgf_shape2d_get_pivot(wgf_actor_t shape);

/* Drawn as an outline `thickness` logical pixels wide, centered on its edge; 0 (the
 * default) fills it, and a line is then a hairline. Clamped to 0 or more. False for a
 * handle that isn't a 2D shape. */
WGF_API bool wgf_shape2d_set_outline(wgf_actor_t shape, float thickness);
WGF_API float wgf_shape2d_get_outline(wgf_actor_t shape);

/* Default: white. False for a handle that isn't a 2D shape. */
WGF_API bool wgf_shape2d_set_color(wgf_actor_t shape, wgf_color_t color);
WGF_API wgf_color_t wgf_shape2d_get_color(wgf_actor_t shape);

#ifdef __cplusplus
}
#endif

#endif
