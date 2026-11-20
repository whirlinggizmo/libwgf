#ifndef WGF_POINTER_H
#define WGF_POINTER_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_actor.h"
#include "wgf_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The pointer over actors: hover, press, and click on the actors of the interactive 2D and
 * 3D stages, from their picks (wgf_stage2d_pick, wgf_stage3d_pick) at the mouse's left
 * button, or the first finger on a touch screen (which drives the mouse,
 * wgf_touch_set_mouse_emulated); libwgt's pointer, wgrender's scene interaction. Once a
 * frame, before its ticks, the interactive stages drawn on the screen in the last frame are
 * picked at the pointer, the one drawn last first, so a HUD drawn over the world takes the
 * pointer where they overlap; a stage not drawn in the last frame is skipped. Actors are hit
 * as those picks hit them (a glTF file's node as its file's root).
 *
 * While the game's UI has the pointer (wgf_input_set_pointer_captured), nothing is under it
 * and no press starts on an actor. A press on a 2D stage's actor captures the pointer
 * (wgf_input_is_pointer_captured) until it is let go, through the frame it is let go in, so
 * camera drags and the game's own clicks leave it alone; a press on a 3D stage's actor
 * doesn't, so the world's camera still turns under a drag. States are read inside a tick or
 * a frame, with edges since the previous tick, or the previous frame, as keys have. */

/* Which stages the pointer picks (default: none). False for a handle that isn't a 2D or a
 * 3D stage. A stage destroyed leaves the list by itself. */
WGF_API bool wgf_pointer_set_interactive(wgf_actor_t stage, bool interactive);
WGF_API bool wgf_pointer_is_interactive(wgf_actor_t stage);

/* The topmost actor under the pointer; 0 for none. */
WGF_API wgf_actor_t wgf_pointer_get_hovered(void);

/* PRESSED: came under the pointer; DOWN: under it; RELEASED: the pointer left it; UP: not
 * under it. */
WGF_API wgf_input_state_t wgf_pointer_get_hover_state(wgf_actor_t actor);

/* The left button, or a finger, for a press that started on `actor`: PRESSED when it went
 * down on it, DOWN while held, also once the pointer has moved off it (a drag), RELEASED when
 * let go, wherever the pointer is then; UP otherwise. */
WGF_API wgf_input_state_t wgf_pointer_get_press_state(wgf_actor_t actor);

/* A press let go while still over the actor it started on: a click. */
WGF_API bool wgf_pointer_is_clicked(wgf_actor_t actor);

/* The actor a held press started on, 0 with none held: what is being dragged. */
WGF_API wgf_actor_t wgf_pointer_get_held(void);

#ifdef __cplusplus
}
#endif

#endif
