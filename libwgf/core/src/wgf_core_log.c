#include "wgf_log.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_core_os_priv.h"
#include "wgf_core_priv.h"
#include "wgf_core_thread_priv.h"

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

/* The last lines logged, kept for the program to read (wgf_log_set_history): a ring of
 * `capacity`, `count` of them from `first`, guarded once core has started (the load workers
 * log too). */
#define HISTORY_MAX 1024
#define LINE_MAX_BYTES 256
static struct {
    char (*lines)[LINE_MAX_BYTES];
    wgf_log_level_t *levels;
    int capacity, count, first;
    bool locking;
    wgf_core_priv_mutex_t lock;
    char read[LINE_MAX_BYTES]; /* a line handed out, a copy */
} history;

static void history_lock(void)
{
    if (history.locking) wgf_core_priv_mutex_lock(&history.lock);
}

static void history_unlock(void)
{
    if (history.locking) wgf_core_priv_mutex_unlock(&history.lock);
}

void wgf_core_priv_log_init(void)
{
    if (history.locking) return;
    wgf_core_priv_mutex_init(&history.lock);
    history.locking = true;
}

void wgf_core_priv_log_deinit(void)
{
    if (!history.locking) return;
    history.locking = false;
    wgf_core_priv_mutex_destroy(&history.lock);
}

/* A line kept, its oldest let go of when the ring is full. */
static void keep(wgf_log_level_t level, const char *file, int line, const char *text)
{
    int at;
    if (history.capacity == 0) return;
    history_lock();
    if (history.capacity > 0) {
        at = (history.first + history.count) % history.capacity;
        if (history.count == history.capacity) history.first = (history.first + 1) % history.capacity;
        else history.count++;
        if (file != NULL && file[0] != '\0') {
            snprintf(history.lines[at], LINE_MAX_BYTES, "%s:%d: %.*s", basename_of(file), line,
                     (int)utf8_whole_length(text), text);
        } else {
            snprintf(history.lines[at], LINE_MAX_BYTES, "%.*s", (int)utf8_whole_length(text), text);
        }
        history.levels[at] = level;
    }
    history_unlock();
}

bool wgf_log_set_history(int count)
{
    char (*lines)[LINE_MAX_BYTES] = NULL;
    wgf_log_level_t *levels = NULL;
    if (count < 0 || count > HISTORY_MAX) return false;
    if (count > 0) {
        lines = (char (*)[LINE_MAX_BYTES])calloc((size_t)count, LINE_MAX_BYTES);
        levels = (wgf_log_level_t *)calloc((size_t)count, sizeof(wgf_log_level_t));
        if (lines == NULL || levels == NULL) {
            free(lines);
            free(levels);
            return false;
        }
    }
    history_lock();
    free(history.lines);
    free(history.levels);
    history.lines = lines;
    history.levels = levels;
    history.capacity = count;
    history.count = history.first = 0;
    history_unlock();
    return true;
}

int wgf_log_get_history(void)
{
    return history.capacity;
}

int wgf_log_get_history_count(void)
{
    int n;
    history_lock();
    n = history.count;
    history_unlock();
    return n;
}

const char *wgf_log_get_history_line(int index)
{
    history_lock();
    if (index >= 0 && index < history.count) {
        memcpy(history.read, history.lines[(history.first + index) % history.capacity], LINE_MAX_BYTES);
    } else {
        history.read[0] = '\0';
    }
    history_unlock();
    return history.read;
}

wgf_log_level_t wgf_log_get_history_level(int index)
{
    wgf_log_level_t level = WGF_LOG_LEVEL_TRACE;
    history_lock();
    if (index >= 0 && index < history.count) level = history.levels[(history.first + index) % history.capacity];
    history_unlock();
    return level;
}

void wgf_log_clear_history(void)
{
    history_lock();
    history.count = history.first = 0;
    history_unlock();
}

static void emit(wgf_log_level_t level, const char *file, int line, const char *text)
{
    if (level < log_level || text == NULL) return;
    keep(level, file, line, text);
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
