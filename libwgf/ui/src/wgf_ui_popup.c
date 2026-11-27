#include <stdio.h>
#include <string.h>

#include "clay.h"
#include "wgf_ui_popup_priv.h"
#include "wgf_core_format_priv.h"

/* The Clay UI's popup layer, what each frame does for it (wgf_ui_popup_priv.h): reached
 * through the hooks the first dropdown, modal, or tooltip installs, so a UI without one
 * links none of it. */

static void begin(wgf_ui_priv_popups_t *popups, bool *pressed, bool *down, bool *released)
{
    popups->dropdown.seen = popups->modal.seen = false;
    popups->dropdown.was_open = popups->dropdown.owner[0] != '\0';
    if (popups->dropdown.owner[0] != '\0' && *pressed) { /* a press off an open dropdown closes it, and is eaten */
        Clay_ElementId list, face;
        memset(&list, 0, sizeof(list));
        memset(&face, 0, sizeof(face));
        list.id = popups->dropdown.list;
        face.id = popups->dropdown.face;
        if (!Clay_PointerOver(list) && !Clay_PointerOver(face)) {
            popups->dropdown.owner[0] = '\0';
            popups->eaten = true;
        }
    }
    if (popups->eaten) {
        if (*released) popups->eaten = false;
        *pressed = *down = *released = false;
    }
}

static void end(wgf_ui_priv_popups_t *popups, float clock)
{
    if (!popups->dropdown.seen) popups->dropdown.owner[0] = '\0'; /* its dropdown not given */
    if (!popups->tip.seen) { /* the tooltip pointed at is let go of: the next one waits */
        wgf_core_priv_format(popups->tip.id, sizeof(popups->tip.id), "%s", popups->tip.next);
        popups->tip.since = clock;
    }
    popups->tip.seen = false;
    popups->tip.next[0] = '\0';
}

static bool focus(wgf_ui_priv_popups_t *popups, char *focused, int *at, int *lo, int *hi)
{
    bool moved = false;
    if (popups->modal.seen) {
        *lo = popups->modal.first;
        *hi = popups->modal.last;
        if (!popups->modal.was) wgf_core_priv_format(popups->modal.before, sizeof(popups->modal.before), "%s", focused);
        if (*at < *lo || *at >= *hi) {
            *at = *lo < *hi ? *lo : -1;
            wgf_core_priv_format(focused, sizeof(popups->modal.before), "%s", wgf_ui_priv_focusable_id(*at));
            moved = true;
        }
    } else if (popups->modal.was && popups->modal.before[0] != '\0') {
        const int to = wgf_ui_priv_focusable_named(popups->modal.before);
        if (to >= 0) {
            wgf_core_priv_format(focused, sizeof(popups->modal.before), "%s", popups->modal.before);
            *at = to;
        }
    }
    popups->modal.was = popups->modal.seen;
    return moved;
}

static const wgf_ui_priv_popup_hooks_t hooks = {begin, end, focus};

void wgf_ui_priv_install_popups(void)
{
    wgf_ui_priv_set_popup_hooks(&hooks);
}
