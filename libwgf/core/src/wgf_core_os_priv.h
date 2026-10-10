#ifndef WGF_CORE_OS_PRIV_H
#define WGF_CORE_OS_PRIV_H

#include <stdbool.h>
#include <stddef.h>

/* The few OS calls that differ between POSIX (native and the web) and
 * Windows, so the code above them has no platform #if.
 * wgf_core_os_posix.c and wgf_core_os_windows.c implement them. */

/* Create one directory; false if it couldn't be made (it may already exist). */
bool wgf_core_priv_os_mkdir(const char *path);

/* Move `from` over `to` in one step, replacing what is there. */
bool wgf_core_priv_os_rename_replace(const char *from, const char *to);

/* The working directory, malloc'd (free it), or NULL. */
char *wgf_core_priv_os_getcwd(void);

/* Is `path` a directory? is_dir follows a symbolic link (or, on Windows, a
 * junction); is_real_dir doesn't, so a link to a directory isn't one. */
bool wgf_core_priv_os_is_dir(const char *path);
bool wgf_core_priv_os_is_real_dir(const char *path);

/* Remove the real directory `path` and everything in it. A link inside it is
 * removed itself, never followed, so nothing outside `path` is touched. False
 * when anything couldn't be removed. */
bool wgf_core_priv_os_remove_tree(const char *path);

/* Remove the directory `path` if it is empty; false otherwise. */
bool wgf_core_priv_os_rmdir(const char *path);

/* The user's cache directory, into `out`: $XDG_CACHE_HOME or ~/.cache on Linux,
 * ~/Library/Caches on macOS, %LOCALAPPDATA% on Windows. False where there is none (the
 * web, or no home). */
bool wgf_core_priv_os_user_cache_dir(char *out, size_t out_size);
/* The user's own data directory, where a program keeps what must last (saves,
 * settings): $XDG_DATA_HOME or ~/.local/share on Linux, ~/Library/Application Support
 * on macOS, %APPDATA% on Windows; false where there is none (the web). */
bool wgf_core_priv_os_user_data_dir(char *out, size_t out_size);

/* The running program's executable's name, without its directory or extension
 * ("game" for /opt/game/bin/game, C:\game\game.exe); false when it can't be told. */
bool wgf_core_priv_os_executable_name(char *out, size_t out_size);

/* The directory the running program's executable is in ("/opt/game/bin" for
 * /opt/game/bin/game; "C:/game" for C:\game\game.exe, with "/"), no trailing slash:
 * where a program's files are, wherever it was started from. False when it can't be
 * told (the web). */
bool wgf_core_priv_os_executable_dir(char *out, size_t out_size);

/* Change the working directory; false when it can't be. For tests of what doesn't
 * depend on it. */
bool wgf_core_priv_os_chdir(const char *path);

/* Have the console show UTF-8 written to stdout and stderr. Windows' console
 * reads them in the old code page otherwise; elsewhere it already does. */
void wgf_core_priv_os_console_utf8(void);

/* Sleep about `seconds` (0 or less: give up the rest of the time slice). OS sleeps
 * overshoot by up to a millisecond or two. */
void wgf_core_priv_os_sleep(double seconds);

#endif
