#ifndef WGF_INPUT_H
#define WGF_INPUT_H

#include <stdbool.h>

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where a key or button is, as read inside a tick or a frame. */
typedef enum wgf_input_state_t {
    WGF_INPUT_STATE_UP = 0,
    WGF_INPUT_STATE_PRESSED = 1, /* went down since the previous tick or frame */
    WGF_INPUT_STATE_DOWN = 2,    /* held */
    WGF_INPUT_STATE_RELEASED = 3 /* went up since the previous tick or frame */
} wgf_input_state_t;

/* The characters typed since the previous tick or frame, in order, as one UTF-8
 * string (a character can be several bytes): what a text field appends. They come
 * after the keyboard layout and any input method -- Shift and A give "A", a held
 * key repeats -- and Backspace, Enter, and the arrows type none (read them as
 * keys). "" when nothing was typed. Reading doesn't use them up: they are there
 * until the next tick or frame. */
WGF_API const char *wgf_input_get_chars(void);

/* Whether game controls (camera drags, selection, hotkeys) should leave the pointer
 * or the keyboard alone because a UI has it. Advisory: input keeps reporting
 * everything; game code checks these first.
 *
 * The pointer is captured while a press that started on an interactive node is held
 * (wgf_pointer.h), through the frame it is released, or while the game's UI says
 * so. The UI's say is sticky: set it from the UI's own hit testing every frame, such
 * as a text field that has the focus for the keyboard. */
WGF_API void wgf_input_set_pointer_captured(bool captured);
WGF_API bool wgf_input_is_pointer_captured(void);
WGF_API void wgf_input_set_keyboard_captured(bool captured);
WGF_API bool wgf_input_is_keyboard_captured(void);

#ifdef __cplusplus
}
#endif

#endif
