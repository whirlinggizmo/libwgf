/* Easing (wgf_ease.h): helix-legacy's Ease, Penner's set, carried whole. */
#include "wgf_ease.h"

#include <math.h>

#define PI 3.14159265358979f

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
        case WGF_EASING_SINE_IN: return 1 - cosf(t * PI / 2);
        case WGF_EASING_SINE_OUT: return sinf(t * PI / 2);
        case WGF_EASING_SINE_IN_OUT: return (1 - cosf(PI * t)) / 2;
        case WGF_EASING_EXPO_IN: return t <= 0 ? 0 : powf(2, 10 * (t - 1));
        case WGF_EASING_EXPO_OUT: return t >= 1 ? 1 : 1 - powf(2, -10 * t);
        case WGF_EASING_EXPO_IN_OUT:
            if (t <= 0) return 0;
            if (t >= 1) return 1;
            return t < 0.5f ? powf(2, 20 * t - 10) / 2 : (2 - powf(2, -20 * t + 10)) / 2;
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
            return -powf(2, 10 * t - 10) * sinf((t * 10 - 10.75f) * 2 * PI / 3);
        case WGF_EASING_ELASTIC_OUT:
            if (t <= 0 || t >= 1) return t;
            return powf(2, -10 * t) * sinf((t * 10 - 0.75f) * 2 * PI / 3) + 1;
        case WGF_EASING_ELASTIC_IN_OUT:
            if (t <= 0 || t >= 1) return t;
            s = 2 * PI / 4.5f;
            return t < 0.5f ? -powf(2, 20 * t - 10) * sinf((20 * t - 11.125f) * s) / 2
                            : powf(2, -20 * t + 10) * sinf((20 * t - 11.125f) * s) / 2 + 1;
        case WGF_EASING_BOUNCE_IN: return 1 - bounce_out(1 - t);
        case WGF_EASING_BOUNCE_OUT: return bounce_out(t);
        case WGF_EASING_BOUNCE_IN_OUT: return t < 0.5f ? (1 - bounce_out(1 - 2 * t)) / 2 : (1 + bounce_out(2 * t - 1)) / 2;
        default: return t;
    }
}
