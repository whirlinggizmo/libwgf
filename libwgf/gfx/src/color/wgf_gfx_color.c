#include "wgf_color.h"

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
