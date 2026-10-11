#ifndef WGF_LOG_H
#define WGF_LOG_H

#include <stdarg.h>
#include <stdio.h>

#include "wgf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Logging to stderr (the browser console on the web). A message below the
 * current level is dropped; the level starts at INFO. Text is UTF-8.
 *
 * The exported calls take finished text, so a binding formats in its own
 * language and passes one string. C code formats with the wgf_log_<level> macros,
 * or with wgf_log_debug and the rest from wgf_log.h. */

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

/* Log one message. */
WGF_API void wgf_log_message(wgf_log_level_t level, const char *text);

/* Log one message with the file and line it came from. */
WGF_API void wgf_log_message_source(wgf_log_level_t level, const char *file, int line,
                                            const char *text);

/* C only: format like printf, then log with the source. A message longer than
 * the buffer is cut, and the cut never splits a UTF-8 character. */
#define WGF_LOG_FORMAT_MAX 2048

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
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    wgf_log_message_source(level, file, line, text);
}

/* The file a line is logged from: its base name where the compiler gives one alone
 * (__FILE_NAME__: clang, GCC 12 and later), which is all the log prints, so a build
 * carries no source paths, and is the same bytes wherever it is made; else __FILE__. */
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
