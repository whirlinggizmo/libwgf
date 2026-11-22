#ifndef WGF_UI_H
#define WGF_UI_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_font.h"
#include "wgf_image.h"
#include "wgf_texture.h"
#include "wgf_vec2.h"
#include "wgf_vec4.h"

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
 * Focus: one widget at a time has it, by its id: a button, a toggle, a checkbox, a slider,
 * a number, tabs, a choice, a combo, a tile, a list, a dropdown, or a text field. The arrow keys and a pad's D-pad move it to
 * the nearest widget that way on the screen, as last laid out (a grid of tiles moves in
 * two dimensions), or, with none that way, through the frame's widgets in the order they
 * were given, Down and Right to the next and Up and Left to the one before, wrapping; Tab
 * and Shift+Tab always go in that order. The first press of one puts it on the first or the
 * last widget. A focused widget keeps the arrows it uses (a slider, a number, tabs, a
 * choice, a combo, and a text field left and right; a slider that fills up or down, and a
 * list, up and down; an open dropdown, all four).
 * Enter, Space, or a pad's south button activates it. The
 * pointer activates the widget it is pressed and released over, and gives it the focus;
 * a focus moved by keys or pads is drawn, one given by the pointer isn't. A focused
 * widget that isn't given in a frame loses the focus. A disabled widget
 * (wgf_ui_set_disabled) is drawn so, answers nothing, and is never focused.
 *
 * The UI says when it has the pointer (wgf_input_set_pointer_captured): while the
 * pointer is over a panel, a button, or a box with a color, or a press on a button is
 * held; and the keyboard (wgf_input_set_keyboard_captured) while a button has the focus.
 * A frame without wgf_ui_end lets both go.
 *
 * Ids name boxes, panels, groups, and widgets, and are unique in a frame (Clay refuses a
 * second, logged); a box, panel, or group may have none (NULL or ""). Text is copied: it
 * may change or go after the call. Every call but the style's and the skins' is false
 * (a widget's: its value as given) outside a begun UI.
 *
 * A widget with a value takes it and returns it as the user left it: the program keeps
 * the value, and the call shows it and answers what was done to it this frame
 * (`volume = wgf_ui_slider("volume", volume, 0, 10)`). */

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

/* A grid: its children laid out in rows of `columns`, left to right, then top to bottom,
 * gapped by the style's gap both ways (Clay wraps no row: the grid makes them); closed by
 * wgf_ui_end_grid. False too for fewer than 1 column. */
WGF_API bool wgf_ui_begin_grid(const char *id, int columns);
WGF_API bool wgf_ui_end_grid(void);

/* A box, its children laid out in `direction`; closed by wgf_ui_end_box. A panel is a
 * column drawn with the style's panel color, corners, and padding, and gapped by the
 * style's gap; closed by wgf_ui_end_panel. A group is a column drawn as an outline in the
 * style's separator color and line width, with `title` (NULL or "": none) as its first
 * line, padded and gapped as a panel; closed by wgf_ui_end_group. False for an end with
 * nothing of its kind open. */
WGF_API bool wgf_ui_begin_box(const char *id, wgf_ui_direction_t direction);
WGF_API bool wgf_ui_end_box(void);
WGF_API bool wgf_ui_begin_panel(const char *id);
WGF_API bool wgf_ui_end_panel(void);
WGF_API bool wgf_ui_begin_group(const char *id, const char *title);
WGF_API bool wgf_ui_end_group(void);

/* A scroll area: a window `width` by `height` logical pixels (0: the room its parent has
 * left that way) onto its children, a column, clipped to it and scrolled when they are
 * longer or wider than it: by the wheel over it (the innermost area under the pointer; a
 * wheel across, or Shift with it, sideways), a drag (a press that moves a few pixels is the
 * area's, and the widget it began on, but a slider, isn't pressed), with a fling when let
 * go moving, and its scroll bars, each shown while there is more to scroll to that way (always,
 * with the style's WGF_UI_VALUE_SCROLL_BAR_ALWAYS: disabled, its thumb the whole track, with
 * no more): the thumb dragged, the track pressed for a page. The focus moved by keys, a pad, or
 * wgf_ui_set_focus to a widget in it scrolls the widget into view, and Page Up and Page
 * Down page while the focus is in it. An area with no widget in it to focus (a page of text)
 * and more than it shows is focused itself, by its id, from the frame after it was first laid
 * out so: focused, its arrows scroll it a row at a time and the page keys a page. The layout calls (padding, gap, alignment, color) are
 * its content's; its size is the call's, so wgf_ui_set_width and _height refuse it. Its
 * scroll is kept while it is given each frame; what it scrolls to shows from the next
 * frame. Closed by wgf_ui_end_scroll. False too for no id, or a size below 0. */
WGF_API bool wgf_ui_begin_scroll(const char *id, float width, float height);
WGF_API bool wgf_ui_end_scroll(void);

/* How far the scroll area or list `id` is scrolled, x across and y down, in logical pixels
 * from its content's top left (0, 0 for one not laid out); and scrolled there, shown from
 * its next layout, held to its content as last laid out: a chat log kept at its end. False
 * for an id that isn't a scroll area or list laid out in the frame before, or a value not
 * finite. These two work outside a begun UI too. */
WGF_API bool wgf_ui_set_scroll(const char *id, float x, float y);
WGF_API wgf_vec2_t wgf_ui_get_scroll(const char *id);

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

/* A line across the open box (along a column's width, down a row's height), in the
 * style's separator color and line width. */
WGF_API bool wgf_ui_separator(void);

/* A texture placed by the layout, `width` by `height` logical pixels (0 or less: the
 * texture's own, as it is when called: 0 by 0 until it is READY), stretched to fill them
 * and untinted (dimmed when disabled). False too for a handle that isn't a texture. */
WGF_API bool wgf_ui_image(wgf_texture_t texture, float width, float height);

/* Room `width` by `height` logical pixels that the program draws in itself
 * (wgf_draw.h), where wgf_ui_get_bounds says it was laid out: raygui's dummy rectangle,
 * for what no widget draws. False too for no id, or a size below 0. */
WGF_API bool wgf_ui_custom(const char *id, float width, float height);

/* Where the element `id` (a box, panel, group, widget, or custom room) was laid out, in
 * the screen's logical pixels: x and y its top left, z and w its width and height; this
 * frame's after wgf_ui_end, the frame before's until then. All 0 for an id not laid out. */
WGF_API wgf_vec4_t wgf_ui_get_bounds(const char *id);

/* A button showing `text`, true in the frame it is activated. False too for no id. An
 * icon button shows `icon` (a texture: 0 for none), as tall as the text and as wide as
 * its shape keeps, before its text (NULL or "": the icon alone). */
WGF_API bool wgf_ui_button(const char *id, const char *text);
WGF_API bool wgf_ui_icon_button(const char *id, wgf_texture_t icon, const char *text);

/* A button that is its art: `normal` at its size, `pressed` (0: `normal`) while held,
 * and only where `mask` (a CPU image, 0 for none: the whole of it) is not clear does the
 * pointer press it: a round button's corners are the screen's. The mask is read as
 * stretched over the button. True in the frame it is activated. False too for no id, or
 * a handle that isn't a texture or an image. */
WGF_API bool wgf_ui_image_button(const char *id, wgf_texture_t normal, wgf_texture_t pressed, wgf_image_t mask);

/* A toggle (a button that stays down while on) and a checkbox (a box, checked while on,
 * `text` beside it: NULL or "" for none), each turned by an activation: `on` as given,
 * or flipped in the frame it is activated. `on` as given when refused (no id). */
WGF_API bool wgf_ui_toggle(const char *id, const char *text, bool on);
WGF_API bool wgf_ui_checkbox(const char *id, const char *text, bool on);

/* A slider: `value` between `min` and `max`, shown by its handle along its track and the
 * fill behind it (the slider's fill direction, wgf_ui_set_fill: from the left by default;
 * from the top or bottom, it stands up), the style's slider width and height and handle
 * size. A press on it or a drag from one sets it where the pointer is; when focused, its
 * arrows (and a pad's D-pad's) move it a twentieth of its range, held ones repeating.
 * The value as given (clamped to the range) or as moved; as given when refused (no id,
 * or `max` not above `min`, or either not finite). */
WGF_API float wgf_ui_slider(const char *id, float value, float min, float max);

/* A number: `value` between `min` and `max` shown between a minus and a plus, which step it
 * by `step`, as its arrows (and a pad's D-pad's) do when it is focused, held ones
 * repeating: raygui's spinner. Shown with as many decimals as `step` has, up to 4. The value
 * as given (clamped to the range) or as stepped; as given when refused (no id, `max` not
 * above `min`, `step` not above 0, or any not finite). */
WGF_API float wgf_ui_number(const char *id, float value, float min, float max, float step);

/* One of `labels`, one a line ("Easy\nNormal\nHard": up to 32), chosen: `selected` as given
 * (clamped to them), or as the user chose this frame. Tabs are a row of them, the chosen
 * one pressed, whose panel the program draws below; a choice is the same drawn as toggles
 * (raygui's toggle group); a combo shows the chosen one alone, and steps to the next when
 * activated (its arrows step either way, wrapping). A press chooses the tab or toggle it is
 * on; when focused, the arrows step. `selected` as given when refused (no id, no labels). */
WGF_API int wgf_ui_tabs(const char *id, const char *labels, int selected);
WGF_API int wgf_ui_choice(const char *id, const char *labels, int selected);
WGF_API int wgf_ui_combo(const char *id, const char *labels, int selected);

/* A tile: `picture` (a texture: 0 for none) above `text` (NULL or "": none), the style's
 * tile size wide, its picture square in it: a level select's level, an inventory's item,
 * in a grid. True in the frame it is activated. False too for no id, or a handle that
 * isn't a texture. */
WGF_API bool wgf_ui_tile(const char *id, wgf_texture_t picture, const char *text);

/* Whether the widgets given after it, until it is changed, are disabled: drawn in their
 * disabled state, answering nothing, never focused. Each UI begins with none disabled.
 * False with no UI begun. */
WGF_API bool wgf_ui_set_disabled(bool disabled);
WGF_API bool wgf_ui_is_disabled(void);

/* Whether the widget given just before it was activated this frame: a button, an icon or
 * image button, a toggle, a checkbox, or a tile, as its own call answered; a list, its chosen
 * item (see wgf_ui_list). False for any other widget, with none before it in the frame, and
 * outside a begun UI. */
WGF_API bool wgf_ui_is_activated(void);

/* A list of `items`, one a line (any number: "" or NULL, none), `rows` of them tall and as
 * wide as its parent's room (at least the style's button width), one of them chosen:
 * `selected` (-1: none) as given (clamped to them), or as the user chose this frame. A press
 * and release on an item chooses it (a drag scrolls, choosing none); when focused, Up and
 * Down (a pad's too) step, held ones repeating, and Page Up and Page Down step a page, the
 * chosen one scrolled into view. It scrolls as a scroll area does, down only. Only the
 * items in view are laid out, so a long list costs its rows. Its chosen item is activated
 * (wgf_ui_is_activated, asked after the call) by Enter, Space, or a pad's south button while
 * the list is focused, or a press and release on the item already chosen: a double press on
 * one not yet. `selected` as given when refused (no id, `rows` below 1). */
WGF_API int wgf_ui_list(const char *id, const char *items, int selected, int rows);

/* A text field: a line of text typed in, as wide as its parent's room (at least the style's
 * button width), its text kept by the UI by its id (wgf_ui_get_text) from the first call or
 * wgf_ui_set_text on, until the UI stops. Focused, it takes what is typed (wgf_input_get_chars)
 * in place of the selection, and the keys:
 *
 *   Left, Right         the caret a character; with Ctrl (Alt on macOS) a word
 *   Home, End           to the start, the end (Cmd+Left and +Right on macOS too)
 *   Shift with a move   the selection extended
 *   Backspace, Delete   the selection, else the character before or after (a word with Ctrl
 *                       or Alt), held ones repeating
 *   Ctrl+A (Cmd on macOS)        all selected
 *   Ctrl+C, +X, +V               copy, cut, and paste, through the system's clipboard
 *                                (wgf_input_set_clipboard); newlines pasted are stripped
 *   Ctrl+Z; Ctrl+Y, Ctrl+Shift+Z undo and redo, a run of typing or of deleting one step
 *
 * A press places the caret (with Shift, extends the selection to it), a drag selects, a
 * double press selects the word under it and a triple the whole; by touch, the same taps.
 * Its caret blinks, and the text scrolls to keep it in view. Focus moved to it by keys or a
 * pad selects the whole; a press off it lets the focus go. While it is focused the platform's
 * on-screen keyboard is shown (a phone's; on the web, the page's hidden input that a phone's
 * keyboard types into). `placeholder` (NULL or "": none) is shown, dimmed, while it is empty;
 * it holds at most `max_length` characters (0 or less: 4095 bytes' worth), a longer text cut
 * to it. A password field is the same drawn as dots, its words the whole of it, and never
 * copied or cut. True in the frame Enter is pressed in it; false too when refused (no id). */
WGF_API bool wgf_ui_text_field(const char *id, const char *placeholder, int max_length);
WGF_API bool wgf_ui_password_field(const char *id, const char *placeholder, int max_length);

/* The text field `id`'s text, its caret put at the end and its undo forgotten; cut to 4095
 * bytes. A field not given yet keeps it for its first call. False for no id, one of 64 bytes
 * or more, or no text. Its text: "" for a field never given or set, valid until the field
 * changes. These two work outside a begun UI too. */
WGF_API bool wgf_ui_set_text(const char *id, const char *text);
WGF_API const char *wgf_ui_get_text(const char *id);

/* Whether the text fields given after it, until it is changed, show their text as refused
 * (what reads it decides: a name already taken), in the style's invalid color, outlined in
 * it. Each UI begins with none invalid. False with no UI begun. */
WGF_API bool wgf_ui_set_invalid(bool invalid);
WGF_API bool wgf_ui_is_invalid(void);

/* The popup layer: what is drawn over every widget, clipped by none, placed by the layer.
 *
 * A dropdown shows the chosen one of `labels`, one a line (up to 32), and opens a list of
 * them when activated, over everything, hung below it (above when the screen has no room
 * below and more above), moved in to stay on the screen: a press and release on one chooses
 * it, and when focused, Up and Down (a pad's too) step through them and Enter, Space, or a
 * pad's south button chooses the one stepped to. It keeps every arrow while open. A press
 * anywhere off it closes it and does nothing else; Escape and a pad's east button close it,
 * as the dropdown losing the focus, being disabled, or not given in a frame does. One is
 * open at a time. `selected` as given (clamped to them), or as chosen this frame; as given
 * when refused (no id, no labels). */
WGF_API int wgf_ui_dropdown(const char *id, const char *labels, int selected);

/* A modal: a panel (its layout, its skin's, and its children a panel's) at the screen's
 * center, over the screen dimmed in the style's dim color: nothing under it answers the
 * pointer, and while it is given the focus moves among its own widgets alone, put on its
 * first when it opens, and given back to the widget that had it when it closes (is no longer
 * given): "Quit? Yes / No". A dropdown in it opens over it. One a frame; closed by
 * wgf_ui_end_modal. False too for no id, or a modal already open. */
WGF_API bool wgf_ui_begin_modal(const char *id);
WGF_API bool wgf_ui_end_modal(void);

/* `text` shown for the widget given just before it (a disabled one too) once the pointer has
 * rested on it half a second, or the focus moved by keys or a pad has: in a box in the
 * style's popup color, below it (above when the screen has no room below and more above),
 * moved in to stay on the screen; the pointer passes through it, and a press hides it. False
 * too for no text, or no widget given before it in the frame. */
WGF_API bool wgf_ui_tooltip(const char *text);

/* The whole UI drawn at `alpha` of its own (0 unseen, 1 as drawn: the default), kept until
 * changed: a menu faded in or out. It answers as ever: a UI faded to nothing is still there
 * until it isn't given. False for a value outside 0 to 1, or not finite. These two work
 * outside a begun UI too. */
WGF_API bool wgf_ui_set_alpha(float alpha);
WGF_API float wgf_ui_get_alpha(void);

/* A bar showing `value`, 0 (empty) to 1 (full), filled from the left (the progress bar's
 * fill direction, wgf_ui_set_fill; from the top or bottom, its bar width and height
 * swap): how far a loading screen's files have come (wgf_asset_task_get_progress), say.
 * The style's bar width and height, its track and fill colors, and its corners; it shows
 * a value, so the pointer over it isn't the UI's. A value below 0 shows 0, above 1 shows
 * 1, and NaN shows 0. */
WGF_API bool wgf_ui_progress(float value);

/* The focus given to the widget `id` (NULL or "": none), drawn as a key or pad would
 * draw it; false for an id of 64 bytes or more. The focused widget's id, "" for none,
 * valid until the focus changes. These two work outside a begun UI too. Each frame a UI
 * ends, the focus is published as the text probe "ui.focus" (wgf_probe.h), so an
 * autopilot can expect on which widget has it. */
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
    WGF_UI_COLOR_FOCUS = 6,     /* the line around a focused widget */
    WGF_UI_COLOR_BAR = 7,       /* a progress bar's and a slider's track, a checkbox's box */
    WGF_UI_COLOR_BAR_FILL = 8,  /* their fill, and a checkbox's check */
    WGF_UI_COLOR_SEPARATOR = 9, /* a separator, and a group's outline */
    WGF_UI_COLOR_HANDLE = 10,   /* a slider's handle */
    WGF_UI_COLOR_POPUP = 11,    /* a dropdown's open list, a tooltip's box */
    WGF_UI_COLOR_DIM = 12,      /* over the screen behind a modal */
    WGF_UI_COLOR_INVALID = 13,  /* an invalid text field's text and outline */
    WGF_UI_COLOR_SELECTION = 14 /* behind a text field's selected text */
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
    WGF_UI_VALUE_BAR_HEIGHT = 8,
    WGF_UI_VALUE_CHECK_SIZE = 9,     /* a checkbox's box */
    WGF_UI_VALUE_SLIDER_WIDTH = 10,  /* a slider's length */
    WGF_UI_VALUE_SLIDER_HEIGHT = 11, /* its track's thickness */
    WGF_UI_VALUE_HANDLE_SIZE = 12,   /* its handle: across its track, and along it (as tall as the slider when skinned) */
    WGF_UI_VALUE_LINE_WIDTH = 13,    /* a separator's and a group's outline */
    WGF_UI_VALUE_TILE_SIZE = 14,     /* a tile's width, and its picture's side less its padding */
    WGF_UI_VALUE_SCROLL_BAR = 15,    /* a scroll bar's thickness */
    WGF_UI_VALUE_SCROLL_BAR_ALWAYS = 16 /* above 0: a scroll bar is shown with nothing to scroll to, too (0: only with more) */
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

/* Skins: a widget's parts drawn from the game's art in place of the style's colors, as
 * helix-raylib's gui skins are. A skin is a widget's part in a state: a texture's region
 * (a source width or height of 0 or less: the whole texture), nine-sliced by its borders
 * (wgf_draw_texture_nine_slice: borders of 0 top and bottom make a bar with caps at its
 * ends, of 0 left and right one up and down), multiplied by its tint (white until set).
 * A state with no skin of its own draws the normal state's (a disabled one dimmed); a part
 * from art has its art's corners, not the corner radius. The parts each widget draws:
 *
 *   PANEL      base: its body
 *   BUTTON     base: the button (an icon button's too)
 *   TOGGLE     base: the toggle, PRESSED while on
 *   CHECKBOX   base: the box; fill: the check, over the box
 *   SLIDER     base: the track; fill: behind the handle; handle: the handle
 *   PROGRESS   base: the track; fill: the progress; edge: at the fill's front
 *   GROUP      base: its outline, in place of the line
 *   TAB        base: each tab, PRESSED while chosen
 *   COMBO      base: the combo
 *   NUMBER     base: the box its value is in (its minus and plus are BUTTON's)
 *   TILE       base: the tile
 *   SCROLL     base: a scroll bar's track; handle: its thumb (a scroll area's, a list's)
 *   LIST       base: its box; item: each item, PRESSED while chosen
 *   DROPDOWN   base: the dropdown; item: each item of its open list, PRESSED while chosen,
 *              HOVERED under the pointer or stepped to
 *   POPUP      base: a dropdown's open list's box, a tooltip's box
 *   TEXT       base: a text field's box, PRESSED while it is focused
 *
 * A choice's toggles are TOGGLE's, each PRESSED while chosen; a modal's panel is PANEL's.
 *
 * A fill is stretched over the filled part, or clipped: drawn over the whole track and
 * shown up to the value, so a bar with round ends shows its far end only when full
 * (Godot's TextureProgressBar); from the left, right, top, or bottom (wgf_ui_set_fill). An
 * edge is drawn at its art's size, centered on the fill's front. A widget's text (a
 * button's, a toggle's) moves by its state's text offset: a face that sinks when pressed
 * takes its text with it. The style holds a reference to each texture; reset lets them go.
 * False for a widget, part, or state that isn't one, a handle that isn't a texture, or a
 * value not finite (a border below 0). */
typedef enum wgf_ui_widget_t {
    WGF_UI_WIDGET_PANEL = 0,
    WGF_UI_WIDGET_BUTTON = 1,
    WGF_UI_WIDGET_TOGGLE = 2,
    WGF_UI_WIDGET_CHECKBOX = 3,
    WGF_UI_WIDGET_SLIDER = 4,
    WGF_UI_WIDGET_PROGRESS = 5,
    WGF_UI_WIDGET_GROUP = 6,
    WGF_UI_WIDGET_TAB = 7,
    WGF_UI_WIDGET_COMBO = 8,
    WGF_UI_WIDGET_NUMBER = 9,
    WGF_UI_WIDGET_TILE = 10,
    WGF_UI_WIDGET_SCROLL = 11,
    WGF_UI_WIDGET_LIST = 12,
    WGF_UI_WIDGET_DROPDOWN = 13,
    WGF_UI_WIDGET_POPUP = 14,
    WGF_UI_WIDGET_TEXT = 15
} wgf_ui_widget_t;

typedef enum wgf_ui_part_t {
    WGF_UI_PART_BASE = 0,
    WGF_UI_PART_FILL = 1,
    WGF_UI_PART_HANDLE = 2,
    WGF_UI_PART_EDGE = 3,
    WGF_UI_PART_ITEM = 4
} wgf_ui_part_t;

typedef enum wgf_ui_state_t {
    WGF_UI_STATE_NORMAL = 0,
    WGF_UI_STATE_HOVERED = 1, /* the pointer over it */
    WGF_UI_STATE_PRESSED = 2, /* held down, or a toggle on */
    WGF_UI_STATE_DISABLED = 3
} wgf_ui_state_t;

typedef enum wgf_ui_fill_t {
    WGF_UI_FILL_STRETCHED = 0,
    WGF_UI_FILL_CLIPPED = 1
} wgf_ui_fill_t;

typedef enum wgf_ui_from_t {
    WGF_UI_FROM_LEFT = 0,
    WGF_UI_FROM_RIGHT = 1,
    WGF_UI_FROM_TOP = 2,
    WGF_UI_FROM_BOTTOM = 3
} wgf_ui_from_t;

/* How a skin's edges and middle fill what is between its corners: stretched; or tiled, its
 * art repeated at its own size, an edge along it and the middle both ways, the last one cut
 * where the room ends (a rope border, a brick panel: art that can't stretch); or tiled to
 * fit, repeated a whole number of times, each stretched the little it takes to end on a
 * whole one. A part drawn smaller than its borders is stretched whichever. */
typedef enum wgf_ui_patch_t {
    WGF_UI_PATCH_STRETCHED = 0,
    WGF_UI_PATCH_TILED = 1,
    WGF_UI_PATCH_TILED_FIT = 2
} wgf_ui_patch_t;

/* A skin set (texture 0: the part drawn in its colors again), its region, borders, and
 * tint; and read back: its texture (0 for none), region (x, y, z width, w height), borders
 * (x left, y top, z right, w bottom), and tint (white for none). */
WGF_API bool wgf_ui_set_skin(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state, wgf_texture_t texture,
                             float source_x, float source_y, float source_width, float source_height);
WGF_API wgf_texture_t wgf_ui_get_skin(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state);
WGF_API wgf_vec4_t wgf_ui_get_skin_source(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state);
WGF_API bool wgf_ui_set_skin_borders(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state, float left,
                                     float top, float right, float bottom);
WGF_API wgf_vec4_t wgf_ui_get_skin_borders(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state);
WGF_API bool wgf_ui_set_skin_tint(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state, wgf_color_t tint);
WGF_API wgf_color_t wgf_ui_get_skin_tint(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state);
/* Its patches' fill (stretched until set); false too for a fill that isn't one. */
WGF_API bool wgf_ui_set_skin_patch(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state,
                                   wgf_ui_patch_t patch);
WGF_API wgf_ui_patch_t wgf_ui_get_skin_patch(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state);

/* A widget's text offset in a state, in logical pixels (0, 0 until set). */
WGF_API bool wgf_ui_set_text_offset(wgf_ui_widget_t widget, wgf_ui_state_t state, float x, float y);
WGF_API wgf_vec2_t wgf_ui_get_text_offset(wgf_ui_widget_t widget, wgf_ui_state_t state);

/* How a progress bar's or a slider's fill is drawn, and which end it fills from
 * (stretched from the left until set). False for a widget other than those two. */
WGF_API bool wgf_ui_set_fill(wgf_ui_widget_t widget, wgf_ui_fill_t fill, wgf_ui_from_t from);
WGF_API wgf_ui_fill_t wgf_ui_get_fill(wgf_ui_widget_t widget);
WGF_API wgf_ui_from_t wgf_ui_get_fill_from(wgf_ui_widget_t widget);

/* Before skins, a part from the game's art by its color: `texture` whole, nine-sliced by
 * its borders, untinted; 0 draws the color again. The parts are WGF_UI_COLOR_PANEL (the
 * panel's base), _BUTTON, _BUTTON_HOVERED, _BUTTON_PRESSED (the button's base in those
 * states), _BAR, and _BAR_FILL (the progress bar's base and fill). False for a part that
 * isn't one of these, a handle that isn't a texture, and a border below 0 or not finite. */
WGF_API bool wgf_ui_set_style_texture(wgf_ui_color_t which, wgf_texture_t texture, float left, float top, float right,
                                      float bottom);
WGF_API wgf_texture_t wgf_ui_get_style_texture(wgf_ui_color_t which);

/* Documents (wgf_document.h), the ui's calls for all of them. */

/* The font RCSS's `font-family: <family>` means in every document (its name matched
 * without case); 0 unsets it, so the family draws in the default font. It holds a
 * reference to `font`. False for no family, or a handle that isn't a font. */
WGF_API bool wgf_ui_set_document_font(const char *family, wgf_font_t font);
WGF_API wgf_font_t wgf_ui_get_document_font(const char *family);

/* A face of a family's own: the font `font-family: <family>` draws in at `font-weight:
 * <weight>` (1 to 1000: 400 normal, 700 bold) and, `italic`, `font-style: italic`. Text
 * draws in the family's face nearest the one it asks for: of its style when the family
 * has one of it (else of the other), the nearest weight, a tie to the heavier above 400
 * and to the lighter at or below; the family's font (wgf_ui_set_document_font) is its
 * face at 400, upright. A face no font is set for is never made up (no bold drawn from a
 * regular). 0 unsets it. It holds a reference to `font`. False for no family, a weight
 * outside 1 to 1000, or a handle that isn't a font. */
WGF_API bool wgf_ui_set_document_font_face(const char *family, int weight, bool italic, wgf_font_t font);
WGF_API wgf_font_t wgf_ui_get_document_font_face(const char *family, int weight, bool italic);

/* The shown documents, worked by this frame's input, laid out, and drawn here, in the frame
 * callback, over what was drawn before; their events are raised as they are worked. Once a
 * frame: a second call in a frame draws them again without the input. False outside a
 * frame (logged), or before any document was created. */
WGF_API bool wgf_ui_draw_documents(void);

#ifdef __cplusplus
}
#endif

#endif
