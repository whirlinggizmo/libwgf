#ifndef WGF_APP_AUTOPILOT_SOURCE_PRIV_H
#define WGF_APP_AUTOPILOT_SOURCE_PRIV_H

#include <stdbool.h>

/* Where an autopilot run's autopilot comes from (wgf_app_autopilot_priv.h): natively the file
 * LIBWGF_AUTOPILOT names (wgf_app_autopilot_native.c), on the web Module["wgfAutopilot"], the
 * text the page gave the module (wgf_app_autopilot_web.c). Its text, malloc'd for the
 * caller to free; NULL when there is none, with `*named` true when one was named but
 * couldn't be read (logged why). */
char *wgf_app_priv_autopilot_read_source(bool *named);

#endif
