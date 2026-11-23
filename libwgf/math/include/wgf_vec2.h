#ifndef WGF_VEC2_H
#define WGF_VEC2_H

#include <math.h>
#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A vector of 2 floats, in order, with no padding.
 *
 * The layout is a contract with every binding, and never changes. */
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

/* Returns `v` at length 1. A zero vector stays zero. */
static inline wgf_vec2_t wgf_vec2_normalize(wgf_vec2_t v)
{
    const float length = wgf_vec2_length(v);
    return length > 0.0f ? wgf_vec2_scale(v, 1.0f / length) : v;
}

/* Returns the vector from `a` (at `t` = 0) to `b` (at `t` = 1). A `t` outside 0..1 extrapolates. */
static inline wgf_vec2_t wgf_vec2_lerp(wgf_vec2_t a, wgf_vec2_t b, float t)
{
    wgf_vec2_t r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    return r;
}

/* Returns how far `a` is from `b`. */
static inline float wgf_vec2_distance(wgf_vec2_t a, wgf_vec2_t b)
{
    return wgf_vec2_length(wgf_vec2_sub(a, b));
}

/* Returns the angle of `v`, in radians from +x toward +y, as atan2 gives it. */
static inline float wgf_vec2_angle(wgf_vec2_t v)
{
    return atan2f(v.y, v.x);
}

/* Returns `v` turned by `radians`, from +x toward +y. */
static inline wgf_vec2_t wgf_vec2_rotate(wgf_vec2_t v, float radians)
{
    const float c = cosf(radians), s = sinf(radians);
    return wgf_vec2_make(v.x * c - v.y * s, v.x * s + v.y * c);
}

/* Returns `from` moved toward `to` by at most `step`, never past it. */
static inline wgf_vec2_t wgf_vec2_move_toward(wgf_vec2_t from, wgf_vec2_t to, float step)
{
    const wgf_vec2_t d = wgf_vec2_sub(to, from);
    const float len = wgf_vec2_length(d);
    return len <= step || len == 0.0f ? to : wgf_vec2_add(from, wgf_vec2_scale(d, step / len));
}

/* Returns `v` bounced off a surface whose normal is `normal`, a unit vector. */
static inline wgf_vec2_t wgf_vec2_reflect(wgf_vec2_t v, wgf_vec2_t normal)
{
    return wgf_vec2_sub(v, wgf_vec2_scale(normal, 2.0f * wgf_vec2_dot(v, normal)));
}

/* Returns each part the smaller of `a`'s and `b`'s (min), or the larger (max). */
static inline wgf_vec2_t wgf_vec2_min(wgf_vec2_t a, wgf_vec2_t b)
{
    return wgf_vec2_make(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y);
}

static inline wgf_vec2_t wgf_vec2_max(wgf_vec2_t a, wgf_vec2_t b)
{
    return wgf_vec2_make(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y);
}

/* Returns each part of `v` held within the same part of `lo` and of `hi`. */
static inline wgf_vec2_t wgf_vec2_clamp(wgf_vec2_t v, wgf_vec2_t lo, wgf_vec2_t hi)
{
    return wgf_vec2_max(wgf_vec2_min(v, hi), lo);
}

/* Returns `v` with its length held within `lo`..`hi` and its direction kept.
 *
 * A zero vector stays zero. */
static inline wgf_vec2_t wgf_vec2_clamp_length(wgf_vec2_t v, float lo, float hi)
{
    const float len = wgf_vec2_length(v);
    if (len == 0.0f) return v;
    return len < lo ? wgf_vec2_scale(v, lo / len) : len > hi ? wgf_vec2_scale(v, hi / len) : v;
}

#ifdef __cplusplus
}
#endif

#endif
