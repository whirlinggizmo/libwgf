#include "wgf_app_script_source_priv.h"

#include <stdio.h>
#include <stdlib.h>

#include "wgf_log.h"

/* A script, natively: the file LIBWGF_SCRIPT names, read at the runtime's start. A
 * tool's way in, not the program's, so it is read before the first frame rather than
 * as a task. */

#define SCRIPT_MAX_BYTES (1 << 20)

char *wgf_app_priv_script_read_source(bool *named)
{
    const char *path = getenv("LIBWGF_SCRIPT");
    FILE *f;
    char *text;
    size_t n;
    *named = path != NULL && path[0] != '\0';
    if (!*named) return NULL;
    f = fopen(path, "rb");
    if (f == NULL) {
        wgf_log_error("wgf_script: LIBWGF_SCRIPT names %s, which can't be opened", path);
        return NULL;
    }
    text = (char *)malloc(SCRIPT_MAX_BYTES + 1);
    n = text != NULL ? fread(text, 1, SCRIPT_MAX_BYTES + 1, f) : 0;
    fclose(f);
    if (text == NULL || n > SCRIPT_MAX_BYTES) {
        wgf_log_error("wgf_script: %s is larger than 1 MB, or out of memory", path);
        free(text);
        return NULL;
    }
    text[n] = '\0';
    wgf_log_info("wgf_script: running %s", path);
    return text;
}
