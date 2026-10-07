#ifndef WGF_DRAW_H
#define WGF_DRAW_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_font.h"
#include "wgf_actor.h"
#include "wgf_text.h"
#include "wgf_texture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Immediate mode 2D: each call draws once, in the frame it is made in, in call order
 * with everything else drawn that frame, and keeps nothing. Logical pixels from the
 * frame's top-left, y down (wgf_render.h). A draw outside a frame draws nothing.
 *
 * Outlines and lines have a thickness in logical pixels, centered on the line; one
 * of 0 or less is a hairline of one logical pixel. A polyline's corners are mitred,
 * and bevelled where a sharp corner's miter would reach past four thicknesses. */

WGF_API void wgf_draw_rectangle(float x, float y, float width, float height, wgf_color_t color);
WGF_API void wgf_draw_rectangle_lines(float x, float y, float width, float height, float thickness,
                                      wgf_color_t color);
WGF_API void wgf_draw_line(float x0, float y0, float x1, float y1, float thickness, wgf_color_t color);
/* Circles are drawn with enough segments for their size on screen. */
WGF_API void wgf_draw_circle(float center_x, float center_y, float radius, wgf_color_t color);
WGF_API void wgf_draw_circle_lines(float center_x, float center_y, float radius, float thickness,
                                   wgf_color_t color);
WGF_API void wgf_draw_triangle(float x0, float y0, float x1, float y1, float x2, float y2, wgf_color_t color);

/* Lines through `points`, x and y in turn (count floats: count / 2 points), the last
 * joined to the first when `closed`. False, drawing nothing, for an odd count, fewer
 * than 2 points, or NULL. */
WGF_API bool wgf_draw_polyline(const float *points, int count, bool closed, float thickness, wgf_color_t color);
/* The polygon through `points` filled, x and y in turn (count floats), convex or not,
 * in either winding, as long as its edges don't cross (one that crosses itself is
 * filled as the fan from its first point). False, drawing nothing, for an odd count,
 * fewer than 3 points, or NULL. */
WGF_API bool wgf_draw_polygon(const float *points, int count, wgf_color_t color);

/* Text (UTF-8) in `font` (0: the default font) at `size` logical pixels (16 for 0 or
 * less), its top-left at (x, y). Newlines break lines. */
WGF_API void wgf_draw_text(wgf_font_t font, const char *text, float x, float y, float size, wgf_color_t color);

/* The same, (x, y) the point the text is aligned to: its left, center, or right, and its
 * top, middle, or bottom (wgf_text.h's alignments), so a HUD anchored to the visible
 * area's right edge (wgf_presentation_get_visible) needs no guessed width. */
WGF_API void wgf_draw_text_aligned(wgf_font_t font, const char *text, float x, float y, float size, wgf_color_t color,
                                   wgf_text_halign_t horizontal, wgf_text_valign_t vertical);

/* A texture into the rectangle (x, y, width, height), multiplied by `tint` (white
 * leaves it as it is). A width or height of 0 or less draws it at its own size.
 * Nothing while it is PENDING; the placeholder checker once it has FAILED. */
WGF_API void wgf_draw_texture(wgf_texture_t texture, float x, float y, float width, float height, wgf_color_t tint);

/* A region of a texture, in its pixels from the top-left (a source width or height of
 * 0 or less: the whole texture), into the rectangle (x, y, width, height), as an icon
 * is cut from an atlas. A width or height of 0 or less draws it at the region's own
 * size. The placeholder fills the rectangle once the texture has FAILED. */
WGF_API void wgf_draw_texture_region(wgf_texture_t texture, float source_x, float source_y, float source_width,
                                     float source_height, float x, float y, float width, float height,
                                     wgf_color_t tint);

/* 3D: between begin_3d and end_3d, the 3D calls draw in the world as `camera` (a 3D
 * camera, wgf_camera3d.h) sees it, into the presentation's visible area, depth tested
 * against each other and against the frame's 3D before them, and under the clip. False
 * when `camera` isn't a 3D camera, or outside a frame. A 3D call outside them draws
 * nothing; a frame starts in 2D, and one left in 3D ends there. A 2D call between them
 * lands in the world's x-y plane, as wgrender's did. */
WGF_API bool wgf_draw_begin_3d(wgf_actor_t camera);
WGF_API void wgf_draw_end_3d(void);

/* Shapes in the world, unlit, their lines a pixel wide (wgrender's shape3d's): a line;
 * a cube, filled or its edges, `width` (x) by `height` (y) by `length` (z), centered on
 * (cx, cy, cz); a sphere; a grid of `slices` by `slices` squares `spacing` apart on the
 * x-z plane, centered on the origin; and a filled rectangle and a circle's outline,
 * centered on (cx, cy, cz), in their x-y plane turned by (rx, ry, rz) radians, as a
 * actor's rotation turns it. A color with alpha below 255 is blended over what is
 * behind it, in the order drawn. */
WGF_API void wgf_draw_line_3d(float x0, float y0, float z0, float x1, float y1, float z1, wgf_color_t color);
WGF_API void wgf_draw_cube(float cx, float cy, float cz, float width, float height, float length, wgf_color_t color);
WGF_API void wgf_draw_cube_wires(float cx, float cy, float cz, float width, float height, float length,
                                 wgf_color_t color);
WGF_API void wgf_draw_sphere(float cx, float cy, float cz, float radius, wgf_color_t color);
WGF_API void wgf_draw_grid(int slices, float spacing, wgf_color_t color);
WGF_API void wgf_draw_rectangle_3d(float cx, float cy, float cz, float width, float height, float rx, float ry,
                                   float rz, wgf_color_t color);
WGF_API void wgf_draw_circle_3d(float cx, float cy, float cz, float radius, float rx, float ry, float rz,
                                wgf_color_t color);

/* Text in the world: a block centered on (x, y, z), facing the camera, its lines `size`
 * world units tall (1 for 0 or less), in `font` (0: the default font), depth tested
 * and not hiding what is drawn behind it after it. wgrender's wgr_text_draw_3d. */
WGF_API void wgf_draw_text_3d(wgf_font_t font, const char *text, float x, float y, float z, float size,
                              wgf_color_t color);

#ifdef __cplusplus
}
#endif

#endif
