#include <stdio.h>
#include <string.h>

#include "wgf_log.h"

/* Logs go to stderr, so the test points stderr at a file and reads it back. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    static char text[16384];
    static char long_text[WGF_LOG_FORMAT_MAX];
    static char expected[WGF_LOG_FORMAT_MAX + 8];
    size_t n;
    FILE *f;
    const char *log_file = "wgf_core_log_test.txt";

    if (freopen(log_file, "w", stderr) == NULL) {
        printf("FAIL: couldn't redirect stderr\n");
        return 1;
    }

    wgf_log_info("info %d", 1);
    wgf_log_debug("dropped below the level");
    wgf_log_set_level(WGF_LOG_LEVEL_DEBUG);
    expect(wgf_log_get_level() == WGF_LOG_LEVEL_DEBUG, "get_level reads back set_level");
    wgf_log_debug("debug %s", "two");
    wgf_log_message(WGF_LOG_LEVEL_WARN, "no source 100%s");
    wgf_log_message_source(WGF_LOG_LEVEL_ERROR, "dir/where.c", 7, "with source");
    wgf_log_info("utf8: h\xC3\xA9llo \xE2\x9C\x93 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x8E\xAE");

    /* 2046 'a' then "é" (2 bytes): the formatter's 2047 bytes end inside the "é" */
    memset(long_text, 'a', WGF_LOG_FORMAT_MAX - 2);
    long_text[WGF_LOG_FORMAT_MAX - 2] = '\0';
    wgf_log_info("%s\xC3\xA9tail", long_text);
    wgf_log_message(WGF_LOG_LEVEL_WARN, "broken tail \xE2\x9C");
    fflush(stderr);

    f = fopen(log_file, "rb");
    if (f == NULL) {
        printf("FAIL: couldn't read %s\n", log_file);
        return 1;
    }
    n = fread(text, 1, sizeof(text) - 1, f);
    text[n] = '\0';
    fclose(f);
    { /* Windows writes stderr's line ends as \r\n */
        size_t from, to = 0;
        for (from = 0; from < n; from++) {
            if (text[from] != '\r' || text[from + 1] != '\n') text[to++] = text[from];
        }
        text[to] = '\0';
    }

    expect(strstr(text, "[INFO ] wgf_core_log_test.c:") != NULL, "INFO line names its file");
    expect(strstr(text, ": info 1\n") != NULL, "INFO message formatted");
    expect(strstr(text, "dropped") == NULL, "message below the level dropped");
    expect(strstr(text, "[DEBUG] ") != NULL && strstr(text, ": debug two\n") != NULL, "sugar logs after level lowered");
    expect(strstr(text, "[WARN ] no source 100%s\n") != NULL, "finished text is not a format");
    expect(strstr(text, "[ERROR] where.c:7: with source\n") != NULL, "message with a source, path trimmed");
    expect(strstr(text, ": utf8: h\xC3\xA9llo \xE2\x9C\x93 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x8E\xAE\n") != NULL,
           "UTF-8 passes through unchanged");
    snprintf(expected, sizeof(expected), ": %s\n", long_text);
    expect(strstr(text, expected) != NULL && strstr(text, "a\xC3") == NULL && strstr(text, "atail") == NULL, "long message cut before a split character");
    expect(strstr(text, "[WARN ] broken tail \n") != NULL, "a cut-off character at the end is dropped");

    return failures == 0 ? 0 : 1;
}
