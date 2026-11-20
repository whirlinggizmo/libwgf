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
 * style sheets), as RmlUi reads them: laid out, drawn, and worked by the pointer, the keys,
 * and the pads by libwgf, beside the immediate UI of wgf_ui.h, which stays for HUDs and
 * small screens. A document is a resource (wgf_resource.h): created from its path, loaded
 * with every file it names (its style sheets, images, and templates, made local first, so
 * it loads the same from a page as from a disk), READY once it is parsed, FAILED (logged)
 * when it can't be read or isn't a document. A program that never creates one links none
 * of RmlUi.
 *
 * Shown documents are drawn by wgf_ui_draw_documents (wgf_ui.h), called in the frame callback, at that
 * point in the frame: over what was drawn before it. The input is read as they are drawn:
 * the pointer (the mouse, which touch drives too), its buttons and wheel, the keys and the
 * characters typed, and a pad's D-pad (as the arrow keys) and south button (as Enter). The
 * focus moves by Tab and Shift+Tab through what can take it (RCSS `tab-index: auto`, and
 * every form control), and by the arrow keys and the D-pad as each element's `nav`
 * properties say (`nav: auto`, or `nav-up: #id`...); Enter, Space, or the south button
 * clicks what has it. Documents say when they have the pointer and the keyboard
 * (wgf_input_set_pointer_captured): the pointer while it is over an element of a shown
 * document other than its body, and the keyboard while an element of one has the focus;
 * a frame that draws none lets both go.
 *
 * Text is drawn in libwgf's fonts: RCSS's `font-family` names a family the program set
 * (wgf_ui_set_document_font, in wgf_ui.h), and one it didn't set draws in the default font
 * (wgf_font.h). A face's weight and style (bold, italic) are the family's one font. Set
 * the fonts before creating the documents: a document waits for the fonts set to be
 * loaded before it is laid out.
 *
 * Elements are found by their id. An element's handle lives as long as its element: one
 * removed from its document (its parent's inner RML set again) or whose document is
 * released is refused from then on.
 *
 * What RmlUi draws with filters, shaders, masks, or layers (`filter`, `backdrop-filter`,
 * `box-shadow`, gradients, `mask-image`, and clipping to rounded corners) isn't drawn. */

/* A document, a resource: a handle of kind "ui.document". */
typedef wgf_handle_t wgf_document_t;
/* One of a document's elements, an object of the document's: a handle of kind "ui.element". */
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

/* The document in the .rml file at `path` under fs's root, loading: hidden until shown. A
 * path created before gives the same document with one more reference. 0 only when there
 * is no room for another (4096). */
WGF_API wgf_document_t wgf_document_create(const char *path);

/* Shown (drawn by wgf_ui_draw_documents, and worked by the input) or hidden; a document
 * asked to show while it loads is shown once READY. Shown documents are drawn in the
 * order they were last shown, the last on top. False for a handle that isn't a document,
 * or one that FAILED. */
WGF_API bool wgf_document_set_visible(wgf_document_t document, bool visible);
WGF_API bool wgf_document_is_visible(wgf_document_t document);

/* The element of `document` whose id is `id`, or 0: none has it, or the document isn't
 * READY. The same element gives the same handle. */
WGF_API wgf_element_t wgf_document_find(wgf_document_t document, const char *id);

/* The element of `document` that has the focus, or 0. Setting it moves the focus there,
 * shown as keys move it (`:focus-visible`); 0 takes it from the document. False for a
 * handle that isn't a document, an element of another document, or one that can't take
 * the focus (hidden, disabled, or neither a form control nor `tab-index: auto`). */
WGF_API bool wgf_document_set_focus(wgf_document_t document, wgf_element_t element);
WGF_API wgf_element_t wgf_document_get_focus(wgf_document_t document);

/* The events of `document` since they were last taken, oldest first: how many wait, and
 * the oldest taken into `out`, two ints each (the event, a wgf_document_event_t, and its
 * element, a wgf_element_t), as many whole events as fit in `count` ints, returning how many
 * ints it filled. An event's element is the one it happened to, or, when that has no id,
 * the nearest element around it with one (a click on a button's label is the button's).
 * Events wait while nobody takes them, the oldest dropped past 1024. 0 for a handle that
 * isn't a document, or no array. */
WGF_API int wgf_document_get_event_count(wgf_document_t document);
WGF_API int wgf_document_take_events(wgf_document_t document, int *out, int count);

/* The document an element is in, and its id ("" for none). 0 or "" for a handle that isn't
 * an element. */
WGF_API wgf_document_t wgf_element_get_document(wgf_element_t element);
WGF_API const char *wgf_element_get_id(wgf_element_t element);

/* Its content as plain text (`text` shown as it is: no markup), or as RML (parsed, its
 * elements made). The text getter gives the text of every element inside it, in order;
 * the RML getter its content as RML. False for a handle that isn't an element, or no
 * text. Text a getter returns is valid until the next call of this header that returns
 * text. */
WGF_API bool wgf_element_set_text(wgf_element_t element, const char *text);
WGF_API const char *wgf_element_get_text(wgf_element_t element);
WGF_API bool wgf_element_set_rml(wgf_element_t element, const char *rml);
WGF_API const char *wgf_element_get_rml(wgf_element_t element);

/* An attribute, set to `value` (`class`, `src`, a form control's `value`...); has: whether
 * it is set; remove: unset, false when it wasn't. get gives "" for an attribute unset.
 * False for a handle that isn't an element, or no name. */
WGF_API bool wgf_element_set_attribute(wgf_element_t element, const char *name, const char *value);
WGF_API const char *wgf_element_get_attribute(wgf_element_t element, const char *name);
WGF_API bool wgf_element_has_attribute(wgf_element_t element, const char *name);
WGF_API bool wgf_element_remove_attribute(wgf_element_t element, const char *name);

/* A class, added (`on`) or taken off, as RCSS's selectors see it; has: whether it is on.
 * False for a handle that isn't an element, or no name. */
WGF_API bool wgf_element_set_class(wgf_element_t element, const char *name, bool on);
WGF_API bool wgf_element_has_class(wgf_element_t element, const char *name);

/* An RCSS property set on the element itself, over its style sheets' (`"color"`,
 * `"#ffcc00"`); "" or NULL takes the element's own off. get gives the property's value as
 * the element has it now, from wherever it comes, as RCSS text ("" for a name that isn't
 * one). False for a handle that isn't an element, a name that isn't a property, or a value
 * that isn't one of its values (logged). */
WGF_API bool wgf_element_set_property(wgf_element_t element, const char *name, const char *value);
WGF_API const char *wgf_element_get_property(wgf_element_t element, const char *name);

/* A form control's value: a text field's text, a slider's number, a choice's chosen value,
 * a checkbox's own value; as text, or as a number (a slider's, clamped to its range, or
 * text read as one: 0 for text that isn't). Setting it raises no event. False (and "", 0)
 * for a handle that isn't a form control. */
WGF_API bool wgf_element_set_value(wgf_element_t element, const char *value);
WGF_API const char *wgf_element_get_value(wgf_element_t element);
WGF_API bool wgf_element_set_number(wgf_element_t element, float value);
WGF_API float wgf_element_get_number(wgf_element_t element);

/* A checkbox or a radio button checked; false for a handle that isn't one. */
WGF_API bool wgf_element_set_checked(wgf_element_t element, bool checked);
WGF_API bool wgf_element_is_checked(wgf_element_t element);

/* A form control disabled: drawn as RCSS's `:disabled` says, taking no input or focus;
 * false for a handle that isn't a form control. */
WGF_API bool wgf_element_set_disabled(wgf_element_t element, bool disabled);
WGF_API bool wgf_element_is_disabled(wgf_element_t element);

#ifdef __cplusplus
}
#endif

#endif
