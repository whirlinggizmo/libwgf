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

/* Game UI, in immediate mode: each frame your game describes the UI as it is now.
 *
 * You describe boxes, panels, labels, buttons, and progress bars between wgf_ui_begin and
 * wgf_ui_end, inside the frame callback. libwgf lays them out, answers what the
 * pointer, the keys, and the pads did to them, and draws them at wgf_ui_end, over what the
 * frame drew before.
 *
 * The screen is the root: a column of every widget given, centered both ways. A box lays its
 * children out in a column or a row, sized to fit them unless set otherwise. A button's answer
 * comes from where the widgets were in the frame before, as in every immediate-mode UI. A UI
 * that moves under the pointer is answered where it was.
 *
 * Focus: one widget at a time has it, by its id. A widget that can take it is a button, a
 * toggle, a checkbox, a slider, a number, tabs, a choice, a combo, a tile, a list, a dropdown,
 * or a text field.
 * - The arrow keys and a pad's D-pad move it to the nearest widget that way on the screen, as
 *   last laid out. A grid of tiles moves in two dimensions.
 * - With no widget that way, they move it through the frame's widgets in the order they were
 *   given: Down and Right to the next, Up and Left to the one before, wrapping.
 * - Tab and Shift+Tab always go in that order.
 * - The first press of one puts the focus on the first or the last widget.
 * - A focused widget keeps the arrows it uses. A slider, a number, tabs, a choice, a combo,
 *   and a text field keep left and right. A slider that fills up or down, and a list, keep up
 *   and down. An open dropdown keeps all four.
 * - Enter, Space, or a pad's south button activates the focused widget.
 * - The pointer activates the widget it is pressed and released over, and gives it the focus.
 * - A focus moved by keys or pads is drawn. One given by the pointer isn't.
 * - A focused widget that isn't given in a frame loses the focus.
 * - A disabled widget (wgf_ui_set_disabled) is drawn disabled, answers nothing, and is never
 *   focused.
 *
 * The UI says when it has the pointer and the keyboard:
 * - The pointer (wgf_input_set_pointer_captured), while it is over a panel, a button, or a box
 *   with a color, or while a press on a button is held.
 * - The keyboard (wgf_input_set_keyboard_captured), while a button has the focus.
 * - A frame without wgf_ui_end lets both go.
 *
 * Ids name boxes, panels, groups, and widgets, and are unique in a frame. A second
 * one is refused (logged). A box, panel, or group may have no id (NULL or ""). Text is copied, so
 * it may change or go after the call.
 *
 * Outside a begun UI, every call but the style's and the skins' returns false, and a widget's
 * call returns its value as given.
 *
 * A widget with a value takes it and returns it as the user left it. Your game keeps the
 * value, and the call shows it and answers what was done to it this frame:
 * `volume = wgf_ui_slider("volume", volume, 0, 10)`. */

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
    WGF_UI_SIZING_PERCENT = 3 /* the value, 0 to 1, of its parent's room, less its padding and gaps */
} wgf_ui_sizing_t;

/* Begins the frame's UI, with the screen at its logical size as the root. Returns false when
 * a UI is already begun, or outside a frame. */
WGF_API bool wgf_ui_begin(void);

/* Lays out, answers, and draws the frame's UI. Boxes left open are closed, and the call then
 * returns false (logged). Also returns false with no UI begun. */
WGF_API bool wgf_ui_end(void);

/* Begins a grid, closed by wgf_ui_end_grid.
 *
 * Its children are laid out in rows of `columns`, left to right, then top to bottom. They are
 * gapped by the style's gap both ways. Nothing else wraps a row: the grid makes them. Also
 * returns false for fewer than 1 column.
 *
 * - The columns line up. Each is as wide as its widest child, and each row as tall as its
 *   tallest. A form of labels and widgets is a grid of 2 columns.
 * - wgf_ui_set_align on the grid places each child in its cell. By default it is at the top left.
 * - A column's width comes from the last frame's layout, so a grid's first frame, or the frame
 *   after a child changes size, is laid out with each child at its own width.
 * - A child sized to grow doesn't grow in a grid: it takes its smallest size.
 * - Columns past the 8th, and grids past the 16th in a frame, don't line up. */
WGF_API bool wgf_ui_begin_grid(const char *id, int columns);
WGF_API bool wgf_ui_end_grid(void);

/* Begins a box, a panel, or a group, each closed by its own end call.
 *
 * - A box lays its children out in `direction`. Close it with wgf_ui_end_box.
 * - A panel is a column drawn with the style's panel color, corners, and padding, and gapped
 *   by the style's gap. Close it with wgf_ui_end_panel.
 * - A group is a column drawn as an outline, in the style's separator color and line width.
 *   Its `title` (NULL or "" for none) is its first line. It is padded and gapped as a panel.
 *   Close it with wgf_ui_end_group.
 *
 * An end call returns false with nothing of its kind open. */
WGF_API bool wgf_ui_begin_box(const char *id, wgf_ui_direction_t direction);
WGF_API bool wgf_ui_end_box(void);
WGF_API bool wgf_ui_begin_panel(const char *id);
WGF_API bool wgf_ui_end_panel(void);
WGF_API bool wgf_ui_begin_group(const char *id, const char *title);
WGF_API bool wgf_ui_end_group(void);

/* Begins a scroll area, closed by wgf_ui_end_scroll.
 *
 * A scroll area is a window onto its children, `width` by `height` logical pixels. 0 takes the
 * room its parent has left that way. Its children are a column, clipped to the window, and
 * scrolled when they are longer or wider than it.
 *
 * It scrolls by:
 * - The wheel over it. The innermost area under the pointer scrolls. A wheel across, or the
 *   wheel with Shift, scrolls sideways.
 * - A drag. A press that moves a few pixels belongs to the area, and the widget it began on
 *   isn't pressed, unless that widget is a slider. Let go while moving, it flings.
 * - Its scroll bars. Drag the thumb, or press the track for a page. Each bar is shown while
 *   there is more to scroll to that way. With the style's WGF_UI_VALUE_SCROLL_BAR_ALWAYS it
 *   is always shown, and with no more to scroll it is disabled, its thumb the whole track.
 *
 * Focus:
 * - Focus moved to a widget in it, by keys, a pad, or wgf_ui_set_focus, scrolls the widget
 *   into view.
 * - Page Up and Page Down page while the focus is in it.
 * - An area with no widget in it to focus (a page of text), and more than it shows, is focused
 *   itself, by its id. That starts from the frame after it was first laid out so. Focused, its
 *   arrows scroll it a row at a time, and the page keys a page.
 *
 * The layout calls (padding, gap, alignment, color) apply to its content. Its size is the
 * call's, so wgf_ui_set_width and wgf_ui_set_height refuse it. Its scroll is kept while it is
 * given each frame. What it scrolls to shows from the next frame.
 *
 * Also returns false for no id, or a size below 0. */
WGF_API bool wgf_ui_begin_scroll(const char *id, float width, float height);
WGF_API bool wgf_ui_end_scroll(void);

/* Sets or gets how far the scroll area or list `id` is scrolled, in logical pixels.
 *
 * x is across and y is down, from its content's top left. get_scroll returns 0, 0 for one not
 * laid out. set_scroll scrolls it there, shown from its next layout, and held to its content
 * as last laid out. Use it to keep a chat log at its end.
 *
 * The setter returns false for an id that isn't a scroll area or list laid out in the frame
 * before, or a value that isn't finite. These two work outside a begun UI too. */
WGF_API bool wgf_ui_set_scroll(const char *id, float x, float y);
WGF_API wgf_vec2_t wgf_ui_get_scroll(const char *id);

/* Sets the open box's or panel's layout, before its first child.
 *
 * Your game describes the open box each frame, so nothing keeps it to read back. A color draws
 * its background, and 0 (the default) draws none.
 *
 * Returns false after its first child, with none open, for a sizing or alignment that isn't
 * one, or a value below 0 (or above 1 for a percent). */
WGF_API bool wgf_ui_set_width(wgf_ui_sizing_t sizing, float value);
WGF_API bool wgf_ui_set_height(wgf_ui_sizing_t sizing, float value);
WGF_API bool wgf_ui_set_padding(float x, float y);
WGF_API bool wgf_ui_set_gap(float gap);
WGF_API bool wgf_ui_set_align(wgf_ui_align_t x, wgf_ui_align_t y);
WGF_API bool wgf_ui_set_color(wgf_color_t color);

/* Shows text, in the style's font and text color, at `size` logical pixels. 0 or less takes
 * the style's text size. Each newline starts a line. Also returns false for no text. */
WGF_API bool wgf_ui_label(const char *text, float size);

/* Adds room along the open box's direction: `size` logical pixels, or 0 for the room left. */
WGF_API bool wgf_ui_spacer(float size);

/* Draws a line across the open box, in the style's separator color and line width. It runs
 * along a column's width, or down a row's height. */
WGF_API bool wgf_ui_separator(void);

/* Shows a texture placed by the layout, `width` by `height` logical pixels.
 *
 * 0 or less takes the texture's own size, as it is when called: 0 by 0 until it is READY. The
 * texture is stretched to fill the size, and untinted (dimmed when disabled). Also returns
 * false for a handle that isn't a texture. */
WGF_API bool wgf_ui_image(wgf_texture_t texture, float width, float height);

/* Adds room `width` by `height` logical pixels that your game draws in itself (wgf_draw.h).
 * Draw where wgf_ui_get_bounds says it was laid out. Use it for what no widget draws. Also
 * returns false for no id, or a size below 0. */
WGF_API bool wgf_ui_custom(const char *id, float width, float height);

/* Returns where the box, panel, group, widget, or custom room `id` was laid out, in the
 * screen's logical pixels.
 *
 * x and y are its top left, and z and w its width and height. After wgf_ui_end it is this
 * frame's layout, and until then the frame before's. Returns all 0 for an id not laid out. */
WGF_API wgf_vec4_t wgf_ui_get_bounds(const char *id);

/* Shows a button with `text`, and returns true in the frame it is activated. Also returns
 * false for no id.
 *
 * An icon button shows `icon` (a texture, or 0 for none) before its text. The icon is as tall
 * as the text, and as wide as its shape keeps. With `text` NULL or "", it shows the icon
 * alone. */
WGF_API bool wgf_ui_button(const char *id, const char *text);
WGF_API bool wgf_ui_icon_button(const char *id, wgf_texture_t icon, const char *text);

/* Shows a button that is its art, and returns true in the frame it is activated.
 *
 * - `normal` is drawn at its size.
 * - `pressed` is drawn while it is held (0 draws `normal`).
 * - `mask` is a CPU image (0 for none, so the whole button). The pointer presses the button
 *   only where the mask isn't clear, so a round button's corners belong to the screen. The
 *   mask is read as stretched over the button.
 *
 * Also returns false for no id, or a handle that isn't a texture or an image. */
WGF_API bool wgf_ui_image_button(const char *id, wgf_texture_t normal, wgf_texture_t pressed, wgf_image_t mask);

/* Shows a toggle or a checkbox, and returns whether it is on.
 *
 * - A toggle is a button that stays down while on.
 * - A checkbox is a box, checked while on, with `text` beside it (NULL or "" for none).
 *
 * Each is turned by an activation. It returns `on` as given, or flipped in the frame it is
 * activated. It returns `on` as given when refused (no id). */
WGF_API bool wgf_ui_toggle(const char *id, const char *text, bool on);
WGF_API bool wgf_ui_checkbox(const char *id, const char *text, bool on);

/* Shows a slider for `value` between `min` and `max`, and returns the value.
 *
 * - It shows the value by its handle along its track, and the fill behind it.
 * - It fills in the slider's fill direction (see wgf_ui_set_fill), from the left by default.
 *   Filling from the top or bottom, it stands up.
 * - Its size is the style's slider width and height, and handle size.
 * - A press on it, or a drag from one, sets it where the pointer is.
 * - When it is focused, its arrows (and a pad's D-pad's) move it a twentieth of its range.
 *   Held ones repeat.
 *
 * Returns the value as given (clamped to the range), or as moved. Returns the value as given
 * when refused: no id, `max` not above `min`, or either not finite. */
WGF_API float wgf_ui_slider(const char *id, float value, float min, float max);

/* Shows a number for `value` between `min` and `max`, and returns the value.
 *
 * It shows the value between a minus and a plus, which step it by `step`. When it is focused,
 * its arrows (and a pad's D-pad's) step it too. Held ones repeat. It is shown with as many
 * decimals as `step` has, up to 4.
 *
 * Returns the value as given (clamped to the range), or as stepped. Returns the value as given
 * when refused: no id, `max` not above `min`, `step` not above 0, or any not finite. */
WGF_API float wgf_ui_number(const char *id, float value, float min, float max, float step);

/* Shows tabs, a choice, or a combo among `labels`, and returns the one chosen.
 *
 * `labels` holds one label a line, up to 32 ("Easy\nNormal\nHard").
 * - Tabs are a row of the labels, with the chosen one pressed. Your game draws its panel
 *   below.
 * - A choice is the same, drawn as toggles.
 * - A combo shows the chosen label alone, and steps to the next when activated. Its arrows
 *   step either way, wrapping.
 *
 * A press chooses the tab or toggle it is on. When focused, the arrows step.
 *
 * Returns `selected` as given (clamped to the labels), or as the user chose this frame. Returns
 * `selected` as given when refused: no id, or no labels. */
WGF_API int wgf_ui_tabs(const char *id, const char *labels, int selected);
WGF_API int wgf_ui_choice(const char *id, const char *labels, int selected);
WGF_API int wgf_ui_combo(const char *id, const char *labels, int selected);

/* Shows a tile, and returns true in the frame it is activated.
 *
 * A tile is `picture` (a texture, or 0 for none) above `text` (NULL or "" for none). It is the
 * style's tile size wide, with its picture square in it. Use it for a level select's level, or
 * an inventory's item, in a grid.
 *
 * Also returns false for no id, or a handle that isn't a texture. */
WGF_API bool wgf_ui_tile(const char *id, wgf_texture_t picture, const char *text);

/* Sets or gets whether the widgets given after it, until it is changed, are disabled.
 *
 * A disabled widget is drawn in its disabled state, answers nothing, and is never focused.
 * Each UI begins with no widget disabled. Returns false with no UI begun. */
WGF_API bool wgf_ui_set_disabled(bool disabled);
WGF_API bool wgf_ui_is_disabled(void);

/* Returns whether the widget given just before it was activated this frame.
 *
 * - For a button, an icon or image button, a toggle, a checkbox, or a tile: as its own call
 *   answered.
 * - For a list: whether its chosen item was activated (see wgf_ui_list).
 *
 * Returns false for any other widget, with no widget before it in the frame, and outside a
 * begun UI. */
WGF_API bool wgf_ui_is_activated(void);

/* Shows a list of `items`, and returns the one chosen.
 *
 * - `items` holds one item a line, any number ("" or NULL for none).
 * - The list is `rows` items tall, and as wide as its parent's room (at least the style's
 *   button width).
 * - `selected` is the chosen item (-1 for none).
 *
 * Choosing:
 * - A press and release on an item chooses it. A drag scrolls, choosing none.
 * - When focused, Up and Down (a pad's too) step, and held ones repeat. Page Up and Page Down
 *   step a page. The chosen item is scrolled into view.
 *
 * It scrolls as a scroll area does, down only. Only the items in view are laid out, so a long
 * list costs only its rows.
 *
 * Its chosen item is activated (ask wgf_ui_is_activated after the call) by:
 * - Enter, Space, or a pad's south button, while the list is focused.
 * - A press and release on the item already chosen. On an item not yet chosen, that takes a
 *   double press.
 *
 * Returns `selected` as given (clamped to the items), or as the user chose this frame. Returns
 * `selected` as given when refused: no id, or `rows` below 1. */
WGF_API int wgf_ui_list(const char *id, const char *items, int selected, int rows);

/* Shows a text field: a line of text typed in. Returns true in the frame Enter is pressed in
 * it.
 *
 * It is as wide as its parent's room, at least the style's button width. The UI keeps its
 * text by its id (see wgf_ui_get_text), from the first call or wgf_ui_set_text on, until the
 * UI stops.
 *
 * Focused, it takes what is typed (wgf_input_get_chars) in place of the selection, and the
 * keys:
 *
 *   Left, Right         move the caret a character; with Ctrl (Alt on macOS) a word
 *   Home, End           go to the start, the end (Cmd+Left and +Right on macOS too)
 *   Shift with a move   extends the selection
 *   Backspace, Delete   delete the selection, else the character before or after (a word with
 *                       Ctrl or Alt), held ones repeating
 *   Ctrl+A (Cmd on macOS)        selects all
 *   Ctrl+C, +X, +V               copy, cut, and paste, through the system's clipboard
 *                                (wgf_input_set_clipboard); newlines pasted are stripped
 *   Ctrl+Z; Ctrl+Y, Ctrl+Shift+Z undo and redo, a run of typing or of deleting one step
 *
 * The pointer and touch:
 * - A press places the caret. With Shift, it extends the selection to it.
 * - A drag selects.
 * - A double press selects the word under it, and a triple press the whole text.
 * - By touch, the same taps work.
 *
 * Its caret blinks, and the text scrolls to keep it in view. Focus moved to it by keys or a
 * pad selects the whole text. A press off it lets the focus go.
 *
 * While it is focused, the platform's on-screen keyboard is shown. That is a phone's, and on
 * the web, the page's hidden input that a phone's keyboard types into.
 *
 * - `placeholder` (NULL or "" for none) is shown, dimmed, while the field is empty.
 * - It holds at most `max_length` characters (0 or less: 4095 bytes' worth). A longer text is
 *   cut to it.
 * - A password field is the same, drawn as dots. Its words are the whole of it, and it is
 *   never copied or cut.
 *
 * Also returns false when refused (no id). */
WGF_API bool wgf_ui_text_field(const char *id, const char *placeholder, int max_length);
WGF_API bool wgf_ui_password_field(const char *id, const char *placeholder, int max_length);

/* Sets or gets the text field `id`'s text.
 *
 * - set_text puts its caret at the end and forgets its undo. The text is cut to 4095 bytes.
 *   A field not given yet keeps the text for its first call.
 * - get_text returns "" for a field never given or set. The string is valid until the field
 *   changes.
 *
 * The setter returns false for no id, an id of 64 bytes or more, or no text. These two work
 * outside a begun UI too. */
WGF_API bool wgf_ui_set_text(const char *id, const char *text);
WGF_API const char *wgf_ui_get_text(const char *id);

/* Sets or gets whether the text fields given after it, until it is changed, show their text
 * as refused.
 *
 * Whatever reads the text decides, as for a name already taken. An invalid field's text is
 * drawn in the style's invalid color, and outlined in it. Each UI begins with no field invalid.
 * Returns false with no UI begun. */
WGF_API bool wgf_ui_set_invalid(bool invalid);
WGF_API bool wgf_ui_is_invalid(void);

/* The popup layer: widgets drawn over every other widget, clipped by none, and placed by the
 * layer.
 *
 * A dropdown shows the chosen one of `labels`, and returns the one chosen. `labels` holds one
 * label a line, up to 32.
 * - When activated, it opens a list of the labels over everything. The list hangs below it, or
 *   above when the screen has no room below and more above. It is moved in to stay on the
 *   screen.
 * - A press and release on a label chooses it.
 * - When focused, Up and Down (a pad's too) step through the labels. Enter, Space, or a pad's
 *   south button chooses the one stepped to.
 * - It keeps every arrow while open.
 * - A press anywhere off it closes it, and does nothing else.
 * - Escape and a pad's east button close it. So does the dropdown losing the focus, being
 *   disabled, or not being given in a frame.
 * - One dropdown is open at a time.
 *
 * Returns `selected` as given (clamped to the labels), or as chosen this frame. Returns
 * `selected` as given when refused: no id, or no labels. */
WGF_API int wgf_ui_dropdown(const char *id, const char *labels, int selected);

/* Begins a modal, closed by wgf_ui_end_modal: a panel at the screen's center, such as
 * "Quit? Yes / No".
 *
 * - Its layout, its skin, and its children are a panel's.
 * - It is drawn over the screen, dimmed in the style's dim color. Nothing under it answers the
 *   pointer.
 * - While it is given, the focus moves among its own widgets alone. The focus is put on its
 *   first widget when it opens.
 * - When it closes (is no longer given), the focus goes back to the widget that had it.
 * - A dropdown in it opens over it.
 * - There is one modal a frame.
 *
 * Also returns false for no id, or a modal already open.
 *
 * A modal is open while the game gives it, so the game closes it. wgf_ui_is_modal_cancelled
 * returns whether the player asked to: Escape or a pad's east button was pressed this frame,
 * with a modal begun and no dropdown open (the press closes the dropdown instead). Ask it
 * after wgf_ui_begin_modal, and stop giving the modal when it is true. */
WGF_API bool wgf_ui_begin_modal(const char *id);
WGF_API bool wgf_ui_end_modal(void);
WGF_API bool wgf_ui_is_modal_cancelled(void);

/* Shows `text` as a tooltip for the widget given just before it, a disabled one too.
 *
 * - It shows once the pointer has rested on the widget half a second, or once the focus is
 *   moved to it by keys or a pad.
 * - It is drawn in a box in the style's popup color, below the widget, or above when the screen
 *   has no room below and more above. It is moved in to stay on the screen.
 * - The pointer passes through it, and a press hides it.
 *
 * Also returns false for no text, or no widget given before it in the frame. */
WGF_API bool wgf_ui_tooltip(const char *text);

/* Sets or gets the whole UI's alpha (default: 1), kept until it is changed.
 *
 * 0 is unseen, and 1 is as drawn. Use it to fade a menu in or out. The UI answers as ever: a
 * UI faded to nothing is still there until it isn't given.
 *
 * The setter returns false for a value outside 0 to 1, or not finite. These two work outside a
 * begun UI too. */
WGF_API bool wgf_ui_set_alpha(float alpha);
WGF_API float wgf_ui_get_alpha(void);

/* Shows a progress bar for `value`, from 0 (empty) to 1 (full), such as how far a loading
 * screen's files have come (wgf_asset_task_get_progress).
 *
 * - It fills from the left. The progress bar's fill direction is set with wgf_ui_set_fill.
 *   Filling from the top or bottom, its bar width and height swap.
 * - It uses the style's bar width and height, its track and fill colors, and its corners.
 * - It only shows a value, so the pointer over it isn't the UI's.
 * - A value below 0 shows 0, above 1 shows 1, and NaN shows 0. */
WGF_API bool wgf_ui_progress(float value);

/* Sets or gets the focused widget.
 *
 * - set_focus gives the focus to the widget `id` (NULL or "" for none). It is drawn as a key
 *   or pad would draw it. It returns false for an id of 64 bytes or more.
 * - get_focus returns the focused widget's id, or "" for none. The string is valid until the
 *   focus changes.
 *
 * These two work outside a begun UI too. Each frame a UI ends, the focus is published as the
 * text probe "ui.focus" (wgf_probe.h), so an autopilot can expect which widget has it. */
WGF_API bool wgf_ui_set_focus(const char *id);
WGF_API const char *wgf_ui_get_focus(void);

/* The style: your game's own colors, sizes in logical pixels, and font (0 for the default
 * font), kept until changed. wgf_ui_reset_style restores libwgf's. */
typedef enum wgf_ui_color_t {
    WGF_UI_COLOR_TEXT = 0,
    WGF_UI_COLOR_PANEL = 1,
    WGF_UI_COLOR_BUTTON = 2,
    WGF_UI_COLOR_BUTTON_HOVERED = 3,
    WGF_UI_COLOR_BUTTON_PRESSED = 4,
    WGF_UI_COLOR_BUTTON_TEXT = 5,
    WGF_UI_COLOR_FOCUS = 6,     /* the line around a focused widget */
    WGF_UI_COLOR_BAR = 7,       /* a progress bar's and a slider's track, and a checkbox's box */
    WGF_UI_COLOR_BAR_FILL = 8,  /* their fill, a checkbox's check, a toggle that is on, the chosen tab or choice */
    WGF_UI_COLOR_SEPARATOR = 9, /* a separator, and a group's outline */
    WGF_UI_COLOR_HANDLE = 10,   /* a slider's handle */
    WGF_UI_COLOR_POPUP = 11,    /* a dropdown's open list, and a tooltip's box */
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
    WGF_UI_VALUE_BAR_WIDTH = 7,      /* a progress bar's width */
    WGF_UI_VALUE_BAR_HEIGHT = 8,
    WGF_UI_VALUE_CHECK_SIZE = 9,     /* a checkbox's box */
    WGF_UI_VALUE_SLIDER_WIDTH = 10,  /* a slider's length */
    WGF_UI_VALUE_SLIDER_HEIGHT = 11, /* its track's thickness */
    WGF_UI_VALUE_HANDLE_SIZE = 12,   /* its handle, across and along its track (as tall as the slider if skinned) */
    WGF_UI_VALUE_LINE_WIDTH = 13,    /* a separator's and a group's outline */
    WGF_UI_VALUE_TILE_SIZE = 14,     /* a tile's width, and its picture's side less its padding */
    WGF_UI_VALUE_SCROLL_BAR = 15,    /* a scroll bar's thickness */
    WGF_UI_VALUE_SCROLL_BAR_ALWAYS = 16 /* above 0: scroll bars show with nothing to scroll (0: only with more) */
} wgf_ui_value_t;

/* Sets or gets a style color or value. The setters return false for a color or value that
 * isn't one, or a value below 0. The getters return 0 for one that isn't one. */
WGF_API bool wgf_ui_set_style_color(wgf_ui_color_t which, wgf_color_t color);
WGF_API wgf_color_t wgf_ui_get_style_color(wgf_ui_color_t which);
WGF_API bool wgf_ui_set_style_value(wgf_ui_value_t which, float value);
WGF_API float wgf_ui_get_style_value(wgf_ui_value_t which);
/* Sets or gets the style's font. Returns false for a handle that isn't a font (0 is allowed). */
WGF_API bool wgf_ui_set_style_font(wgf_font_t font);
WGF_API wgf_font_t wgf_ui_get_style_font(void);
WGF_API void wgf_ui_reset_style(void);

/* Skins: a widget's parts drawn from your game's art, in place of the style's colors.
 *
 * A skin is a widget's part in a state:
 * - A texture's region. A source width or height of 0 or less takes the whole texture.
 * - Nine-sliced by its borders (wgf_draw_texture_nine_slice). Borders of 0 top and bottom
 *   make a bar with caps at its ends. Borders of 0 left and right make one up and down.
 * - Multiplied by its tint (white until set).
 *
 * A state with no skin of its own draws the normal state's skin, dimmed if disabled. A part
 * drawn from art has its art's corners, not the corner radius.
 *
 * The parts each widget draws:
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
 * A choice's toggles are TOGGLE's, each PRESSED while chosen. A modal's panel is PANEL's.
 *
 * A fill is drawn one of two ways, from the left, right, top, or bottom (see wgf_ui_set_fill):
 * - Stretched over the filled part.
 * - Clipped: drawn over the whole track, and shown up to the value. A bar with round ends then
 *   shows its far end only when full, like Godot's TextureProgressBar.
 *
 * An edge is drawn at its art's size, centered on the fill's front. A widget's text (a
 * button's, a toggle's) moves by its state's text offset, so a face that sinks when pressed
 * takes its text with it.
 *
 * The style holds a reference to each texture, and wgf_ui_reset_style lets them go.
 *
 * The skin calls return false for a widget, part, or state that isn't one, a handle that isn't
 * a texture, or a value that isn't finite (or a border below 0). */
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

/* How a skin's edges and middle fill the room between its corners.
 *
 * - STRETCHED: stretched.
 * - TILED: its art repeated at its own size, an edge along its length and the middle both ways.
 *   The last repeat is cut where the room ends. Use it for art that can't stretch, such as a
 *   rope border or a brick panel.
 * - TILED_FIT: repeated a whole number of times, each repeat stretched the little it takes to
 *   end on a whole one.
 *
 * A part drawn smaller than its borders is stretched, whichever is set. */
typedef enum wgf_ui_patch_t {
    WGF_UI_PATCH_STRETCHED = 0,
    WGF_UI_PATCH_TILED = 1,
    WGF_UI_PATCH_TILED_FIT = 2
} wgf_ui_patch_t;

/* Sets or gets a skin: its texture, region, borders, and tint.
 *
 * - set_skin with texture 0 draws the part in its colors again.
 * - get_skin returns its texture, or 0 for none.
 * - get_skin_source returns its region: x, y, z the width, and w the height.
 * - get_skin_borders returns its borders: x left, y top, z right, and w bottom.
 * - get_skin_tint returns its tint, or white for none. */
WGF_API bool wgf_ui_set_skin(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state, wgf_texture_t texture,
                             float source_x, float source_y, float source_width, float source_height);
WGF_API wgf_texture_t wgf_ui_get_skin(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state);
WGF_API wgf_vec4_t wgf_ui_get_skin_source(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state);
WGF_API bool wgf_ui_set_skin_borders(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state, float left,
                                     float top, float right, float bottom);
WGF_API wgf_vec4_t wgf_ui_get_skin_borders(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state);
WGF_API bool wgf_ui_set_skin_tint(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state, wgf_color_t tint);
WGF_API wgf_color_t wgf_ui_get_skin_tint(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state);
/* Sets or gets how a skin's patches fill (default: STRETCHED). The setter also returns false
 * for a fill that isn't one. */
WGF_API bool wgf_ui_set_skin_patch(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state,
                                   wgf_ui_patch_t patch);
WGF_API wgf_ui_patch_t wgf_ui_get_skin_patch(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state);

/* Sets or gets a widget's text offset in a state, in logical pixels (default: 0, 0). */
WGF_API bool wgf_ui_set_text_offset(wgf_ui_widget_t widget, wgf_ui_state_t state, float x, float y);
WGF_API wgf_vec2_t wgf_ui_get_text_offset(wgf_ui_widget_t widget, wgf_ui_state_t state);

/* Sets or gets how a progress bar's or a slider's fill is drawn, and which end it fills from
 * (default: stretched, from the left). Returns false for a widget other than those two. */
WGF_API bool wgf_ui_set_fill(wgf_ui_widget_t widget, wgf_ui_fill_t fill, wgf_ui_from_t from);
WGF_API wgf_ui_fill_t wgf_ui_get_fill(wgf_ui_widget_t widget);
WGF_API wgf_ui_from_t wgf_ui_get_fill_from(wgf_ui_widget_t widget);

/* Sets or gets a part drawn from your game's art, by its color. It is a short form of
 * wgf_ui_set_skin for a part's base.
 *
 * The part draws `texture` whole, nine-sliced by its borders, and untinted. 0 draws the color
 * again. The parts are:
 * - WGF_UI_COLOR_PANEL: the panel's base.
 * - WGF_UI_COLOR_BUTTON, _BUTTON_HOVERED, and _BUTTON_PRESSED: the button's base in those
 *   states.
 * - WGF_UI_COLOR_BAR and _BAR_FILL: the progress bar's base and fill.
 *
 * Returns false for a part that isn't one of these, a handle that isn't a texture, or a border
 * below 0 or not finite. */
WGF_API bool wgf_ui_set_style_texture(wgf_ui_color_t which, wgf_texture_t texture, float left, float top, float right,
                                      float bottom);
WGF_API wgf_texture_t wgf_ui_get_style_texture(wgf_ui_color_t which);

/* Documents (wgf_document.h): the ui's calls for all of them. */

/* Sets or gets the font that RCSS's `font-family: <family>` means in every document.
 *
 * The family's name is matched without case. 0 unsets it, so the family draws in the default
 * font. The UI holds a reference to `font`. Returns false for no family, or a handle that
 * isn't a font. */
WGF_API bool wgf_ui_set_document_font(const char *family, wgf_font_t font);
WGF_API wgf_font_t wgf_ui_get_document_font(const char *family);

/* Sets or gets one of a family's own faces.
 *
 * A face is the font that `font-family: <family>` draws in at `font-weight: <weight>`, and, if
 * `italic`, at `font-style: italic`. `weight` is 1 to 1000: 400 is normal, and 700 bold.
 *
 * Text draws in the family's face nearest the one it asks for:
 * - A face of its style, when the family has one of that style. Otherwise, one of the other
 *   style.
 * - The nearest weight. A tie goes to the heavier above 400, and to the lighter at or below.
 * - The family's font (wgf_ui_set_document_font) is its face at 400, upright.
 *
 * A face no font is set for is never made up: no bold is drawn from a regular. 0 unsets a
 * face. The UI holds a reference to `font`.
 *
 * Returns false for no family, a weight outside 1 to 1000, or a handle that isn't a font. */
WGF_API bool wgf_ui_set_document_font_face(const char *family, int weight, bool italic, wgf_font_t font);
WGF_API wgf_font_t wgf_ui_get_document_font_face(const char *family, int weight, bool italic);

/* Works, lays out, and draws the shown documents, here in the frame callback.
 *
 * The documents are worked by this frame's input, and drawn over what was drawn before. Their
 * events are raised as they are worked. Call it once a frame: a second call in a frame draws
 * them again, without the input.
 *
 * Returns false outside a frame (logged), or before any document was created. */
WGF_API bool wgf_ui_draw_documents(void);

#ifdef __cplusplus
}
#endif

#endif
