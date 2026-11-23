#ifndef WGF_INPUT_H
#define WGF_INPUT_H

#include <stdbool.h>

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The state of a key or button, as read inside a tick or a frame. */
typedef enum wgf_input_state_t {
    WGF_INPUT_STATE_UP = 0,
    WGF_INPUT_STATE_PRESSED = 1, /* went down since the previous tick or frame */
    WGF_INPUT_STATE_DOWN = 2,    /* held */
    WGF_INPUT_STATE_RELEASED = 3 /* went up since the previous tick or frame */
} wgf_input_state_t;

/* Returns the characters typed since the previous tick or frame, in order, as one UTF-8
 * string.
 *
 * This is what a text field appends. A character can be several bytes.
 * - The characters come after the keyboard layout and any input method: Shift and A give "A".
 * - A held key repeats.
 * - Backspace, Enter, and the arrows type no characters. Read them as keys.
 *
 * Returns "" if nothing was typed. Reading doesn't use the characters up: they stay until the
 * next tick or frame. */
WGF_API const char *wgf_input_get_chars(void);

/* Sets or gets whether a UI has the pointer or the keyboard, so game controls should leave it
 * alone.
 *
 * Game controls are camera drags, selection, and hotkeys. The flags are advisory: input keeps
 * reporting everything, and game code checks these first.
 *
 * Each is captured while the game's UI says so, and a setting stays until it is set again.
 * libwgf's UI (wgf_ui.h) sets both from its own hit testing and focus in every frame it is
 * drawn. It lets them go in a frame it isn't drawn. */
WGF_API void wgf_input_set_pointer_captured(bool captured);
WGF_API bool wgf_input_is_pointer_captured(void);
WGF_API void wgf_input_set_keyboard_captured(bool captured);
WGF_API bool wgf_input_is_keyboard_captured(void);

/* Sets or gets the text on the system's clipboard.
 *
 * - Text is UTF-8, at most 64 KB.
 * - The string read is valid until the next clipboard call.
 * - On the web, a copy works only in the frame of an input, as browsers allow. A read returns
 *   what the page last copied or was pasted.
 *
 * wgf_input_set_clipboard returns false for NULL. */
WGF_API bool wgf_input_set_clipboard(const char *text);
WGF_API const char *wgf_input_get_clipboard(void);

#ifdef __cplusplus
}
#endif

#endif
