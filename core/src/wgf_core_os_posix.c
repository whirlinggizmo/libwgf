#define _POSIX_C_SOURCE 200809L

#include "wgf_core_os_priv.h"

#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h> /* _NSGetExecutablePath */
#endif

bool wgf_core_priv_os_mkdir(const char *path)
{
    return mkdir(path, 0755) == 0;
}

bool wgf_core_priv_os_rename_replace(const char *from, const char *to)
{
    return rename(from, to) == 0; /* replaces atomically on POSIX */
}

char *wgf_core_priv_os_getcwd(void)
{
    return getcwd(NULL, 0);
}

void wgf_core_priv_os_console_utf8(void)
{
    /* terminals and the browser console already read UTF-8 */
}

void wgf_core_priv_os_sleep(double seconds)
{
    struct timespec ts = {0, 0};
    if (seconds > 0.0) {
        ts.tv_sec = (time_t)seconds;
        ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    }
    nanosleep(&ts, NULL);
}

bool wgf_core_priv_os_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool wgf_core_priv_os_is_real_dir(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool wgf_core_priv_os_remove_tree(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    bool ok = true;
    if (dir == NULL) return false;
    while ((entry = readdir(dir)) != NULL) {
        char child[1100];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if ((size_t)snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= sizeof(child)) {
            ok = false;
            continue;
        }
        if (wgf_core_priv_os_is_real_dir(child)) {
            ok = wgf_core_priv_os_remove_tree(child) && ok;
        } else {
            ok = (remove(child) == 0) && ok;
        }
    }
    closedir(dir);
    return (rmdir(path) == 0) && ok;
}

bool wgf_core_priv_os_rmdir(const char *path)
{
    return rmdir(path) == 0;
}

bool wgf_core_priv_os_user_cache_dir(char *out, size_t out_size)
{
#if defined(__EMSCRIPTEN__)
    (void)out;
    (void)out_size;
    return false; /* the browser caches */
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    return home != NULL && home[0] != '\0' && snprintf(out, out_size, "%s/Library/Caches", home) < (int)out_size;
#else
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    if (xdg != NULL && xdg[0] == '/') return snprintf(out, out_size, "%s", xdg) < (int)out_size;
    return home != NULL && home[0] != '\0' && snprintf(out, out_size, "%s/.cache", home) < (int)out_size;
#endif
}

bool wgf_core_priv_os_user_data_dir(char *out, size_t out_size)
{
#if defined(__EMSCRIPTEN__)
    (void)out;
    (void)out_size;
    return false; /* the browser keeps a site's files */
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    return home != NULL && home[0] != '\0' &&
           snprintf(out, out_size, "%s/Library/Application Support", home) < (int)out_size;
#else
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (xdg != NULL && xdg[0] == '/') return snprintf(out, out_size, "%s", xdg) < (int)out_size;
    return home != NULL && home[0] != '\0' && snprintf(out, out_size, "%s/.local/share", home) < (int)out_size;
#endif
}

/* The executable's path into `path`; false where it can't be told. */
static bool executable_path(char *path, size_t size)
{
#if defined(__EMSCRIPTEN__)
    (void)path;
    (void)size;
    return false;
#elif defined(__APPLE__)
    uint32_t n = (uint32_t)size;
    return _NSGetExecutablePath(path, &n) == 0;
#else
    const ssize_t n = readlink("/proc/self/exe", path, size - 1);
    if (n <= 0) return false;
    path[n] = '\0';
    return true;
#endif
}

bool wgf_core_priv_os_executable_dir(char *out, size_t out_size)
{
    char path[4096];
    const char *slash;
    if (!executable_path(path, sizeof(path))) return false;
    slash = strrchr(path, '/');
    if (slash == NULL) return false;
    return snprintf(out, out_size, "%.*s", slash == path ? 1 : (int)(slash - path), path) < (int)out_size;
}

bool wgf_core_priv_os_chdir(const char *path)
{
    return chdir(path) == 0;
}

bool wgf_core_priv_os_executable_name(char *out, size_t out_size)
{
    char path[4096];
    const char *name, *dot;
    if (!executable_path(path, sizeof(path))) return false;
    name = strrchr(path, '/');
    name = name != NULL ? name + 1 : path;
    dot = strrchr(name, '.');
    if (name[0] == '\0') return false;
    return snprintf(out, out_size, "%.*s", dot != NULL && dot != name ? (int)(dot - name) : (int)strlen(name), name) <
           (int)out_size;
}

