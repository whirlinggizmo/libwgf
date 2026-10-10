#include "wgf_app_autopilot_source_priv.h"

#include <stdio.h>
#include <stdlib.h>

#include "wgf_log.h"

/* An autopilot, natively: the file LIBWGF_AUTOPILOT names, read at the runtime's start. A
 * tool's way in, not the program's, so it is read before the first frame rather than
 * as a task. */

#define AUTOPILOT_MAX_BYTES (1 << 20)

char *wgf_app_priv_autopilot_read_source(bool *named)
{
    const char *path = getenv("LIBWGF_AUTOPILOT");
    FILE *f;
    char *text;
    size_t n;
    *named = path != NULL && path[0] != '\0';
    if (!*named) return NULL;
    f = fopen(path, "rb");
    if (f == NULL) {
        wgf_log_error("wgf_autopilot: LIBWGF_AUTOPILOT names %s, which can't be opened", path);
        return NULL;
    }
    text = (char *)malloc(AUTOPILOT_MAX_BYTES + 1);
    n = text != NULL ? fread(text, 1, AUTOPILOT_MAX_BYTES + 1, f) : 0;
    fclose(f);
    if (text == NULL || n > AUTOPILOT_MAX_BYTES) {
        wgf_log_error("wgf_autopilot: %s is larger than 1 MB, or out of memory", path);
        free(text);
        return NULL;
    }
    text[n] = '\0';
    wgf_log_info("wgf_autopilot: running %s", path);
    return text;
}
