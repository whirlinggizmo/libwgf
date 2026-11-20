#ifndef WGF_GFX_ROUNDED_PRIV_H
#define WGF_GFX_ROUNDED_PRIV_H

/* Rounded rectangles' outlines (wgf_gfx_draw_rounded.c), shared by immediate mode's
 * (wgf_draw_rectangle_rounded) and a 2D shape's corner radius. */

/* The most straight edges a corner is drawn with. */
#define WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS 64

/* A corner radius as drawn: clamped to 0 up to half the rectangle's shorter side (a
 * width or height below 0 counts as 0). */
float wgf_gfx_priv_rounded_radius(float width, float height, float radius);

/* How many straight edges a corner of `pixels` radius (on the frame's pixels) needs to look
 * round: each falls at most a quarter pixel inside its curve, as a circle's; 1 at least,
 * WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS at most. */
int wgf_gfx_priv_rounded_segments(float pixels);

/* The outline of the rectangle x, y, width by height with its corners rounded to `radius`
 * (clamped as wgf_gfx_priv_rounded_radius says), each corner `segments` straight edges
 * (clamped to 1 up to WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS): its points clockwise on the
 * screen (y down), from the top-left corner's arc where it leaves the left edge, x and y
 * in turn into `out`, which has room for 8 * (WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS + 1)
 * floats. Each corner's arc is segments + 1 points, its ends included; returns the
 * points, 4 * (segments + 1). */
int wgf_gfx_priv_rounded_points(float x, float y, float width, float height, float radius, int segments, float *out);

#endif
