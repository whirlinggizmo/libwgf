#ifndef WGF_EASING_H
#define WGF_EASING_H

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An easing: how a tween (wgf_tween.h) runs from its start to its end.
 *
 * The set is Robert Penner's. Each easing but LINEAR comes in three forms:
 * - IN starts slow.
 * - OUT ends slow.
 * - IN_OUT starts and ends slow.
 *
 * BACK goes a little past and returns. ELASTIC springs. BOUNCE bounces at the end. */
typedef enum wgf_easing_t {
    WGF_EASING_LINEAR = 0,
    WGF_EASING_QUAD_IN = 1,
    WGF_EASING_QUAD_OUT = 2,
    WGF_EASING_QUAD_IN_OUT = 3,
    WGF_EASING_CUBIC_IN = 4,
    WGF_EASING_CUBIC_OUT = 5,
    WGF_EASING_CUBIC_IN_OUT = 6,
    WGF_EASING_QUART_IN = 7,
    WGF_EASING_QUART_OUT = 8,
    WGF_EASING_QUART_IN_OUT = 9,
    WGF_EASING_SINE_IN = 10,
    WGF_EASING_SINE_OUT = 11,
    WGF_EASING_SINE_IN_OUT = 12,
    WGF_EASING_EXPO_IN = 13,
    WGF_EASING_EXPO_OUT = 14,
    WGF_EASING_EXPO_IN_OUT = 15,
    WGF_EASING_CIRC_IN = 16,
    WGF_EASING_CIRC_OUT = 17,
    WGF_EASING_CIRC_IN_OUT = 18,
    WGF_EASING_BACK_IN = 19,
    WGF_EASING_BACK_OUT = 20,
    WGF_EASING_BACK_IN_OUT = 21,
    WGF_EASING_ELASTIC_IN = 22,
    WGF_EASING_ELASTIC_OUT = 23,
    WGF_EASING_ELASTIC_IN_OUT = 24,
    WGF_EASING_BOUNCE_IN = 25,
    WGF_EASING_BOUNCE_OUT = 26,
    WGF_EASING_BOUNCE_IN_OUT = 27
} wgf_easing_t;

/* Returns `t` eased by `easing`.
 *
 * `t` runs from 0 to 1, and is clamped to that range. The result is 0 at 0 and 1 at 1. In
 * between, BACK and ELASTIC go past that range. An `easing` that isn't one of
 * wgf_easing_t's values is treated as LINEAR. */
WGF_API float wgf_ease_value(wgf_easing_t easing, float t);

#ifdef __cplusplus
}
#endif

#endif /* WGF_EASING_H */
