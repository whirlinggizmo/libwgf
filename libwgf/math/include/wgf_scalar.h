#ifndef WGF_SCALAR_H
#define WGF_SCALAR_H

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The math on single numbers a game writes for itself otherwise, helix-legacy's Interpolate:
 * C's alone, static inline, as math's vectors are (a binding has its language's). */

/* `v` held within lo..hi. */
static inline float wgf_scalar_clamp(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* a to b by t (not clamped). */
static inline float wgf_scalar_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* `v` from the range a..b to the range c..d (not clamped); c when a and b are equal. */
static inline float wgf_scalar_remap(float v, float a, float b, float c, float d)
{
    return a == b ? c : c + (d - c) * (v - a) / (b - a);
}

/* `v` wrapped into lo..hi (hi itself wrapping to lo); lo when the range is empty. */
static inline float wgf_scalar_wrap(float v, float lo, float hi)
{
    const float range = hi - lo;
    return range <= 0.0f ? lo : v - range * floorf((v - lo) / range);
}

/* 0 below e0, 1 above e1, smooth between (Hermite). */
static inline float wgf_scalar_smooth_step(float e0, float e1, float v)
{
    const float t = wgf_scalar_clamp(e0 == e1 ? (v < e0 ? 0.0f : 1.0f) : (v - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* `from` toward `to`, at `rate` a second over dt, the same however the frames fall (an
 * exponential approach; rate 0 or less: at once). */
static inline float wgf_scalar_damp(float from, float to, float rate, float dt)
{
    return rate <= 0.0f ? to : to + (from - to) * expf(-rate * dt);
}

/* `from` toward `to` by at most `step`, never past it. */
static inline float wgf_scalar_move_toward(float from, float to, float step)
{
    return fabsf(to - from) <= step ? to : from + (to > from ? step : -step);
}

/* The turn from angle a to angle b the short way round, in radians: -pi..pi. */
static inline float wgf_scalar_angle_delta(float a, float b)
{
    const float pi = 3.14159265358979f;
    float d = fmodf(b - a, 2.0f * pi);
    if (d > pi) d -= 2.0f * pi;
    if (d < -pi) d += 2.0f * pi;
    return d;
}

#ifdef __cplusplus
}
#endif

#endif /* WGF_SCALAR_H */
