#ifndef WGF_VEC3_H
#define WGF_VEC3_H

#include <math.h>
#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 3 floats, in order, with no padding: the layout is a contract with every
 * binding, and never changes. */
typedef struct wgf_vec3_t {
    float x;
    float y;
    float z;
} wgf_vec3_t;

#ifndef __cplusplus
_Static_assert(sizeof(wgf_vec3_t) == 3 * sizeof(float), "wgf_vec3_t is 3 floats");
#endif

static inline wgf_vec3_t wgf_vec3_make(float x, float y, float z)
{
    wgf_vec3_t r;
    r.x = x;
    r.y = y;
    r.z = z;
    return r;
}

static inline wgf_vec3_t wgf_vec3_add(wgf_vec3_t a, wgf_vec3_t b)
{
    wgf_vec3_t r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    r.z = a.z + b.z;
    return r;
}

static inline wgf_vec3_t wgf_vec3_sub(wgf_vec3_t a, wgf_vec3_t b)
{
    wgf_vec3_t r;
    r.x = a.x - b.x;
    r.y = a.y - b.y;
    r.z = a.z - b.z;
    return r;
}

static inline wgf_vec3_t wgf_vec3_scale(wgf_vec3_t v, float s)
{
    wgf_vec3_t r;
    r.x = v.x * s;
    r.y = v.y * s;
    r.z = v.z * s;
    return r;
}

static inline float wgf_vec3_dot(wgf_vec3_t a, wgf_vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline float wgf_vec3_length(wgf_vec3_t v)
{
    return sqrtf(wgf_vec3_dot(v, v));
}

/* `v` at length 1; a zero vector stays zero. */
static inline wgf_vec3_t wgf_vec3_normalize(wgf_vec3_t v)
{
    const float length = wgf_vec3_length(v);
    return length > 0.0f ? wgf_vec3_scale(v, 1.0f / length) : v;
}

/* From a (t = 0) to b (t = 1); t outside 0..1 extrapolates. */
static inline wgf_vec3_t wgf_vec3_lerp(wgf_vec3_t a, wgf_vec3_t b, float t)
{
    wgf_vec3_t r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t;
    return r;
}

/* Right-handed: x cross y is z. */
static inline wgf_vec3_t wgf_vec3_cross(wgf_vec3_t a, wgf_vec3_t b)
{
    wgf_vec3_t r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}

/* How far a is from b. */
static inline float wgf_vec3_distance(wgf_vec3_t a, wgf_vec3_t b)
{
    return wgf_vec3_length(wgf_vec3_sub(a, b));
}

/* The angle between a and b, radians, 0 to pi (0 when either has no length). */
static inline float wgf_vec3_angle(wgf_vec3_t a, wgf_vec3_t b)
{
    const float len = wgf_vec3_length(a) * wgf_vec3_length(b);
    float c;
    if (len == 0.0f) return 0.0f;
    c = wgf_vec3_dot(a, b) / len;
    return acosf(c < -1.0f ? -1.0f : c > 1.0f ? 1.0f : c);
}

/* `from` toward `to` by at most `step`, never past it. */
static inline wgf_vec3_t wgf_vec3_move_toward(wgf_vec3_t from, wgf_vec3_t to, float step)
{
    const wgf_vec3_t d = wgf_vec3_sub(to, from);
    const float len = wgf_vec3_length(d);
    return len <= step || len == 0.0f ? to : wgf_vec3_add(from, wgf_vec3_scale(d, step / len));
}

/* Bounced off a surface of unit `normal`. */
static inline wgf_vec3_t wgf_vec3_reflect(wgf_vec3_t v, wgf_vec3_t normal)
{
    return wgf_vec3_sub(v, wgf_vec3_scale(normal, 2.0f * wgf_vec3_dot(v, normal)));
}

/* Its part along `onto` (any length; 0 when it has none). */
static inline wgf_vec3_t wgf_vec3_project(wgf_vec3_t v, wgf_vec3_t onto)
{
    const float d = wgf_vec3_dot(onto, onto);
    return d == 0.0f ? wgf_vec3_make(0.0f, 0.0f, 0.0f) : wgf_vec3_scale(onto, wgf_vec3_dot(v, onto) / d);
}

/* Each part the smaller, or the larger, of the two. */
static inline wgf_vec3_t wgf_vec3_min(wgf_vec3_t a, wgf_vec3_t b)
{
    return wgf_vec3_make(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z);
}

static inline wgf_vec3_t wgf_vec3_max(wgf_vec3_t a, wgf_vec3_t b)
{
    return wgf_vec3_make(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z);
}

/* Each part held within lo's and hi's. */
static inline wgf_vec3_t wgf_vec3_clamp(wgf_vec3_t v, wgf_vec3_t lo, wgf_vec3_t hi)
{
    return wgf_vec3_max(wgf_vec3_min(v, hi), lo);
}

/* Its length held within lo..hi, its direction kept; a zero vector stays zero. */
static inline wgf_vec3_t wgf_vec3_clamp_length(wgf_vec3_t v, float lo, float hi)
{
    const float len = wgf_vec3_length(v);
    if (len == 0.0f) return v;
    return len < lo ? wgf_vec3_scale(v, lo / len) : len > hi ? wgf_vec3_scale(v, hi / len) : v;
}

#ifdef __cplusplus
}
#endif

#endif
