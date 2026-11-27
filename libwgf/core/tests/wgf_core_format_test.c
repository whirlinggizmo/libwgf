#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_core_format_priv.h"

/* libwgf's own formatting and reading (wgf_core_format.c) against the C library's, which the
 * test may link: each format libwgf uses, and more, the same text as snprintf's; numbers
 * read as strtod reads them; what %.17g writes read back the same; and scan as sscanf for
 * what the shader reader asks. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

#define SAME(...)                                                                                                      \
    do {                                                                                                               \
        char ours[256], theirs[256];                                                                                   \
        const int n_ours = wgf_core_priv_format(ours, sizeof(ours), __VA_ARGS__);                                      \
        const int n_theirs = snprintf(theirs, sizeof(theirs), __VA_ARGS__);                                            \
        if (strcmp(ours, theirs) != 0 || n_ours != n_theirs) {                                                         \
            printf("FAIL: %s: \"%s\" (%d), the C library's \"%s\" (%d)\n", #__VA_ARGS__, ours, n_ours, theirs,         \
                   n_theirs);                                                                                          \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static void read_same(const char *text)
{
    char *end_ours, *end_theirs;
    const double ours = wgf_core_priv_parse_double(text, &end_ours);
    const double theirs = strtod(text, &end_theirs);
    if (end_ours != end_theirs || !(ours == theirs || fabs(ours - theirs) <= fabs(theirs) * 4e-16)) {
        printf("FAIL: reading \"%s\": %.17g (%d read), the C library's %.17g (%d)\n", text, ours,
        (int)(end_ours - text),
               theirs, (int)(end_theirs - text));
        failures++;
    }
}

static void round_trip(double x)
{
    char text[64];
    wgf_core_priv_format(text, sizeof(text), "%.17g", x);
    if (wgf_core_priv_parse_double(text, NULL) != x) {
        printf("FAIL: %.17g written as \"%s\" reads back otherwise\n", x, text);
        failures++;
    }
}

int main(void)
{
    static const double values[] = {0.0,      1.0,          -1.0,    0.5,      2.5,        3.5,          0.1,
                                    0.125,    3.14159265,   -2.75,   1.0 / 3,  123456.789, 1e-5,         1e-7,
                                    999.9996, 1e15,         1e20,    1e-300,   1e300,      6.02214076e23, 42.0,
                                    100.0,    0.000123456,  65535.0, 1234.5678};
    int i;

    /* integers, text, and characters */
    SAME("%d %d %d", 0, 42, -42);
    SAME("[%5d] [%-5d] [%05d] [%+d] [% d]", 42, 42, 42, 42, 42);
    SAME("%ld %lld %lu %llu", -1234567L, -123456789012345LL, 1234567UL, 18446744073709551615ULL);
    SAME("%u %x %X %08x %o", 4000000000u, 0xbeefu, 0xbeefu, 0xbeefu, 8u);
    SAME("%zu %d", (size_t)12345, -2147483647 - 1);
    SAME("%.3d %.0d|", 7, 0);
    SAME("%c%c %%", 'o', 'k');
    SAME("[%s] [%10s] [%-10s] [%.3s] [%.*s]", "text", "text", "text", "text", 2, "text");
    SAME("[%-5s] %s:%d: %.*s", "WARN", "file.c", 12, 5, "message long");
    SAME("%s", "");

    /* floats, in each form libwgf uses and its neighbours */
    for (i = 0; i < (int)(sizeof(values) / sizeof(values[0])); i++) {
        const double v = values[i];
        if (fabs(v) < 1e17) SAME("%f %.0f %.1f %.3f %10.2f %-10.2f| %+.2f", v, v, v, v, v, v, v);
        SAME("%e %.3e %E %.0e", v, v, v, v);
        SAME("%g %.3g %.6g %.10g %.15g %G %#g", v, v, v, v, v, v, v);
        if (fabs(v) < 1e17) SAME("%.*f %.*g", 4, v, 9, v);
        round_trip(v);
        round_trip(-v);
    }
    SAME("%.0f %.0f %.0f %.0f", 0.5, 1.5, 2.5, 2.4999); /* halves to even */
    SAME("%f %f %f", (double)NAN, (double)INFINITY, -(double)INFINITY);
    SAME("%g %.17g", -0.0, -0.0);
    { /* past 10^17, %f writes 17 significant digits, then zeros (the C library, every digit of the binary) */
        char text[64];
        wgf_core_priv_format(text, sizeof(text), "%.1f", 1e30);
        expect(strcmp(text, "1000000000000000000000000000000.0") == 0, "%f of a number past 10^17");
    }
    for (i = 0; i < 2000; i++) { /* scattered doubles, wide and narrow */
        const double x = ldexp((double)rand() / RAND_MAX + 0.5, (rand() % 200) - 100);
        round_trip(x);
        SAME("%.6g %.9e", x, x);
        if (x < 1e17) SAME("%.3f", x);
    }

    /* the length the whole would have, past the room given */
    {
        char small[6];
        expect(wgf_core_priv_format(small, sizeof(small), "%s-%d", "abcdef", 12) == 9 && strcmp(small, "abcde") == 0,
               "cut to the room, its 0 kept, the whole length returned");
        expect(wgf_core_priv_format(NULL, 0, "%d", 12345) == 5, "no room: the length alone");
    }

    /* reading numbers */
    read_same("0");
    read_same("1");
    read_same("-2.5");
    read_same("+3.14159");
    read_same("  42abc");
    read_same("abc");
    read_same("1e10");
    read_same("1.5e-7");
    read_same("6.02214076e23");
    read_same("0.1");
    read_same("123456789012345678901234");
    read_same("0.000000000000000000000000000123");
    read_same("1e-320");
    read_same("1e400");
    read_same("1.7976931348623157e308");
    read_same("12.5e");
    read_same(".5");
    read_same("5.");
    read_same("-");
    read_same("1e+3x");
    for (i = 0; i < 2000; i++) {
        char text[64];
        snprintf(text, sizeof(text), "%.*g", 1 + rand() % 17, ldexp((double)rand() / RAND_MAX, (rand() % 120) - 60));
        read_same(text);
    }

    /* scanning */
    {
        char word[16], other[8];
        int a = 0, b = 0;
        expect(wgf_core_priv_scan("attr 3 position", "attr %d %15s", &a, word) == 2 && a == 3 &&
                   strcmp(word, "position") == 0,
               "a number and a word");
        expect(wgf_core_priv_scan("ub 1   vertex   80 wgf_object 5", "ub %d %15s %d %7s %d", &a, word, &b, other,
                                  &a) == 4 &&
                   b == 80 && strcmp(other, "wgf_obj") == 0,
               "spaces skipped; a word cut to its width, as sscanf cuts it, leaving the rest to the next");
        expect(wgf_core_priv_scan("-Y 512 +X 1024", "-Y %d +X %d", &a, &b) == 2 && a == 512 && b == 1024,
               "literal text matched");
        expect(wgf_core_priv_scan("kind", "kind %7s", word) == 0, "a word missing: none");
        expect(wgf_core_priv_scan("param x", "attr %d", &a) == 0, "another line: none");
    }

    if (failures == 0) printf("wgf_core_format_test: PASS\n");
    return failures == 0 ? 0 : 1;
}
