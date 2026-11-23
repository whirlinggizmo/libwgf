#ifndef WGF_COLLIDE2D_H
#define WGF_COLLIDE2D_H

#include <stdbool.h>

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 2D collision as plain math, with no actor or collider.
 *
 * - Shapes are tested against each other: rectangles, circles, segments, and polygons.
 * - Rays are cast, and moving shapes swept.
 * - A box is moved and slid against solid rectangles, or against a grid of solid cells, as a
 *   platformer or a top-down game needs.
 *
 * y runs down, as it does on a 2D stage. A rectangle is its top-left x and y, then its width
 * and its height. A polygon is its points, x and y in turn, `count` floats in all.
 *
 * What a test found goes into `hit`, an array you pass. As many of these values as fit in
 * `hit_count` are written. Pass NULL and 0 for none.
 *
 *   0, 1  the normal: the way out of the second shape, a unit vector (0, 0 when a ray or
 *         sweep started inside)
 *   2     the depth of an overlap along the normal: the least move apart (0 for a ray)
 *   3, 4  the point: the second shape's point nearest the first's center, or where a ray or
 *         sweep met it (for a sweep, the moving shape's top-left or center at that time)
 *   5     the time: how far along a ray or sweep, from 0 at its start to 1 at its end
 *         (0 for an overlap) */

/* Overlap tests. Each returns true if its two shapes overlap, and fills `hit`. */
WGF_API bool wgf_collide2d_rect_rect(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh,
                                     float *hit, int hit_count);
WGF_API bool wgf_collide2d_circle_circle(float ax, float ay, float ar, float bx, float by, float br, float *hit,
                                         int hit_count);
WGF_API bool wgf_collide2d_circle_rect(float cx, float cy, float r, float rx, float ry, float rw, float rh, float *hit,
                                       int hit_count);
/* Tests a circle against the segment from x0, y0 to x1, y1, such as a wall or a slope. */
WGF_API bool wgf_collide2d_circle_segment(float cx, float cy, float r, float x0, float y0, float x1, float y1,
                                          float *hit, int hit_count);
/* Tests a circle against a polygon.
 *
 * The polygon may be convex or not, as long as its edges don't cross. */
WGF_API bool wgf_collide2d_circle_polygon(float cx, float cy, float r, const float *points, int count, float *hit,
                                          int hit_count);
/* Tests two convex polygons against each other, by the separating axis theorem.
 *
 * Either polygon may be a segment of two points. The hit's point is always 0, 0. */
WGF_API bool wgf_collide2d_polygon_polygon(const float *points, int count, const float *others, int others_count,
                                           float *hit, int hit_count);

/* Point tests. Each returns whether the point px, py is inside the shape.
 *
 * - A rectangle: its left and top edges count as inside, its right and bottom edges don't.
 * - A circle.
 * - A triangle.
 * - A polygon, convex or not, tested by counting crossings. */
WGF_API bool wgf_collide2d_point_in_rect(float px, float py, float rx, float ry, float rw, float rh);
WGF_API bool wgf_collide2d_point_in_circle(float px, float py, float cx, float cy, float r);
WGF_API bool wgf_collide2d_point_in_triangle(float px, float py, float x0, float y0, float x1, float y1, float x2,
                                             float y2);
WGF_API bool wgf_collide2d_point_in_polygon(float px, float py, const float *points, int count);

/* Tests whether segment a crosses segment b.
 *
 * Only a true crossing counts: touching, or lying along b, doesn't. The hit holds the time
 * along a, the point, and b's normal, facing a's start. */
WGF_API bool wgf_collide2d_segment_segment(float ax0, float ay0, float ax1, float ay1, float bx0, float by0, float bx1,
                                           float by1, float *hit, int hit_count);

/* Casts a ray from ox, oy along dx, dy against a rectangle or a circle.
 *
 * The ray is dx, dy long: time 1 is its end. The hit holds the time, the point, and the
 * normal. */
WGF_API bool wgf_collide2d_ray_rect(float ox, float oy, float dx, float dy, float rx, float ry, float rw, float rh,
                                    float *hit, int hit_count);
WGF_API bool wgf_collide2d_ray_circle(float ox, float oy, float dx, float dy, float cx, float cy, float r, float *hit,
                                      int hit_count);

/* Sweeps a rectangle, or a circle, by dx, dy, and tests whether it meets another on the way.
 *
 * The hit holds the time, the moving shape's top-left (for a circle, its center) at that
 * time, and the normal. If they already overlap, the time is 0 and there is no normal. */
WGF_API bool wgf_collide2d_sweep_rect_rect(float ax, float ay, float aw, float ah, float dx, float dy, float bx,
                                           float by, float bw, float bh, float *hit, int hit_count);
WGF_API bool wgf_collide2d_sweep_circle_circle(float ax, float ay, float ar, float dx, float dy, float bx, float by,
                                               float br, float *hit, int hit_count);

/* Moves a box by dx, dy and slides it along the solids it meets.
 *
 * The box's top-left is at x, y, and it is w by h. It moves across first, and stops at the
 * nearest solid's side in the direction it moves. Then it moves down or up at its new x, in
 * the same way. A box that starts inside a solid is left where it is on that axis.
 *
 * As many of these values as fit in `out_count` are written to `out`:
 * - 0, 1: where the box ended, x and y.
 * - 2 to 5: whether its left, right, top, and bottom met a solid, 1 or 0. A bottom of 1
 *   means the box is standing on a solid.
 *
 * The solids are one of these:
 * - move_rects: solid rectangles in `rects`, four floats each, `count` floats in all.
 * - move_cells: a grid of `count` cells in `cells`, row by row from the top, `columns` to a
 *   row. A nonzero cell is solid. Each cell is `size` square, and cell 0's top-left is at
 *   0, 0. Outside the grid is open.
 *
 * Returns false if there is no `out` (NULL, or an `out_count` of 0). */
WGF_API bool wgf_collide2d_move_rects(float x, float y, float w, float h, float dx, float dy, const float *rects,
                                      int count, float *out, int out_count);
WGF_API bool wgf_collide2d_move_cells(float x, float y, float w, float h, float dx, float dy, float size,
                                      const int *cells, int count, int columns, float *out, int out_count);

#ifdef __cplusplus
}
#endif

#endif /* WGF_COLLIDE2D_H */
