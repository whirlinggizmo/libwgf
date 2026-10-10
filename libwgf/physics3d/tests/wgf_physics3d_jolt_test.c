#include <stdio.h>
#include <string.h>

#include "wgf_physics3d_jolt_priv.h"

/* physics3d's way into Jolt, alone: a box through a sensor onto a floor, a car driven ten
 * seconds and turned, a body destroyed in a sensor. Its numbers are pinned to the bit, the
 * same on every target (Jolt's cross-platform determinism): a target that gives others has
 * lost it, and a lap would replay differently there. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static wgf_physics3d_priv_body_desc_t desc(wgf_physics3d_priv_motion_t motion, wgf_physics3d_priv_shape_t shape,
                                           float x, float y, float z, float sx, float sy, float sz, uint64_t user)
{
    wgf_physics3d_priv_body_desc_t d;
    memset(&d, 0, sizeof(d));
    d.motion = motion;
    d.shape = shape;
    d.size[0] = sx;
    d.size[1] = sy;
    d.size[2] = sz;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    d.rotation[3] = 1.0f;
    d.friction = 0.8f;
    d.layer = 1;
    d.mask = 0x7FFF;
    d.user = user;
    return d;
}

/* The bits of a float, to compare exactly and to print what a target gave. */
static unsigned bits(float f)
{
    unsigned u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

int main(void)
{
    wgf_physics3d_priv_body_desc_t d;
    wgf_physics3d_priv_vehicle_desc_t v;
    wgf_physics3d_priv_overlap_t o[8];
    uint32_t box, sensor, car, gone;
    float p[3], q[4];
    int i, n, vehicle, entered = 0, left = 0, enter_tick = -1, exit_tick = -1;

    expect(wgf_physics3d_priv_jolt_start(0.0f, -9.81f, 0.0f), "started");
    d = desc(WGF_PHYSICS3D_PRIV_STATIC, WGF_PHYSICS3D_PRIV_BOX, 0, -0.5f, 0, 1000, 0.5f, 1000, 1);
    expect(wgf_physics3d_priv_jolt_body_create(&d) != WGF_PHYSICS3D_PRIV_NO_BODY, "a floor");
    d = desc(WGF_PHYSICS3D_PRIV_DYNAMIC, WGF_PHYSICS3D_PRIV_BOX, 5, 5, 0, 0.5f, 0.5f, 0.5f, 2);
    d.mass = 10.0f;
    box = wgf_physics3d_priv_jolt_body_create(&d);
    d = desc(WGF_PHYSICS3D_PRIV_STATIC, WGF_PHYSICS3D_PRIV_BOX, 5, 2, 0, 1, 0.5f, 1, 3);
    d.sensor = true;
    sensor = wgf_physics3d_priv_jolt_body_create(&d);
    d = desc(WGF_PHYSICS3D_PRIV_DYNAMIC, WGF_PHYSICS3D_PRIV_BOX, 0, 1.2f, 0, 0.9f, 0.3f, 2.0f, 4);
    d.mass = 1200.0f;
    car = wgf_physics3d_priv_jolt_body_create(&d);
    expect(box != WGF_PHYSICS3D_PRIV_NO_BODY && sensor != WGF_PHYSICS3D_PRIV_NO_BODY && car != WGF_PHYSICS3D_PRIV_NO_BODY,
           "a box, a sensor, a car's body");

    memset(&v, 0, sizeof(v));
    v.wheel_count = 4;
    for (i = 0; i < 4; i++) {
        v.wheels[i][0] = i % 2 ? 0.8f : -0.8f;
        v.wheels[i][1] = -0.1f;
        v.wheels[i][2] = i < 2 ? 1.4f : -1.4f;
    }
    v.radius = 0.34f;
    v.width = 0.22f;
    v.suspension = 0.3f;
    v.frequency = 1.6f;
    v.damping = 0.5f;
    v.max_steer = 0.5f;
    v.grip = 1.2f;
    v.engine_torque = 420.0f;
    v.max_rpm = 7000.0f;
    v.drive = 1;
    vehicle = wgf_physics3d_priv_jolt_vehicle_create(car, &v);
    expect(vehicle >= 0, "a vehicle on the car's body");
    expect(wgf_physics3d_priv_jolt_vehicle_create(box + 0x10000u, &v) < 0, "none on a body that isn't one");

    for (i = 0; i < 600; i++) {
        wgf_physics3d_priv_jolt_vehicle_set_input(vehicle, 1.0f, i > 300 ? 0.3f : 0.0f, 0.0f, 0.0f);
        wgf_physics3d_priv_jolt_step(1.0f / 60.0f);
        n = wgf_physics3d_priv_jolt_take_overlaps(o, 8);
        while (n-- > 0) {
            if (o[n].sensor != 3 || o[n].other != 2) continue;
            if (o[n].entered) {
                entered++;
                enter_tick = i;
            } else {
                left++;
                exit_tick = i;
            }
        }
    }
    expect(entered == 1 && left == 1 && enter_tick == 38 && exit_tick == 54,
           "the box entered the sensor once, and left it once, at its ticks");

    /* recorded on x86-64 Linux, debug and release alike; every target must give the same */
    wgf_physics3d_priv_jolt_body_get_pose(box, p, q);
    if (bits(p[0]) != 0x409ffff0u || bits(p[1]) != 0x3efdbc59u || bits(p[2]) != 0x37e4e28bu) {
        printf("box %08x %08x %08x\n", bits(p[0]), bits(p[1]), bits(p[2]));
        expect(0, "the box at rest where every target puts it, to the bit");
    }
    wgf_physics3d_priv_jolt_body_get_pose(car, p, q);
    if (bits(p[0]) != 0xc2d6824bu || bits(p[1]) != 0x3f22df56u || bits(p[2]) != 0x4311d6beu || bits(q[0]) != 0x3d033605u ||
        bits(q[1]) != 0xbf356e6bu || bits(q[2]) != 0xbd16b6e3u || bits(q[3]) != 0x3f342c91u ||
        bits(wgf_physics3d_priv_jolt_vehicle_get_rpm(vehicle)) != 0x45dac000u || wgf_physics3d_priv_jolt_vehicle_get_gear(vehicle) != 2) {
        printf("car %08x %08x %08x %08x %08x %08x %08x rpm %08x gear %d\n", bits(p[0]), bits(p[1]), bits(p[2]), bits(q[0]),
               bits(q[1]), bits(q[2]), bits(q[3]), bits(wgf_physics3d_priv_jolt_vehicle_get_rpm(vehicle)),
               wgf_physics3d_priv_jolt_vehicle_get_gear(vehicle));
        expect(0, "the car driven and turned to where every target puts it, its engine and gear, to the bit");
    }
    wgf_physics3d_priv_jolt_vehicle_get_wheel(vehicle, 0, p, q);
    expect(p[0] < -0.7f && p[0] > -0.9f && p[2] > 1.3f && p[2] < 1.5f, "the front left wheel where it was put");
    expect(wgf_physics3d_priv_jolt_vehicle_get_slip(vehicle, 2) >= 0.0f && wgf_physics3d_priv_jolt_vehicle_get_slip(vehicle, 9) == 0.0f,
           "a wheel's slip, and none for a wheel it hasn't");

    /* a body gone while in a sensor: the sensor told it left */
    d = desc(WGF_PHYSICS3D_PRIV_DYNAMIC, WGF_PHYSICS3D_PRIV_SPHERE, 5, 2, 0, 0.3f, 0, 0, 5);
    gone = wgf_physics3d_priv_jolt_body_create(&d);
    wgf_physics3d_priv_jolt_step(1.0f / 60.0f);
    n = wgf_physics3d_priv_jolt_take_overlaps(o, 8);
    expect(n == 1 && o[0].entered && o[0].sensor == 3 && o[0].other == 5, "a sphere made in the sensor: entered");
    wgf_physics3d_priv_jolt_body_destroy(gone);
    n = wgf_physics3d_priv_jolt_take_overlaps(o, 8);
    expect(n == 1 && !o[0].entered && o[0].sensor == 3 && o[0].other == 5, "destroyed there: left");

    wgf_physics3d_priv_jolt_vehicle_destroy(vehicle);
    wgf_physics3d_priv_jolt_stop();
    expect(wgf_physics3d_priv_jolt_body_create(&d) == WGF_PHYSICS3D_PRIV_NO_BODY, "nothing after the stop");
    return failures == 0 ? 0 : 1;
}
