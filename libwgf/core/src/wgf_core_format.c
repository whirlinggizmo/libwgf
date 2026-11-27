#include "wgf_core_format_priv.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* libwgf's own formatting and reading of text (wgf_core_format_priv.h says why). */

/* Powers of ten a double holds exactly. */
static const double exact_tens[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
#define EXACT_TENS 22

/* 10^n for any n a double reaches: exact up to 22 either way, else multiplied out of exact
 * steps. */
static double ten_to(int n)
{
    double value = 1.0;
    const bool down = n < 0;
    if (down) n = -n;
    while (n > EXACT_TENS) {
        value *= exact_tens[EXACT_TENS];
        n -= EXACT_TENS;
    }
    value *= exact_tens[n];
    return down ? 1.0 / value : value;
}

/* Double-doubles: hi + lo, lo the part hi rounded away, so a power of ten applied in steps
 * keeps what a double alone would lose (Dekker's products, Knuth's sums, without fma). */
static void two_sum(double a, double b, double *hi, double *lo)
{
    const double s = a + b, v = s - a;
    *hi = s;
    *lo = (a - (s - v)) + (b - v);
}

static void two_product(double a, double b, double *hi, double *lo)
{
    const double split = 134217729.0; /* 2^27 + 1 */
    const double ca = split * a, cb = split * b;
    const double a_hi = ca - (ca - a), a_lo = a - a_hi, b_hi = cb - (cb - b), b_lo = b - b_hi;
    *hi = a * b;
    *lo = ((a_hi * b_hi - *hi) + a_hi * b_lo + a_lo * b_hi) + a_lo * b_lo;
}

/* (hi, lo) * 10^n, in steps of exact powers. */
static void scale_by_ten(double *hi, double *lo, int n)
{
    while (n != 0) {
        const int step = n > EXACT_TENS ? EXACT_TENS : n < -EXACT_TENS ? -EXACT_TENS : n;
        const double p = exact_tens[step < 0 ? -step : step];
        double h, l;
        if (step > 0) {
            two_product(*hi, p, &h, &l);
            l += *lo * p;
        } else { /* q = hi / p, and what is left of hi + lo after q * p, over p */
            double ph, pl;
            h = *hi / p;
            two_product(h, p, &ph, &pl);
            l = (((*hi - ph) - pl) + *lo) / p;
        }
        two_sum(h, l, hi, lo);
        n -= step;
    }
}

/* hi + lo (>= 0, under 2^63) to the nearest whole number, a half to the even one. */
static uint64_t rounded_pair(double hi, double lo)
{
    uint64_t r = (uint64_t)hi;
    double rest = (hi - (double)r) + lo;
    while (rest < 0.0 && r > 0) {
        r--;
        rest += 1.0;
    }
    while (rest >= 1.0) {
        r++;
        rest -= 1.0;
    }
    if (rest > 0.5 || (rest == 0.5 && (r & 1) != 0)) r++;
    return r;
}

/* x * 10^n (x >= 0), rounded to a whole number as above. */
static uint64_t scaled_whole(double x, int n)
{
    double hi = x, lo = 0.0;
    scale_by_ten(&hi, &lo, n);
    return rounded_pair(hi, lo);
}

static bool is_nan(double x)
{
    return x != x;
}

static bool is_infinite(double x)
{
    return !is_nan(x) && is_nan(x - x);
}

/* ---- writing ---- */

typedef struct writer_t {
    char *out;
    size_t size;
    size_t length; /* what the whole would have, past `size` too */
} writer_t;

static void put(writer_t *w, char c)
{
    if (w->length + 1 < w->size) w->out[w->length] = c;
    w->length++;
}

static void put_many(writer_t *w, char c, int count)
{
    while (count-- > 0) put(w, c);
}

static void put_text(writer_t *w, const char *text, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) put(w, text[i]);
}

typedef struct spec_t {
    bool left, zero, plus, space, alternate;
    int width;     /* 0: none */
    int precision; /* -1: none */
} spec_t;

/* `body` (`n` bytes) with its sign (or none) and padding as `spec` says; `zeros`, whether a
 * 0 flag pads with zeros between the sign and the body (numbers but not nan or inf). */
static void put_field(writer_t *w, const spec_t *spec, char sign, const char *body, size_t n, bool zeros)
{
    const int length = (int)n + (sign != 0 ? 1 : 0);
    const int pad = spec->width > length ? spec->width - length : 0;
    if (!spec->left && !(spec->zero && zeros)) put_many(w, ' ', pad);
    if (sign != 0) put(w, sign);
    if (!spec->left && spec->zero && zeros) put_many(w, '0', pad);
    put_text(w, body, n);
    if (spec->left) put_many(w, ' ', pad);
}

static char sign_of(bool negative, const spec_t *spec)
{
    return negative ? '-' : spec->plus ? '+' : spec->space ? ' ' : 0;
}

static void put_integer(writer_t *w, const spec_t *spec, uint64_t magnitude, bool negative, int base, bool upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char reversed[24], body[96];
    int count = 0, n = 0, i;
    while (magnitude > 0) {
        reversed[count++] = digits[magnitude % (unsigned)base];
        magnitude /= (unsigned)base;
    }
    for (i = count; i < spec->precision && n < (int)sizeof(body) - 24; i++) body[n++] = '0';
    if (count == 0 && spec->precision != 0) body[n++] = '0'; /* %.0d of 0 is no digits */
    while (count > 0) body[n++] = reversed[--count];
    put_field(w, spec, sign_of(negative, spec), body, (size_t)n, spec->precision < 0);
}

/* The decimal digits of x (finite, > 0), rounded to `count` significant ones (1 to 17), into
 * `digits`; the power of ten of the first. */
static int significant(double x, int count, char *digits)
{
    uint64_t r, limit = 1, floor_of = 1;
    int e = 0, i;
    for (i = 0; i < count; i++) limit *= 10;
    floor_of = limit / 10;
    /* the first digit's power, estimated from the binary exponent, then corrected */
    {
        double y = x;
        while (y >= 1e16) {
            y /= 1e16;
            e += 16;
        }
        while (y < 1.0) {
            y *= 1e16;
            e -= 16;
        }
        while (y >= 10.0) {
            y /= 10.0;
            e += 1;
        }
    }
    r = scaled_whole(x, count - 1 - e);
    for (i = 0; r >= limit && i < 4; i++) { /* the estimate a power too low, or rounding carried */
        e++;
        r = scaled_whole(x, count - 1 - e);
        if (r >= limit) r = limit / 10; /* 9.99... rounded up: 10.0, one place on */
    }
    for (i = 0; r < floor_of && r > 0 && i < 4; i++) { /* the estimate a power too high */
        e--;
        r = scaled_whole(x, count - 1 - e);
    }
    for (i = count - 1; i >= 0; i--) {
        digits[i] = (char)('0' + r % 10);
        r /= 10;
    }
    return e;
}

/* x (finite, >= 0) as %f with `precision` decimals into body; its length. */
static int fixed(double x, int precision, bool alternate, char *body, int room)
{
    int n = 0, i;
    if (precision > 40) precision = 40;
    if (x < 1e17 && x * ten_to(precision) < 1.8e19) { /* whole in a uint64: exact */
        const uint64_t scale = (uint64_t)(ten_to(precision) + 0.5);
        const uint64_t r = scaled_whole(x, precision);
        uint64_t whole = r / scale, part = r % scale;
        char reversed[24];
        int count = 0;
        do {
            reversed[count++] = (char)('0' + whole % 10);
            whole /= 10;
        } while (whole > 0);
        while (count > 0) body[n++] = reversed[--count];
        if (precision > 0 || alternate) body[n++] = '.';
        for (i = precision - 1; i >= 0; i--) {
            body[n + i] = (char)('0' + part % 10);
            part /= 10;
        }
        return n + precision;
    } else { /* past what a uint64 holds: 17 significant digits, then zeros */
        char digits[17];
        const int e = x > 0.0 ? significant(x, 17, digits) : 0;
        for (i = 0; i <= e && n < room - precision - 2; i++) body[n++] = i < 17 ? digits[i] : '0';
        if (precision > 0 || alternate) body[n++] = '.';
        for (i = 0; i < precision; i++) body[n++] = '0';
        return n;
    }
}

/* x (finite, >= 0) as %e with `precision` decimals (at most 16) into body; its length. */
static int exponential(double x, int precision, bool alternate, bool upper, char *body)
{
    char digits[17];
    int n = 0, i, e = 0, magnitude;
    if (precision > 16) precision = 16;
    if (x > 0.0) {
        e = significant(x, precision + 1, digits);
    } else {
        memset(digits, '0', sizeof(digits));
    }
    body[n++] = digits[0];
    if (precision > 0 || alternate) body[n++] = '.';
    for (i = 1; i <= precision; i++) body[n++] = digits[i];
    body[n++] = upper ? 'E' : 'e';
    body[n++] = e < 0 ? '-' : '+';
    magnitude = e < 0 ? -e : e;
    if (magnitude >= 100) body[n++] = (char)('0' + magnitude / 100);
    body[n++] = (char)('0' + magnitude / 10 % 10);
    body[n++] = (char)('0' + magnitude % 10);
    return n;
}

/* x (finite, >= 0) as %g with `precision` significant digits (at most 17) into body. */
static int general(double x, int precision, bool alternate, bool upper, char *body)
{
    char digits[17];
    int n = 0, e = 0, i, last;
    if (precision == 0) precision = 1;
    if (precision > 17) precision = 17;
    if (x > 0.0) {
        e = significant(x, precision, digits);
    } else {
        memset(digits, '0', sizeof(digits));
    }
    if (e < -4 || e >= precision) {
        n = exponential(x, precision - 1, alternate, upper, body);
        if (!alternate) { /* trailing zeros of the mantissa go */
            const char *mark = memchr(body, upper ? 'E' : 'e', (size_t)n);
            int at = (int)(mark - body), cut = at;
            if (memchr(body, '.', (size_t)at) != NULL) {
                while (cut > 0 && body[cut - 1] == '0') cut--;
                if (cut > 0 && body[cut - 1] == '.') cut--;
                memmove(body + cut, body + at, (size_t)(n - at));
                n -= at - cut;
            }
        }
        return n;
    }
    /* fixed: e+1 digits before the point (or "0" and zeros after it) */
    last = precision - 1;
    if (!alternate) {
        while (last > e && last > 0 && digits[last] == '0') last--;
    }
    if (e >= 0) {
        for (i = 0; i <= e; i++) body[n++] = digits[i];
        if (last > e || alternate) body[n++] = '.';
        for (i = e + 1; i <= last; i++) body[n++] = digits[i];
    } else {
        body[n++] = '0';
        body[n++] = '.';
        for (i = -1; i > e; i--) body[n++] = '0';
        for (i = 0; i <= last; i++) body[n++] = digits[i];
    }
    return n;
}

static void put_double(writer_t *w, const spec_t *spec, double x, char conversion)
{
    char body[400];
    const bool upper = conversion >= 'A' && conversion <= 'Z';
    const char lower = (char)(upper ? conversion - 'A' + 'a' : conversion);
    const bool negative = x < 0.0 || (x == 0.0 && 1.0 / x < 0.0);
    const int precision = spec->precision < 0 ? 6 : spec->precision;
    int n;
    if (is_nan(x) || is_infinite(x)) {
        put_field(w, spec, is_nan(x) ? sign_of(false, spec) : sign_of(negative, spec),
                  is_nan(x) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf"), 3, false);
        return;
    }
    if (negative) x = -x;
    n = lower == 'f'   ? fixed(x, precision, spec->alternate, body, (int)sizeof(body))
        : lower == 'e' ? exponential(x, precision, spec->alternate, upper, body)
                       : general(x, precision, spec->alternate, upper, body);
    put_field(w, spec, sign_of(negative, spec), body, (size_t)n, true);
}

int wgf_core_priv_vformat(char *out, size_t size, const char *format, va_list args)
{
    writer_t w;
    const char *p = format;
    w.out = out;
    w.size = size;
    w.length = 0;
    while (*p != '\0') {
        spec_t spec;
        int length = 0; /* 1 l, 2 ll, -1 h, -2 hh, 3 z j t */
        if (*p != '%') {
            put(&w, *p++);
            continue;
        }
        p++;
        memset(&spec, 0, sizeof(spec));
        spec.precision = -1;
        for (;; p++) {
            if (*p == '-') spec.left = true;
            else if (*p == '0') spec.zero = true;
            else if (*p == '+') spec.plus = true;
            else if (*p == ' ') spec.space = true;
            else if (*p == '#') spec.alternate = true;
            else break;
        }
        if (*p == '*') {
            spec.width = va_arg(args, int);
            if (spec.width < 0) {
                spec.left = true;
                spec.width = -spec.width;
            }
            p++;
        } else {
            while (*p >= '0' && *p <= '9') spec.width = spec.width * 10 + (*p++ - '0');
        }
        if (*p == '.') {
            p++;
            spec.precision = 0;
            if (*p == '*') {
                spec.precision = va_arg(args, int);
                if (spec.precision < 0) spec.precision = -1;
                p++;
            } else {
                while (*p >= '0' && *p <= '9') spec.precision = spec.precision * 10 + (*p++ - '0');
            }
        }
        if (*p == 'h') length = *++p == 'h' ? (p++, -2) : -1;
        else if (*p == 'l') length = *++p == 'l' ? (p++, 2) : 1;
        else if (*p == 'z' || *p == 'j' || *p == 't') length = 3, p++;
        else if (*p == 'L') p++;
        switch (*p) {
        case 'd':
        case 'i': {
            const int64_t v = length == 2   ? (int64_t)va_arg(args, long long)
                              : length == 1 ? (int64_t)va_arg(args, long)
                              : length == 3 ? (int64_t)va_arg(args, ptrdiff_t)
                              : length == -1 ? (int64_t)(short)va_arg(args, int)
                              : length == -2 ? (int64_t)(signed char)va_arg(args, int)
                                             : (int64_t)va_arg(args, int);
            put_integer(&w, &spec, v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v, v < 0, 10, false);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            const uint64_t v = length == 2   ? (uint64_t)va_arg(args, unsigned long long)
                               : length == 1 ? (uint64_t)va_arg(args, unsigned long)
                               : length == 3 ? (uint64_t)va_arg(args, size_t)
                               : length == -1 ? (uint64_t)(unsigned short)va_arg(args, unsigned)
                               : length == -2 ? (uint64_t)(unsigned char)va_arg(args, unsigned)
                                              : (uint64_t)va_arg(args, unsigned);
            spec.plus = spec.space = false;
            put_integer(&w, &spec, v, false, *p == 'u' ? 10 : *p == 'o' ? 8 : 16, *p == 'X');
            break;
        }
        case 'p': {
            spec.plus = spec.space = false;
            put(&w, '0');
            put(&w, 'x');
            put_integer(&w, &spec, (uint64_t)(uintptr_t)va_arg(args, void *), false, 16, false);
            break;
        }
        case 'c': {
            const char c = (char)va_arg(args, int);
            spec.plus = spec.space = false;
            put_field(&w, &spec, 0, &c, 1, false);
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            size_t n = 0;
            if (s == NULL) s = "(null)";
            while (s[n] != '\0' && (spec.precision < 0 || n < (size_t)spec.precision)) n++;
            spec.plus = spec.space = false;
            put_field(&w, &spec, 0, s, n, false);
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            put_double(&w, &spec, va_arg(args, double), *p);
            break;
        case '%':
            put(&w, '%');
            break;
        case '\0':
            p--; /* a lone % at the end */
            break;
        default: /* a conversion it doesn't know: written as it is */
            put(&w, '%');
            put(&w, *p);
            break;
        }
        p++;
    }
    if (size > 0) out[w.length < size ? w.length : size - 1] = '\0';
    return (int)w.length;
}

int wgf_core_priv_format(char *out, size_t size, const char *format, ...)
{
    va_list args;
    int n;
    va_start(args, format);
    n = wgf_core_priv_vformat(out, size, format, args);
    va_end(args);
    return n;
}

/* ---- reading ---- */

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

double wgf_core_priv_parse_double(const char *text, char **end)
{
    const char *p = text;
    uint64_t mantissa = 0;
    int significant_digits = 0, exponent = 0;
    bool negative = false, any = false;
    double value;
    while (is_space(*p)) p++;
    if (*p == '-' || *p == '+') negative = *p++ == '-';
    for (; *p >= '0' && *p <= '9'; p++, any = true) {
        if (significant_digits < 19) {
            mantissa = mantissa * 10 + (uint64_t)(*p - '0');
            if (mantissa > 0) significant_digits++;
        } else {
            exponent++; /* a digit past 19 in the whole part: its place only */
        }
    }
    if (*p == '.') {
        for (p++; *p >= '0' && *p <= '9'; p++, any = true) {
            if (significant_digits < 19) {
                mantissa = mantissa * 10 + (uint64_t)(*p - '0');
                if (mantissa > 0) significant_digits++;
                exponent--;
            }
        }
    }
    if (!any) {
        if (end != NULL) *end = (char *)text;
        return 0.0;
    }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        bool down = false;
        int e = 0;
        if (*q == '-' || *q == '+') down = *q++ == '-';
        if (*q >= '0' && *q <= '9') {
            for (; *q >= '0' && *q <= '9'; q++) {
                if (e < 100000) e = e * 10 + (*q - '0');
            }
            exponent += down ? -e : e;
            p = q;
        }
    }
    if (end != NULL) *end = (char *)p;
    if (mantissa == 0) {
        value = 0.0;
    } else if (mantissa < ((uint64_t)1 << 53) && exponent >= -EXACT_TENS && exponent <= EXACT_TENS) {
        /* both exact: one rounding, so the nearest double (Clinger's fast path) */
        value = exponent < 0 ? (double)mantissa / exact_tens[-exponent] : (double)mantissa * exact_tens[exponent];
    } else if (exponent < -330) {
        value = 0.0;
    } else if (exponent > 330) {
        value = 1e308 * 10.0; /* inf */
    } else { /* the mantissa and its scaling as double-doubles: within a hair of the nearest */
        /* near either end of a double's range, worked 2^64 away from it and brought back by
           an exact power of two, so no step overflows or loses bits to subnormals */
        const double two_64 = 18446744073709551616.0;
        const double shift = exponent > 250 ? 1.0 / two_64 : exponent < -250 ? two_64 : 1.0;
        double hi = (double)mantissa, lo;
        lo = (double)(mantissa - (uint64_t)hi);
        if ((uint64_t)hi > mantissa) lo = -(double)((uint64_t)hi - mantissa);
        hi *= shift;
        lo *= shift;
        scale_by_ten(&hi, &lo, exponent);
        value = (hi + lo) / shift;
    }
    return negative ? -value : value;
}

int wgf_core_priv_scan(const char *text, const char *pattern, ...)
{
    va_list args;
    const char *t = text, *p = pattern;
    int count = 0;
    va_start(args, pattern);
    while (*p != '\0') {
        if (is_space(*p)) {
            while (is_space(*p)) p++;
            while (is_space(*t)) t++;
            continue;
        }
        if (*p != '%') {
            if (*t != *p) break;
            t++;
            p++;
            continue;
        }
        p++;
        {
            int width = 0;
            while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
            if (*p == 'd') {
                int *out = va_arg(args, int *);
                int64_t value = 0;
                bool negative = false, digits = false;
                while (is_space(*t)) t++;
                if (*t == '-' || *t == '+') negative = *t++ == '-';
                for (; *t >= '0' && *t <= '9'; t++, digits = true) {
                    if (value < 100000000000LL) value = value * 10 + (*t - '0'); /* past an int: saturates */
                }
                if (!digits) break;
                *out = (int)(negative ? -value : value);
            } else if (*p == 's' && width > 0) {
                char *out = va_arg(args, char *);
                int n = 0;
                while (is_space(*t)) t++;
                if (*t == '\0') break;
                while (*t != '\0' && !is_space(*t) && n < width) out[n++] = *t++;
                out[n] = '\0';
            } else {
                break; /* a conversion it doesn't read */
            }
            count++;
            p++;
        }
    }
    va_end(args);
    return count;
}
