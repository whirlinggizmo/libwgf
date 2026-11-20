#ifndef WGF_UI_PRIV_H
#define WGF_UI_PRIV_H

#include <stdbool.h>
#include <stdint.h>

/* The ui module's own, for tests and tools. */

/* Where the box, panel, or button `id` was laid out in the last wgf_ui_end, in logical
 * pixels; false for an id it hasn't laid out. */
bool wgf_ui_priv_get_bounds(const char *id, float *x, float *y, float *width, float *height);

/* Where a document's element (a wgf_element_t) was laid out in the last
 * wgf_ui_draw_documents, its border box in logical pixels; false for a handle that isn't
 * an element. */
bool wgf_ui_priv_document_element_bounds(uint32_t element, float *x, float *y, float *width, float *height);

/* The font (a wgf_font_t) text of `family` at `weight` and `italic` draws in: the family's
 * face set nearest it (wgf_ui_set_document_font_face), or 0 for none, the default's. */
uint32_t wgf_ui_priv_document_face_font(const char *family, int weight, bool italic);

#endif
