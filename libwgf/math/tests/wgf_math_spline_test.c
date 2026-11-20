#include <math.h>
#include <stdio.h>

#include "wgf_spline.h"

/* Splines: their segments counted, points along each kind at known places, t held to 0..1,
 * and points that make no segment. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

int main(void)
{
    const float line[6] = {0, 0, 10, 0, 10, 10};
    const float four[8] = {0, 0, 0, 0, 10, 0, 10, 0};
    const float quad[6] = {0, 0, 5, 10, 10, 0};
    const float cubic[8] = {0, 0, 0, 10, 10, 10, 10, 0};
    wgf_vec2_t p;
    expect(wgf_spline_get_segment_count(WGF_SPLINE_KIND_LINEAR, line, 6) == 2 &&
               wgf_spline_get_segment_count(WGF_SPLINE_KIND_BEZIER_QUADRATIC, quad, 6) == 1 &&
               wgf_spline_get_segment_count(WGF_SPLINE_KIND_BEZIER_CUBIC, cubic, 8) == 1 &&
               wgf_spline_get_segment_count(WGF_SPLINE_KIND_CATMULL_ROM, four, 8) == 1,
           "segments counted");
    expect(wgf_spline_get_segment_count(WGF_SPLINE_KIND_LINEAR, line, 5) == 0 &&
               wgf_spline_get_segment_count(WGF_SPLINE_KIND_BEZIER_CUBIC, line, 6) == 0 &&
               wgf_spline_get_segment_count((wgf_spline_kind_t)9, line, 6) == 0,
           "an odd count, too few points, a kind that isn't one: none");
    p = wgf_spline_get_point(WGF_SPLINE_KIND_LINEAR, line, 6, 0.75f);
    expect(near(p.x, 10) && near(p.y, 5), "linear: three quarters along, halfway up its second segment");
    p = wgf_spline_get_point(WGF_SPLINE_KIND_LINEAR, line, 6, 2.0f);
    expect(near(p.x, 10) && near(p.y, 10), "t past 1: its end");
    p = wgf_spline_get_point(WGF_SPLINE_KIND_CATMULL_ROM, four, 8, 0.0f);
    expect(near(p.x, 0) && near(wgf_spline_get_point(WGF_SPLINE_KIND_CATMULL_ROM, four, 8, 1.0f).x, 10),
           "Catmull-Rom: through its inner points");
    p = wgf_spline_get_point(WGF_SPLINE_KIND_BEZIER_QUADRATIC, quad, 6, 0.5f);
    expect(near(p.x, 5) && near(p.y, 5), "a quadratic Bezier's middle: halfway to its control");
    p = wgf_spline_get_point(WGF_SPLINE_KIND_BEZIER_CUBIC, cubic, 8, 0.5f);
    expect(near(p.x, 5) && near(p.y, 7.5f), "a cubic Bezier's middle");
    p = wgf_spline_get_point(WGF_SPLINE_KIND_BASIS, four, 8, 0.0f);
    expect(near(p.x, 10.0f / 6.0f) && near(p.y, 0), "a B-spline near, not through, its points");
    p = wgf_spline_get_point(WGF_SPLINE_KIND_LINEAR, line, 2, 0.5f);
    expect(p.x == 0 && p.y == 0, "no segment: 0, 0");
    if (failures == 0) printf("wgf_math_spline_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
