/* Splines (wgf_spline.h): raylib's GetSplinePoint* over a whole path, its segments an equal
 * share of t. */
#include "wgf_spline.h"

#include <stddef.h>

int wgf_spline_get_segment_count(wgf_spline_kind_t kind, const float *points, int count)
{
    const int n = count / 2;
    if (points == NULL || count % 2 != 0) return 0;
    switch (kind) {
    case WGF_SPLINE_KIND_LINEAR: return n >= 2 ? n - 1 : 0;
    case WGF_SPLINE_KIND_BASIS:
    case WGF_SPLINE_KIND_CATMULL_ROM: return n >= 4 ? n - 3 : 0;
    case WGF_SPLINE_KIND_BEZIER_QUADRATIC: return n >= 3 && (n - 1) % 2 == 0 ? (n - 1) / 2 : 0;
    case WGF_SPLINE_KIND_BEZIER_CUBIC: return n >= 4 && (n - 1) % 3 == 0 ? (n - 1) / 3 : 0;
    default: return 0;
    }
}

wgf_vec2_t wgf_spline_get_point(wgf_spline_kind_t kind, const float *points, int count, float t)
{
    const int segments = wgf_spline_get_segment_count(kind, points, count);
    const float *p;
    float u, along;
    int s;
    if (segments == 0) return wgf_vec2_make(0.0f, 0.0f);
    along = (t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t) * (float)segments;
    s = (int)along;
    if (s >= segments) s = segments - 1;
    u = along - (float)s;
    switch (kind) {
    case WGF_SPLINE_KIND_LINEAR:
        p = points + 2 * s;
        return wgf_vec2_make(p[0] + (p[2] - p[0]) * u, p[1] + (p[3] - p[1]) * u);
    case WGF_SPLINE_KIND_BASIS: {
        const float u2 = u * u, u3 = u2 * u;
        const float b0 = (1 - u) * (1 - u) * (1 - u) / 6.0f, b1 = (3 * u3 - 6 * u2 + 4) / 6.0f;
        const float b2 = (-3 * u3 + 3 * u2 + 3 * u + 1) / 6.0f, b3 = u3 / 6.0f;
        p = points + 2 * s;
        return wgf_vec2_make(b0 * p[0] + b1 * p[2] + b2 * p[4] + b3 * p[6],
                             b0 * p[1] + b1 * p[3] + b2 * p[5] + b3 * p[7]);
    }
    case WGF_SPLINE_KIND_CATMULL_ROM: {
        const float u2 = u * u, u3 = u2 * u;
        const float c0 = -u3 + 2 * u2 - u, c1 = 3 * u3 - 5 * u2 + 2, c2 = -3 * u3 + 4 * u2 + u, c3 = u3 - u2;
        p = points + 2 * s;
        return wgf_vec2_make(0.5f * (c0 * p[0] + c1 * p[2] + c2 * p[4] + c3 * p[6]),
                             0.5f * (c0 * p[1] + c1 * p[3] + c2 * p[5] + c3 * p[7]));
    }
    case WGF_SPLINE_KIND_BEZIER_QUADRATIC: {
        const float a = (1 - u) * (1 - u), b = 2 * (1 - u) * u, c = u * u;
        p = points + 4 * s;
        return wgf_vec2_make(a * p[0] + b * p[2] + c * p[4], a * p[1] + b * p[3] + c * p[5]);
    }
    default: { /* cubic Bezier */
        const float v = 1 - u, a = v * v * v, b = 3 * v * v * u, c = 3 * v * u * u, d = u * u * u;
        p = points + 6 * s;
        return wgf_vec2_make(a * p[0] + b * p[2] + c * p[4] + d * p[6], a * p[1] + b * p[3] + c * p[5] + d * p[7]);
    }
    }
}
