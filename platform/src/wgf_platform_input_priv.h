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

/* The pointer's: a press that started on an interactive node is held (or was let go
 * this frame), which captures the pointer along with the UI's own say. */
void wgf_platform_priv_input_set_pointer_held(bool held);

#endif
