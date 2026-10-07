#ifndef WGF_PHYSICS3D_JOLT_PRIV_H
#define WGF_PHYSICS3D_JOLT_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* physics3d's way into Jolt (wgf_physics3d_jolt.cpp): the calls physics3d makes, and no
 * more, in C, so C++ stays in that one file and no header outside it names Jolt (SPEC.md).
 * A body is Jolt's body id; 0xFFFFFFFF none. One world, stepped on the calling thread
 * (one thread on the web, the same everywhere for determinism), with Jolt's cross-platform
 * determinism on: the same steps give the same bits on every target. */

#define WGF_PHYSICS3D_PRIV_NO_BODY 0xFFFFFFFFu

typedef enum wgf_physics3d_priv_motion_t {
    WGF_PHYSICS3D_PRIV_STATIC = 0,
    WGF_PHYSICS3D_PRIV_DYNAMIC = 1,
    WGF_PHYSICS3D_PRIV_KINEMATIC = 2
} wgf_physics3d_priv_motion_t;

typedef enum wgf_physics3d_priv_shape_t {
    WGF_PHYSICS3D_PRIV_BOX = 0,     /* half extents in size[0..2] */
    WGF_PHYSICS3D_PRIV_SPHERE = 1,  /* radius in size[0] */
    WGF_PHYSICS3D_PRIV_CAPSULE = 2, /* radius in size[0], the cylinder's half height in size[1] */
    WGF_PHYSICS3D_PRIV_CONVEX = 3,  /* the hull of points */
    WGF_PHYSICS3D_PRIV_MESH = 4     /* triangles: points and indices (a static's) */
} wgf_physics3d_priv_shape_t;

typedef struct wgf_physics3d_priv_body_desc_t {
    wgf_physics3d_priv_motion_t motion;
    bool sensor;
    wgf_physics3d_priv_shape_t shape;
    float size[3];
    const float *points; /* x, y, z each, point_count of them, in the body's space */
    int point_count;
    const uint32_t *indices; /* three a triangle */
    int index_count;
    float position[3];
    float rotation[4]; /* x, y, z, w */
    float mass;        /* kg; 0 for the shape's own from a density of 1000 */
    float friction, restitution, linear_damping, angular_damping;
    uint32_t layer, mask; /* 15 bits each: a pair meets when each one's layer is in the other's mask */
    uint64_t user;        /* the actor */
} wgf_physics3d_priv_body_desc_t;

/* The world, with its gravity; false when it couldn't be made (logged). Started once. */
bool wgf_physics3d_priv_jolt_start(float gx, float gy, float gz);
void wgf_physics3d_priv_jolt_stop(void);
void wgf_physics3d_priv_jolt_set_gravity(float gx, float gy, float gz);

uint32_t wgf_physics3d_priv_jolt_body_create(const wgf_physics3d_priv_body_desc_t *desc);
void wgf_physics3d_priv_jolt_body_destroy(uint32_t body);

/* Where it is, and turned (x, y, z, w); set teleports it (waking it), move takes a
 * kinematic one there over the next step. */
void wgf_physics3d_priv_jolt_body_get_pose(uint32_t body, float position[3], float rotation[4]);
void wgf_physics3d_priv_jolt_body_set_pose(uint32_t body, const float position[3], const float rotation[4]);
void wgf_physics3d_priv_jolt_body_move(uint32_t body, const float position[3], const float rotation[4], float dt);
void wgf_physics3d_priv_jolt_body_get_velocity(uint32_t body, float linear[3], float angular[3]);
void wgf_physics3d_priv_jolt_body_set_velocity(uint32_t body, const float linear[3], const float angular[3]);
void wgf_physics3d_priv_jolt_body_add_impulse(uint32_t body, const float impulse[3]);
bool wgf_physics3d_priv_jolt_body_is_active(uint32_t body);

/* One step of `dt`, then the sensors' overlaps as they changed in it: each a sensor and the
 * body it began (entered) or stopped (left) overlapping, counted per pair of bodies so a
 * many-part shape enters once; taken by the caller after the step. */
void wgf_physics3d_priv_jolt_step(float dt);
typedef struct wgf_physics3d_priv_overlap_t {
    bool entered;
    uint64_t sensor, other; /* their users */
} wgf_physics3d_priv_overlap_t;
int wgf_physics3d_priv_jolt_take_overlaps(wgf_physics3d_priv_overlap_t *out, int count);

/* A wheeled vehicle on a dynamic body: wheels at points in its space (+y up, +z forward),
 * the first pair steering, driven by axle per `drive` (0 front, 1 rear, 2 both). */
typedef struct wgf_physics3d_priv_vehicle_desc_t {
    int wheel_count;     /* up to 8, in left/right pairs, front first */
    float wheels[8][3];  /* each wheel's attachment, in the body's space */
    float radius, width;
    float suspension, frequency, damping; /* travel (m), spring (Hz), damping ratio */
    float max_steer;                      /* radians */
    float grip;                           /* tire friction, 1 normal */
    float engine_torque, max_rpm;
    float gears[8];
    int gear_count;
    int drive;
} wgf_physics3d_priv_vehicle_desc_t;

/* A vehicle: an index from 0, or -1. */
int wgf_physics3d_priv_jolt_vehicle_create(uint32_t body, const wgf_physics3d_priv_vehicle_desc_t *desc);
void wgf_physics3d_priv_jolt_vehicle_destroy(int vehicle);
void wgf_physics3d_priv_jolt_vehicle_set_input(int vehicle, float forward, float right, float brake, float hand_brake);
float wgf_physics3d_priv_jolt_vehicle_get_rpm(int vehicle);
int wgf_physics3d_priv_jolt_vehicle_get_gear(int vehicle);
/* A wheel: how far its tire slides (0 gripping, about 1 sliding), and its transform in the
 * body's space (position, and rotation x, y, z, w: steered and spun). */
float wgf_physics3d_priv_jolt_vehicle_get_slip(int vehicle, int wheel);
void wgf_physics3d_priv_jolt_vehicle_get_wheel(int vehicle, int wheel, float position[3], float rotation[4]);
/* Its wheels, engine, and gearbox to rest. */
void wgf_physics3d_priv_jolt_vehicle_reset(int vehicle);

#ifdef __cplusplus
}
#endif

#endif
