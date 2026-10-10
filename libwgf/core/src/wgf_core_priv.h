#ifndef WGF_CORE_PRIV_H
#define WGF_CORE_PRIV_H

#include <stdbool.h>
#include <stddef.h>

/* core's start, stop, and update, run by app's runtime: start before anything
 * else, update once a frame, stop last. */

/* Start core: the clock, and file storage with its platform's default root.
 * Logging and the version work without it. Again while running: nothing. */
void wgf_core_priv_init(void);

/* Stop core. When it isn't running: nothing; init starts it again. */
void wgf_core_priv_shutdown(void);

/* Advance core's asynchronous work: tasks change status only here. When core
 * isn't running: nothing. */
void wgf_core_priv_update(void);

/* The program's identity (wgf_identity.h; libwgf/core/src/wgf_core_identity.c). `name` made one safe
 * path component into `out`, as the header says; false for a name refused (NULL, too
 * long, nothing left). And the directory the identity names under the user's cache,
 * <user cache>/<company>/<app> (Windows: .../cache under it), with "/"; false where
 * there is no user cache directory (the web). */
bool wgf_core_priv_app_clean_name(const char *name, char *out, size_t out_size);
bool wgf_core_priv_app_cache_dir(char *out, size_t out_size);
/* The program's own data directory: <user data>/<company>/<product>, with "/"; false
 * where there is no user data directory (the web). */
bool wgf_core_priv_app_data_dir(char *out, size_t out_size);

#endif
