#ifndef WGF_FONT_H
#define WGF_FONT_H

#include <stdbool.h>

#include "wgf_handle.h"
#include "wgf_api.h"
#include "wgf_resource.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A font, a resource: a handle of kind "gfx.font". 0 is the default font. */
typedef wgf_handle_t wgf_font_t;

/* A font: a TrueType or OpenType typeface, loaded from a file. It is a resource
 * (wgf_resource.h): shared, reference counted, and loaded on create. Sizes are chosen where text is drawn, not here.
 *
 * Font 0 means the default font, everywhere a font is taken. It starts as the
 * built-in font -- JetBrains Mono, printable ASCII only -- so text works with no
 * font file at all. A font not yet ready, or failed, draws as the default font. */

/* A font from a .ttf or .otf file under fs's root. 0 only when there is no room
 * for another font; one that can't be loaded FAILED. A font loaded before, and
 * released since, comes back READY at once. */
WGF_API wgf_font_t wgf_font_create(const char *path);

/* The font that font 0 means. It holds a reference to `font`; 0 goes back to the
 * built-in font. For text beyond ASCII, set a font that has it. False for a handle
 * that isn't a font. */
WGF_API bool wgf_font_set_default(wgf_font_t font);
WGF_API wgf_font_t wgf_font_get_default(void);

/* How much room `text` (UTF-8) takes at `size` logical pixels (16 for 0 or less),
 * measured at the frame's pixel density as it is drawn: the widest line, and the
 * lines' height together. Newlines break lines; nothing wraps. A line keeps its
 * spaces, so " " measures a space's advance. 0, 0 for empty text, or before gfx is
 * set up. */
WGF_API wgf_vec2_t wgf_font_measure(wgf_font_t font, const char *text, float size);

#ifdef __cplusplus
}
#endif

#endif
