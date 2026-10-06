#ifndef WGF_BOUNDS_H
#define WGF_BOUNDS_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_entity.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bounds (WGF_COMPONENT_BOUNDS): a rectangle in the entity's parent's space, x and y,
 * and what happens to an entity whose position leaves it, by more than its margin, each
 * tick after motion. Defaults: 0, 0, 800 by 600, WRAP, a margin of 0. Every call is false
 * (or 0) for an entity without bounds. */
typedef enum wgf_bounds_mode_t {
    WGF_BOUNDS_MODE_WRAP = 0,    /* it comes back in at the other side, drawn moving on rather than across */
    WGF_BOUNDS_MODE_CLAMP = 1,   /* it is held at the edge, and its velocity across it stopped */
    WGF_BOUNDS_MODE_DESTROY = 2  /* it is destroyed */
} wgf_bounds_mode_t;

/* False too for a width or height below 0. Read back as x, y, width, height. */
WGF_API bool wgf_bounds_set_rect(wgf_entity_t entity, float x, float y, float width, float height);
WGF_API wgf_vec4_t wgf_bounds_get_rect(wgf_entity_t entity);

/* False too for a mode that isn't one. */
WGF_API bool wgf_bounds_set_mode(wgf_entity_t entity, wgf_bounds_mode_t mode);
WGF_API wgf_bounds_mode_t wgf_bounds_get_mode(wgf_entity_t entity);

/* How far past the rectangle's edge it may go first, so a rock wraps once it is wholly
 * off the screen. Clamped to 0 or more. */
WGF_API bool wgf_bounds_set_margin(wgf_entity_t entity, float margin);
WGF_API float wgf_bounds_get_margin(wgf_entity_t entity);

#ifdef __cplusplus
}
#endif

#endif
