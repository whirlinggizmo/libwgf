#ifndef WGF_DOCUMENT_H
#define WGF_DOCUMENT_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_font.h"
#include "wgf_handle.h"
#include "wgf_resource.h"
#include "wgf_ui.h" /* the ui's calls for every document: their fonts, and their drawing */

#ifdef __cplusplus
extern "C" {
#endif

/* Documents: menus written as files, in RML (HTML-like markup) styled with RCSS (CSS-like
 * style sheets), as RmlUi reads them.
 *
 * libwgf lays documents out, draws them, and works them by the pointer, the keys, and the
 * pads. They sit beside the immediate UI of wgf_ui.h, which stays for HUDs and small screens.
 *
 * A document is a resource (wgf_resource.h), created from its path.
 * - It loads with every file it names: its style sheets, images, and templates. They are made
 *   local first, so it loads the same from a web page as from a disk.
 * - It is READY once it is parsed.
 * - It is FAILED (logged) when it can't be read or isn't a document: it has no <rml>
 *   element, or RmlUi's parser finds it malformed (a tag left open, as in a file cut short,
 *   or a close tag that doesn't match), which RmlUi logs, naming the line.
 *
 * A game that never creates a document links none of RmlUi.
 *
 * Shown documents are drawn by wgf_ui_draw_documents (wgf_ui.h), called in the frame
 * callback. They are drawn at that point in the frame, over what was drawn before.
 *
 * The input is read as they are drawn:
 * - The pointer (the mouse), its buttons, and its wheel.
 * - Fingers, as touches. A tap clicks. A finger dragged scrolls what it drags, gliding on as
 *   it lifts.
 * - The keys, and the characters typed.
 * - A pad's D-pad, as the arrow keys, and its south button, as Enter.
 *
 * A text field asks for the device's on-screen keyboard while it has the focus. On a phone's
 * browser that is a hidden input element of the page, at the field's caret. What is typed
 * there is typed into the field.
 *
 * The focus moves:
 * - By Tab and Shift+Tab, through the elements that can take it (RCSS `tab-index: auto`, and
 *   every form control).
 * - By the arrow keys and the D-pad, as each element's `nav` properties say (`nav: auto`, or
 *   `nav-up: #id` and so on).
 *
 * Enter, Space, or the south button clicks the element with the focus.
 *
 * Documents say when they have the pointer and the keyboard (wgf_input_set_pointer_captured):
 * - The pointer, while it is over an element of a shown document other than its body.
 * - The keyboard, while an element of a shown document has the focus.
 * - A frame that draws no document lets both go.
 *
 * Text is drawn in libwgf's fonts.
 * - RCSS's `font-family` names a family your game set (wgf_ui_set_document_font, in
 *   wgf_ui.h). A family it didn't set draws in the default font (wgf_font.h).
 * - A family's bold and italic are faces of their own (wgf_ui_set_document_font_face).
 *   `font-weight` and `font-style` draw in the family's face nearest them.
 * - Set the fonts before creating the documents. A document waits for the fonts set to be
 *   loaded before it is laid out.
 *
 * Elements are found by their id. An element's handle lives as long as its element. It is
 * refused from then on if the element is removed from its document (its parent's inner RML set
 * again), or its document is released.
 *
 * A document loads again in place (wgf_asset_reload) when its file is saved, or a style
 * sheet, template, or image it names:
 * - It keeps its handle, its visibility, and its events not yet taken.
 * - An element's handle is kept on the new document's element of the same id. A handle to an
 *   element without an id, or whose id the new file hasn't, is refused from then on.
 * - The changes the program made to elements with an id are made again, in the order made:
 *   text, RML, attributes, classes, properties, values, checked, and disabled. The last of
 *   each is kept, from wgf_asset_enable_reload on.
 * - The focus is on the element of the id it was on.
 * - A file that isn't a document, as wgf_document_create says (one saved half-written, for
 *   one), keeps it as it was, with one error logged.
 *
 * Tabs are RmlUi's `<tabset>`. Its `<tabs>`' `<tab>`s choose among its `<panels>`' `<panel>`s,
 * and the chosen tab and panel are `:selected` (wgf_element_get_number reads which). A tooltip
 * is an element shown by its parent's `:hover` (`.hint:hover .tip { display: block; }`), so it
 * shows while the pointer is over its parent. examples/assets/ui/options.rml has both.
 *
 * These RCSS features are drawn: gradients (linear, radial, conic, repeating), `box-shadow`,
 * `filter` (blur, drop-shadow, opacity, and the color filters), `backdrop-filter` (over what
 * the documents drew), `mask-image`, and clipping to rounded corners. Inside your game's own
 * render target (wgf_render_begin_target), documents draw without `filter`,
 * `backdrop-filter`, `mask-image`, and `box-shadow`, which need render targets of their
 * own. */

/* A document, a resource: a handle of kind "ui.document". */
typedef wgf_handle_t wgf_document_t;
/* One of a document's elements, an object owned by the document: a handle of kind "ui.element". */
typedef wgf_handle_t wgf_element_t;

/* What happened to an element, as wgf_document_take_events gives it. */
typedef enum wgf_document_event_t {
    WGF_DOCUMENT_EVENT_NONE = 0,
    WGF_DOCUMENT_EVENT_CLICK = 1,  /* pressed and released over it, or Enter, Space, or the south button on it */
    WGF_DOCUMENT_EVENT_CHANGE = 2, /* a form control's value changed: a checkbox toggled, a slider moved, a
                                      choice made, a text field typed into */
    WGF_DOCUMENT_EVENT_SUBMIT = 3, /* a form was submitted (a submit button clicked, Enter in its text field) */
    WGF_DOCUMENT_EVENT_FOCUS = 4,  /* it took the focus */
    WGF_DOCUMENT_EVENT_BLUR = 5    /* it lost the focus */
} wgf_document_event_t;

/* Creates the document in the .rml file at `path` under fs's root, and starts loading it. It
 * is hidden until shown.
 *
 * A path created before returns the same document, with one more reference. Returns 0 only
 * when there is no room for another document (at most 4096). */
WGF_API wgf_document_t wgf_document_create(const char *path);

/* Shows or hides a document, or gets whether it is shown.
 *
 * A shown document is drawn by wgf_ui_draw_documents, and worked by the input. A document
 * asked to show while it loads is shown once it is READY. Shown documents are drawn in the
 * order they were last shown, the last on top.
 *
 * Returns false for a handle that isn't a document, or one that FAILED. */
WGF_API bool wgf_document_set_visible(wgf_document_t document, bool visible);
WGF_API bool wgf_document_is_visible(wgf_document_t document);

/* Returns the element of `document` whose id is `id`. The same element returns the same
 * handle. Returns 0 if no element has that id, or the document isn't READY. */
WGF_API wgf_element_t wgf_document_find(wgf_document_t document, const char *id);

/* Sets or gets the element of `document` that has the focus (0 for none).
 *
 * Setting it moves the focus there, shown as keys move it (`:focus-visible`). 0 takes the
 * focus from the document.
 *
 * The setter returns false for a handle that isn't a document, an element of another
 * document, or an element that can't take the focus. An element can't take it if it is
 * hidden, disabled, or neither a form control nor `tab-index: auto`. */
WGF_API bool wgf_document_set_focus(wgf_document_t document, wgf_element_t element);
WGF_API wgf_element_t wgf_document_get_focus(wgf_document_t document);

/* Returns how many events of `document` wait, or takes the oldest, since they were last
 * taken.
 *
 * take_events fills `out` with two ints an event: the event (a wgf_document_event_t) and its
 * element (a wgf_element_t). It takes as many whole events as fit in `count` ints, oldest
 * first, and returns how many ints it filled.
 *
 * An event's element is the one it happened to. If that element has no id, it is the nearest
 * element around it that has one: a click on a button's label is the button's.
 *
 * Events wait while nobody takes them. Past 1024, the oldest are dropped. Returns 0 for a
 * handle that isn't a document, or no array. */
WGF_API int wgf_document_get_event_count(wgf_document_t document);
WGF_API int wgf_document_take_events(wgf_document_t document, int *out, int count);

/* Returns the document an element is in, and its id ("" for none). Returns 0 or "" for a
 * handle that isn't an element. */
WGF_API wgf_document_t wgf_element_get_document(wgf_element_t element);
WGF_API const char *wgf_element_get_id(wgf_element_t element);

/* Images named at run time are fetched before they are used.
 *
 * An image can be named at run time in RML that is set, in an attribute (`src`), or in a
 * property (a decorator's). If it isn't local yet, it is fetched first. On the web, that is
 * an image the document's files didn't name. Natively, a file on disk is local.
 *
 * - The change is held, and the element stays as it was.
 * - The change is made in the update in which its images have arrived, or failed (RmlUi logs
 *   the failure).
 * - While it is held, the call returns true, and the getters read the element as it is until
 *   then.
 * - Another change to the same element's same attribute or property (or its RML) drops a held
 *   change. */

/* Sets or gets an element's content, as plain text or as RML.
 *
 * - set_text shows `text` as it is, with no markup.
 * - set_rml parses `rml` and makes its elements.
 * - get_text returns the text of every element inside it, in order.
 * - get_rml returns its content as RML.
 *
 * Returns false for a handle that isn't an element, or no text. Text a getter returns is valid
 * until the next call in this header that returns text. */
WGF_API bool wgf_element_set_text(wgf_element_t element, const char *text);
WGF_API const char *wgf_element_get_text(wgf_element_t element);
WGF_API bool wgf_element_set_rml(wgf_element_t element, const char *rml);
WGF_API const char *wgf_element_get_rml(wgf_element_t element);

/* Sets, gets, checks, or removes an attribute, such as `class`, `src`, or a form control's
 * `value`.
 *
 * - get_attribute returns "" for an attribute that isn't set.
 * - has_attribute returns whether it is set.
 * - remove_attribute unsets it, and returns false if it wasn't set.
 *
 * Returns false for a handle that isn't an element, or no name. */
WGF_API bool wgf_element_set_attribute(wgf_element_t element, const char *name, const char *value);
WGF_API const char *wgf_element_get_attribute(wgf_element_t element, const char *name);
WGF_API bool wgf_element_has_attribute(wgf_element_t element, const char *name);
WGF_API bool wgf_element_remove_attribute(wgf_element_t element, const char *name);

/* Adds a class (`on`) or takes it off, as RCSS's selectors see it. has_class returns whether it
 * is on. Returns false for a handle that isn't an element, or no name. */
WGF_API bool wgf_element_set_class(wgf_element_t element, const char *name, bool on);
WGF_API bool wgf_element_has_class(wgf_element_t element, const char *name);

/* Sets or gets an RCSS property on the element itself, over its style sheets' value
 * (`"color"`, `"#ffcc00"`).
 *
 * - A `value` of "" or NULL takes the element's own value off.
 * - get_property returns the property's value as the element has it now, from wherever it
 *   comes, as RCSS text. It returns "" for a name that isn't a property.
 *
 * The setter returns false for a handle that isn't an element, a name that isn't a property, or
 * a value that isn't one of its values (logged). */
WGF_API bool wgf_element_set_property(wgf_element_t element, const char *name, const char *value);
WGF_API const char *wgf_element_get_property(wgf_element_t element, const char *name);

/* Sets or gets a form control's value, as text or as a number.
 *
 * - The value is a text field's text, a slider's number, a choice's chosen value, or a
 *   checkbox's own value.
 * - As a number: a slider's value is clamped to its range. Text is read as a number, and text
 *   that isn't one reads 0.
 * - A tabset's number is its chosen tab, from 0 in its `<tabs>`' order. Set it as a whole
 *   number among them. A tab chosen by the player raises the tabset's CHANGE.
 *
 * Setting either raises no event. Returns false (and "", or 0) for a handle that isn't a form
 * control or a tabset. set_value takes a form control only. */
WGF_API bool wgf_element_set_value(wgf_element_t element, const char *value);
WGF_API const char *wgf_element_get_value(wgf_element_t element);
WGF_API bool wgf_element_set_number(wgf_element_t element, float value);
WGF_API float wgf_element_get_number(wgf_element_t element);

/* Sets or gets whether a checkbox or a radio button is checked. Returns false for a handle that
 * isn't one. */
WGF_API bool wgf_element_set_checked(wgf_element_t element, bool checked);
WGF_API bool wgf_element_is_checked(wgf_element_t element);

/* Sets or gets whether a form control is disabled. A disabled control is drawn as RCSS's
 * `:disabled` says, and takes no input or focus. Returns false for a handle that isn't a form
 * control. */
WGF_API bool wgf_element_set_disabled(wgf_element_t element, bool disabled);
WGF_API bool wgf_element_is_disabled(wgf_element_t element);

#ifdef __cplusplus
}
#endif

#endif
