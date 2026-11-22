#ifndef WGF_MATH_DRAW_TRIG_PRIV_H
#define WGF_MATH_DRAW_TRIG_PRIV_H

#include <math.h>

#include "wgf_trig.h"

/* Drawing's trigonometry: a circle's points, a particle's turn, a light's cone, what is drawn
 * and never read back, so its last bit may be the target's. It is the C library's, or libwgf's
 * own (wgf_trig.h) in a build with WGF_EXACT_DRAW_TRIG, which the web's is: there libwgf's is
 * as fast, and a page whose drawing calls it carries none of the C library's (its reduction
 * of huge angles is a table and 4 KB of gzip). Natively glibc's is five times as fast.
 * What the simulation reads (a collider's turn, a camera's place, an easing curve) calls
 * wgf_trig.h itself, on every target. */

#ifdef WGF_PRIV_EXACT_DRAW_TRIG
static inline float draw_sin(float x) { return wgf_trig_sin(x); }
static inline float draw_cos(float x) { return wgf_trig_cos(x); }
static inline float draw_tan(float x) { return wgf_trig_tan(x); }
static inline float draw_atan2(float y, float x) { return wgf_trig_atan2(y, x); }
static inline float draw_acos(float x) { return wgf_trig_acos(x); }
/* The cosine (x) and the sine (y) together: one reduction for the two. */
static inline wgf_vec2_t draw_cos_sin(float x) { return wgf_trig_cos_sin(x); }
#else
static inline float draw_sin(float x) { return sinf(x); }
static inline float draw_cos(float x) { return cosf(x); }
static inline float draw_tan(float x) { return tanf(x); }
static inline float draw_atan2(float y, float x) { return atan2f(y, x); }
static inline float draw_acos(float x) { return acosf(x); }
static inline wgf_vec2_t draw_cos_sin(float x) { return wgf_vec2_make(cosf(x), sinf(x)); }
#endif

#endif
