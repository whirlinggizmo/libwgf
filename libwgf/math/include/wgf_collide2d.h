#ifndef WGF_COLLIDE2D_H
#define WGF_COLLIDE2D_H

#include <stdbool.h>

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 2D collision as plain math, helix-legacy's Collide2D: shapes tested against each other with
 * no actor or collider, rays and sweeps, and a box moved and slid against solid rectangles
 * or a grid of solid cells (a platformer's, a top-down game's). y runs down, as a 2D stage's
 * does. A rectangle is its top-left x, y and its width and height; a polygon its points, x
 * and y in turn (`count` floats).
 *
 * What a test found goes into `hit`, a caller's array, as many of these as fit in
 * `hit_count` (NULL, 0 for none):
 *   0, 1  the normal: the way out of the second shape (a unit vector; 0, 0 when a ray or
 *         sweep started inside)
 *   2     how deep an overlap goes along the normal: the least move apart (0 for a ray)
 *   3, 4  the point: of the second shape nearest the first's center, or where a ray or
 *         sweep met it (a sweep's: the moving shape's top-left or center then)
 *   5     how far along a ray or sweep, 0 at its start to 1 at its end (0 for an overlap) */

/* Overlaps. */
WGF_API bool wgf_collide2d_rect_rect(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh,
                                     float *hit, int hit_count);
WGF_API bool wgf_collide2d_circle_circle(float ax, float ay, float ar, float bx, float by, float br, float *hit,
                                         int hit_count);
WGF_API bool wgf_collide2d_circle_rect(float cx, float cy, float r, float rx, float ry, float rw, float rh, float *hit,
                                       int hit_count);
/* A circle and the segment from x0, y0 to x1, y1 (a wall, a slope). */
WGF_API bool wgf_collide2d_circle_segment(float cx, float cy, float r, float x0, float y0, float x1, float y1,
                                          float *hit, int hit_count);
/* A circle and a polygon, convex or not, as long as its edges don't cross. */
WGF_API bool wgf_collide2d_circle_polygon(float cx, float cy, float r, const float *points, int count, float *hit,
                                          int hit_count);
/* Two convex polygons (the separating axis theorem); the hit's point is 0, 0. Either may
 * be a segment, two points. */
WGF_API bool wgf_collide2d_polygon_polygon(const float *points, int count, const float *others, int others_count,
                                           float *hit, int hit_count);

/* Points: in a rectangle (its left and top edges count, its right and bottom don't), a
 * circle, a triangle, a polygon (convex or not, by crossings). */
WGF_API bool wgf_collide2d_point_in_rect(float px, float py, float rx, float ry, float rw, float rh);
WGF_API bool wgf_collide2d_point_in_circle(float px, float py, float cx, float cy, float r);
WGF_API bool wgf_collide2d_point_in_triangle(float px, float py, float x0, float y0, float x1, float y1, float x2,
                                             float y2);
WGF_API bool wgf_collide2d_point_in_polygon(float px, float py, const float *points, int count);

/* The segment a crossing segment b (not only touching, nor lying along it): the hit's time
 * along a, the point, and b's normal facing a's start. */
WGF_API bool wgf_collide2d_segment_segment(float ax0, float ay0, float ax1, float ay1, float bx0, float by0, float bx1,
                                           float by1, float *hit, int hit_count);

/* A ray from ox, oy along dx, dy (its whole length: time 1 is its end) meeting a rectangle or
 * a circle: the time, the point, the normal. */
WGF_API bool wgf_collide2d_ray_rect(float ox, float oy, float dx, float dy, float rx, float ry, float rw, float rh,
                                    float *hit, int hit_count);
WGF_API bool wgf_collide2d_ray_circle(float ox, float oy, float dx, float dy, float cx, float cy, float r, float *hit,
                                      int hit_count);

/* A rectangle (a circle) moved by dx, dy meeting another on the way: the time, its top-left
 * (its center) then, and the normal; time 0 and no normal when they already overlap. */
WGF_API bool wgf_collide2d_sweep_rect_rect(float ax, float ay, float aw, float ah, float dx, float dy, float bx, float by,
                                           float bw, float bh, float *hit, int hit_count);
WGF_API bool wgf_collide2d_sweep_circle_circle(float ax, float ay, float ar, float dx, float dy, float bx, float by,
                                               float br, float *hit, int hit_count);

/* Move and slide: the box at x, y (its top-left), w by h, moved by dx, dy, across first,
 * stopped at the nearest solid's side the way it moves, then down or up at its new x,
 * sliding along what it meets; into `out` (as fit in `out_count`) where it ended, x and y,
 * then whether its left, right, top, and bottom met a solid (1 or 0: bottom is standing on
 * something). A box starting inside a solid is left where it is on that axis. False for no
 * `out`.
 *
 * Against solid rectangles (`rects`, four floats each, `count` floats), or a grid of cells
 * (`cells`, row by row from the top, `columns` a row, `count` of them, nonzero
 * solid; cell 0's top-left at 0, 0, each `size` square; outside the grid is open). */
WGF_API bool wgf_collide2d_move_rects(float x, float y, float w, float h, float dx, float dy, const float *rects,
                                      int count, float *out, int out_count);
WGF_API bool wgf_collide2d_move_cells(float x, float y, float w, float h, float dx, float dy, float size,
                                      const int *cells, int count, int columns, float *out, int out_count);

#ifdef __cplusplus
}
#endif

#endif /* WGF_COLLIDE2D_H */
