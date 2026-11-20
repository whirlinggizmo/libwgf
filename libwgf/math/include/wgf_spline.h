#ifndef WGF_SPLINE_H
#define WGF_SPLINE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Splines, raylib's GetSplinePoint* (wgf_draw_spline draws one): a path through or near
 * `points`, x and y in turn (`count` floats), read at `t` from 0 (its start) to 1 (its end),
 * each of its segments an equal share of t. Its kinds:
 *   LINEAR            straight between each point and the next (2 points or more)
 *   BASIS             a cubic B-spline, near its points, through none (4 or more)
 *   CATMULL_ROM       through every point but its first and last, which steer its ends (4 or more)
 *   BEZIER_QUADRATIC  start, control, end; each next segment's start the last's end (3, 5, 7...)
 *   BEZIER_CUBIC      start, control, control, end; and on (4, 7, 10...) */

typedef enum wgf_spline_kind_t {
    WGF_SPLINE_KIND_LINEAR = 0,
    WGF_SPLINE_KIND_BASIS = 1,
    WGF_SPLINE_KIND_CATMULL_ROM = 2,
    WGF_SPLINE_KIND_BEZIER_QUADRATIC = 3,
    WGF_SPLINE_KIND_BEZIER_CUBIC = 4
} wgf_spline_kind_t;

/* How many segments `count` floats of points make for `kind`; 0 for too few, an odd count, or
 * a kind that isn't one. */
WGF_API int wgf_spline_get_segment_count(wgf_spline_kind_t kind, const float *points, int count);

/* The point at `t` (held to 0..1); 0, 0 for points that make no segment. */
WGF_API wgf_vec2_t wgf_spline_get_point(wgf_spline_kind_t kind, const float *points, int count, float t);

#ifdef __cplusplus
}
#endif

#endif /* WGF_SPLINE_H */
