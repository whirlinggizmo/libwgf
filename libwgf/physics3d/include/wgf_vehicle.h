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

/* A wheeled vehicle (WGF_COMPONENT_VEHICLE, wgf_component.h), on an actor with a dynamic body
 * (wgf_body3d.h).
 *
 * A vehicle has wheels on springs, an engine, an automatic gearbox, and differentials. Each
 * tick it is driven by your game's intent (see wgf_vehicle_set_input), in the physics world
 * (wgf_physics3d.h). It faces its actor's +z, with +y up.
 *
 * Its wheels are actors under it, given in left and right pairs, front first.
 * - Where each wheel is when the vehicle is made is where it attaches, with its spring at
 *   rest.
 * - Each tick, the wheel's actor is set where the wheel is: steered, sprung, and turning about
 *   its own x axis. A model under it with its axle along x shows the wheel.
 * - The first pair steers.
 *
 * Defaults:
 * - no wheels
 * - an engine of 500 Nm, up to 6000 rpm
 * - Jolt's gear ratios
 * - rear drive
 * - wheels 0.3 meters in radius and 0.2 meters wide
 * - 0.3 meters of spring travel, at 1.5 Hz, damped by half
 * - 0.5 radians of steering
 * - grip 1
 *
 * Its settings take effect at the next tick, when it is made again from them, at rest, as a
 * body's are. A change to its anti-roll bars is the exception: it takes effect at once.
 * wgf_vehicle_reset puts it at rest where it is.
 *
 * The calls return false (or 0) for an actor without a vehicle. */
typedef enum wgf_vehicle_drive_t {
    WGF_VEHICLE_DRIVE_FRONT = 0,
    WGF_VEHICLE_DRIVE_REAR = 1,
    WGF_VEHICLE_DRIVE_ALL = 2
} wgf_vehicle_drive_t;

/* Sets or gets the vehicle's wheels: 2 to 8 actors under it, in left and right pairs, front
 * first. The setter returns false for an odd count, more than 8, or a wheel that isn't an
 * actor under it. */
WGF_API bool wgf_vehicle_set_wheels(wgf_actor_t actor, const wgf_actor_t *wheels, int count);
WGF_API int wgf_vehicle_get_wheel_count(wgf_actor_t actor);
WGF_API wgf_actor_t wgf_vehicle_get_wheel(wgf_actor_t actor, int index);

/* Sets the wheels, springs, steering, and grip.
 *
 * - set_wheel_size: each wheel's radius and width, in meters.
 * - set_suspension: each spring's travel (meters), stiffness (Hz), and damping (0 is none, and
 *   1 is just enough).
 * - set_steering: how far the front pair steers, in radians.
 * - set_grip: the tires' grip. 1 is a road tire's, and more grips harder. */
WGF_API bool wgf_vehicle_set_wheel_size(wgf_actor_t actor, float radius, float width);
WGF_API bool wgf_vehicle_set_suspension(wgf_actor_t actor, float travel, float stiffness, float damping);
WGF_API bool wgf_vehicle_set_steering(wgf_actor_t actor, float radians);
WGF_API bool wgf_vehicle_set_grip(wgf_actor_t actor, float grip);
/* Sets or gets each axle's anti-roll bar stiffness, in N/m (default: 0, none). The stiffer it
 * is, the less the vehicle leans in a corner. A change takes effect at once. */
WGF_API bool wgf_vehicle_set_anti_roll(wgf_actor_t actor, float stiffness);
WGF_API float wgf_vehicle_get_anti_roll(wgf_actor_t actor);

/* Returns the settings as set.
 *
 * - get_wheel_size: (radius, width).
 * - get_suspension: (travel, stiffness, damping).
 * - get_steering: radians.
 * - get_grip: the grip. */
WGF_API wgf_vec2_t wgf_vehicle_get_wheel_size(wgf_actor_t actor);
WGF_API wgf_vec3_t wgf_vehicle_get_suspension(wgf_actor_t actor);
WGF_API float wgf_vehicle_get_steering(wgf_actor_t actor);
WGF_API float wgf_vehicle_get_grip(wgf_actor_t actor);

/* Sets the engine, the gearbox, and the drive.
 *
 * - set_engine: the engine's torque (Nm) and top speed (rpm).
 * - set_gears: the gearbox's forward ratios, 1 to 8 of them, the first the lowest. For
 *   example, 3.2 down to 0.9.
 * - set_drive: which axles the engine drives. */
WGF_API bool wgf_vehicle_set_engine(wgf_actor_t actor, float torque, float max_rpm);
WGF_API bool wgf_vehicle_set_gears(wgf_actor_t actor, const float *ratios, int count);
WGF_API bool wgf_vehicle_set_drive(wgf_actor_t actor, wgf_vehicle_drive_t drive);
/* Returns the engine, gearbox, and drive as set.
 *
 * - get_engine: (torque, max_rpm).
 * - get_gear_count: how many forward ratios.
 * - get_gear_ratio: ratio `index`, or 0 for one the gearbox doesn't have.
 * - get_drive: the drive. */
WGF_API wgf_vec2_t wgf_vehicle_get_engine(wgf_actor_t actor);
WGF_API int wgf_vehicle_get_gear_count(wgf_actor_t actor);
WGF_API float wgf_vehicle_get_gear_ratio(wgf_actor_t actor, int index);
WGF_API wgf_vehicle_drive_t wgf_vehicle_get_drive(wgf_actor_t actor);

/* Sets the driver's intent, which holds until it is set again. Set it each tick from the input
 * (wgf_action.h). Each value is clamped.
 *
 * - `throttle`: 0 to 1. Below 0 is reverse.
 * - `brake`: 0 to 1.
 * - `steer`: -1 (full left) to 1 (full right).
 * - `hand_brake`: the hand brake, on the rear wheels. */
WGF_API bool wgf_vehicle_set_input(wgf_actor_t actor, float throttle, float brake, float steer, bool hand_brake);

/* Returns how the vehicle is going.
 *
 * - get_speed: its speed along its +z, in meters a second. Below 0 is backing up.
 * - get_rpm: the engine's rpm.
 * - get_gear: the gear it is in. 1 is the lowest, -1 is reverse, and 0 is neutral.
 * - get_wheel_slip: how hard wheel `index`'s tire works, as a share of its grip's peak. It is
 *   the larger of the slip along the tire and across it.
 *
 * Wheel slip:
 * - Below 1, the tire grips. A free-rolling wheel reads a little above 0, from its small slip
 *   angle.
 * - 1 is at the limit.
 * - Above 1 it is sliding or spinning, up to 10. Traction control keeps it near 1, and tire
 *   smoke shows above about 1.5.
 * - It is 0 for a wheel the vehicle doesn't have, or a wheel in the air. */
WGF_API float wgf_vehicle_get_speed(wgf_actor_t actor);
WGF_API float wgf_vehicle_get_rpm(wgf_actor_t actor);
WGF_API int wgf_vehicle_get_gear(wgf_actor_t actor);
WGF_API float wgf_vehicle_get_wheel_slip(wgf_actor_t actor, int index);

/* Puts the vehicle at rest where it is. Its velocity, spin, wheels, engine, and gearbox are
 * stopped, and its intent is cleared. To put a car back on the grid, set its transform, then
 * reset it. */
WGF_API bool wgf_vehicle_reset(wgf_actor_t actor);

#ifdef __cplusplus
}
#endif

#endif
