#ifndef WGF_BOUNDS_H
#define WGF_BOUNDS_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_component.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bounds (WGF_COMPONENT_BOUNDS): a rectangle that an actor's position is kept within.
 *
 * The rectangle is in the actor's parent's space, at x and y. Each tick, after motion, an
 * actor whose position leaves it by more than its margin is wrapped, clamped, or destroyed,
 * as its mode says.
 *
 * Defaults: a rectangle at 0, 0, 800 by 600, WRAP, and a margin of 0.
 *
 * Every call returns false (or 0) for an actor without bounds. */
typedef enum wgf_bounds_mode_t {
    WGF_BOUNDS_MODE_WRAP = 0,    /* comes back in at the other side, drawn moving on, not sweeping back across */
    WGF_BOUNDS_MODE_CLAMP = 1,   /* held at the edge, with its velocity across the edge stopped */
    WGF_BOUNDS_MODE_DESTROY = 2  /* destroyed */
} wgf_bounds_mode_t;

/* Sets or gets the rectangle. get_rect returns it as x, y, width, height. The setter also
 * returns false for a width or height below 0. */
WGF_API bool wgf_bounds_set_rect(wgf_actor_t actor, float x, float y, float width, float height);
WGF_API wgf_vec4_t wgf_bounds_get_rect(wgf_actor_t actor);

/* Sets or gets the mode (default: WRAP). The setter also returns false for a mode that isn't
 * one. */
WGF_API bool wgf_bounds_set_mode(wgf_actor_t actor, wgf_bounds_mode_t mode);
WGF_API wgf_bounds_mode_t wgf_bounds_get_mode(wgf_actor_t actor);

/* Sets or gets whether the rectangle is the presentation's visible area (default: off).
 *
 * The visible area (wgf_presentation.h) is read each tick. An actor that wraps at the screen's
 * edge then wraps at the window's edge, whatever its size. A scene file sets this with
 * `bounds visible=true`. get_rect returns the rectangle as it is now. Setting a rectangle
 * turns this off. */
WGF_API bool wgf_bounds_set_visible(wgf_actor_t actor, bool visible);
WGF_API bool wgf_bounds_is_visible(wgf_actor_t actor);

/* Sets or gets the margin: how far past the rectangle's edge the actor may go first
 * (default: 0). With a margin, a rock wraps only once it is wholly off the screen. It is
 * clamped to 0 or more. */
WGF_API bool wgf_bounds_set_margin(wgf_actor_t actor, float margin);
WGF_API float wgf_bounds_get_margin(wgf_actor_t actor);

#ifdef __cplusplus
}
#endif

#endif
