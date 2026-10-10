#include "wgf_core_os_priv.h"

#include <direct.h>  /* _mkdir: Windows' mkdir takes no mode; _getcwd, _rmdir */
#include <io.h>      /* _findfirst and friends, to walk a directory */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <windows.h> /* MoveFileExA: rename there won't replace a file */

bool wgf_core_priv_os_mkdir(const char *path)
{
    return _mkdir(path) == 0;
}

bool wgf_core_priv_os_rename_replace(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

char *wgf_core_priv_os_getcwd(void)
{
    return _getcwd(NULL, 0); /* same NULL, 0 -> malloc contract as POSIX */
}

void wgf_core_priv_os_console_utf8(void)
{
    SetConsoleOutputCP(CP_UTF8);
}

void wgf_core_priv_os_sleep(double seconds)
{
    Sleep(seconds > 0.0 ? (DWORD)(seconds * 1000.0) : 0);
}

bool wgf_core_priv_os_is_dir(const char *path)
{
    struct _stat st;
    return _stat(path, &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
}

bool wgf_core_priv_os_is_real_dir(const char *path)
{
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0; /* a symbolic link or junction isn't one */
}

bool wgf_core_priv_os_remove_tree(const char *path)
{
    char pattern[1100];
    struct _finddata_t entry;
    intptr_t find;
    bool ok = true;
    if ((size_t)snprintf(pattern, sizeof(pattern), "%s/*", path) >= sizeof(pattern)) return false;
    find = _findfirst(pattern, &entry);
    if (find == -1) return false;
    do {
        char child[1100];
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) continue;
        if ((size_t)snprintf(child, sizeof(child), "%s/%s", path, entry.name) >= sizeof(child)) {
            ok = false;
            continue;
        }
        if (wgf_core_priv_os_is_real_dir(child)) {
            ok = wgf_core_priv_os_remove_tree(child) && ok;
        } else if ((entry.attrib & _A_SUBDIR) != 0) {
            ok = (_rmdir(child) == 0) && ok; /* a link to a directory: remove the link, not its target */
        } else {
            ok = (remove(child) == 0) && ok;
        }
    } while (_findnext(find, &entry) == 0);
    _findclose(find);
    return (_rmdir(path) == 0) && ok;
}

bool wgf_core_priv_os_rmdir(const char *path)
{
    return _rmdir(path) == 0;
}

bool wgf_core_priv_os_user_cache_dir(char *out, size_t out_size)
{
    char local[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", local, (DWORD)sizeof(local));
    return n > 0 && n < sizeof(local) && snprintf(out, out_size, "%s", local) < (int)out_size;
}

bool wgf_core_priv_os_user_data_dir(char *out, size_t out_size)
{
    char roaming[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("APPDATA", roaming, (DWORD)sizeof(roaming));
    return n > 0 && n < sizeof(roaming) && snprintf(out, out_size, "%s", roaming) < (int)out_size;
}

bool wgf_core_priv_os_executable_dir(char *out, size_t out_size)
{
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    char *slash;
    if (n == 0 || n >= sizeof(path)) return false;
    for (char *c = path; *c != '\0'; c++) {
        if (*c == '\\') *c = '/'; /* Windows takes either, and fs joins at "/" */
    }
    slash = strrchr(path, '/');
    if (slash == NULL) return false;
    return snprintf(out, out_size, "%.*s", (int)(slash - path), path) < (int)out_size;
}

bool wgf_core_priv_os_chdir(const char *path)
{
    return _chdir(path) == 0;
}

bool wgf_core_priv_os_executable_name(char *out, size_t out_size)
{
    char path[MAX_PATH];
    const char *name, *dot;
    const DWORD n = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    if (n == 0 || n >= sizeof(path)) return false;
    name = strrchr(path, '\\');
    name = name != NULL ? name + 1 : path;
    dot = strrchr(name, '.');
    if (name[0] == '\0') return false;
    return snprintf(out, out_size, "%.*s", dot != NULL && dot != name ? (int)(dot - name) : (int)strlen(name), name) <
           (int)out_size;
}

