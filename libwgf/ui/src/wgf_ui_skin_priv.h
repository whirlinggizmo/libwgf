#ifndef WGF_UI_SKIN_PRIV_H
#define WGF_UI_SKIN_PRIV_H

#include <stdbool.h>

/* The Clay UI's skins (wgf_ui.h), a file of their own (wgf_ui_skin.c) that the frame's
 * drawing reaches through these hooks, set by the first skin call: a UI drawn in its
 * colors alone links none of it. */

typedef struct wgf_ui_priv_skins_t {
    /* The widget's part in `state` drawn from its skin into the rectangle (a state with no
     * skin of its own: the normal state's, dimmed when disabled); false, drawing nothing,
     * for a part with no skin. */
    bool (*draw)(int widget, int part, int state, float x, float y, float width, float height);
    /* The same, at its art's size, centered on (x, y): a progress bar's edge. */
    bool (*draw_at)(int widget, int part, int state, float x, float y);
    /* Whether the widget's part has a skin in any state: a widget whose base is drawn from
     * art draws its other parts from art alone, none in colors. */
    bool (*has)(int widget, int part);
    /* The widget's text offset in `state`. */
    void (*text_offset)(int widget, int state, float *x, float *y);
    /* Every skin and offset let go of: the style's reset, and the ui's stop. */
    void (*reset)(void);
} wgf_ui_priv_skins_t;

void wgf_ui_priv_set_skins(const wgf_ui_priv_skins_t *skins);

#endif
