#ifndef WGF_COLLIDE3D_H
#define WGF_COLLIDE3D_H

#include <stdbool.h>

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 3D collision as plain math, with no actor or body.
 *
 * It tests boxes and spheres against each other, and casts rays against them and against
 * triangles. To cast against a physics world instead, see wgf_physics3d.h.
 *
 * - A box is axis-aligned: its least corner, then its greatest (x0, y0, z0, x1, y1, z1).
 * - A ray is its origin, then its direction. The direction needn't be a unit vector.
 *
 * What a test found goes into `hit`, an array you pass. As many of these values as fit in
 * `hit_count` are written. Pass NULL and 0 for none. The layout is wgf_collide2d.h's, with z:
 *
 *   0, 1, 2  the normal: the way out of the second shape, a unit vector. For a ray, the
 *            surface's normal at the point it met, facing the ray (0, 0, 0 when it started inside)
 *   3        the depth of an overlap along the normal (0 for a ray)
 *   4, 5, 6  the point: the second shape's point nearest the first's center, or where the
 *            ray met it
 *   7        the time: how far along the ray, in lengths of its direction (0 for an overlap,
 *            and 0 for a ray starting inside) */

/* Point tests. Each returns whether the point px, py, pz is inside the box or the sphere. */
WGF_API bool wgf_collide3d_point_in_box(float px, float py, float pz, float x0, float y0, float z0, float x1, float y1,
                                        float z1);
WGF_API bool wgf_collide3d_point_in_sphere(float px, float py, float pz, float cx, float cy, float cz, float r);

/* Overlap tests: two boxes, two spheres, or a sphere and a box.
 *
 * Touching doesn't count as overlapping. */
WGF_API bool wgf_collide3d_box_box(float ax0, float ay0, float az0, float ax1, float ay1, float az1, float bx0, float by0,
                                   float bz0, float bx1, float by1, float bz1, float *hit, int hit_count);
WGF_API bool wgf_collide3d_sphere_sphere(float ax, float ay, float az, float ar, float bx, float by, float bz, float br,
                                         float *hit, int hit_count);
WGF_API bool wgf_collide3d_sphere_box(float cx, float cy, float cz, float r, float x0, float y0, float z0, float x1,
                                      float y1, float z1, float *hit, int hit_count);

/* Casts a ray against a sphere, a box, or a triangle, and finds where it first meets it.
 *
 * - Only a meeting at or after the ray's origin counts.
 * - A triangle is its points in order, and either face is hit.
 * - The ray goes on without end. The time in the hit says how far it went, so a game that
 *   wants a segment keeps only a time of 1 or less.
 *
 * Returns false if the ray meets none. */
WGF_API bool wgf_collide3d_ray_sphere(float ox, float oy, float oz, float dx, float dy, float dz, float cx, float cy,
                                      float cz, float r, float *hit, int hit_count);
WGF_API bool wgf_collide3d_ray_box(float ox, float oy, float oz, float dx, float dy, float dz, float x0, float y0,
                                   float z0, float x1, float y1, float z1, float *hit, int hit_count);
WGF_API bool wgf_collide3d_ray_triangle(float ox, float oy, float oz, float dx, float dy, float dz, const float *points,
                                        int count, float *hit, int hit_count);

#ifdef __cplusplus
}
#endif

#endif /* WGF_COLLIDE3D_H */
