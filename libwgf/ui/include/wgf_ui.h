#ifndef WGF_UI_H
#define WGF_UI_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Game UI, immediate mode: each frame the program describes the UI as it is now --
 * boxes, panels, labels, buttons, progress bars -- between wgf_ui_begin and wgf_ui_end, inside the
 * frame callback; libwgf lays it out (Clay), answers what the pointer, the keys, and the
 * pads did to it, and draws it at wgf_ui_end, over what the frame drew before.
 *
 * The screen is the root: a column of everything given, centered both ways. A box lays
 * its children out in a column or a row, sized to fit them unless set otherwise. A
 * button's answer comes from where things were in the frame before, as with every
 * immediate-mode UI: a UI that moves under the pointer is answered where it was.
 *
 * Focus: one button at a time has it, by its id. Arrow keys, Tab and Shift+Tab, and a
 * pad's D-pad move it through the frame's buttons in the order they were given (the
 * first press of one puts it on the first or the last); Enter, Space, or a pad's south
 * button activates it. The pointer activates the button it is pressed and released over,
 * and gives it the focus; a focus moved by keys or pads is drawn, one given by the
 * pointer isn't. A focused button that isn't given in a frame loses the focus.
 *
 * The UI says when it has the pointer (wgf_input_set_pointer_captured): while the
 * pointer is over a panel, a button, or a box with a color, or a press on a button is
 * held; and the keyboard (wgf_input_set_keyboard_captured) while a button has the focus.
 * A frame without wgf_ui_end lets both go.
 *
 * Ids name boxes, panels, and buttons, and are unique in a frame (Clay refuses a
 * second, logged); a box or panel may have none (NULL or ""). Text is copied: it may
 * change or go after the call. Every call but the style's is false outside a begun UI. */

typedef enum wgf_ui_direction_t {
    WGF_UI_DIRECTION_COLUMN = 0, /* children top to bottom */
    WGF_UI_DIRECTION_ROW = 1     /* children left to right */
} wgf_ui_direction_t;

typedef enum wgf_ui_align_t {
    WGF_UI_ALIGN_START = 0, /* left, or top */
    WGF_UI_ALIGN_CENTER = 1,
    WGF_UI_ALIGN_END = 2 /* right, or bottom */
} wgf_ui_align_t;

typedef enum wgf_ui_sizing_t {
    WGF_UI_SIZING_FIT = 0,    /* as big as its children (the default) */
    WGF_UI_SIZING_GROW = 1,   /* the room its parent has left */
    WGF_UI_SIZING_FIXED = 2,  /* the value, in logical pixels */
    WGF_UI_SIZING_PERCENT = 3 /* the value, 0 to 1, of its parent's room: less its padding and gaps */
} wgf_ui_sizing_t;

/* The frame's UI begun, the screen at its logical size as the root; false when one is
 * already begun, or outside a frame. */
WGF_API bool wgf_ui_begin(void);

/* Laid out, answered, and drawn; boxes left open are closed, and the call is false
 * (logged). False too with no UI begun. */
WGF_API bool wgf_ui_end(void);

/* A box, its children laid out in `direction`; closed by wgf_ui_end_box. A panel is a
 * column drawn with the style's panel color, corners, and padding, and gapped by the
 * style's gap; closed by wgf_ui_end_panel. False for an end with nothing of its kind
 * open. */
WGF_API bool wgf_ui_begin_box(const char *id, wgf_ui_direction_t direction);
WGF_API bool wgf_ui_end_box(void);
WGF_API bool wgf_ui_begin_panel(const char *id);
WGF_API bool wgf_ui_end_panel(void);

/* The open box's or panel's layout, before its first child; false after it, with none
 * open, for a sizing or alignment that isn't one, or a value below 0 (above 1 for a
 * percent). The open one is the program's to describe each frame: nothing keeps it to
 * read back. A color draws its background; 0 (the default) none. */
WGF_API bool wgf_ui_set_width(wgf_ui_sizing_t sizing, float value);
WGF_API bool wgf_ui_set_height(wgf_ui_sizing_t sizing, float value);
WGF_API bool wgf_ui_set_padding(float x, float y);
WGF_API bool wgf_ui_set_gap(float gap);
WGF_API bool wgf_ui_set_align(wgf_ui_align_t x, wgf_ui_align_t y);
WGF_API bool wgf_ui_set_color(wgf_color_t color);

/* Text, in the style's font and text color, at `size` logical pixels (0 or less: the
 * style's text size), on one line per newline. False too for no text. */
WGF_API bool wgf_ui_label(const char *text, float size);

/* Room, along the open box's direction: `size` logical pixels, or the room left (0). */
WGF_API bool wgf_ui_spacer(float size);

/* A button showing `text`, true in the frame it is activated. False too for no id. */
WGF_API bool wgf_ui_button(const char *id, const char *text);

/* A bar showing `value`, 0 (empty) to 1 (full), filled from the left: how far a
 * loading screen's files have come (wgf_asset_task_get_progress), say. The style's bar
 * width and height, its track and fill colors, and its corners; it shows a value, so
 * the pointer over it isn't the UI's. A value below 0 shows 0, above 1 shows 1, and NaN
 * shows 0. */
WGF_API bool wgf_ui_progress(float value);

/* The focus given to the button `id` (NULL or "": none), drawn as a key or pad would
 * draw it; false for an id of 64 bytes or more. The focused button's id, "" for none,
 * valid until the focus changes. These two work outside a begun UI too. Each frame a UI
 * ends, the focus is published as the text probe "ui.focus" (wgf_probe.h), so an
 * autopilot can expect on which button has it. */
WGF_API bool wgf_ui_set_focus(const char *id);
WGF_API const char *wgf_ui_get_focus(void);

/* The style, the game's own, kept until changed: colors, sizes in logical pixels, and a
 * font (0: the default font). Reset gives libwgf's. */
typedef enum wgf_ui_color_t {
    WGF_UI_COLOR_TEXT = 0,
    WGF_UI_COLOR_PANEL = 1,
    WGF_UI_COLOR_BUTTON = 2,
    WGF_UI_COLOR_BUTTON_HOVERED = 3,
    WGF_UI_COLOR_BUTTON_PRESSED = 4,
    WGF_UI_COLOR_BUTTON_TEXT = 5,
    WGF_UI_COLOR_FOCUS = 6, /* the line around a focused button */
    WGF_UI_COLOR_BAR = 7,   /* a progress bar's track */
    WGF_UI_COLOR_BAR_FILL = 8
} wgf_ui_color_t;

typedef enum wgf_ui_value_t {
    WGF_UI_VALUE_TEXT_SIZE = 0,      /* a label's and a button's text */
    WGF_UI_VALUE_PADDING = 1,        /* inside a panel */
    WGF_UI_VALUE_GAP = 2,            /* between a panel's children */
    WGF_UI_VALUE_CORNER_RADIUS = 3,  /* a panel's and a button's corners */
    WGF_UI_VALUE_BUTTON_PADDING = 4, /* around a button's text, across; half that up and down */
    WGF_UI_VALUE_BUTTON_WIDTH = 5,   /* a button's least width */
    WGF_UI_VALUE_FOCUS_WIDTH = 6,    /* the focus line's thickness */
    WGF_UI_VALUE_BAR_WIDTH = 7,      /* a progress bar's */
    WGF_UI_VALUE_BAR_HEIGHT = 8
} wgf_ui_value_t;

/* False for a color or value that isn't one, or a value below 0. 0 for one that isn't. */
WGF_API bool wgf_ui_set_style_color(wgf_ui_color_t which, wgf_color_t color);
WGF_API wgf_color_t wgf_ui_get_style_color(wgf_ui_color_t which);
WGF_API bool wgf_ui_set_style_value(wgf_ui_value_t which, float value);
WGF_API float wgf_ui_get_style_value(wgf_ui_value_t which);
/* False for a handle that isn't a font (0 is). */
WGF_API bool wgf_ui_set_style_font(wgf_font_t font);
WGF_API wgf_font_t wgf_ui_get_style_font(void);
WGF_API void wgf_ui_reset_style(void);

#ifdef __cplusplus
}
#endif

#endif
