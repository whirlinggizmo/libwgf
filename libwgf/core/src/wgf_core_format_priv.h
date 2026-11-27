#ifndef WGF_CORE_FORMAT_PRIV_H
#define WGF_CORE_FORMAT_PRIV_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Text formatted and read by libwgf's own small code, not the C library's (wgf_core_format.c):
 * a page that formats a log line or reads a number would otherwise link the C library's whole
 * printf, scanf, and strtod, its long-double float parsing and printing among them, about
 * 15 to 20 KB of wasm gzip. Every source in libwgf formats and reads through these. */

/* snprintf's: at most `size` bytes written, the last a 0, and the length the whole would
 * have had. The conversions libwgf uses: d i u x X c s p % with the flags - 0 + and space,
 * a width and a precision (numbers or *), the lengths hh h l ll z j t; and f e g (and their
 * capitals) for doubles. Floats are worked in doubles: %f and %e exact while the digits fit
 * 17 significant ones, %g's last of 17 digits may differ from the C library's, but what
 * %.17g writes reads back the same (wgf_core_priv_parse_double). nan and inf as "nan" and
 * "inf". */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
int wgf_core_priv_format(char *out, size_t size, const char *format, ...);
int wgf_core_priv_vformat(char *out, size_t size, const char *format, va_list args);

/* sscanf's, for what libwgf reads: %d (an int) and %s (a word, at most its width's bytes,
 * the width required); a space in `pattern` skips any spaces and tabs, and anything else
 * must match. The count of conversions made, stopping at the first that fails. */
int wgf_core_priv_scan(const char *text, const char *pattern, ...);

/* strtod's for decimals: spaces, a sign, digits with a point, and an exponent. Exact (the
 * nearest double) for up to 19 significant digits and a power of ten that is exact itself
 * (up to 10^22 either way, as most numbers a game reads are), else within a few units in
 * its last place. `*end`, when not NULL, is where reading stopped: `text` itself when it
 * holds no number. Infinities, nan, and hexadecimal aren't read. */
double wgf_core_priv_parse_double(const char *text, char **end);

#ifdef __cplusplus
}
#endif

#endif
