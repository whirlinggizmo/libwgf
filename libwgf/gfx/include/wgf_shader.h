#ifndef WGF_SHADER_H
#define WGF_SHADER_H

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A custom shader, libwgt's (wgrender's): a material drawn by a shader of your own.
 *
 * - Write a fragment shader (and, if you like, a vertex hook) against wgf.glsl, which says
 *   what libwgf gives it: the surface, its lights, the environment, shadows, time. Pack it
 *   for every graphics backend with tools/pack_shader.py name.glsl, which writes
 *   name.wgfshader. Both come with libwgf: in its checkout (libwgf/gfx/shaders/, tools/),
 *   and in each staged variant (share/wgf/shaders/, share/wgf/tools/).
 * - Load that file here, and draw with it through wgf_material_create_custom
 *   (wgf_material.h): the material's parameters and textures are the shader's, set by their
 *   names with the wgf_material_set_* calls.
 * - A surface shader draws a stage's models (not their shadows: a model casts the shadow
 *   of its mesh as it is, without the vertex hook).
 * - It is a resource (wgf_resource.h): shared by its path, reference counted, and loaded on
 *   create, PENDING at once and READY or FAILED in a later update. Materials hold their own
 *   references. */
typedef wgf_handle_t wgf_shader_t;

/* The shader in the .wgfshader file at `path` under fs's root. 0 only when there is no
 * room for another; one that can't be loaded FAILED (logged once, saying why: a file made
 * by another version of pack_shader.py, one with no program for this graphics backend, or
 * one the graphics backend refused). */
WGF_API wgf_shader_t wgf_shader_create(const char *path);

#ifdef __cplusplus
}
#endif

#endif
