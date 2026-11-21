#ifndef WGF_PLATFORM_INPUT_PRIV_H
#define WGF_PLATFORM_INPUT_PRIV_H

#include <stdbool.h>

#include "sokol_app.h"

/* Input from the window's events. Held state (keys and buttons down, where the
 * pointer is) is shared; edges (pressed, released, movement, scrolling, typing)
 * are kept twice, since they are relative to whoever reads them:
 *
 *   frame edges  since the previous frame callback; cleared after it runs
 *   tick edges   since the previous tick; cleared after each tick. A frame that
 *                runs no tick carries them over, so every press is seen by
 *                exactly one tick
 *
 * The loop says which set the getters read before each callback. Cribbed from
 * wgrender's wgr_input. */

typedef enum wgf_platform_priv_input_context_t {
    WGF_PLATFORM_PRIV_INPUT_FRAME = 0,
    WGF_PLATFORM_PRIV_INPUT_TICK = 1
} wgf_platform_priv_input_context_t;

void wgf_platform_priv_input_reset(void);
/* An event from the window; `dpi_scale` turns its pixels into logical ones. */
void wgf_platform_priv_input_handle_event(const sapp_event *event, float dpi_scale);
void wgf_platform_priv_input_set_context(wgf_platform_priv_input_context_t context);
wgf_platform_priv_input_context_t wgf_platform_priv_input_get_context(void);
void wgf_platform_priv_input_end_tick(void);
void wgf_platform_priv_input_end_frame(void);

/* Everything held let go: each key, mouse button, and finger down goes up, its release an
 * edge of the frame's and the tick's, with no gesture. What a replay (wgf_replay.h) does as
 * it starts and ends, so what it plays starts from nothing held and leaves nothing held. */
void wgf_platform_priv_input_release_all(void);

/* Called as a tick's or a frame's edges end, before they are cleared: input actions'
 * (wgf_platform_action.c) keep each action's state there, set by the first binding, so a
 * program with no actions links none of them. */
void wgf_platform_priv_input_set_edges_hook(void (*ended)(wgf_platform_priv_input_context_t context));

/* The optional input parts the loop drives, each through hooks its first call sets, so a
 * program that never reads a pad or asks for tilt links neither (wgf_platform_gamepad.c,
 * wgf_platform_tilt.c): the pads opened with the window (at once, and read then, when it is
 * open already: the call that reached them sees them),
 * read at each frame's start, their edges ended with the tick's and the frame's, and closed;
 * tilt taken in at each frame's end. The loop (app) calls the wrappers; one with no part set
 * does nothing. */
typedef struct wgf_platform_priv_pads_t {
    void (*open)(void);
    void (*close)(void);
    void (*begin_frame)(void);
    void (*end_tick)(void);
    void (*end_frame)(void);
} wgf_platform_priv_pads_t;
void wgf_platform_priv_set_pads(const wgf_platform_priv_pads_t *pads);
void wgf_platform_priv_set_tilt_frame(void (*end_frame)(void));
void wgf_platform_priv_pads_open(void);
void wgf_platform_priv_pads_close(void);
void wgf_platform_priv_pads_begin_frame(void);
void wgf_platform_priv_pads_end_tick(void);
void wgf_platform_priv_pads_end_frame(void);
void wgf_platform_priv_tilt_frame(void);

/* The pointer's: a press that started on an interactive actor is held (or was let go
 * this frame), which captures the pointer along with the UI's own say. */
void wgf_platform_priv_input_set_pointer_held(bool held);
/* The UI's say alone (wgf_input_set_pointer_captured), without the pointer's hold: what the
 * pointer section leaves the pointer to. */
bool wgf_platform_priv_input_is_ui_pointer_captured(void);

#endif
