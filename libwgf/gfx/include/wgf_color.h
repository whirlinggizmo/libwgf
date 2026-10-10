#ifndef WGF_COLOR_H
#define WGF_COLOR_H

#include <stdint.h>

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A color is a value, not a handle: 8-bit RGBA packed as 0xRRGGBBAA, so
 * 0xFF0000FF is opaque red and 0 is fully transparent. There is nothing to
 * create or destroy: write a literal, take a stock color, or build one.
 *
 * 0 is transparent black, not "unset": a tint that changes nothing is white. */
typedef uint32_t wgf_color_t;

/* The stock colors. These name a color; they aren't one: wgf_color_get turns
 * a name into its color. (A packed color doesn't fit in an enum, so names are what
 * a binding can see.) */
typedef enum wgf_color_stock_t {
    WGF_COLOR_STOCK_BLANK = 0,      /* 0x00000000, transparent */
    WGF_COLOR_STOCK_WHITE = 1,      /* 0xFFFFFFFF */
    WGF_COLOR_STOCK_BLACK = 2,      /* 0x000000FF */
    WGF_COLOR_STOCK_LIGHTGRAY = 3,  /* 0xC8C8C8FF */
    WGF_COLOR_STOCK_GRAY = 4,       /* 0x828282FF */
    WGF_COLOR_STOCK_DARKGRAY = 5,   /* 0x505050FF */
    WGF_COLOR_STOCK_YELLOW = 6,     /* 0xFFFF00FF */
    WGF_COLOR_STOCK_GOLD = 7,       /* 0xFFCB00FF */
    WGF_COLOR_STOCK_ORANGE = 8,     /* 0xFFA100FF */
    WGF_COLOR_STOCK_PINK = 9,       /* 0xFF6DC2FF */
    WGF_COLOR_STOCK_RED = 10,       /* 0xE62937FF */
    WGF_COLOR_STOCK_MAROON = 11,    /* 0xBE212DFF */
    WGF_COLOR_STOCK_GREEN = 12,     /* 0x00E430FF */
    WGF_COLOR_STOCK_LIME = 13,      /* 0x009E2FFF */
    WGF_COLOR_STOCK_DARKGREEN = 14, /* 0x00752CFF */
    WGF_COLOR_STOCK_SKYBLUE = 15,   /* 0x66BFFFFF */
    WGF_COLOR_STOCK_BLUE = 16,      /* 0x0079F1FF */
    WGF_COLOR_STOCK_DARKBLUE = 17,  /* 0x0052ACFF */
    WGF_COLOR_STOCK_PURPLE = 18,    /* 0xC87AFFFF */
    WGF_COLOR_STOCK_VIOLET = 19,    /* 0x873CBEFF */
    WGF_COLOR_STOCK_DARKPURPLE = 20, /* 0x701F7EFF */
    WGF_COLOR_STOCK_BEIGE = 21,     /* 0xD3B083FF */
    WGF_COLOR_STOCK_BROWN = 22,     /* 0x7F6A4FFF */
    WGF_COLOR_STOCK_DARKBROWN = 23, /* 0x4C3F2FFF */
    WGF_COLOR_STOCK_MAGENTA = 24,   /* 0xFF00FFFF */
    WGF_COLOR_STOCK_RAYWHITE = 25   /* 0xF5F5F5FF */
} wgf_color_stock_t;

/* The stock color `stock` names; 0 (transparent) for a value that names none. */
WGF_API wgf_color_t wgf_color_get(wgf_color_stock_t stock);

/* Each stock color as a value, for C: WGF_COLOR_SKYBLUE. A binding calls
 * wgf_color_get with the enum. */
#define WGF_COLOR_BEIGE wgf_color_get(WGF_COLOR_STOCK_BEIGE)
#define WGF_COLOR_BLACK wgf_color_get(WGF_COLOR_STOCK_BLACK)
#define WGF_COLOR_BLANK wgf_color_get(WGF_COLOR_STOCK_BLANK)
#define WGF_COLOR_BLUE wgf_color_get(WGF_COLOR_STOCK_BLUE)
#define WGF_COLOR_BROWN wgf_color_get(WGF_COLOR_STOCK_BROWN)
#define WGF_COLOR_DARKBLUE wgf_color_get(WGF_COLOR_STOCK_DARKBLUE)
#define WGF_COLOR_DARKBROWN wgf_color_get(WGF_COLOR_STOCK_DARKBROWN)
#define WGF_COLOR_DARKGRAY wgf_color_get(WGF_COLOR_STOCK_DARKGRAY)
#define WGF_COLOR_DARKGREEN wgf_color_get(WGF_COLOR_STOCK_DARKGREEN)
#define WGF_COLOR_DARKPURPLE wgf_color_get(WGF_COLOR_STOCK_DARKPURPLE)
#define WGF_COLOR_GOLD wgf_color_get(WGF_COLOR_STOCK_GOLD)
#define WGF_COLOR_GRAY wgf_color_get(WGF_COLOR_STOCK_GRAY)
#define WGF_COLOR_GREEN wgf_color_get(WGF_COLOR_STOCK_GREEN)
#define WGF_COLOR_LIGHTGRAY wgf_color_get(WGF_COLOR_STOCK_LIGHTGRAY)
#define WGF_COLOR_LIME wgf_color_get(WGF_COLOR_STOCK_LIME)
#define WGF_COLOR_MAGENTA wgf_color_get(WGF_COLOR_STOCK_MAGENTA)
#define WGF_COLOR_MAROON wgf_color_get(WGF_COLOR_STOCK_MAROON)
#define WGF_COLOR_ORANGE wgf_color_get(WGF_COLOR_STOCK_ORANGE)
#define WGF_COLOR_PINK wgf_color_get(WGF_COLOR_STOCK_PINK)
#define WGF_COLOR_PURPLE wgf_color_get(WGF_COLOR_STOCK_PURPLE)
#define WGF_COLOR_RAYWHITE wgf_color_get(WGF_COLOR_STOCK_RAYWHITE)
#define WGF_COLOR_RED wgf_color_get(WGF_COLOR_STOCK_RED)
#define WGF_COLOR_SKYBLUE wgf_color_get(WGF_COLOR_STOCK_SKYBLUE)
#define WGF_COLOR_VIOLET wgf_color_get(WGF_COLOR_STOCK_VIOLET)
#define WGF_COLOR_WHITE wgf_color_get(WGF_COLOR_STOCK_WHITE)
#define WGF_COLOR_YELLOW wgf_color_get(WGF_COLOR_STOCK_YELLOW)

/* A color from components, clamped: 0..255 for make, 0..1 for make_float, which
 * rounds to the nearest 8-bit step. Out-of-range components saturate; they never
 * spill into the next channel. */
WGF_API wgf_color_t wgf_color_make(int r, int g, int b, int a);
WGF_API wgf_color_t wgf_color_make_float(float r, float g, float b, float a);
/* `color` with its alpha replaced, clamped to 0..255. */
WGF_API wgf_color_t wgf_color_with_alpha(wgf_color_t color, int a);

/* Components back out, 0..255. */
WGF_API int wgf_color_get_red(wgf_color_t color);
WGF_API int wgf_color_get_green(wgf_color_t color);
WGF_API int wgf_color_get_blue(wgf_color_t color);
WGF_API int wgf_color_get_alpha(wgf_color_t color);

/* A straight blend, component by component, from `from` (t = 0) to `to` (t = 1);
 * t is clamped to 0..1. */
WGF_API wgf_color_t wgf_color_lerp(wgf_color_t from, wgf_color_t to, float t);

#ifdef __cplusplus
}
#endif

#endif
