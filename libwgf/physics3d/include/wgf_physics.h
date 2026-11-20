#ifndef WGF_PHYSICS_H
#define WGF_PHYSICS_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Physics in 3D: rigid bodies on actors (wgf_body.h), and wheeled vehicles (wgf_vehicle.h),
 * simulated in the tick, after the program's and the ecs's systems. One world for every
 * 3D stage, its gravity set here; a body's actor moves where the world moves it, drawn
 * between ticks as any simulated actor is (wgf_actor.h). The same steps make the same world
 * to the bit on every target, so an autopilot's lap is the same on the desktop and in a
 * browser.
 *
 * Physics is an optional part: a program that never starts it links none of it. It starts
 * with the first of these calls, or of wgf_body's or wgf_vehicle's; until then a body or a
 * vehicle component (added, or a scene's line) is refused, and logged once. A game whose
 * scene has bodies sets the gravity before it loads the scene. */

/* The world's gravity, in units a second squared (default 0, -9.81, 0: y up, in meters);
 * starting physics. False only when physics couldn't start (logged). */
WGF_API bool wgf_physics_set_gravity(float x, float y, float z);
WGF_API wgf_vec3_t wgf_physics_get_gravity(void);

/* Every body's shape outlined in `color` (a sensor's in half its alpha), and each vehicle's
 * wheels: a debug view, drawn into the 3D drawing begun (wgf_draw_begin_3d, wgf_draw.h);
 * nothing outside one, or before physics starts. A mesh is drawn by its triangles' edges. */
WGF_API void wgf_physics_draw_bodies(wgf_color_t color);

/* Queries of the world as its bodies are now (their last tick's), in its 3D stage's space,
 * each meeting the bodies whose layer is in `mask` (wgf_body_set_layer) and never a sensor;
 * starting physics.
 *
 * A ray from x, y, z along dx, dy, dz (any length; it is normalized) for `distance`, and a
 * sphere of `radius` swept so: what they hit, nearest first, a body once (its nearest
 * hit), as many as fit in `count`: each one's actor into `actors`, and into `hits`, when
 * it is given, 7 floats a hit as fit in `hits_count` (the point, the surface's normal
 * there, and the distance along the ray), returning how many hits. 0 for nothing hit, a
 * direction of no length, a distance or radius not above 0, or no array. */
WGF_API int wgf_physics_cast_ray(float x, float y, float z, float dx, float dy, float dz, float distance, int mask,
                                 wgf_actor_t *actors, int count, float *hits, int hits_count);
WGF_API int wgf_physics_cast_sphere(float x, float y, float z, float radius, float dx, float dy, float dz,
                                    float distance, int mask, wgf_actor_t *actors, int count, float *hits,
                                    int hits_count);

/* The bodies overlapping a sphere of `radius` at x, y, z, or a box of width, height, and
 * depth centered there (its axes the world's), in no order, as many as fit in `count`,
 * returning how many. 0 for none, a size not above 0, or no array. */
WGF_API int wgf_physics_overlap_sphere(float x, float y, float z, float radius, int mask, wgf_actor_t *actors,
                                       int count);
WGF_API int wgf_physics_overlap_box(float x, float y, float z, float width, float height, float depth, int mask,
                                    wgf_actor_t *actors, int count);

#ifdef __cplusplus
}
#endif

#endif
