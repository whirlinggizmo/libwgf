#include "wgf_log.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wgf_core_os_priv.h"

static wgf_log_level_t log_level = WGF_LOG_LEVEL_INFO;
static bool console_ready;

static const char *level_name(wgf_log_level_t level)
{
    switch (level) {
        case WGF_LOG_LEVEL_TRACE: return "TRACE";
        case WGF_LOG_LEVEL_DEBUG: return "DEBUG";
        case WGF_LOG_LEVEL_INFO:  return "INFO";
        case WGF_LOG_LEVEL_WARN:  return "WARN";
        case WGF_LOG_LEVEL_ERROR: return "ERROR";
        case WGF_LOG_LEVEL_FATAL: return "FATAL";
        default:                      return "INFO";
    }
}

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\'); /* MSVC's __FILE__ is a Windows path */
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    return slash != NULL ? slash + 1 : path;
}

/* The length of `text` without a UTF-8 character cut off at its end, as a
 * message cut to fit a buffer can be. */
static size_t utf8_whole_length(const char *text)
{
    const size_t n = strlen(text);
    size_t lead = n;
    size_t need;
    unsigned char c;

    /* back over up to three continuation bytes (10xxxxxx) to the lead byte */
    while (lead > 0 && n - lead < 4 && ((unsigned char)text[lead - 1] & 0xC0) == 0x80) lead--;
    if (lead == 0 || n - lead >= 4) return n; /* no lead byte: not ours to fix */
    lead--;
    c = (unsigned char)text[lead];
    if (c < 0x80) return n;
    if ((c & 0xE0) == 0xC0) need = 2;
    else if ((c & 0xF0) == 0xE0) need = 3;
    else if ((c & 0xF8) == 0xF0) need = 4;
    else return n;
    return (lead + need > n) ? lead : n;
}

static void emit(wgf_log_level_t level, const char *file, int line, const char *text)
{
    if (level < log_level || text == NULL) return;
    if (!console_ready) {
        wgf_core_priv_os_console_utf8();
        console_ready = true;
    }

    if (file != NULL && file[0] != '\0') {
        fprintf(stderr, "[%-5s] %s:%d: %.*s\n", level_name(level), basename_of(file), line,
                (int)utf8_whole_length(text), text);
    } else {
        fprintf(stderr, "[%-5s] %.*s\n", level_name(level), (int)utf8_whole_length(text), text);
    }
}

void wgf_log_set_level(wgf_log_level_t level)
{
    log_level = level;
}

wgf_log_level_t wgf_log_get_level(void)
{
    return log_level;
}

void wgf_log_message(wgf_log_level_t level, const char *text)
{
    emit(level, NULL, 0, text);
}

void wgf_log_message_source(wgf_log_level_t level, const char *file, int line, const char *text)
{
    emit(level, file, line, text);
}
