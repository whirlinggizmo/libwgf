#ifndef WGF_TRIG_H
#define WGF_TRIG_H

#include "wgf_api.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the sine, cosine, or tangent of an angle in radians, with the same bits on every target.
 *
 * The results match natively, under Windows, and in a browser. So a simulation started from
 * the same numbers runs the same everywhere. The rotations (wgf_quat.h) and the generated
 * meshes are made with these calls.
 *
 * The C library's sinf and cosf don't match. glibc's and Emscripten's (musl's) differ in the
 * last bit for some angles. That is enough for a pile of a thousand physics boxes, each turned
 * as it is made, to come apart within seconds. A binding's own functions (Haxe's Math on hxcpp
 * and on JS) differ in the same way.
 *
 * Each result is within an ulp of the C library's, and usually equal to it.
 *
 * The results are exact in this way for angles up to about 1.6 million radians. A larger angle
 * is first brought within one turn. Its result is always within -1..1, but it loses accuracy
 * as the angle grows.
 *
 * Returns NaN for an infinite or NaN angle, as the C library does. asin and acos also return
 * NaN past -1..1. */
WGF_API float wgf_trig_sin(float radians);
WGF_API float wgf_trig_cos(float radians);
WGF_API float wgf_trig_tan(float radians);

/* Returns the cosine (x) and the sine (y) of an angle in radians together.
 *
 * Each is what wgf_trig_cos and wgf_trig_sin give, for about the cost of one of them. The
 * result is the point at that angle on a circle of radius 1, which is what a turn needs.
 *
 * Returns NaN, NaN for an infinite or NaN angle. */
WGF_API wgf_vec2_t wgf_trig_cos_sin(float radians);

/* Returns the angle of (x, y) from the x axis, in radians (-pi..pi), as atan2f(y, x) does. */
WGF_API float wgf_trig_atan2(float y, float x);

/* Returns the angle, in radians, whose sine (asin, -pi/2..pi/2) or cosine (acos, 0..pi) is `x`. */
WGF_API float wgf_trig_asin(float x);
WGF_API float wgf_trig_acos(float x);

#ifdef __cplusplus
}
#endif

#endif
