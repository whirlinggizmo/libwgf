#ifndef WGF_PLATFORM_WINDOW_PRIV_H
#define WGF_PLATFORM_WINDOW_PRIV_H

#include <stdbool.h>

#include "wgf_platform_priv.h"

/* The window's settings, kept from before it opens, and whether it is open: set
 * before, they say how it opens; after, they change it. */

/* Fill in the window's part of what the platform opens. */
void wgf_platform_priv_window_describe(wgf_platform_priv_desc_t *desc);
/* The window opened (true) or closed (false). Opening applies what can only be
 * set on an open window, such as the mouse lock. */
void wgf_platform_priv_window_set_open(bool open);
bool wgf_platform_priv_window_is_open(void);

#endif
