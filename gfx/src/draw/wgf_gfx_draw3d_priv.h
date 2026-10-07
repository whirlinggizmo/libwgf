#ifndef WGF_GFX_DRAW3D_PRIV_H
#define WGF_GFX_DRAW3D_PRIV_H

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_color.h"
#include "wgf_mat4.h"

/* Immediate mode's 3D geometry (wgf_gfx_draw3d.c), wgrender's shape3d's, recorded into
 * the frame's sokol_gl recording through whatever projection and model-view are loaded:
 * shared by wgf_draw.h's 3D calls and a stage's 3D shape actors. The rectangle and the
 * circle are in the local x-y plane, about the origin. */
void wgf_gfx_priv_draw3d_line(float x0, float y0, float z0, float x1, float y1, float z1, wgf_color_t color);
void wgf_gfx_priv_draw3d_cube(float cx, float cy, float cz, float width, float height, float length,
                              wgf_color_t color);
void wgf_gfx_priv_draw3d_sphere(float cx, float cy, float cz, float radius, wgf_color_t color);
void wgf_gfx_priv_draw3d_rectangle(float width, float height, wgf_color_t color);
void wgf_gfx_priv_draw3d_circle(float radius, wgf_color_t color);
void wgf_gfx_priv_draw3d_strip(const float *points, int count, wgf_color_t color);

/* A 3D shape actor's geometry (wgf_gfx_shape3d.c), in its color, placed by `world` (the
 * model-view is left as `world`). */
void wgf_gfx_priv_shape3d_draw(const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *world);

#endif
