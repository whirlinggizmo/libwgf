#ifndef WGF_TRIG_H
#define WGF_TRIG_H

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Trigonometry that gives the same bits on every target -- natively, under Windows, and in a
 * browser -- which the rotations (wgf_quat.h) and the generated meshes are made with, so a
 * simulation started from the same numbers runs the same everywhere. The C library's sinf and
 * cosf don't: glibc's and Emscripten's (musl's) differ in the last bit for some angles, enough
 * for a pile of a thousand physics boxes, each turned as it is made, to come apart within
 * seconds; and a binding's own (Haxe's Math on hxcpp and on JS) differ the same way. Within an
 * ulp of the C library's, and usually equal to it. Radians; NaN for an infinite or NaN angle,
 * and for asin and acos past -1..1, as the C library's. */
WGF_API float wgf_trig_sin(float radians);
WGF_API float wgf_trig_cos(float radians);
WGF_API float wgf_trig_tan(float radians);

/* The angle of (x, y) from the x axis, -pi..pi, as atan2f(y, x). */
WGF_API float wgf_trig_atan2(float y, float x);

/* The angle whose sine (asin, -pi/2..pi/2) or cosine (acos, 0..pi) is x. */
WGF_API float wgf_trig_asin(float x);
WGF_API float wgf_trig_acos(float x);

#ifdef __cplusplus
}
#endif

#endif
