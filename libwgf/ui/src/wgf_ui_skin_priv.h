#ifndef WGF_UI_SKIN_PRIV_H
#define WGF_UI_SKIN_PRIV_H

#include <stdbool.h>

#include "wgf_color.h"
#include "wgf_texture.h"

/* The Clay UI's skins (wgf_ui.h), a file of their own (wgf_ui_skin.c) that the frame's
 * drawing reaches through these hooks, set by the first skin call: a UI drawn in its
 * colors alone links none of it. */

typedef struct wgf_ui_priv_skins_t {
    /* The widget's part in `state` drawn from its skin into the rectangle (a state with no
     * skin of its own: the normal state's, dimmed when disabled); false, drawing nothing,
     * for a part with no skin; its tint's alpha times `alpha`, the whole UI's. */
    bool (*draw)(int widget, int part, int state, float x, float y, float width, float height, float alpha);
    /* The same, at its art's size, centered on (x, y): a progress bar's edge. */
    bool (*draw_at)(int widget, int part, int state, float x, float y, float alpha);
    /* Whether the widget's part has a skin in any state: a widget whose base is drawn from
     * art draws its other parts from art alone, none in colors. */
    bool (*has)(int widget, int part);
    /* The widget's text offset in `state`. */
    void (*text_offset)(int widget, int state, float *x, float *y);
    /* Every skin and offset let go of: the style's reset, and the ui's stop. */
    void (*reset)(void);
} wgf_ui_priv_skins_t;

void wgf_ui_priv_set_skins(const wgf_ui_priv_skins_t *skins);

/* A skin's nine patches drawn with its edges and middle tiled (`fit`: a whole number of
 * times), from `texture`'s region `source` by its `borders`; false, drawing nothing, for a
 * rectangle smaller than its borders. wgf_ui_skin_patch.c's, set as a skin is first given a
 * tiled fill. */
typedef bool (*wgf_ui_priv_patch_draw_t)(wgf_texture_t texture, const float source[4], const float borders[4], float x,
                                         float y, float width, float height, bool fit, wgf_color_t tint);
void wgf_ui_priv_set_patch_draw(wgf_ui_priv_patch_draw_t draw_patches); /* wgf_ui_skin.c's */
/* Where a skin keeps its fill (a wgf_ui_patch_t); NULL for a widget, part, or state that isn't one. */
unsigned char *wgf_ui_priv_skin_patch(int widget, int part, int state);

#endif
