#include "wgf_trig.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* Trigonometry the same to the bit on every target (wgf_trig.h): each worked in double, with
 * no library call but sqrt and floor (both exact everywhere), and rounded to float once: a
 * reduction to within pi/4 of a multiple of pi/2 (pi/2 in three parts, fdlibm's, so the
 * reduction is exact well past any angle a game turns through), then a polynomial fitted
 * to a float's precision (the C library's way: within an ulp, and nearly always the nearest
 * float); atan reduced to |x| <= tan(pi/32), its polynomial to a double's precision. Each step is its own statement, so no compiler fuses a multiply and an add into an
 * FMA, which a target without one would round differently; compiled here once, with
 * libwgf's flags, rather than inlined into every caller. No C library trigonometry is
 * called, so a page whose own calls are these links none. */

#define PI 3.14159265358979323846

#ifdef WGF_PRIV_STANDARD_TRIG

/* A build with WGF_STANDARD_TRIG: each the C library's, its last bit the target's. */
float wgf_trig_sin(float x) { return sinf(x); }
float wgf_trig_cos(float x) { return cosf(x); }
wgf_vec2_t wgf_trig_cos_sin(float x) { return wgf_vec2_make(cosf(x), sinf(x)); }
float wgf_trig_tan(float x) { return tanf(x); }
float wgf_trig_atan2(float y, float x) { return atan2f(y, x); }
float wgf_trig_asin(float x) { return asinf(x); }
float wgf_trig_acos(float x) { return acosf(x); }

#else

/* sin and cos of |r| <= pi/4, as r + r^3 S(r^2) and 1 + r^2 C(r^2): polynomials fitted at
 * Chebyshev nodes to a relative error of 2e-11 (sin) and 3e-13 (cos), far inside a float's
 * 6e-8, each worked as two halves (Estrin's way) so the two run side by side. */
static double sin_reduced(double r)
{
    const double z = r * r, zz = z * z;
    double low = z * 0.008333331873837089, high = z * 2.7249864655139585e-06, p;
    low = low - 0.16666666663853044;
    high = high - 0.00019840086263755443;
    high = high * zz;
    p = low + high;
    low = r * z;
    p = p * low;
    return r + p;
}

static double cos_reduced(double r)
{
    const double z = r * r, zz = z * z;
    double low = z * 0.041666666637372694, high = z * 2.479986150250612e-05, top = zz * -2.7237076731325097e-07, p;
    low = low - 0.49999999999963857;
    high = high - 0.0013888885089126252;
    high = high + top;
    high = high * zz;
    p = low + high;
    p = p * z;
    return 1.0 + p;
}

/* x = k * pi/2 + r, |r| <= pi/4: r into `r`, and k's quadrant (k mod 4) returned. pi/2 in
 * three parts, each product exact for |k| < 2^20. k is x * 2/pi rounded to the nearest
 * integer by adding and taking away 1.5 * 2^52, where a double's last bit is 1 (exact in any
 * IEEE arithmetic, a tie to even), and k's low bits are then that sum's; no conversion to an
 * integer, which wasm checks, nor floor, a library call on some targets. Past 2^51 (an angle
 * past 3e15, long past meaning), by floor. */
static int reduce(double x, double *r)
{
    const double pio2_1 = 1.57079632673412561417e+00, pio2_2 = 6.07710050630396597660e-11;
    const double pio2_3 = 2.02226624879595063154e-21;
    const double shift = 6755399441055744.0; /* 1.5 * 2^52 */
    const double v = x * 0.63661977236758134308;
    double k, rest;
    int quadrant;
    if (v > -2251799813685248.0 && v < 2251799813685248.0) { /* |v| < 2^51 */
        const double shifted = v + shift;
        uint64_t bits;
        memcpy(&bits, &shifted, sizeof(bits));
        k = shifted - shift;
        quadrant = (int)(bits & 3u);
    } else {
        k = floor(v + 0.5);
        quadrant = (int)(k - 4.0 * floor(k * 0.25));
    }
    rest = x - k * pio2_1;
    rest = rest - k * pio2_2;
    rest = rest - k * pio2_3;
    *r = rest;
    return quadrant;
}

/* sin and cos of `x`, in double, alike on every target. */
static void sin_cos(double x, double *sine, double *cosine)
{
    double r;
    const int quadrant = reduce(x, &r);
    const double s = sin_reduced(r), c = cos_reduced(r);
    switch (quadrant) {
        case 0: *sine = s; *cosine = c; break;
        case 1: *sine = c; *cosine = -s; break;
        case 2: *sine = -s; *cosine = -c; break;
        default: *sine = -c; *cosine = s; break;
    }
    if (x == 0.0) *sine = x; /* a zero's sine keeps its sign, as sinf's */
}

/* Each alone works the one polynomial its quadrant needs: the same bits as sin_cos's. */
float wgf_trig_sin(float x)
{
    double r, v;
    int quadrant;
    if (!isfinite(x)) return x - x; /* NaN, as sinf's */
    if (x == 0.0f) return x;         /* a zero keeps its sign */
    quadrant = reduce((double)x, &r);
    v = (quadrant & 1) ? cos_reduced(r) : sin_reduced(r);
    return (float)((quadrant & 2) ? -v : v);
}

float wgf_trig_cos(float x)
{
    double r, v;
    int quadrant;
    if (!isfinite(x)) return x - x;
    quadrant = reduce((double)x, &r);
    v = (quadrant & 1) ? sin_reduced(r) : cos_reduced(r);
    return (float)(((quadrant + 1) & 2) ? -v : v);
}

wgf_vec2_t wgf_trig_cos_sin(float x)
{
    double s, c;
    if (!isfinite(x)) return wgf_vec2_make(x - x, x - x);
    sin_cos((double)x, &s, &c);
    return wgf_vec2_make((float)c, (float)s);
}

float wgf_trig_tan(float x)
{
    double s, c;
    if (!isfinite(x)) return x - x;
    sin_cos((double)x, &s, &c);
    return (float)(s / c);
}

/* atan of |x| <= tan(pi/32), a polynomial to x^15 (the next term below a double's precision). */
static double atan_reduced(double x)
{
    const double x2 = x * x;
    double p = -6.6666666666666667e-02; /* -1/15 */
    p = p * x2;
    p = p + 7.6923076923076927e-02; /*  1/13 */
    p = p * x2;
    p = p - 9.0909090909090912e-02; /* -1/11 */
    p = p * x2;
    p = p + 1.1111111111111111e-01; /*  1/9 */
    p = p * x2;
    p = p - 1.4285714285714285e-01; /* -1/7 */
    p = p * x2;
    p = p + 2.0000000000000001e-01; /*  1/5 */
    p = p * x2;
    p = p - 3.3333333333333331e-01; /* -1/3 */
    p = p * x2;
    p = p * x;
    return x + p;
}

/* atan of any finite x, in double: reduced about the nearest multiple of pi/16, whose tangent
 * is c, by atan(x) = atan(c) + atan((x - c) / (1 + x c)), and past the last of them by
 * atan(x) = pi/2 - atan(1 / x): one division, and a short polynomial. */
static double atan_double(double x)
{
    /* each multiple's tangent, its angle, and the tangent halfway to the next */
    static const double tangent[8] = {0.0, 0.198912367379658, 0.41421356237309503, 0.6681786379192989,
                                      1.0, 1.496605762665489, 2.414213562373095,   5.027339492125848};
    static const double angle[8] = {0.0, 0.19634954084936207, 0.39269908169872414, 0.5890486225480862,
                                    0.7853981633974483, 0.9817477042468103, 1.1780972450961724, 1.3744467859455345};
    static const double upto[8] = {0.09849140335716425, 0.3033466836073424, 0.5345111359507917, 0.8206787908286604,
                                   1.2185035255879764,  1.8708684117893895, 3.2965582089383205, 10.15317038760886};
    const double t = x < 0.0 ? -x : x;
    double a;
    if (t <= upto[0]) {
        a = atan_reduced(t);
    } else if (t > upto[7]) {
        a = PI / 2.0 - atan_reduced(1.0 / t);
    } else {
        int i = 1;
        double over;
        while (t > upto[i]) i++;
        over = t * tangent[i];
        over = over + 1.0;
        a = angle[i] + atan_reduced((t - tangent[i]) / over);
    }
    return x < 0.0 ? -a : a;
}

float wgf_trig_atan2(float y, float x)
{
    const double dy = (double)y, dx = (double)x;
    if (isnan(y) || isnan(x)) return y + x;
    if (dx == 0.0 && dy == 0.0) return signbit(x) ? (signbit(y) ? (float)-PI : (float)PI) : y;
    if (isinf(y)) { /* the infinities' fixed angles */
        const double a = isinf(x) ? (dx > 0.0 ? PI / 4.0 : 3.0 * PI / 4.0) : PI / 2.0;
        return (float)(dy > 0.0 ? a : -a);
    }
    if (isinf(x)) return dx > 0.0 ? (signbit(y) ? -0.0f : 0.0f) : (signbit(y) ? (float)-PI : (float)PI);
    if (dx > 0.0) return (float)atan_double(dy / dx);
    if (dx < 0.0) return (float)(atan_double(dy / dx) + (signbit(y) ? -PI : PI));
    return dy > 0.0 ? (float)(PI / 2.0) : (float)(-PI / 2.0);
}

/* asin and acos of x in -1..1 (NaN past it, as the C library's). */
float wgf_trig_asin(float x)
{
    const double d = (double)x;
    if (!(d >= -1.0 && d <= 1.0)) return (x - x) / (x - x);
    if (d == 1.0 || d == -1.0) return (float)(d * PI / 2.0);
    return (float)atan_double(d / sqrt((1.0 - d) * (1.0 + d)));
}

float wgf_trig_acos(float x)
{
    const double d = (double)x;
    if (!(d >= -1.0 && d <= 1.0)) return (x - x) / (x - x);
    if (d == 0.0) return (float)(PI / 2.0);
    {
        const double a = atan_double(sqrt((1.0 - d) * (1.0 + d)) / d);
        return (float)(d > 0.0 ? a : a + PI);
    }
}

#endif
