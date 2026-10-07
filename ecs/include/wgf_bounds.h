#ifndef WGF_BOUNDS_H
#define WGF_BOUNDS_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_component.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bounds (WGF_COMPONENT_BOUNDS): a rectangle in the actor's parent's space, x and y,
 * and what happens to an actor whose position leaves it, by more than its margin, each
 * tick after motion. Defaults: 0, 0, 800 by 600, WRAP, a margin of 0. Every call is false
 * (or 0) for an actor without bounds. */
typedef enum wgf_bounds_mode_t {
    WGF_BOUNDS_MODE_WRAP = 0,    /* it comes back in at the other side, drawn moving on rather than across */
    WGF_BOUNDS_MODE_CLAMP = 1,   /* it is held at the edge, and its velocity across it stopped */
    WGF_BOUNDS_MODE_DESTROY = 2  /* it is destroyed */
} wgf_bounds_mode_t;

/* False too for a width or height below 0. Read back as x, y, width, height. */
WGF_API bool wgf_bounds_set_rect(wgf_actor_t actor, float x, float y, float width, float height);
WGF_API wgf_vec4_t wgf_bounds_get_rect(wgf_actor_t actor);

/* False too for a mode that isn't one. */
WGF_API bool wgf_bounds_set_mode(wgf_actor_t actor, wgf_bounds_mode_t mode);
WGF_API wgf_bounds_mode_t wgf_bounds_get_mode(wgf_actor_t actor);

/* Whether the rectangle is the presentation's visible area (wgf_presentation.h), read
 * each tick, so what wraps at the screen's edge wraps at the window's whatever its size
 * (a scene file's `bounds visible=true`); read back by get_rect as it is now. Setting a
 * rectangle turns it off. Default: off. */
WGF_API bool wgf_bounds_set_visible(wgf_actor_t actor, bool visible);
WGF_API bool wgf_bounds_is_visible(wgf_actor_t actor);

/* How far past the rectangle's edge it may go first, so a rock wraps once it is wholly
 * off the screen. Clamped to 0 or more. */
WGF_API bool wgf_bounds_set_margin(wgf_actor_t actor, float margin);
WGF_API float wgf_bounds_get_margin(wgf_actor_t actor);

#ifdef __cplusplus
}
#endif

#endif
