#ifndef WGF_PHYSICS3D_H
#define WGF_PHYSICS3D_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Physics in 3D: rigid bodies on actors (wgf_body3d.h), and wheeled vehicles (wgf_vehicle.h).
 *
 * Physics is simulated in the tick, after your game's tick and the ecs's systems.
 * - There is one world for every 3D stage, and its gravity is set here.
 * - A body's actor moves where the world moves it. It is drawn between ticks, as any
 *   simulated actor is (wgf_actor.h).
 * - The same steps make the same world, to the bit, on every target. An autopilot's lap is
 *   the same on the desktop and in a browser.
 *
 * Physics is an optional part: a game that never starts it links none of it. It starts with
 * the first call in this header, or in wgf_body3d.h or wgf_vehicle.h. Until then, a body or
 * vehicle component is refused (logged once), whether it is added or is a scene's line. A
 * game whose scene has bodies sets the gravity before it loads the scene. */

/* Enables characters (WGF_BODY3D_TYPE_CHARACTER in wgf_body3d.h, Jolt's CharacterVirtual), or
 * gets whether they are enabled.
 *
 * Characters are linked on purpose. This call links them, so a game with no characters carries
 * none of their code (about 42 KB). A game enables them with "characters" in wgf.json's
 * features. Without it, a character type is refused (logged), in a scene too. Call it any time
 * before the first character. */
WGF_API bool wgf_physics3d_enable_characters(void);
WGF_API bool wgf_physics3d_is_characters_enabled(void);

/* Enables vehicles (wgf_vehicle.h, Jolt's VehicleConstraint), or gets whether they are
 * enabled.
 *
 * Vehicles are linked on purpose. This call links them, so a game with physics and no car
 * carries none of their code (about 46 KB). A game enables them with "vehicles" in wgf.json's
 * features. Without it, a vehicle component is refused (logged), in a scene too. Call it any
 * time before the first vehicle. */
WGF_API bool wgf_physics3d_enable_vehicles(void);
WGF_API bool wgf_physics3d_is_vehicles_enabled(void);

/* Sets or gets the world's gravity, in units a second squared (default: 0, -9.81, 0, with y
 * up and units in meters). Setting it starts physics. The setter returns false only when
 * physics couldn't start (logged). */
WGF_API bool wgf_physics3d_set_gravity(float x, float y, float z);
WGF_API wgf_vec3_t wgf_physics3d_get_gravity(void);

/* Draws every body's shape outlined in `color`, and each vehicle's wheels, as a debug view.
 *
 * - A sensor is outlined at half the color's alpha.
 * - A mesh is drawn by its triangles' edges.
 * - It draws into the 3D drawing begun (wgf_draw_begin_3d, wgf_draw.h). It draws nothing
 *   outside one, or before physics starts. */
WGF_API void wgf_physics3d_draw_bodies(wgf_color_t color);

/* Casts a ray or a sphere through the world, and returns what it hits.
 *
 * The queries in this header see the bodies as they are now (as of their last tick), in their
 * 3D stage's space. Each meets the bodies whose layer is in `mask` (wgf_body3d_set_layer), and
 * never a sensor. Each starts physics.
 *
 * - The ray starts at `x`, `y`, `z` and goes along `dx`, `dy`, `dz` for `distance`. The
 *   direction may be any length: it is normalized.
 * - cast_sphere sweeps a sphere of `radius` the same way.
 * - Hits come nearest first, each body once (its nearest hit), as many as fit in `count`.
 * - Each hit's actor goes into `actors`.
 * - If `hits` is given, each hit also fills 7 floats of it, as many as fit in `hits_count`:
 *   the point, the surface's normal there, and the distance along the ray.
 *
 * Returns how many hits. Returns 0 for nothing hit, a direction of no length, a distance or
 * radius not above 0, or no array. */
WGF_API int wgf_physics3d_cast_ray(float x, float y, float z, float dx, float dy, float dz, float distance, int mask,
                                 wgf_actor_t *actors, int count, float *hits, int hits_count);
WGF_API int wgf_physics3d_cast_sphere(float x, float y, float z, float radius, float dx, float dy, float dz,
                                    float distance, int mask, wgf_actor_t *actors, int count, float *hits,
                                    int hits_count);

/* Finds the bodies overlapping a sphere or a box, and returns how many.
 *
 * - overlap_sphere: a sphere of `radius` at `x`, `y`, `z`.
 * - overlap_box: a box `width`, `height`, and `depth`, centered there, with the world's axes.
 *
 * The bodies go into `actors` in no order, as many as fit in `count`. Like the casts, these
 * see the bodies as of their last tick, meet only those whose layer is in `mask` and never a
 * sensor, and start physics. Returns 0 for none, a size not above 0, or no array. */
WGF_API int wgf_physics3d_overlap_sphere(float x, float y, float z, float radius, int mask, wgf_actor_t *actors,
                                       int count);
WGF_API int wgf_physics3d_overlap_box(float x, float y, float z, float width, float height, float depth, int mask,
                                    wgf_actor_t *actors, int count);

#ifdef __cplusplus
}
#endif

#endif
