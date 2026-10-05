#ifndef WGF_DRAW_H
#define WGF_DRAW_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"

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

#ifdef __cplusplus
}
#endif

#endif
