#ifndef WGF_LOG_H
#define WGF_LOG_H

#include <stdarg.h>
#include <stdio.h>

#include "wgf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Logging to stderr, or to the browser console on the web.
 *
 * A message below the current level is dropped. The level starts at INFO. Text is UTF-8.
 *
 * The exported calls take finished text, so a binding formats in its own language and
 * passes one string. C code formats with the wgf_log_<level> macros, such as wgf_log_debug,
 * from wgf_log.h. */

typedef enum wgf_log_level_t {
    WGF_LOG_LEVEL_TRACE = 0,
    WGF_LOG_LEVEL_DEBUG = 1,
    WGF_LOG_LEVEL_INFO = 2,
    WGF_LOG_LEVEL_WARN = 3,
    WGF_LOG_LEVEL_ERROR = 4,
    WGF_LOG_LEVEL_FATAL = 5
} wgf_log_level_t;

WGF_API void wgf_log_set_level(wgf_log_level_t level);
WGF_API wgf_log_level_t wgf_log_get_level(void);

/* Keeps the last lines logged for the program to read, such as for a game's console or a
 * bug report.
 *
 * - set_history keeps the last `count` lines, at most 1024 (default: 0, none). It forgets
 *   the lines already kept. It returns false for a count outside 0 to 1024.
 * - get_history_line returns a line as the console shows it, without its level, such as
 *   "file.c:12: text" (255 bytes at most). Index 0 is the oldest.
 * - get_history_level returns that line's level.
 * - For an index outside the lines kept, they return "" and TRACE.
 *
 * A line read is a copy, valid until the next read. Lines below the level aren't kept,
 * just as they aren't shown. */
WGF_API bool wgf_log_set_history(int count);
WGF_API int wgf_log_get_history(void);
WGF_API int wgf_log_get_history_count(void);
WGF_API const char *wgf_log_get_history_line(int index);
WGF_API wgf_log_level_t wgf_log_get_history_level(int index);
WGF_API void wgf_log_clear_history(void);

/* Logs one message. */
WGF_API void wgf_log_message(wgf_log_level_t level, const char *text);

/* Logs one message with the file and line it came from. */
WGF_API void wgf_log_message_source(wgf_log_level_t level, const char *file, int line,
                                            const char *text);

/* C only: formats a message like printf, then logs it with its source. A message longer
 * than the buffer is cut, and the cut never splits a UTF-8 character. */
#define WGF_LOG_FORMAT_MAX 2048

/* What formats it: a program's vsnprintf; inside libwgf (WGF_LIBRARY, defined for its own
 * sources alone) core's small formatter, so a page links no printf for libwgf's log lines. */
#ifdef WGF_LIBRARY
int wgf_core_priv_vformat(char *out, size_t size, const char *format, va_list args);
#define WGF_LOG_VFORMAT wgf_core_priv_vformat
#else
#define WGF_LOG_VFORMAT vsnprintf
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
static inline void wgf_log_format(wgf_log_level_t level, const char *file, int line,
                                      const char *format, ...)
{
    char text[WGF_LOG_FORMAT_MAX];
    va_list args;
    if (level < wgf_log_get_level()) return; /* skip formatting what would be dropped */
    va_start(args, format);
    WGF_LOG_VFORMAT(text, sizeof(text), format, args);
    va_end(args);
    wgf_log_message_source(level, file, line, text);
}

/* The file a line is logged from.
 *
 * Where the compiler gives the base name alone (__FILE_NAME__, in clang and GCC 12 and
 * later), that is used. It is all the log prints, so a build carries no source paths and is
 * the same bytes wherever it is made. Other compilers use __FILE__. */
#ifdef __FILE_NAME__
#define WGF_LOG_FILE __FILE_NAME__
#else
#define WGF_LOG_FILE __FILE__
#endif

#define wgf_log_trace(...) wgf_log_format(WGF_LOG_LEVEL_TRACE, WGF_LOG_FILE, __LINE__, __VA_ARGS__)
#define wgf_log_debug(...) wgf_log_format(WGF_LOG_LEVEL_DEBUG, WGF_LOG_FILE, __LINE__, __VA_ARGS__)
#define wgf_log_info(...) wgf_log_format(WGF_LOG_LEVEL_INFO, WGF_LOG_FILE, __LINE__, __VA_ARGS__)
#define wgf_log_warn(...) wgf_log_format(WGF_LOG_LEVEL_WARN, WGF_LOG_FILE, __LINE__, __VA_ARGS__)
#define wgf_log_error(...) wgf_log_format(WGF_LOG_LEVEL_ERROR, WGF_LOG_FILE, __LINE__, __VA_ARGS__)
#define wgf_log_fatal(...) wgf_log_format(WGF_LOG_LEVEL_FATAL, WGF_LOG_FILE, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
