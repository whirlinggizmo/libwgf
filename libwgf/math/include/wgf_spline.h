#ifndef WGF_SPLINE_H
#define WGF_SPLINE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A spline: a path through or near a list of points. To draw one, see wgf_draw_spline.
 *
 * The points are `points`, x and y in turn, `count` floats in all. A spline is read at `t`,
 * from 0 at its start to 1 at its end. Each of its segments takes an equal share of `t`.
 *
 * The kinds, and how many points each needs:
 *   LINEAR            straight from each point to the next (2 points or more)
 *   BASIS             a cubic B-spline, near its points but through none (4 or more)
 *   CATMULL_ROM       through every point but the first and last, which steer its ends (4 or more)
 *   BEZIER_QUADRATIC  start, control, end; each next one starts at the last's end (3, 5, 7...)
 *   BEZIER_CUBIC      start, control, control, end, and so on in the same way (4, 7, 10...) */

typedef enum wgf_spline_kind_t {
    WGF_SPLINE_KIND_LINEAR = 0,
    WGF_SPLINE_KIND_BASIS = 1,
    WGF_SPLINE_KIND_CATMULL_ROM = 2,
    WGF_SPLINE_KIND_BEZIER_QUADRATIC = 3,
    WGF_SPLINE_KIND_BEZIER_CUBIC = 4
} wgf_spline_kind_t;

/* Returns how many segments `count` floats of points make for `kind`.
 *
 * Returns 0 if there are too few points, if `count` is odd, or if `kind` isn't a
 * wgf_spline_kind_t value. */
WGF_API int wgf_spline_get_segment_count(wgf_spline_kind_t kind, const float *points, int count);

/* Returns the point at `t`, which is held to 0..1.
 *
 * Returns 0, 0 if the points make no segment. */
WGF_API wgf_vec2_t wgf_spline_get_point(wgf_spline_kind_t kind, const float *points, int count, float t);

#ifdef __cplusplus
}
#endif

#endif /* WGF_SPLINE_H */
