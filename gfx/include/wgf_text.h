#ifndef WGF_TEXT_H
#define WGF_TEXT_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_font.h"
#include "wgf_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A text node: a string drawn in a font, in a canvas. Its node's position is where its
 * block of lines sits, by its alignment, and the node turns and scales it like any
 * other. It holds a reference to its font (0: the default font). Text is a
 * node (wgf_node.h): place, parent, and destroy it with the node calls. */

typedef enum wgf_text_halign_t {
    WGF_TEXT_HALIGN_LEFT = 0,   /* the position is the block's left edge (the default) */
    WGF_TEXT_HALIGN_CENTER = 1,
    WGF_TEXT_HALIGN_RIGHT = 2
} wgf_text_halign_t;

typedef enum wgf_text_valign_t {
    WGF_TEXT_VALIGN_TOP = 0,    /* the position is the block's top edge (the default) */
    WGF_TEXT_VALIGN_MIDDLE = 1,
    WGF_TEXT_VALIGN_BOTTOM = 2
} wgf_text_valign_t;

/* A text node in `font` (0: the default font), with no string yet. 0 when `font`
 * isn't a font, or there is no room for another node. */
WGF_API wgf_node_t wgf_text_create(wgf_font_t font);

/* False when `font` isn't a font (0 is: the default font). */
WGF_API bool wgf_text_set_font(wgf_node_t text, wgf_font_t font);
WGF_API wgf_font_t wgf_text_get_font(wgf_node_t text);

/* What it says, UTF-8, copied. get_string's is the node's: valid until the next
 * set_string, or until it is destroyed. */
WGF_API bool wgf_text_set_string(wgf_node_t text, const char *string);
WGF_API const char *wgf_text_get_string(wgf_node_t text);

/* Its font size: the height of its lines, before the node's scale, in canvas units (a
 * font size, so it isn't taken for a sprite's size). 0 or less (the default) is 16.
 * Read back as the size it draws at. False for a handle that isn't text. */
WGF_API bool wgf_text_set_font_size(wgf_node_t text, float size);
WGF_API float wgf_text_get_font_size(wgf_node_t text);

/* Default: white. */
WGF_API bool wgf_text_set_color(wgf_node_t text, wgf_color_t color);
WGF_API wgf_color_t wgf_text_get_color(wgf_node_t text);

/* Wrap lines at spaces and tabs to fit `width`, in canvas units; a word wider keeps
 * a line to itself. 0 (the default) wraps nothing. Newlines always break a line. A
 * wrapped block is `width` wide for its alignment, or as wide as its widest line
 * where a word is wider. False for less than 0. */
WGF_API bool wgf_text_set_wrap_width(wgf_node_t text, float width);
WGF_API float wgf_text_get_wrap_width(wgf_node_t text);

/* Where the block sits relative to the node's position, on each axis; lines line
 * up the same way inside the block. False for a value that isn't one. */
WGF_API bool wgf_text_set_align(wgf_node_t text, wgf_text_halign_t horizontal,
                                      wgf_text_valign_t vertical);
WGF_API wgf_text_halign_t wgf_text_get_halign(wgf_node_t text);
WGF_API wgf_text_valign_t wgf_text_get_valign(wgf_node_t text);

#ifdef __cplusplus
}
#endif

#endif
