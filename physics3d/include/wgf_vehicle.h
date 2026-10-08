#ifndef WGF_VEHICLE_H
#define WGF_VEHICLE_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_vec2.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A wheeled vehicle (WGF_COMPONENT_VEHICLE, wgf_component.h) on an actor with a dynamic
 * body (wgf_body.h): wheels on springs, an engine, an automatic gearbox, and differentials,
 * driven by the program's intent (set_input), each tick, in the physics world
 * (wgf_physics.h). It faces its actor's +z, +y up.
 *
 * Its wheels are actors under it, given in left and right pairs, front first: where each
 * is as the vehicle is made is where it attaches (its spring at rest), and each tick it is
 * set where the wheel is, steered, sprung, and turning about its own x, so a model under
 * it with its axle along x shows the wheel. The first pair steers. Defaults: no wheels, an engine of 500 Nm to 6000 rpm,
 * Jolt's gear ratios, rear drive, wheels of 0.3 m by 0.2 m, 0.3 m of spring travel at 1.5
 * Hz damped by half, 0.5 radians of steering, grip 1. Its settings take effect at the next
 * tick, when it is made again from them (at rest), as a body's are; reset puts it at rest
 * where it is. The calls are false (or 0) for an actor without a vehicle. */
typedef enum wgf_vehicle_drive_t {
    WGF_VEHICLE_DRIVE_FRONT = 0,
    WGF_VEHICLE_DRIVE_REAR = 1,
    WGF_VEHICLE_DRIVE_ALL = 2
} wgf_vehicle_drive_t;

/* Its wheels: 2 to 8 actors under it, in left and right pairs, front first; false for an
 * odd count, more than 8, or one that isn't an actor under it. */
WGF_API bool wgf_vehicle_set_wheels(wgf_actor_t actor, const wgf_actor_t *wheels, int count);
WGF_API int wgf_vehicle_get_wheel_count(wgf_actor_t actor);
WGF_API wgf_actor_t wgf_vehicle_get_wheel(wgf_actor_t actor, int index);

/* Each wheel's radius and width (m), its spring's travel (m), stiffness (Hz), and damping
 * (0 none, 1 just enough), the front pair's steering (radians), and the tires' grip
 * (1 a road tire's; more grips harder). */
WGF_API bool wgf_vehicle_set_wheel_size(wgf_actor_t actor, float radius, float width);
WGF_API bool wgf_vehicle_set_suspension(wgf_actor_t actor, float travel, float stiffness, float damping);
WGF_API bool wgf_vehicle_set_steering(wgf_actor_t actor, float radians);
WGF_API bool wgf_vehicle_set_grip(wgf_actor_t actor, float grip);
/* As set: (radius, width); (travel, stiffness, damping); radians; grip. */
WGF_API wgf_vec2_t wgf_vehicle_get_wheel_size(wgf_actor_t actor);
WGF_API wgf_vec3_t wgf_vehicle_get_suspension(wgf_actor_t actor);
WGF_API float wgf_vehicle_get_steering(wgf_actor_t actor);
WGF_API float wgf_vehicle_get_grip(wgf_actor_t actor);

/* The engine's torque (Nm) and top speed (rpm), its gearbox's forward ratios (1 to 8, the
 * first the lowest: 3.2 down to 0.9, say), and which axles it drives. */
WGF_API bool wgf_vehicle_set_engine(wgf_actor_t actor, float torque, float max_rpm);
WGF_API bool wgf_vehicle_set_gears(wgf_actor_t actor, const float *ratios, int count);
WGF_API bool wgf_vehicle_set_drive(wgf_actor_t actor, wgf_vehicle_drive_t drive);
/* As set: (torque, max_rpm); how many forward ratios, and ratio `index` (0 for one it
 * hasn't); the drive. */
WGF_API wgf_vec2_t wgf_vehicle_get_engine(wgf_actor_t actor);
WGF_API int wgf_vehicle_get_gear_count(wgf_actor_t actor);
WGF_API float wgf_vehicle_get_gear_ratio(wgf_actor_t actor, int index);
WGF_API wgf_vehicle_drive_t wgf_vehicle_get_drive(wgf_actor_t actor);

/* The driver's intent, until set again: throttle 0 to 1 (below 0 is reverse), brake 0 to 1,
 * steering -1 (full left) to 1 (full right), and the hand brake on the rear wheels; each
 * clamped. Set it each tick from the input (wgf_action.h). */
WGF_API bool wgf_vehicle_set_input(wgf_actor_t actor, float throttle, float brake, float steer, bool hand_brake);

/* How it goes: its speed along its +z (m/s; below 0 backing), the engine's rpm, the gear
 * it is in (1 the lowest, -1 reverse, 0 neutral), and how far wheel `index`'s tire slides
 * (0 gripping, 1 sliding: tire smoke above about 0.3; 0 for a wheel it hasn't, or in the
 * air). */
WGF_API float wgf_vehicle_get_speed(wgf_actor_t actor);
WGF_API float wgf_vehicle_get_rpm(wgf_actor_t actor);
WGF_API int wgf_vehicle_get_gear(wgf_actor_t actor);
WGF_API float wgf_vehicle_get_wheel_slip(wgf_actor_t actor, int index);

/* At rest where it is: its velocity, spin, wheels, engine, and gearbox stopped, its intent
 * none (a car put back on the grid: set its transform, then reset). */
WGF_API bool wgf_vehicle_reset(wgf_actor_t actor);

#ifdef __cplusplus
}
#endif

#endif
