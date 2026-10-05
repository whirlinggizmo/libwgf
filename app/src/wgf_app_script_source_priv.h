#ifndef WGF_APP_SCRIPT_SOURCE_PRIV_H
#define WGF_APP_SCRIPT_SOURCE_PRIV_H

#include <stdbool.h>

/* Where a scripted run's script comes from (wgf_app_script_priv.h): natively the file
 * LIBWGF_SCRIPT names (wgf_app_script_native.c), on the web Module["wgfScript"], the
 * text the page gave the module (wgf_app_script_web.c). Its text, malloc'd for the
 * caller to free; NULL when there is none, with `*named` true when one was named but
 * couldn't be read (logged why). */
char *wgf_app_priv_script_read_source(bool *named);

#endif
