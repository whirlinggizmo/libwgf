#ifndef WGF_UI_SCROLL_PRIV_H
#define WGF_UI_SCROLL_PRIV_H

#include <stdbool.h>
#include <stdint.h>

/* The Clay UI's scroll areas and lists (wgf_ui.h): the frame's start, which keeps Clay's
 * areas and reads the pointer and the wheel for them, is a file of its own
 * (wgf_ui_scroll.c) that wgf_ui_begin reaches through a hook the first area or list sets
 * from there. Set in wgf_ui.c, the optimizer would see its one target and call it directly
 * in every UI. */

/* A scroll area as laid out: its Clay id, its window's, and how far it was scrolled. */
typedef struct wgf_ui_priv_area_t {
    uint32_t element, window;
    float offset[2];
    bool bare; /* nothing in it could be focused: a scroll area takes the focus itself */
} wgf_ui_priv_area_t;

/* The scrolling's state, the ui's. */
typedef struct wgf_ui_priv_scroll_t {
    uint32_t target;                  /* the innermost scroll area under the pointer: its window's Clay id */
    wgf_ui_priv_area_t *areas, *before; /* this frame's areas, and the frame before's */
    int area_count, area_capacity, before_count, before_capacity;
    uint32_t held;     /* the one a held press began on */
    bool started;      /* the held press seen by its area */
    bool dragging;     /* it moved past the slop: the area's, not a widget's */
    bool press_drags;  /* it began on a widget that drags (a slider): never the area's */
    int bar;           /* it began on a thumb, across (0) or down (1); 2 a track; -1 neither */
    int item;          /* the list's item it began on, -1 none */
    bool item_chosen;  /* that item was the chosen one already: a release on it activates it */
    float from[2], offset_from[2], grab; /* the pointer and the scroll as it began; where on the thumb */
    uint32_t flung;    /* the area let go of moving */
    float velocity[2]; /* its scroll's, in logical pixels a second */
    float wheel[2];
    bool reveal, reveal_next; /* the focus moved by keys, a pad, or the program: the area it is in scrolls to it */
} wgf_ui_priv_scroll_t;

/* The frame's start for scrolling, after Clay is told the pointer and before its layout
 * begins: Clay's areas kept (one not given last frame dropped, each held to its content),
 * the innermost under the pointer found from the frame before's areas, and a press
 * (`pressed`), a release (not `down`), and the wheel read. */
typedef void (*wgf_ui_priv_scroll_frame_t)(wgf_ui_priv_scroll_t *scroll, bool pressed, bool down);

void wgf_ui_priv_set_scroll_frame(wgf_ui_priv_scroll_frame_t frame); /* wgf_ui.c's */
void wgf_ui_priv_install_scrolling(void);                           /* wgf_ui_scroll.c's: sets it */

#endif
