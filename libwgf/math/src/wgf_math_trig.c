#include "wgf_trig.h"

#include <math.h>

/* Trigonometry the same to the bit on every target (wgf_trig.h): each worked in double, with
 * no library call but sqrt and floor (both exact everywhere), and rounded to float once: a
 * reduction to within pi/4 of a multiple of pi/2 (pi/2 in three parts, fdlibm's, so the
 * reduction is exact well past any angle a game turns through), then a Taylor polynomial
 * whose next term is below a double's precision; atan reduced to |x| <= tan(pi/32) the same
 * way. Each step is its own statement, so no compiler fuses a multiply and an add into an
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

/* sin and cos of |r| <= pi/4, each a polynomial to r^17 or r^18. */
static double sin_reduced(double r)
{
    const double r2 = r * r;
    double p = 2.8114572543455206e-15; /*  1/17! */
    p = p * r2;
    p = p - 7.6471637318198164e-13; /* -1/15! */
    p = p * r2;
    p = p + 1.6059043836821613e-10; /*  1/13! */
    p = p * r2;
    p = p - 2.5052108385441720e-08; /* -1/11! */
    p = p * r2;
    p = p + 2.7557319223985893e-06; /*  1/9! */
    p = p * r2;
    p = p - 1.9841269841269841e-04; /* -1/7! */
    p = p * r2;
    p = p + 8.3333333333333333e-03; /*  1/5! */
    p = p * r2;
    p = p - 1.6666666666666667e-01; /* -1/3! */
    p = p * r2;
    p = p * r;
    return r + p;
}

static double cos_reduced(double r)
{
    const double r2 = r * r;
    double p = -1.5619206968586225e-16; /* -1/18! */
    p = p * r2;
    p = p + 4.7794773323873853e-14; /*  1/16! */
    p = p * r2;
    p = p - 1.1470745597729725e-11; /* -1/14! */
    p = p * r2;
    p = p + 2.0876756987868099e-09; /*  1/12! */
    p = p * r2;
    p = p - 2.7557319223985888e-07; /* -1/10! */
    p = p * r2;
    p = p + 2.4801587301587302e-05; /*  1/8! */
    p = p * r2;
    p = p - 1.3888888888888889e-03; /* -1/6! */
    p = p * r2;
    p = p + 4.1666666666666667e-02; /*  1/4! */
    p = p * r2;
    p = p - 0.5;
    p = p * r2;
    return 1.0 + p;
}

/* sin and cos of `x`, in double, alike on every target. */
static void sin_cos(double x, double *sine, double *cosine)
{
    /* x = k * pi/2 + r: pi/2 in three parts, each product exact for |k| < 2^20 */
    const double pio2_1 = 1.57079632673412561417e+00, pio2_2 = 6.07710050630396597660e-11;
    const double pio2_3 = 2.02226624879595063154e-21;
    const double k = floor(x * 0.63661977236758134308 + 0.5); /* the nearest multiple: x * 2/pi, rounded */
    const double quadrant = k - 4.0 * floor(k * 0.25);    /* 0..3, exact */
    double r = x - k * pio2_1;
    double s, c;
    r = r - k * pio2_2;
    r = r - k * pio2_3;
    s = sin_reduced(r);
    c = cos_reduced(r);
    switch ((int)quadrant) {
        case 0: *sine = s; *cosine = c; break;
        case 1: *sine = c; *cosine = -s; break;
        case 2: *sine = -s; *cosine = -c; break;
        default: *sine = -c; *cosine = s; break;
    }
}

float wgf_trig_sin(float x)
{
    double s, c;
    if (!isfinite(x)) return x - x; /* NaN, as sinf's */
    sin_cos((double)x, &s, &c);
    return (float)s;
}

float wgf_trig_cos(float x)
{
    double s, c;
    if (!isfinite(x)) return x - x;
    sin_cos((double)x, &s, &c);
    return (float)c;
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
