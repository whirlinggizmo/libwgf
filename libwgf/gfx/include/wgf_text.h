#ifndef WGF_TEXT_H
#define WGF_TEXT_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_font.h"
#include "wgf_actor.h"
#include "wgf_sprite.h" /* wgf_sprite_facing_t */

#ifdef __cplusplus
extern "C" {
#endif

/* A text actor: a string drawn in a font.
 *
 * - On a 2D stage it is flat on the screen.
 * - On a 3D stage it is a block in the world, facing as it is told (wgf_text_set_facing).
 *   It is depth tested, and sorted with the other see-through drawing.
 * - Its actor's position is where its block of lines sits, by its alignment. The actor
 *   turns and scales it like any other.
 * - It holds a reference to its font (0: the default font).
 *
 * Text is an actor (wgf_actor.h): place, parent, and destroy it with the actor calls. */

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

/* Creates a text actor in `font` (0: the default font), with no string yet.
 *
 * Returns 0 if `font` isn't a font, or if there is no room for another actor. */
WGF_API wgf_actor_t wgf_text_create(wgf_font_t font);

/* Sets or gets the text's font. The setter returns false if `font` isn't a font. 0 is a
 * font: the default font. */
WGF_API bool wgf_text_set_font(wgf_actor_t text, wgf_font_t font);
WGF_API wgf_font_t wgf_text_get_font(wgf_actor_t text);

/* Sets or gets what the text says, in UTF-8. The setter copies the string.
 *
 * The string get_string returns belongs to the actor. It is valid until the next
 * set_string, or until the actor is destroyed. */
WGF_API bool wgf_text_set_string(wgf_actor_t text, const char *string);
WGF_API const char *wgf_text_get_string(wgf_actor_t text);

/* Sets or gets the text's font size: the height of its lines in stage units, before the
 * actor's scale.
 *
 * It is called a font size so it isn't taken for a sprite's size. A size of 0 or less (the
 * default) is 16 on a 2D stage, and 1 on a 3D one. The getter returns the size it draws at.
 *
 * The setter returns false for a handle that isn't text. */
WGF_API bool wgf_text_set_font_size(wgf_actor_t text, float size);
WGF_API float wgf_text_get_font_size(wgf_actor_t text);

/* Sets or gets the text's color (default: white). */
WGF_API bool wgf_text_set_color(wgf_actor_t text, wgf_color_t color);
WGF_API wgf_color_t wgf_text_get_color(wgf_actor_t text);

/* Sets or gets the width the text's lines wrap to, in stage units (default: 0).
 *
 * - Lines wrap at spaces and tabs to fit `width`.
 * - A word wider than `width` keeps a line to itself.
 * - 0 wraps nothing.
 * - Newlines always break a line.
 * - For its alignment, a wrapped block is `width` wide, or as wide as its widest line where
 *   a word is wider.
 *
 * The setter returns false for a `width` below 0. */
WGF_API bool wgf_text_set_wrap_width(wgf_actor_t text, float width);
WGF_API float wgf_text_get_wrap_width(wgf_actor_t text);

/* Sets or gets where the block sits relative to the actor's position, on each axis.
 *
 * Lines line up the same way inside the block. The setter returns false for a value that
 * isn't an alignment. */
WGF_API bool wgf_text_set_align(wgf_actor_t text, wgf_text_halign_t horizontal,
                                      wgf_text_valign_t vertical);
WGF_API wgf_text_halign_t wgf_text_get_halign(wgf_actor_t text);
WGF_API wgf_text_valign_t wgf_text_get_valign(wgf_actor_t text);

/* Sets or gets the spacing added between the text's letters and between its lines, in
 * logical pixels.
 *
 * Both start at 0. Spacing below 0 tightens the text. The setter returns false for numbers
 * that aren't finite. */
WGF_API bool wgf_text_set_spacing(wgf_actor_t text, float letter, float line);
WGF_API float wgf_text_get_letter_spacing(wgf_actor_t text);
WGF_API float wgf_text_get_line_spacing(wgf_actor_t text);

/* Sets or gets which way the text's block faces on a 3D stage, as a sprite's does
 * (wgf_sprite.h).
 *
 * It faces one of these:
 * - the camera (the default);
 * - the camera, turning about Y only;
 * - up, lying flat;
 * - the way its actor is turned.
 *
 * Text on a 2D stage is flat on the screen whatever this says. The setter returns false if
 * `facing` isn't a facing. */
WGF_API bool wgf_text_set_facing(wgf_actor_t text, wgf_sprite_facing_t facing);
WGF_API wgf_sprite_facing_t wgf_text_get_facing(wgf_actor_t text);

/* Sets or gets how far nearer the camera the text is drawn on a 3D stage, in stage units.
 *
 * It works as a sprite's does (wgf_sprite_set_depth_offset). Use it for text over the sprite
 * it labels, to keep the text in front at a distance.
 *
 * The setter returns false for a handle that isn't text, or an `offset` that isn't finite. */
WGF_API bool wgf_text_set_depth_offset(wgf_actor_t text, float offset);
WGF_API float wgf_text_get_depth_offset(wgf_actor_t text);

#ifdef __cplusplus
}
#endif

#endif
