#ifndef WGF_POINTER_H
#define WGF_POINTER_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_actor.h"
#include "wgf_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The pointer over actors: hover, press, and click on the actors of interactive 2D and 3D
 * stages.
 *
 * It works from the stages' picks (wgf_stage2d_pick, wgf_stage3d_pick), at the mouse's left
 * button. On a touch screen, the first finger drives the mouse (wgf_touch_set_mouse_emulated),
 * so it works there too.
 *
 * - Once a frame, before its ticks, the interactive stages drawn on the screen in the last
 *   frame are picked at the pointer.
 * - The stage drawn last is picked first. So a HUD drawn over the world takes the pointer where
 *   they overlap.
 * - A stage not drawn in the last frame is skipped.
 * - Actors are hit as those picks hit them. A glTF file's node is hit as its file's root.
 *
 * While the game's UI has the pointer (wgf_input_set_pointer_captured), no actor is under it,
 * and no press starts on an actor.
 *
 * - A press on a 2D stage's actor captures the pointer (wgf_input_is_pointer_captured) until it
 *   is let go, through the frame it is let go in. So camera drags and the game's own clicks
 *   leave it alone.
 * - A press on a 3D stage's actor doesn't capture it, so the world's camera still turns under
 *   a drag.
 *
 * Read the states inside a tick or a frame. Their edges are since the previous tick, or the
 * previous frame, as keys' are. */

/* Sets or gets whether the pointer picks a stage (default: not picked).
 *
 * A stage that is destroyed leaves the list by itself.
 *
 * set_interactive returns false if `stage` isn't a 2D or a 3D stage. */
WGF_API bool wgf_pointer_set_interactive(wgf_actor_t stage, bool interactive);
WGF_API bool wgf_pointer_is_interactive(wgf_actor_t stage);

/* Returns the topmost actor under the pointer, or 0 if there is none. */
WGF_API wgf_actor_t wgf_pointer_get_hovered(void);

/* Returns whether `actor` is under the pointer.
 *
 * - PRESSED: it came under the pointer.
 * - DOWN: it is under the pointer.
 * - RELEASED: the pointer left it.
 * - UP: it isn't under the pointer. */
WGF_API wgf_input_state_t wgf_pointer_get_hover_state(wgf_actor_t actor);

/* Returns the state of a press that started on `actor`, by the left button or a finger.
 *
 * - PRESSED: it went down on the actor.
 * - DOWN: it is held, even once the pointer has moved off the actor (a drag).
 * - RELEASED: it was let go, wherever the pointer is then.
 * - UP: otherwise. */
WGF_API wgf_input_state_t wgf_pointer_get_press_state(wgf_actor_t actor);

/* Returns true for a click: a press let go while still over the actor it started on. */
WGF_API bool wgf_pointer_is_clicked(wgf_actor_t actor);

/* Returns the actor a held press started on: the actor being dragged. Returns 0 if no press is
 * held. */
WGF_API wgf_actor_t wgf_pointer_get_held(void);

#ifdef __cplusplus
}
#endif

#endif
