#ifndef WGF_COLLIDE3D_H
#define WGF_COLLIDE3D_H

#include <stdbool.h>

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 3D collision as plain math, raylib's shapes (its CheckCollision* and GetRayCollision*):
 * boxes, spheres, and rays against them and triangles, with no actor or body
 * (wgf_physics3d.h casts against a physics world). A box is axis-aligned, its least corner and
 * its greatest (x0, y0, z0, x1, y1, z1); a ray its origin and its direction, which needn't be
 * a unit vector.
 *
 * What a test found goes into `hit`, a caller's array, as many of these as fit in
 * `hit_count` (NULL, 0 for none), as wgf_collide2d.h's with z:
 *   0, 1, 2  the normal: the way out of the second shape (a unit vector; a ray's: the
 *            surface's at the point it met, facing the ray; 0, 0, 0 when it started inside)
 *   3        how deep an overlap goes along the normal (0 for a ray)
 *   4, 5, 6  the point: of the second shape nearest the first's center, or where the ray met
 *   7        how far along the ray, in its direction's lengths (0 for an overlap; 0 too for a
 *            ray starting inside) */

/* Points. */
WGF_API bool wgf_collide3d_point_in_box(float px, float py, float pz, float x0, float y0, float z0, float x1, float y1,
                                        float z1);
WGF_API bool wgf_collide3d_point_in_sphere(float px, float py, float pz, float cx, float cy, float cz, float r);

/* Overlaps: two boxes, two spheres, a sphere and a box. Touching doesn't count. */
WGF_API bool wgf_collide3d_box_box(float ax0, float ay0, float az0, float ax1, float ay1, float az1, float bx0, float by0,
                                   float bz0, float bx1, float by1, float bz1, float *hit, int hit_count);
WGF_API bool wgf_collide3d_sphere_sphere(float ax, float ay, float az, float ar, float bx, float by, float bz, float br,
                                         float *hit, int hit_count);
WGF_API bool wgf_collide3d_sphere_box(float cx, float cy, float cz, float r, float x0, float y0, float z0, float x1,
                                      float y1, float z1, float *hit, int hit_count);

/* A ray's first meeting with a sphere, a box, or a triangle (either face; its points in
 * order), at or after its origin; false for none. The ray goes on without end: the time in
 * the hit says how far, for a game that wants a segment (a time of 1 or less). */
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
