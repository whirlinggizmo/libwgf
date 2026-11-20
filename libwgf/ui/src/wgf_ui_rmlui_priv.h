#ifndef WGF_UI_RMLUI_PRIV_H
#define WGF_UI_RMLUI_PRIV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The ui module's way into RmlUi (wgf_ui_rmlui.cpp), as physics3d's into Jolt: C both
 * ways, so C++ stays in that one file and no header outside it names RmlUi. The C++ side
 * (wgf_ui_priv_rml_*) is RmlUi: its one context, its documents and elements, and the
 * interfaces RmlUi asks the program for, each of which calls the C side
 * (wgf_ui_priv_doc_*, in wgf_ui_document.c), which reads files, makes textures, measures
 * and lays out glyphs, and draws, through libwgf. A document is a number the C++ side
 * gives (0 none); an element is known to the C++ side by the handle the C side made for
 * it. Main thread only. */

/* ---- the C++ side: RmlUi ----------------------------------------------------------- */

/* RmlUi started, with its one context; false (logged) when it couldn't be. Once. */
bool wgf_ui_priv_rml_start(void);
void wgf_ui_priv_rml_stop(void);
/* The GPU's context given back: every texture RmlUi holds released (its ReleaseTexture each),
 * asked for again as it next draws. */
void wgf_ui_priv_rml_release_textures(void);

/* A document parsed from `rml` (size bytes; its relative paths from `path`'s directory),
 * hidden; 0 (logged by RmlUi) when it isn't one. `owner` is the C side's handle, given back
 * in the events. Unloading closes it: its elements gone, their handles told
 * (wgf_ui_priv_doc_element_gone). */
int wgf_ui_priv_rml_load(const char *rml, int size, const char *path, uint32_t owner);
void wgf_ui_priv_rml_unload(int document);
void wgf_ui_priv_rml_set_visible(int document, bool visible);
bool wgf_ui_priv_rml_is_visible(int document);

/* The element of `document` with `id`, as the C side's handle (made the first time,
 * wgf_ui_priv_doc_element_made), or 0. */
uint32_t wgf_ui_priv_rml_find(int document, const char *id);
/* The document's focused element's handle, or 0; focus moves it there (0 off). */
uint32_t wgf_ui_priv_rml_get_focus(int document);
bool wgf_ui_priv_rml_set_focus(int document, int slot);

/* The document's events since taken, two ints each (wgf_document_event_t, the element's
 * handle), into `out` as fit in `count` ints; how many filled. */
int wgf_ui_priv_rml_event_count(int document);
int wgf_ui_priv_rml_take_events(int document, int *out, int count);

/* An element, by the slot the C side was given for it (wgf_ui_priv_doc_element_made):
 * false (or NULL, 0) for one gone. Text returned is the C++ side's, valid until its next
 * call returning text. */
typedef enum wgf_ui_priv_rml_text_t {
    WGF_UI_PRIV_RML_ID,
    WGF_UI_PRIV_RML_TEXT,
    WGF_UI_PRIV_RML_RML,
    WGF_UI_PRIV_RML_VALUE
} wgf_ui_priv_rml_text_t;
bool wgf_ui_priv_rml_element_alive(int slot);
int wgf_ui_priv_rml_element_document(int slot);
const char *wgf_ui_priv_rml_get_text(int slot, wgf_ui_priv_rml_text_t which);
bool wgf_ui_priv_rml_set_text(int slot, wgf_ui_priv_rml_text_t which, const char *text);
const char *wgf_ui_priv_rml_get_attribute(int slot, const char *name, bool *has);
bool wgf_ui_priv_rml_set_attribute(int slot, const char *name, const char *value); /* NULL removes */
bool wgf_ui_priv_rml_has_class(int slot, const char *name);
bool wgf_ui_priv_rml_set_class(int slot, const char *name, bool on);
const char *wgf_ui_priv_rml_get_property(int slot, const char *name);
bool wgf_ui_priv_rml_set_property(int slot, const char *name, const char *value); /* NULL or "": removed */
bool wgf_ui_priv_rml_is_form_control(int slot);
bool wgf_ui_priv_rml_get_number(int slot, float *value);
bool wgf_ui_priv_rml_set_number(int slot, float value);
bool wgf_ui_priv_rml_is_checkable(int slot);
bool wgf_ui_priv_rml_is_checked(int slot);
bool wgf_ui_priv_rml_set_checked(int slot, bool checked);
bool wgf_ui_priv_rml_is_disabled(int slot);
bool wgf_ui_priv_rml_set_disabled(int slot, bool disabled);
/* Its border box, in the context's pixels, for tests. */
bool wgf_ui_priv_rml_element_box(int slot, float *x, float *y, float *width, float *height);

/* A frame's input, in the context's pixels (the presentation's visible area's, from its
 * top left), and the context's size. Keys are libwgf's (wgf_keyboard_key_t), pressed or
 * repeated (down) and released (up) this frame. */
typedef struct wgf_ui_priv_rml_input_t {
    float width, height;
    float mouse_x, mouse_y;
    bool mouse_inside;    /* the pointer is over the window */
    int buttons_pressed;  /* bit 0 left, 1 right, 2 middle */
    int buttons_released;
    float wheel_x, wheel_y; /* RmlUi's: down and right positive, in lines */
    bool shift, ctrl, alt, super;
    const int *keys_down;
    int key_down_count;
    const int *keys_up;
    int key_up_count;
    const char *text; /* typed, UTF-8 */
    /* fingers that touched down, moved, lifted, or were lost (`touch_phase`: 0 to 3)
       this frame; `touching` while any is down and drives the mouse too, which isn't
       read then */
    int touch_count;
    int touch_ids[8];
    int touch_phase[8];
    float touch_x[8], touch_y[8];
    bool touching;
} wgf_ui_priv_rml_input_t;

/* The context worked by `input` (NULL: none, its size kept) and updated; then drawn, through
 * the C side's drawing (wgf_ui_priv_doc_draw ...). What it has: the pointer, over an element
 * of a shown document other than its body; the keyboard, an element of one focused. */
void wgf_ui_priv_rml_frame(const wgf_ui_priv_rml_input_t *input, bool *pointer, bool *keyboard);

/* ---- the C side: libwgf, for RmlUi --------------------------------------------------- */

/* Logged (0 debug, 1 info, 2 warn, 3 error); the seconds since start. */
void wgf_ui_priv_doc_log(int level, const char *message);
double wgf_ui_priv_doc_seconds(void);
void wgf_ui_priv_doc_set_clipboard(const char *text);
const char *wgf_ui_priv_doc_get_clipboard(void);
/* The pointer's shape, by CSS's name for it ("pointer", "text", "move"...). */
void wgf_ui_priv_doc_set_cursor(const char *name);
/* A text field typed into wants an on-screen keyboard (shown), its caret at x, y (the
 * context's pixels), a line `height` tall; or no longer does. */
void wgf_ui_priv_doc_keyboard(bool shown, float x, float y, float height);

/* A file under fs's root (local: the document's loader made it so), whole: malloc'd,
 * freed with file_free; NULL when it can't be read. */
unsigned char *wgf_ui_priv_doc_file_read(const char *path, int *size);
void wgf_ui_priv_doc_file_free(unsigned char *data);

/* An element was given a slot by the C++ side: its handle, of document `owner` (0 when there
 * is no room: the element then has none); and an element with one gone. */
uint32_t wgf_ui_priv_doc_element_made(int slot, uint32_t owner);
void wgf_ui_priv_doc_element_gone(uint32_t element);

/* Textures: one from an image file (a texture libwgf has at the path, or the file decoded)
 * or from RGBA (premultiplied, as RmlUi makes them), and its size; 0 when it can't be. The
 * font atlas is WGF_UI_PRIV_ATLAS, never released. */
#define WGF_UI_PRIV_ATLAS ((uintptr_t)1)
uintptr_t wgf_ui_priv_doc_texture_load(const char *path, int *width, int *height);
uintptr_t wgf_ui_priv_doc_texture_generate(const unsigned char *rgba, int width, int height);
/* A texture of libwgf's, READY, drawn as it is (a bitmap font's page): a reference held
 * until it is released; 0 when it isn't READY. */
uintptr_t wgf_ui_priv_doc_texture_share(uint32_t texture, int *width, int *height);
void wgf_ui_priv_doc_texture_release(uintptr_t texture);

/* A vertex: where, its texture's coordinates, and its color, not premultiplied. */
typedef struct wgf_ui_priv_vertex_t {
    float x, y, u, v;
    uint8_t r, g, b, a;
} wgf_ui_priv_vertex_t;

/* Drawing, in the context's pixels, recorded into the frame's immediate mode while
 * wgf_ui_priv_rml_frame draws: triangles (three indices each) moved by tx, ty, textured
 * (0: none); the scissor, on or off; the transform (16 floats, column by column; NULL: none). */
void wgf_ui_priv_doc_draw(const wgf_ui_priv_vertex_t *vertices, int vertex_count, const int *indices, int index_count,
                          float tx, float ty, uintptr_t texture);
void wgf_ui_priv_doc_scissor(bool enable, int x, int y, int width, int height);
void wgf_ui_priv_doc_transform(const float *matrix);

/* What RmlUi draws past triangles (wgf_ui_document_draw.c), as its render interface's
 * optional calls ask.
 *
 * The clip mask: on or off; set to triangles (operation 0), to what is outside them (1),
 * or narrowed to them (2), moved by tx, ty and the transform. While it is on, nothing
 * drawn (triangles, gradients, a layer composited) falls outside it. */
void wgf_ui_priv_doc_clip_mask(bool enable);
void wgf_ui_priv_doc_clip_mask_draw(int operation, const wgf_ui_priv_vertex_t *vertices, int vertex_count,
                                    const int *indices, int index_count, float tx, float ty);

/* Layers: a new one, transparent, drawn into from now (its number, 1 up; 0 is what the
 * documents draw on); one drawn onto another or itself through filters (in order),
 * blended or replacing it, under the scissor and the clip mask; the top one let go of,
 * the one below drawn into again. The top layer saved: the scissor's region of it as a
 * texture, premultiplied; or the whole of it as a mask, a filter keeping what is drawn
 * through it by its alpha. 0 when it can't be. */
int wgf_ui_priv_doc_layer_push(void);
void wgf_ui_priv_doc_layer_composite(int source, int destination, bool replace, const uintptr_t *filters,
                                     int filter_count);
void wgf_ui_priv_doc_layer_pop(void);
uintptr_t wgf_ui_priv_doc_layer_save_texture(int x, int y, int width, int height);
uintptr_t wgf_ui_priv_doc_layer_save_mask(void);

/* Filters, each a few numbers: a color matrix (16, column by column, on premultiplied
 * colors: opacity and CSS's color filters), a blur (its sigma, in the context's pixels),
 * or a shadow (its sigma, its offset x and y, its color r, g, b, a, 0 to 1, straight). 0
 * when there is no room. */
typedef enum wgf_ui_priv_filter_t {
    WGF_UI_PRIV_FILTER_MATRIX,
    WGF_UI_PRIV_FILTER_BLUR,
    WGF_UI_PRIV_FILTER_SHADOW,
    WGF_UI_PRIV_FILTER_MASK /* the C side's own: a layer saved as a mask */
} wgf_ui_priv_filter_t;
uintptr_t wgf_ui_priv_doc_filter_make(wgf_ui_priv_filter_t kind, const float *values);
void wgf_ui_priv_doc_filter_release(uintptr_t filter);

/* Gradients, RmlUi's shaders: linear (0: from p toward p + v), radial (1: about p, v its
 * radii), or conic (2: about p, from the angle v points at), repeating or not, in the
 * element's pixels (a vertex's u, v); their stops, five floats each (where along it, then
 * r, g, b, a, 0 to 1, premultiplied), up to 16. Drawn on a geometry's triangles, which
 * the C side keeps on the GPU by `geometry` (any number the C++ side names it by) until
 * told it is gone. 0 when there is no room. */
uintptr_t wgf_ui_priv_doc_gradient_make(int kind, bool repeating, float px, float py, float vx, float vy,
                                        const float *stops, int stop_count);
void wgf_ui_priv_doc_gradient_draw(uintptr_t gradient, uintptr_t geometry, const wgf_ui_priv_vertex_t *vertices,
                                   int vertex_count, const int *indices, int index_count, float tx, float ty);
void wgf_ui_priv_doc_gradient_release(uintptr_t gradient);
void wgf_ui_priv_doc_geometry_gone(uintptr_t geometry);

/* Fonts: a face, a family at a size in pixels (1.. ; 0 when none can be had), a weight (1 to
 * 1000; another, 400), and a style, its metrics,
 * a line's width, and its glyphs' quads (8 floats each: x0, y0, x1, y1, s0, t0, s1, t1 in
 * the atlas) from x along the baseline y, as many as fit in max; and the version the
 * quads made are good for, changed whenever they would be laid out again. */
int wgf_ui_priv_doc_font_face(const char *family, int size, int weight, bool italic);
bool wgf_ui_priv_doc_font_metrics(int face, float *ascent, float *descent, float *line_height, float *x_height);
float wgf_ui_priv_doc_font_width(int face, const char *text, int length);
int wgf_ui_priv_doc_font_quads(int face, const char *text, int length, float x, float y, int page, float *quads,
                               int max);
/* The pages a face's glyphs are drawn from: 1 for the atlas; and a bitmap font's page's
 * texture (a wgf_texture_t), 0 for the atlas. */
int wgf_ui_priv_doc_font_page_count(int face);
uint32_t wgf_ui_priv_doc_font_page(int face, int page);
int wgf_ui_priv_doc_font_version(void);

#ifdef __cplusplus
}
#endif

#endif
