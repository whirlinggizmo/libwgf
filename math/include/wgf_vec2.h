#ifndef WGF_VEC2_H
#define WGF_VEC2_H

#include <math.h>
#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 2 floats, in order, with no padding: the layout is a contract with every
 * binding, and never changes. */
typedef struct wgf_vec2_t {
    float x;
    float y;
} wgf_vec2_t;

#ifndef __cplusplus
_Static_assert(sizeof(wgf_vec2_t) == 2 * sizeof(float), "wgf_vec2_t is 2 floats");
#endif

static inline wgf_vec2_t wgf_vec2_make(float x, float y)
{
    wgf_vec2_t r;
    r.x = x;
    r.y = y;
    return r;
}

static inline wgf_vec2_t wgf_vec2_add(wgf_vec2_t a, wgf_vec2_t b)
{
    wgf_vec2_t r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    return r;
}

static inline wgf_vec2_t wgf_vec2_sub(wgf_vec2_t a, wgf_vec2_t b)
{
    wgf_vec2_t r;
    r.x = a.x - b.x;
    r.y = a.y - b.y;
    return r;
}

static inline wgf_vec2_t wgf_vec2_scale(wgf_vec2_t v, float s)
{
    wgf_vec2_t r;
    r.x = v.x * s;
    r.y = v.y * s;
    return r;
}

static inline float wgf_vec2_dot(wgf_vec2_t a, wgf_vec2_t b)
{
    return a.x * b.x + a.y * b.y;
}

static inline float wgf_vec2_length(wgf_vec2_t v)
{
    return sqrtf(wgf_vec2_dot(v, v));
}

/* `v` at length 1; a zero vector stays zero. */
static inline wgf_vec2_t wgf_vec2_normalize(wgf_vec2_t v)
{
    const float length = wgf_vec2_length(v);
    return length > 0.0f ? wgf_vec2_scale(v, 1.0f / length) : v;
}

/* From a (t = 0) to b (t = 1); t outside 0..1 extrapolates. */
static inline wgf_vec2_t wgf_vec2_lerp(wgf_vec2_t a, wgf_vec2_t b, float t)
{
    wgf_vec2_t r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    return r;
}

#ifdef __cplusplus
}
#endif

#endif
