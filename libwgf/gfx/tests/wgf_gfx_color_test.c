#include <math.h>
#include <stdio.h>

#include "wgf_color.h"

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    const wgf_color_t c = wgf_color_make(10, 20, 30, 40);

    expect(wgf_color_get(WGF_COLOR_STOCK_SKYBLUE) == 0x66BFFFFFu, "a stock color");
    expect(WGF_COLOR_RAYWHITE == 0xF5F5F5FFu, "sugar gives the color itself");
    expect(wgf_color_get(WGF_COLOR_STOCK_BLANK) == 0, "blank is 0, transparent");
    expect(wgf_color_get((wgf_color_stock_t)99) == 0 && wgf_color_get((wgf_color_stock_t)-1) == 0,
           "a value that names no color gives 0");
    expect(c == 0x0A141E28u, "make packs RRGGBBAA");
    expect(wgf_color_get_red(c) == 10 && wgf_color_get_green(c) == 20 && wgf_color_get_blue(c) == 30 &&
               wgf_color_get_alpha(c) == 40,
           "components back out");
    expect(wgf_color_make(300, -5, 255, 0) == 0xFF00FF00u, "out-of-range components saturate, never spill");
    expect(wgf_color_make_float(1.0f, 0.5f, 0.0f, 2.0f) == 0xFF8000FFu, "make_float rounds and clamps");
    expect(wgf_color_with_alpha(0x11223344u, 128) == 0x11223380u, "with_alpha replaces alpha only");
    expect(wgf_color_lerp(0x000000FFu, 0xFFFFFFFFu, 0.5f) == 0x808080FFu, "lerp halfway");
    expect(wgf_color_lerp(0x000000FFu, 0xFFFFFFFFu, 2.0f) == 0xFFFFFFFFu, "lerp clamps t");
    /* hue, saturation, value; brightness; multiplying */
    expect(wgf_color_make_hsv(0, 1, 1, 255) == 0xFF0000FFu && wgf_color_make_hsv(120, 1, 1, 255) == 0x00FF00FFu &&
               wgf_color_make_hsv(600, 1, 1, 128) == 0x0000FF80u && wgf_color_make_hsv(30, 0, 0.5f, 255) == 0x808080FFu,
           "colors from hue, saturation, and value, the hue wrapped");
    expect(fabsf(wgf_color_get_hue(0x00FF00FFu) - 120.0f) < 0.01f &&
               fabsf(wgf_color_get_saturation(0x808080FFu)) < 1e-6f &&
               fabsf(wgf_color_get_value(0x804020FFu) - 128.0f / 255.0f) < 1e-4f,
           "and read back");
    expect(wgf_color_brighten(0x000000FFu, 1.0f) == 0xFFFFFFFFu &&
               wgf_color_brighten(0xFFFFFF80u, -1.0f) == 0x00000080u &&
               wgf_color_brighten(0x804020FFu, 0.0f) == 0x804020FFu,
           "brightened and darkened, its alpha kept");
    expect(wgf_color_multiply(0xFF8000FFu, 0x80FFFF80u) == 0x80800080u, "two colors multiplied");

    return failures == 0 ? 0 : 1;
}
