#ifndef WGF_SHADER_H
#define WGF_SHADER_H

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A custom shader: a shader of your own that draws a material.
 *
 * - Write a fragment shader against wgf.glsl, and a vertex hook if you like. wgf.glsl says
 *   what libwgf gives the shader: the surface, its lights, the environment, shadows, and time.
 * - Pack it for every graphics backend with tools/pack_shader.py name.glsl, which writes
 *   name.wgfshader.
 * - wgf.glsl and pack_shader.py come with libwgf. They are in its checkout
 *   (libwgf/gfx/shaders/, tools/), and in each staged variant (share/wgf/shaders/,
 *   share/wgf/tools/).
 * - Load the .wgfshader file here, and draw with it through wgf_material_create_custom
 *   (wgf_material.h). The material's parameters and textures are the shader's. Set them by
 *   their names with the wgf_material_set_* calls.
 * - A surface shader draws a stage's models, but not their shadows. A model casts the shadow
 *   of its mesh as it is, without the vertex hook.
 * - A screen effect redraws the finished frame (wgf_render_add_effect, wgf_render.h).
 * - A shader is a resource (wgf_resource.h): shared by its path, reference counted, and
 *   loaded on create. It is PENDING at once, and READY or FAILED in a later update.
 * - Materials hold their own references to it.
 * - A shader loads again in place (wgf_asset_reload) when its .wgfshader file is saved. Each
 *   material drawn by it keeps the values and textures the new shader still has, by name
 *   and type. One it no longer has is dropped (logged). A file that can't be loaded, or one
 *   of the other kind (a surface shader, a screen effect), keeps the shader as it was, with
 *   one error logged. */
typedef wgf_handle_t wgf_shader_t;

/* Creates a shader from the .wgfshader file at `path`, under fs's root.
 *
 * Returns 0 only if there is no room for another shader. A shader that can't be loaded
 * FAILED (logged once, saying why). The reasons are:
 * - a file made by another version of pack_shader.py;
 * - a file with no program for this graphics backend;
 * - a file the graphics backend refused. */
WGF_API wgf_shader_t wgf_shader_create(const char *path);

#ifdef __cplusplus
}
#endif

#endif
