#include "wgf_color.h"

#include <math.h>

/* Cribbed from wgrender's wgr_color. */

static const wgf_color_t stock_colors[] = {
    0x00000000u, /* BLANK */
    0xFFFFFFFFu, /* WHITE */
    0x000000FFu, /* BLACK */
    0xC8C8C8FFu, /* LIGHTGRAY */
    0x828282FFu, /* GRAY */
    0x505050FFu, /* DARKGRAY */
    0xFFFF00FFu, /* YELLOW */
    0xFFCB00FFu, /* GOLD */
    0xFFA100FFu, /* ORANGE */
    0xFF6DC2FFu, /* PINK */
    0xE62937FFu, /* RED */
    0xBE212DFFu, /* MAROON */
    0x00E430FFu, /* GREEN */
    0x009E2FFFu, /* LIME */
    0x00752CFFu, /* DARKGREEN */
    0x66BFFFFFu, /* SKYBLUE */
    0x0079F1FFu, /* BLUE */
    0x0052ACFFu, /* DARKBLUE */
    0xC87AFFFFu, /* PURPLE */
    0x873CBEFFu, /* VIOLET */
    0x701F7EFFu, /* DARKPURPLE */
    0xD3B083FFu, /* BEIGE */
    0x7F6A4FFFu, /* BROWN */
    0x4C3F2FFFu, /* DARKBROWN */
    0xFF00FFFFu, /* MAGENTA */
    0xF5F5F5FFu, /* RAYWHITE */
};

_Static_assert(sizeof(stock_colors) / sizeof(stock_colors[0]) == WGF_COLOR_STOCK_RAYWHITE + 1,
               "a color for every stock name");

static unsigned int clamp_component(int v)
{
    return (unsigned int)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* 0..1 to 0..255, clamped and rounded to the nearest step. */
static unsigned int clamp_component_float(float v)
{
    const float scaled = v * 255.0f + 0.5f;
    return (unsigned int)(scaled < 0.0f ? 0.0f : scaled > 255.0f ? 255.0f : scaled);
}

wgf_color_t wgf_color_get(wgf_color_stock_t stock)
{
    const int i = (int)stock;
    return i >= 0 && i < (int)(sizeof(stock_colors) / sizeof(stock_colors[0])) ? stock_colors[i] : 0u;
}

wgf_color_t wgf_color_make(int r, int g, int b, int a)
{
    return (wgf_color_t)((clamp_component(r) << 24) | (clamp_component(g) << 16) | (clamp_component(b) << 8) |
                            clamp_component(a));
}

wgf_color_t wgf_color_make_float(float r, float g, float b, float a)
{
    return (wgf_color_t)((clamp_component_float(r) << 24) | (clamp_component_float(g) << 16) |
                            (clamp_component_float(b) << 8) | clamp_component_float(a));
}

wgf_color_t wgf_color_with_alpha(wgf_color_t color, int a)
{
    return (wgf_color_t)((color & 0xFFFFFF00u) | clamp_component(a));
}

int wgf_color_get_red(wgf_color_t color)
{
    return (int)((color >> 24) & 0xFFu);
}

int wgf_color_get_green(wgf_color_t color)
{
    return (int)((color >> 16) & 0xFFu);
}

int wgf_color_get_blue(wgf_color_t color)
{
    return (int)((color >> 8) & 0xFFu);
}

int wgf_color_get_alpha(wgf_color_t color)
{
    return (int)(color & 0xFFu);
}

wgf_color_t wgf_color_lerp(wgf_color_t from, wgf_color_t to, float t)
{
    const float k = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    wgf_color_t out = 0;
    int shift;
    for (shift = 24; shift >= 0; shift -= 8) {
        const float a = (float)((from >> shift) & 0xFFu);
        const float b = (float)((to >> shift) & 0xFFu);
        out |= (wgf_color_t)((unsigned int)(a + (b - a) * k + 0.5f) & 0xFFu) << shift;
    }
    return out;
}

/* ---- hue, saturation, value; brightness; multiplying ---------------------------------- */

wgf_color_t wgf_color_make_hsv(float hue, float saturation, float value, int a)
{
    const float h = hue - 360.0f * floorf(hue / 360.0f);
    const float s = saturation < 0.0f ? 0.0f : saturation > 1.0f ? 1.0f : saturation;
    const float v = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
    const float c = v * s, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
    float r, g, b;
    if (h < 60.0f) r = c, g = x, b = 0;
    else if (h < 120.0f) r = x, g = c, b = 0;
    else if (h < 180.0f) r = 0, g = c, b = x;
    else if (h < 240.0f) r = 0, g = x, b = c;
    else if (h < 300.0f) r = x, g = 0, b = c;
    else r = c, g = 0, b = x;
    return wgf_color_make((int)((r + m) * 255.0f + 0.5f), (int)((g + m) * 255.0f + 0.5f), (int)((b + m) * 255.0f + 0.5f), a);
}

static void hsv_of(wgf_color_t color, float *hue, float *saturation, float *value)
{
    const float r = (float)wgf_color_get_red(color) / 255.0f, g = (float)wgf_color_get_green(color) / 255.0f;
    const float b = (float)wgf_color_get_blue(color) / 255.0f;
    const float most = fmaxf(r, fmaxf(g, b)), least = fminf(r, fminf(g, b)), d = most - least;
    float h = 0.0f;
    if (d > 0.0f) {
        if (most == r) h = 60.0f * fmodf((g - b) / d, 6.0f);
        else if (most == g) h = 60.0f * ((b - r) / d + 2.0f);
        else h = 60.0f * ((r - g) / d + 4.0f);
        if (h < 0.0f) h += 360.0f;
    }
    *hue = h;
    *saturation = most > 0.0f ? d / most : 0.0f;
    *value = most;
}

float wgf_color_get_hue(wgf_color_t color)
{
    float h, s, v;
    hsv_of(color, &h, &s, &v);
    return h;
}

float wgf_color_get_saturation(wgf_color_t color)
{
    float h, s, v;
    hsv_of(color, &h, &s, &v);
    return s;
}

float wgf_color_get_value(wgf_color_t color)
{
    float h, s, v;
    hsv_of(color, &h, &s, &v);
    return v;
}

wgf_color_t wgf_color_brighten(wgf_color_t color, float amount)
{
    const float k = amount < -1.0f ? -1.0f : amount > 1.0f ? 1.0f : amount;
    const wgf_color_t toward = k >= 0.0f ? 0xFFFFFFFFu : 0x000000FFu;
    return wgf_color_with_alpha(wgf_color_lerp(color, toward, fabsf(k)), wgf_color_get_alpha(color));
}

wgf_color_t wgf_color_multiply(wgf_color_t color, wgf_color_t by)
{
    wgf_color_t out = 0;
    int shift;
    for (shift = 24; shift >= 0; shift -= 8) {
        const unsigned a = (color >> shift) & 0xFFu, b = (by >> shift) & 0xFFu;
        out |= (wgf_color_t)((a * b + 127u) / 255u) << shift;
    }
    return out;
}
