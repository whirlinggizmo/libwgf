#ifndef WGF_APP_PRIV_H
#define WGF_APP_PRIV_H

/* What the runtime draws over a program's frame, after its frame callback and before
 * gfx's end: the debug overlay's drawing (wgf_app_debug.c), NULL for none. Set only by
 * a call that shows one, so a program that never does links none of its drawing. */
typedef void (*wgf_app_priv_overlay_t)(void);
void wgf_app_priv_set_overlay(wgf_app_priv_overlay_t overlay);

#endif
