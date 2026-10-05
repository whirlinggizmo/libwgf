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

#ifdef __cplusplus
}
#endif

#endif
