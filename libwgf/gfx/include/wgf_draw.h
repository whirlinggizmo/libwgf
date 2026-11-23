#ifndef WGF_DRAW_H
#define WGF_DRAW_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_font.h"
#include "wgf_actor.h"
#include "wgf_text.h"
#include "wgf_spline.h"
#include "wgf_texture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Immediate mode 2D drawing: each call draws once, in the frame it is made in, and keeps nothing.
 *
 * - Calls draw in the order made, along with everything else drawn that frame.
 * - Coordinates are logical pixels from the frame's top-left, with y down (wgf_render.h).
 * - A draw outside a frame draws nothing.
 *
 * Outlines and lines have a thickness in logical pixels, centered on the line. A thickness
 * of 0 or less draws a hairline one logical pixel wide. A polyline's corners are mitred.
 * Where a sharp corner's miter would reach past four thicknesses, the corner is bevelled. */

WGF_API void wgf_draw_rectangle(float x, float y, float width, float height, wgf_color_t color);
WGF_API void wgf_draw_rectangle_lines(float x, float y, float width, float height, float thickness,
                                      wgf_color_t color);
/* Draws a rectangle with rounded corners, filled or outlined, such as a panel or a button.
 *
 * - Each corner is a quarter circle of `radius` logical pixels, clamped to half the
 *   rectangle's shorter side.
 * - A `radius` of 0 or less gives square corners: the plain rectangle.
 * - Each corner is drawn with `segments` straight edges (at most 64).
 * - A `segments` of 0 or less uses enough for the corner's size on screen, as a circle does. */
WGF_API void wgf_draw_rectangle_rounded(float x, float y, float width, float height, float radius, int segments,
                                        wgf_color_t color);
WGF_API void wgf_draw_rectangle_rounded_lines(float x, float y, float width, float height, float radius,
                                              int segments, float thickness, wgf_color_t color);
WGF_API void wgf_draw_line(float x0, float y0, float x1, float y1, float thickness, wgf_color_t color);
/* Draws a circle, filled or outlined, with enough segments for its size on screen. */
WGF_API void wgf_draw_circle(float center_x, float center_y, float radius, wgf_color_t color);
WGF_API void wgf_draw_circle_lines(float center_x, float center_y, float radius, float thickness,
                                   wgf_color_t color);
WGF_API void wgf_draw_triangle(float x0, float y0, float x1, float y1, float x2, float y2, wgf_color_t color);

/* Draws lines through `points`, joining the last point to the first when `closed` is true.
 *
 * `points` holds x and y in turn: `count` floats, so count / 2 points.
 *
 * Returns false, drawing nothing, for an odd `count`, fewer than 2 points, or a NULL `points`. */
WGF_API bool wgf_draw_polyline(const float *points, int count, bool closed, float thickness, wgf_color_t color);
/* Draws a spline (wgf_spline.h) through or near `points`, as its `kind` makes it.
 *
 * The spline is drawn as a polyline of `steps` points for each segment. A `steps` of 0 or
 * less uses 24.
 *
 * Returns false for points that make no segment. */
WGF_API bool wgf_draw_spline(wgf_spline_kind_t kind, const float *points, int count, int steps, float thickness,
                             wgf_color_t color);
/* Draws the polygon through `points`, filled.
 *
 * - `points` holds x and y in turn: `count` floats.
 * - The polygon may be convex or not, in either winding, as long as its edges don't cross.
 * - A polygon that crosses itself is filled as the fan from its first point.
 *
 * Returns false, drawing nothing, for an odd `count`, fewer than 3 points, or a NULL `points`. */
WGF_API bool wgf_draw_polygon(const float *points, int count, wgf_color_t color);

/* Draws UTF-8 `text` with its top-left at (x, y). Newlines break lines.
 *
 * - `font` 0 uses the default font.
 * - `size` is in logical pixels. A `size` of 0 or less uses 16. */
WGF_API void wgf_draw_text(wgf_font_t font, const char *text, float x, float y, float size, wgf_color_t color);
/* Sets or gets how immediate mode drawing after this call blends with what is under it.
 *
 * The modes are wgf_material.h's alpha modes: BLEND (the default), OPAQUE, ADD, MULTIPLY,
 * and PREMULTIPLIED. MASK draws as BLEND, because immediate mode cuts nothing out.
 *
 * The setter returns false for a `mode` that isn't an alpha mode. */
WGF_API bool wgf_draw_set_blend_mode(wgf_alpha_mode_t mode);
WGF_API wgf_alpha_mode_t wgf_draw_get_blend_mode(void);

/* Sets or gets the spacing added between letters and between lines, in logical pixels.
 *
 * It applies to text drawn and measured after this call (wgf_draw_text, wgf_font_measure).
 * A text actor has its own spacing.
 *
 * Both start at 0. A number that isn't finite is taken as 0. */
WGF_API void wgf_draw_set_text_spacing(float letter, float line);
WGF_API wgf_vec2_t wgf_draw_get_text_spacing(void);

/* Draws text as wgf_draw_text does, with (x, y) as the point the text is aligned to.
 *
 * `horizontal` aligns the text's left, center, or right to x. `vertical` aligns its top,
 * middle, or bottom to y (wgf_text.h's alignments). A HUD anchored to the visible area's
 * right edge (wgf_presentation_get_visible) then needs no guessed width. */
WGF_API void wgf_draw_text_aligned(wgf_font_t font, const char *text, float x, float y, float size, wgf_color_t color,
                                   wgf_text_halign_t horizontal, wgf_text_valign_t vertical);

/* Draws a texture into the rectangle (x, y, width, height), multiplied by `tint`.
 *
 * - A white `tint` leaves the texture as it is.
 * - A width or height of 0 or less draws the texture at its own size.
 * - Nothing is drawn while the texture is PENDING.
 * - The placeholder checker is drawn once the texture has FAILED. */
WGF_API void wgf_draw_texture(wgf_texture_t texture, float x, float y, float width, float height, wgf_color_t tint);

/* Draws a region of a texture into the rectangle (x, y, width, height), as an icon is cut
 * from an atlas.
 *
 * - The source region is in the texture's pixels, from its top-left.
 * - A source width or height of 0 or less uses the whole texture.
 * - A width or height of 0 or less draws the region at its own size.
 * - The placeholder fills the rectangle once the texture has FAILED. */
WGF_API void wgf_draw_texture_region(wgf_texture_t texture, float source_x, float source_y, float source_width,
                                     float source_height, float x, float y, float width, float height,
                                     wgf_color_t tint);

/* Draws rings, sectors, arcs, and ellipses.
 *
 * - wgf_draw_ring draws the band between two radii, from angle `start` to `end`.
 * - Angles are in radians from +x, clockwise on the screen.
 * - A whole turn draws a full ring. An inner radius of 0 draws a sector, such as a pie.
 * - wgf_draw_arc draws a ring `thickness` wide along a circle.
 * - wgf_draw_ellipse and wgf_draw_ellipse_lines draw an ellipse filled and outlined. */
WGF_API void wgf_draw_ring(float center_x, float center_y, float inner_radius, float outer_radius, float start,
                           float end, wgf_color_t color);
WGF_API void wgf_draw_arc(float center_x, float center_y, float radius, float start, float end, float thickness,
                          wgf_color_t color);
WGF_API void wgf_draw_ellipse(float center_x, float center_y, float radius_x, float radius_y, wgf_color_t color);
WGF_API void wgf_draw_ellipse_lines(float center_x, float center_y, float radius_x, float radius_y, float thickness,
                                    wgf_color_t color);

/* Draws gradients.
 *
 * - A rectangle gives each of its four corners its own color, blended between them. Make
 *   two corners alike for a vertical or a horizontal gradient.
 * - A circle blends from its center's color (`inner`) to its edge's (`outer`). */
WGF_API void wgf_draw_rectangle_gradient(float x, float y, float width, float height, wgf_color_t top_left,
                                         wgf_color_t top_right, wgf_color_t bottom_right, wgf_color_t bottom_left);
WGF_API void wgf_draw_circle_gradient(float center_x, float center_y, float radius, wgf_color_t inner,
                                      wgf_color_t outer);

/* Draws a texture's region, as wgf_draw_texture_region does, turned about a pivot.
 *
 * The region is drawn `width` by `height`, with its pivot at (x, y), turned `radians` about it.
 * The pivot is a share of the region's size: 0.5, 0.5 is its middle. */
WGF_API void wgf_draw_texture_rotated(wgf_texture_t texture, float source_x, float source_y, float source_width,
                                      float source_height, float x, float y, float width, float height, float pivot_x,
                                      float pivot_y, float radians, wgf_color_t tint);

/* Draws a texture over a polygon.
 *
 * - `points` holds x and y in turn: `count` floats, 3 points or more.
 * - The polygon is convex, or is drawn as a fan from its first point.
 * - `uvs` holds each point's texture coordinates, 0 to 1 across the whole texture.
 * - `uvs_count` is the number of floats in `uvs`, as many as in `points`.
 *
 * Returns false for too few points, an odd `count`, or texture coordinates that aren't one
 * pair for each point. */
WGF_API bool wgf_draw_texture_polygon(wgf_texture_t texture, const float *points, int count, const float *uvs,
                                      int uvs_count, wgf_color_t tint);

/* Draws a texture's region repeated over the rectangle, starting from its top-left.
 *
 * - `scale` 1 draws one texel to a logical pixel.
 * - The last row and column are cut off where the rectangle ends.
 * - A source width or height of 0 or less uses the whole texture. */
WGF_API void wgf_draw_texture_tiled(wgf_texture_t texture, float source_x, float source_y, float source_width,
                                    float source_height, float x, float y, float width, float height, float scale,
                                    wgf_color_t tint);

/* Draws a texture's region as a nine-slice into the rectangle: a panel or a button of any
 * size from one small image.
 *
 * - The borders are `left`, `top`, `right`, and `bottom` texels in from the region's edges.
 * - The corners keep their size. They are scaled down together when the rectangle is smaller
 *   than its borders.
 * - The edges stretch along their length, and the middle stretches both ways.
 * - A source width or height of 0 or less uses the whole texture. */
WGF_API void wgf_draw_texture_nine_slice(wgf_texture_t texture, float source_x, float source_y, float source_width,
                                         float source_height, float left, float top, float right, float bottom, float x,
                                         float y, float width, float height, wgf_color_t tint);

/* How wgf_draw_texture_fill fills: a texture drawn into the rectangle only up to `amount`
 * (0 to 1).
 *
 * - LEFT_TO_RIGHT and BOTTOM_TO_TOP fill a bar from the left or from the bottom.
 * - CLOCKWISE sweeps clockwise from 12 o'clock, as for a cooldown or a health orb.
 *
 * A width or height of 0 or less uses the texture's size. */
typedef enum wgf_draw_fill_t {
    WGF_DRAW_FILL_LEFT_TO_RIGHT = 0,
    WGF_DRAW_FILL_BOTTOM_TO_TOP = 1,
    WGF_DRAW_FILL_CLOCKWISE = 2
} wgf_draw_fill_t;
WGF_API void wgf_draw_texture_fill(wgf_texture_t texture, float x, float y, float width, float height, float amount,
                                   wgf_draw_fill_t fill, wgf_color_t tint);

/* Begins or ends immediate mode 3D drawing, as `camera` sees the world.
 *
 * - Between begin_3d and end_3d, the 3D calls draw in the world as `camera` (a 3D camera,
 *   wgf_camera3d.h) sees it, into the presentation's visible area, under the clip.
 * - They are depth tested against each other, and against the frame's 3D drawn before them.
 * - A 3D call outside begin_3d and end_3d draws nothing.
 * - A frame starts in 2D. A frame left in 3D ends there.
 * - A 2D call between them lands in the world's x-y plane.
 *
 * begin_3d returns false if `camera` isn't a 3D camera, or outside a frame. */
WGF_API bool wgf_draw_begin_3d(wgf_actor_t camera);
WGF_API void wgf_draw_end_3d(void);

/* Draws shapes in the world, unlit, with lines a pixel wide.
 *
 * - wgf_draw_line_3d draws a line.
 * - wgf_draw_cube and wgf_draw_cube_wires draw a cube, filled or its edges, `width` (x) by
 *   `height` (y) by `length` (z), centered on (cx, cy, cz).
 * - wgf_draw_sphere draws a sphere.
 * - wgf_draw_grid draws a grid of `slices` by `slices` squares, `spacing` apart on the x-z
 *   plane, centered on the origin.
 * - wgf_draw_rectangle_3d draws a filled rectangle, and wgf_draw_circle_3d a circle's
 *   outline. Each is centered on (cx, cy, cz), in its x-y plane, turned by (rx, ry, rz)
 *   radians as an actor's rotation turns it.
 *
 * A color with alpha below 255 is blended over what is behind it, in the order drawn. */
WGF_API void wgf_draw_line_3d(float x0, float y0, float z0, float x1, float y1, float z1, wgf_color_t color);
WGF_API void wgf_draw_cube(float cx, float cy, float cz, float width, float height, float length, wgf_color_t color);
WGF_API void wgf_draw_cube_wires(float cx, float cy, float cz, float width, float height, float length,
                                 wgf_color_t color);
WGF_API void wgf_draw_sphere(float cx, float cy, float cz, float radius, wgf_color_t color);
WGF_API void wgf_draw_grid(int slices, float spacing, wgf_color_t color);
/* Draws debug shapes in the world.
 *
 * - wgf_draw_triangle_3d draws a triangle.
 * - wgf_draw_cylinder and wgf_draw_cylinder_wires draw a cylinder upright (along y),
 *   centered at (cx, cy, cz), filled or as wires.
 * - wgf_draw_sphere_wires draws a sphere as wires.
 * - wgf_draw_capsule_wires draws an upright capsule as wires, `height` from end to end. */
WGF_API void wgf_draw_triangle_3d(float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2,
                                  float z2, wgf_color_t color);
WGF_API void wgf_draw_cylinder(float cx, float cy, float cz, float radius, float height, wgf_color_t color);
WGF_API void wgf_draw_cylinder_wires(float cx, float cy, float cz, float radius, float height, wgf_color_t color);
WGF_API void wgf_draw_sphere_wires(float cx, float cy, float cz, float radius, wgf_color_t color);
WGF_API void wgf_draw_capsule_wires(float cx, float cy, float cz, float radius, float height, wgf_color_t color);
WGF_API void wgf_draw_rectangle_3d(float cx, float cy, float cz, float width, float height, float rx, float ry,
                                   float rz, wgf_color_t color);
WGF_API void wgf_draw_circle_3d(float cx, float cy, float cz, float radius, float rx, float ry, float rz,
                                wgf_color_t color);

/* Draws a model as it stands this frame, for debugging: as wires, as points, or as its skeleton.
 *
 * The model's mesh is placed by the model's actor. For a file's root (wgf_mesh_create),
 * each visible node of its tree is placed by its own actor. A skinned part is posed as its
 * animation has it, using the stage's joint matrices applied on the CPU.
 *
 * - Wires draw every triangle's three edges.
 * - Points draw every vertex, a few pixels wide.
 * - Skeleton draws each joint of each skin of a file's root, or of a node's skin when given
 *   a node. Each joint gets a line from the nearest joint above it, and a joint with none
 *   above it gets a small cross.
 *
 * Drawing is unlit, in `color`, with lines a pixel wide. A crowd's costs add up: wires
 * record 6 vertices for each triangle.
 *
 * Returns false, drawing nothing, outside begin_3d and end_3d, for a handle that isn't a
 * model, or for a model whose mesh isn't READY (such as a file's root before its tree is
 * made). A model with no skin has no skeleton, and draws none. */
WGF_API bool wgf_draw_model_wires(wgf_actor_t model, wgf_color_t color);
WGF_API bool wgf_draw_model_points(wgf_actor_t model, wgf_color_t color);
WGF_API bool wgf_draw_model_skeleton(wgf_actor_t model, wgf_color_t color);

/* Draws text in the world: a block centered on (x, y, z), facing the camera.
 *
 * - Its lines are `size` world units tall. A `size` of 0 or less uses 1.
 * - `font` 0 uses the default font.
 * - The text is depth tested, and doesn't hide what is drawn behind it after it. */
WGF_API void wgf_draw_text_3d(wgf_font_t font, const char *text, float x, float y, float z, float size,
                              wgf_color_t color);

#ifdef __cplusplus
}
#endif

#endif
