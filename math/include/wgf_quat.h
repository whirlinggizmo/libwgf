#ifndef WGF_QUAT_H
#define WGF_QUAT_H

#include <math.h>
#include "wgf_trig.h"
#include "wgf_api.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A rotation, as a unit quaternion: 4 floats, x y z w, with no padding. The
 * identity is (0, 0, 0, 1). */
typedef struct wgf_quat_t {
    float x;
    float y;
    float z;
    float w;
} wgf_quat_t;

#ifndef __cplusplus
_Static_assert(sizeof(wgf_quat_t) == 4 * sizeof(float), "wgf_quat_t is 4 floats");
#endif

static inline wgf_quat_t wgf_quat_make(float x, float y, float z, float w)
{
    wgf_quat_t r;
    r.x = x;
    r.y = y;
    r.z = z;
    r.w = w;
    return r;
}

static inline wgf_quat_t wgf_quat_identity(void)
{
    return wgf_quat_make(0.0f, 0.0f, 0.0f, 1.0f);
}

/* A turn of `radians` about `axis`, right-handed; the axis needn't be unit length.
 * A zero axis gives the identity. */
static inline wgf_quat_t wgf_quat_from_axis_angle(wgf_vec3_t axis, float radians)
{
    const wgf_vec3_t unit = wgf_vec3_normalize(axis);
    const float s = wgf_trig_sin(radians * 0.5f);
    if (wgf_vec3_dot(unit, unit) == 0.0f) return wgf_quat_identity();
    return wgf_quat_make(unit.x * s, unit.y * s, unit.z * s, wgf_trig_cos(radians * 0.5f));
}

/* A rotation from three angles in radians, about x, then y, then z, each about the
 * fixed axes: roll, pitch, then yaw. (As a product, z times y times x.) */
static inline wgf_quat_t wgf_quat_from_euler(wgf_vec3_t radians)
{
    const float cx = wgf_trig_cos(radians.x * 0.5f), sx = wgf_trig_sin(radians.x * 0.5f);
    const float cy = wgf_trig_cos(radians.y * 0.5f), sy = wgf_trig_sin(radians.y * 0.5f);
    const float cz = wgf_trig_cos(radians.z * 0.5f), sz = wgf_trig_sin(radians.z * 0.5f);
    return wgf_quat_make(sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz,
                             cx * cy * cz + sx * sy * sz);
}

/* The three angles wgf_quat_from_euler takes, for a unit quaternion: x and z in
 * -pi..pi, y in -pi/2..pi/2. Where y is +-pi/2 (gimbal lock) x and z share one
 * turn, and x is given as 0. */
static inline wgf_vec3_t wgf_quat_to_euler(wgf_quat_t q)
{
    const float sin_y = 2.0f * (q.w * q.y - q.z * q.x);
    if (sin_y >= 0.99999f || sin_y <= -0.99999f) {
        const float half_pi = 1.57079632679f;
        return wgf_vec3_make(0.0f, sin_y > 0.0f ? half_pi : -half_pi,
                                 -2.0f * wgf_trig_atan2(q.x, q.w) * (sin_y > 0.0f ? -1.0f : 1.0f));
    }
    return wgf_vec3_make(wgf_trig_atan2(2.0f * (q.w * q.x + q.y * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)),
                             wgf_trig_asin(sin_y),
                             wgf_trig_atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z)));
}

/* `b`, then `a`: rotating by the result is rotating by b and then by a. */
static inline wgf_quat_t wgf_quat_mul(wgf_quat_t a, wgf_quat_t b)
{
    return wgf_quat_make(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                             a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                             a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                             a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

static inline float wgf_quat_dot(wgf_quat_t a, wgf_quat_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/* The inverse of a unit quaternion. */
static inline wgf_quat_t wgf_quat_conjugate(wgf_quat_t q)
{
    return wgf_quat_make(-q.x, -q.y, -q.z, q.w);
}

/* `q` at length 1; a zero quaternion gives the identity. */
static inline wgf_quat_t wgf_quat_normalize(wgf_quat_t q)
{
    const float length = sqrtf(wgf_quat_dot(q, q));
    if (length <= 0.0f) return wgf_quat_identity();
    return wgf_quat_make(q.x / length, q.y / length, q.z / length, q.w / length);
}

/* `v` rotated by the unit quaternion `q`. */
static inline wgf_vec3_t wgf_quat_rotate(wgf_quat_t q, wgf_vec3_t v)
{
    const wgf_vec3_t u = wgf_vec3_make(q.x, q.y, q.z);
    const wgf_vec3_t t = wgf_vec3_scale(wgf_vec3_cross(u, v), 2.0f);
    return wgf_vec3_add(wgf_vec3_add(v, wgf_vec3_scale(t, q.w)), wgf_vec3_cross(u, t));
}

/* From a (t = 0) to b (t = 1) along the shorter arc, at a steady angular speed. */
static inline wgf_quat_t wgf_quat_slerp(wgf_quat_t a, wgf_quat_t b, float t)
{
    float d = wgf_quat_dot(a, b);
    float wa, wb;
    if (d < 0.0f) { /* the shorter way round */
        b = wgf_quat_make(-b.x, -b.y, -b.z, -b.w);
        d = -d;
    }
    if (d > 0.9995f) { /* nearly the same: a straight blend is as good, and stable */
        wa = 1.0f - t;
        wb = t;
    } else {
        const float angle = wgf_trig_acos(d);
        const float s = wgf_trig_sin(angle);
        wa = wgf_trig_sin((1.0f - t) * angle) / s;
        wb = wgf_trig_sin(t * angle) / s;
    }
    return wgf_quat_normalize(wgf_quat_make(wa * a.x + wb * b.x, wa * a.y + wb * b.y,
                                                    wa * a.z + wb * b.z, wa * a.w + wb * b.w));
}

/* The rotation that turns the -z axis to `forward` and the +y axis as near to `up`
 * as can be: how a camera or a light, which looks down its -z, is aimed. Neither
 * needs unit length. Where forward is zero, or along up, the identity. */
static inline wgf_quat_t wgf_quat_look_rotation(wgf_vec3_t forward, wgf_vec3_t up)
{
    const wgf_vec3_t z = wgf_vec3_normalize(wgf_vec3_scale(forward, -1.0f));
    const wgf_vec3_t x = wgf_vec3_normalize(wgf_vec3_cross(up, z));
    const wgf_vec3_t y = wgf_vec3_cross(z, x);
    /* the rotation matrix with columns x, y, z, as a quaternion */
    const float trace = x.x + y.y + z.z;
    if (wgf_vec3_dot(z, z) == 0.0f || wgf_vec3_dot(x, x) == 0.0f) return wgf_quat_identity();
    if (trace > 0.0f) {
        const float s = sqrtf(trace + 1.0f) * 2.0f;
        return wgf_quat_normalize(wgf_quat_make((y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s));
    }
    if (x.x > y.y && x.x > z.z) {
        const float s = sqrtf(1.0f + x.x - y.y - z.z) * 2.0f;
        return wgf_quat_normalize(wgf_quat_make(0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s));
    }
    if (y.y > z.z) {
        const float s = sqrtf(1.0f + y.y - x.x - z.z) * 2.0f;
        return wgf_quat_normalize(wgf_quat_make((y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s));
    }
    {
        const float s = sqrtf(1.0f + z.z - x.x - y.y) * 2.0f;
        return wgf_quat_normalize(wgf_quat_make((z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s));
    }
}

#ifdef __cplusplus
}
#endif

#endif
