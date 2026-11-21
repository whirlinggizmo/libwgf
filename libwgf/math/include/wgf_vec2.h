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

/* How far a is from b. */
static inline float wgf_vec2_distance(wgf_vec2_t a, wgf_vec2_t b)
{
    return wgf_vec2_length(wgf_vec2_sub(a, b));
}

/* Its angle, radians from +x toward +y (atan2). */
static inline float wgf_vec2_angle(wgf_vec2_t v)
{
    return atan2f(v.y, v.x);
}

/* Turned by `radians`, from +x toward +y. */
static inline wgf_vec2_t wgf_vec2_rotate(wgf_vec2_t v, float radians)
{
    const float c = cosf(radians), s = sinf(radians);
    return wgf_vec2_make(v.x * c - v.y * s, v.x * s + v.y * c);
}

/* `from` toward `to` by at most `step`, never past it. */
static inline wgf_vec2_t wgf_vec2_move_toward(wgf_vec2_t from, wgf_vec2_t to, float step)
{
    const wgf_vec2_t d = wgf_vec2_sub(to, from);
    const float len = wgf_vec2_length(d);
    return len <= step || len == 0.0f ? to : wgf_vec2_add(from, wgf_vec2_scale(d, step / len));
}

/* Bounced off a surface of unit `normal`. */
static inline wgf_vec2_t wgf_vec2_reflect(wgf_vec2_t v, wgf_vec2_t normal)
{
    return wgf_vec2_sub(v, wgf_vec2_scale(normal, 2.0f * wgf_vec2_dot(v, normal)));
}

/* Each part the smaller, or the larger, of the two. */
static inline wgf_vec2_t wgf_vec2_min(wgf_vec2_t a, wgf_vec2_t b)
{
    return wgf_vec2_make(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y);
}

static inline wgf_vec2_t wgf_vec2_max(wgf_vec2_t a, wgf_vec2_t b)
{
    return wgf_vec2_make(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y);
}

/* Each part held within lo's and hi's. */
static inline wgf_vec2_t wgf_vec2_clamp(wgf_vec2_t v, wgf_vec2_t lo, wgf_vec2_t hi)
{
    return wgf_vec2_max(wgf_vec2_min(v, hi), lo);
}

/* Its length held within lo..hi, its direction kept; a zero vector stays zero. */
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
