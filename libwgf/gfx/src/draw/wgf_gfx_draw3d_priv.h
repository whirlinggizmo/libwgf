#ifndef WGF_GFX_DRAW3D_PRIV_H
#define WGF_GFX_DRAW3D_PRIV_H

#include <stdbool.h>
#include <stdint.h>

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

/* Whether a 3D call draws now: between begin_3d and end_3d, in the frame begun in. */
bool wgf_gfx_priv_draw3d_is_on(void);

/* A model as it stands this frame (wgf_gfx_draw_model.c), what immediate mode's model draws
 * read and a test checks: each primitive of its mesh (or, a file's root, of each of its
 * tree's visible nodes) given to `posed` as its vertices' places in the world (3 floats
 * each, `vertex_count` of them; skinned on the CPU as its skin poses it, with the stage's
 * joint matrices) and its triangles' indices. The places are immediate mode's scratch,
 * good until `posed` returns. False for a handle that isn't a model, or a model with
 * nothing READY to pose (a file's root before its tree is made). */
typedef void (*wgf_gfx_priv_draw_posed_t)(const float *positions, int vertex_count, const uint32_t *indices,
                                          int index_count, void *user);
bool wgf_gfx_priv_draw_model_pose(wgf_actor_t model, wgf_gfx_priv_draw_posed_t posed, void *user);

/* A 3D shape actor's geometry (wgf_gfx_shape3d.c), in its color, placed by `world` (the
 * model-view is left as `world`). */
void wgf_gfx_priv_shape3d_draw(const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *world);

#endif
