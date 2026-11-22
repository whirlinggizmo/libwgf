#ifndef WGF_UI_POPUP_PRIV_H
#define WGF_UI_POPUP_PRIV_H

#include <stdbool.h>
#include <stdint.h>

/* The Clay UI's popup layer (wgf_ui.h: dropdowns, modals, tooltips): what every frame does
 * for it, as the frame begins, ends, and moves the focus, is a file of its own
 * (wgf_ui_popup.c) that the frame reaches through hooks the first dropdown, modal, or
 * tooltip sets from there, so a UI with none links none of it. The widgets themselves are
 * wgf_ui.c's, linked from their calls. */

/* The popups' state, the ui's. */
typedef struct wgf_ui_priv_popups_t {
    bool eaten; /* a press off an open dropdown, which closed it: no widget's, until let go */
    struct {    /* the open dropdown's list */
        char owner[64];      /* the dropdown's id; "" none open */
        uint32_t list, face; /* its list's Clay id and its face's, as last laid out */
        int hot, item;       /* the item stepped to, and the one a press began on (-1 none) */
        float pointer[2];    /* where the pointer was: one that moves over an item steps to it */
        bool seen;           /* given this frame */
    } dropdown;
    struct {
        bool seen, was;  /* given this frame, and the frame before */
        int first, last; /* this frame's widgets in it */
        char before[64]; /* the focus as it opened, given back as it closes */
    } modal;
    struct {
        char id[64];   /* the widget whose tooltip waits or shows */
        char next[64]; /* the first other one pointed at this frame */
        float since;   /* the clock as it was first pointed at */
        bool seen;     /* pointed at this frame */
    } tip;
} wgf_ui_priv_popups_t;

typedef struct wgf_ui_priv_popup_hooks_t {
    /* The frame's start, once Clay has the pointer: a press off an open dropdown's list and
     * face closes it and is eaten, the pointer's press, hold, and release cleared until it is
     * let go. */
    void (*begin)(wgf_ui_priv_popups_t *popups, bool *pressed, bool *down, bool *released);
    /* The frame's end, before the focus moves: a dropdown not given closed, and the tooltip
     * no longer pointed at let go of, the next one waiting from `clock`. */
    void (*end)(wgf_ui_priv_popups_t *popups, float clock);
    /* The focus (`focus`, at the frame's widget `at`, -1 none) held to a modal's widgets, `lo`
     * to `hi`: put on its first as it opens, and given back as it closes. Whether it was moved
     * into the modal, to be scrolled to. */
    bool (*focus)(wgf_ui_priv_popups_t *popups, char *focus, int *at, int *lo, int *hi);
} wgf_ui_priv_popup_hooks_t;

void wgf_ui_priv_set_popup_hooks(const wgf_ui_priv_popup_hooks_t *hooks); /* wgf_ui.c's */
void wgf_ui_priv_install_popups(void);                                    /* wgf_ui_popup.c's: sets them */

/* wgf_ui.c's, for the modal's focus: the frame's widget named `id` (-1 none), and the id of
 * the one at `index` ("" past them). */
int wgf_ui_priv_focusable_named(const char *id);
const char *wgf_ui_priv_focusable_id(int index);

#endif
