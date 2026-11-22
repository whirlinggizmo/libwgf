/* Easing (wgf_ease.h): helix-legacy's Ease, Penner's set, carried whole. A tween moves what
 * the simulation reads, so the curves give the same bits on every target: their sines are
 * wgf_trig's, and their powers of 2 worked here the same way, never the C library's (whose
 * last bit differs between targets), as helix's Interpolate uses its Trig. */
#include "wgf_ease.h"

#include <math.h>

#include "wgf_trig.h"

#define PI 3.14159265358979f

/* 2 to the power `x`, the same bits on every target: its whole part an exact scale (ldexp),
 * the rest e^(f ln 2) by a Taylor polynomial in double whose next term is below a double's
 * precision, rounded to float once; each step its own statement, so no compiler fuses an FMA
 * in (wgf_math_trig.c's way). Exact at whole powers. */
static float exp2_of(float x)
{
    const double whole = floor((double)x);
    const double r = ((double)x - whole) * 0.69314718055994530942; /* 0 <= r < ln 2 */
    double p = 2.8114572543455206e-15;                             /* 1/17! */
    p = p * r;
    p = p + 4.7794773323873853e-14; /* 1/16! */
    p = p * r;
    p = p + 7.6471637318198164e-13;
    p = p * r;
    p = p + 1.1470745597729725e-11;
    p = p * r;
    p = p + 1.6059043836821613e-10;
    p = p * r;
    p = p + 2.0876756987868100e-09;
    p = p * r;
    p = p + 2.5052108385441720e-08;
    p = p * r;
    p = p + 2.7557319223985888e-07;
    p = p * r;
    p = p + 2.7557319223985893e-06;
    p = p * r;
    p = p + 2.4801587301587302e-05;
    p = p * r;
    p = p + 1.9841269841269841e-04;
    p = p * r;
    p = p + 1.3888888888888889e-03;
    p = p * r;
    p = p + 8.3333333333333332e-03;
    p = p * r;
    p = p + 4.1666666666666664e-02;
    p = p * r;
    p = p + 1.6666666666666666e-01;
    p = p * r;
    p = p + 0.5;
    p = p * r;
    p = p + 1.0;
    p = p * r;
    p = p + 1.0;
    return (float)ldexp(p, (int)whole);
}

static float bounce_out(float t)
{
    const float n = 7.5625f, d = 2.75f;
    if (t < 1.0f / d) return n * t * t;
    if (t < 2.0f / d) {
        t -= 1.5f / d;
        return n * t * t + 0.75f;
    }
    if (t < 2.5f / d) {
        t -= 2.25f / d;
        return n * t * t + 0.9375f;
    }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

float wgf_ease_value(wgf_easing_t easing, float t)
{
    float u, s;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    switch (easing) {
        case WGF_EASING_QUAD_IN: return t * t;
        case WGF_EASING_QUAD_OUT: return t * (2 - t);
        case WGF_EASING_QUAD_IN_OUT: return t < 0.5f ? 2 * t * t : -1 + (4 - 2 * t) * t;
        case WGF_EASING_CUBIC_IN: return t * t * t;
        case WGF_EASING_CUBIC_OUT: u = t - 1; return u * u * u + 1;
        case WGF_EASING_CUBIC_IN_OUT: return t < 0.5f ? 4 * t * t * t : (t - 1) * (2 * t - 2) * (2 * t - 2) + 1;
        case WGF_EASING_QUART_IN: return t * t * t * t;
        case WGF_EASING_QUART_OUT: u = t - 1; return 1 - u * u * u * u;
        case WGF_EASING_QUART_IN_OUT:
            if (t < 0.5f) return 8 * t * t * t * t;
            u = t - 1;
            return 1 - 8 * u * u * u * u;
        case WGF_EASING_SINE_IN: return 1 - wgf_trig_cos(t * PI / 2);
        case WGF_EASING_SINE_OUT: return wgf_trig_sin(t * PI / 2);
        case WGF_EASING_SINE_IN_OUT: return (1 - wgf_trig_cos(PI * t)) / 2;
        case WGF_EASING_EXPO_IN: return t <= 0 ? 0 : exp2_of(10 * (t - 1));
        case WGF_EASING_EXPO_OUT: return t >= 1 ? 1 : 1 - exp2_of(-10 * t);
        case WGF_EASING_EXPO_IN_OUT:
            if (t <= 0) return 0;
            if (t >= 1) return 1;
            return t < 0.5f ? exp2_of(20 * t - 10) / 2 : (2 - exp2_of(-20 * t + 10)) / 2;
        case WGF_EASING_CIRC_IN: return 1 - sqrtf(1 - t * t);
        case WGF_EASING_CIRC_OUT: u = t - 1; return sqrtf(1 - u * u);
        case WGF_EASING_CIRC_IN_OUT:
            if (t < 0.5f) return (1 - sqrtf(1 - 4 * t * t)) / 2;
            u = 2 * t - 2;
            return (sqrtf(1 - u * u) + 1) / 2;
        case WGF_EASING_BACK_IN: return t * t * (2.70158f * t - 1.70158f);
        case WGF_EASING_BACK_OUT: u = t - 1; return 1 + u * u * (2.70158f * u + 1.70158f);
        case WGF_EASING_BACK_IN_OUT:
            s = 1.70158f * 1.525f;
            if (t < 0.5f) {
                u = 2 * t;
                return u * u * ((s + 1) * u - s) / 2;
            }
            u = 2 * t - 2;
            return (u * u * ((s + 1) * u + s) + 2) / 2;
        case WGF_EASING_ELASTIC_IN:
            if (t <= 0 || t >= 1) return t;
            return -exp2_of(10 * t - 10) * wgf_trig_sin((t * 10 - 10.75f) * 2 * PI / 3);
        case WGF_EASING_ELASTIC_OUT:
            if (t <= 0 || t >= 1) return t;
            return exp2_of(-10 * t) * wgf_trig_sin((t * 10 - 0.75f) * 2 * PI / 3) + 1;
        case WGF_EASING_ELASTIC_IN_OUT:
            if (t <= 0 || t >= 1) return t;
            s = 2 * PI / 4.5f;
            return t < 0.5f ? -exp2_of(20 * t - 10) * wgf_trig_sin((20 * t - 11.125f) * s) / 2
                            : exp2_of(-20 * t + 10) * wgf_trig_sin((20 * t - 11.125f) * s) / 2 + 1;
        case WGF_EASING_BOUNCE_IN: return 1 - bounce_out(1 - t);
        case WGF_EASING_BOUNCE_OUT: return bounce_out(t);
        case WGF_EASING_BOUNCE_IN_OUT: return t < 0.5f ? (1 - bounce_out(1 - 2 * t)) / 2 : (1 + bounce_out(2 * t - 1)) / 2;
        default: return t;
    }
}
