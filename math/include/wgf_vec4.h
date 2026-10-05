#ifndef WGF_VEC4_H
#define WGF_VEC4_H

#include <math.h>
#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 4 floats, in order, with no padding: the layout is a contract with every
 * binding, and never changes. */
typedef struct wgf_vec4_t {
    float x;
    float y;
    float z;
    float w;
} wgf_vec4_t;

#ifndef __cplusplus
_Static_assert(sizeof(wgf_vec4_t) == 4 * sizeof(float), "wgf_vec4_t is 4 floats");
#endif

static inline wgf_vec4_t wgf_vec4_make(float x, float y, float z, float w)
{
    wgf_vec4_t r;
    r.x = x;
    r.y = y;
    r.z = z;
    r.w = w;
    return r;
}

static inline wgf_vec4_t wgf_vec4_add(wgf_vec4_t a, wgf_vec4_t b)
{
    wgf_vec4_t r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    r.z = a.z + b.z;
    r.w = a.w + b.w;
    return r;
}

static inline wgf_vec4_t wgf_vec4_sub(wgf_vec4_t a, wgf_vec4_t b)
{
    wgf_vec4_t r;
    r.x = a.x - b.x;
    r.y = a.y - b.y;
    r.z = a.z - b.z;
    r.w = a.w - b.w;
    return r;
}

static inline wgf_vec4_t wgf_vec4_scale(wgf_vec4_t v, float s)
{
    wgf_vec4_t r;
    r.x = v.x * s;
    r.y = v.y * s;
    r.z = v.z * s;
    r.w = v.w * s;
    return r;
}

static inline float wgf_vec4_dot(wgf_vec4_t a, wgf_vec4_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

static inline float wgf_vec4_length(wgf_vec4_t v)
{
    return sqrtf(wgf_vec4_dot(v, v));
}

/* `v` at length 1; a zero vector stays zero. */
static inline wgf_vec4_t wgf_vec4_normalize(wgf_vec4_t v)
{
    const float length = wgf_vec4_length(v);
    return length > 0.0f ? wgf_vec4_scale(v, 1.0f / length) : v;
}

/* From a (t = 0) to b (t = 1); t outside 0..1 extrapolates. */
static inline wgf_vec4_t wgf_vec4_lerp(wgf_vec4_t a, wgf_vec4_t b, float t)
{
    wgf_vec4_t r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t;
    r.w = a.w + (b.w - a.w) * t;
    return r;
}

#ifdef __cplusplus
}
#endif

#endif
