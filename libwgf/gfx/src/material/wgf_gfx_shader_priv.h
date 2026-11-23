#ifndef WGF_GFX_SHADER_PRIV_H
#define WGF_GFX_SHADER_PRIV_H

#include <stdbool.h>

#include "sokol_gfx.h"
#include "wgf_material.h"
#include "wgf_shader.h"

/* A custom shader (.wgfshader, written by tools/pack_shader.py), libwgt's
 * wgt_gfx_shader_priv.h: the running backend's programs, and its parameters and textures
 * by name. A surface shader has the model programs (static and skinned) and draws through a stage
 * (stage/wgf_gfx_stage3d_custom.c); a screen effect's (its fragment shader includes
 * wgf_screen) has one program and redraws the finished frame. */

#define WGF_GFX_PRIV_SHADER_MAX_PARAMS 32
#define WGF_GFX_PRIV_SHADER_MAX_TEXTURES 8
#define WGF_GFX_PRIV_SHADER_NAME_MAX 32

typedef enum wgf_gfx_priv_shader_param_type_t {
    WGF_GFX_PRIV_SHADER_PARAM_FLOAT = 0,
    WGF_GFX_PRIV_SHADER_PARAM_INT,
    WGF_GFX_PRIV_SHADER_PARAM_VEC2,
    WGF_GFX_PRIV_SHADER_PARAM_VEC3,
    WGF_GFX_PRIV_SHADER_PARAM_VEC4
} wgf_gfx_priv_shader_param_type_t;

/* The uniform blocks, by binding (libwgf/gfx/shaders/wgf.glsl). */
typedef enum wgf_gfx_priv_shader_block_t {
    WGF_GFX_PRIV_SHADER_BLOCK_OBJECT = 0,    /* libwgf's vertex block: the camera, time, where the records start */
    WGF_GFX_PRIV_SHADER_BLOCK_FRAME = 1,     /* libwgf's fragment block: wgf_frame (or a screen's wgf_screen_frame) */
    WGF_GFX_PRIV_SHADER_BLOCK_FS_PARAMS = 2, /* the shader's fragment parameters */
    WGF_GFX_PRIV_SHADER_BLOCK_VS_PARAMS = 3, /* the vertex hook's parameters */
    WGF_GFX_PRIV_SHADER_BLOCK_COUNT
} wgf_gfx_priv_shader_block_t;

typedef struct wgf_gfx_priv_shader_param_t {
    char name[WGF_GFX_PRIV_SHADER_NAME_MAX];
    wgf_gfx_priv_shader_param_type_t type;
    wgf_gfx_priv_shader_block_t block; /* FS_PARAMS or VS_PARAMS */
    int offset;                        /* bytes into the block (std140) */
} wgf_gfx_priv_shader_param_t;

/* libwgf's textures a program may read, by their names in wgf.glsl. */
typedef enum wgf_gfx_priv_shader_view_t {
    WGF_GFX_PRIV_SHADER_VIEW_ENVIRONMENT, /* wgf_env_tex: the environment's prefiltered cube */
    WGF_GFX_PRIV_SHADER_VIEW_BRDF,        /* wgf_brdf_tex: its split-sum table */
    WGF_GFX_PRIV_SHADER_VIEW_SHADOW,      /* wgf_shadow_tex: the casting lights' maps */
    WGF_GFX_PRIV_SHADER_VIEW_INSTANCE,    /* wgf_instance_tex: the frame's placements */
    WGF_GFX_PRIV_SHADER_VIEW_SCREEN,      /* wgf_screen_tex: a screen effect's frame */
    WGF_GFX_PRIV_SHADER_VIEW_JOINT,       /* wgf_joint_tex: the frame's joint matrices, a skinned part's */
    WGF_GFX_PRIV_SHADER_VIEW_COUNT
} wgf_gfx_priv_shader_view_t;

typedef struct wgf_gfx_priv_shader_program_t {
    sg_shader shader;
    bool has_block[WGF_GFX_PRIV_SHADER_BLOCK_COUNT]; /* the blocks it uses (unused ones are compiled out) */
    int view_slot[WGF_GFX_PRIV_SHADER_MAX_TEXTURES];    /* the shader's texture t: its view slot, -1 unused */
    int sampler_slot[WGF_GFX_PRIV_SHADER_MAX_TEXTURES]; /* and the sampler it is paired with, -1 none */
    int library_view[WGF_GFX_PRIV_SHADER_VIEW_COUNT];    /* libwgf's: each one's view slot, -1 unused */
    int library_sampler[WGF_GFX_PRIV_SHADER_VIEW_COUNT]; /* and its sampler's, -1 none */
} wgf_gfx_priv_shader_program_t;

enum {
    WGF_GFX_PRIV_SHADER_PROGRAM_MODEL,  /* a surface shader's */
    WGF_GFX_PRIV_SHADER_PROGRAM_SCREEN, /* a screen effect's */
    WGF_GFX_PRIV_SHADER_PROGRAM_MODEL_SKINNED, /* a surface shader's, for a skinned part */
    WGF_GFX_PRIV_SHADER_PROGRAM_COUNT
};

typedef struct wgf_gfx_priv_shader_t {
    wgf_gfx_priv_shader_program_t programs[WGF_GFX_PRIV_SHADER_PROGRAM_COUNT];
    /* a surface's models': by blended, double sided, mirrored, skinned; made on first use */
    sg_pipeline pipelines[16];
    sg_pipeline screen_pipelines[2];  /* a screen effect's: by whether its pass is multisampled */
    bool screen;                      /* a screen effect, not a surface shader */
    wgf_gfx_priv_shader_param_t params[WGF_GFX_PRIV_SHADER_MAX_PARAMS];
    int param_count;
    int block_size[WGF_GFX_PRIV_SHADER_BLOCK_COUNT]; /* FS_PARAMS and VS_PARAMS: bytes (std140, a 16-byte multiple) */
    char textures[WGF_GFX_PRIV_SHADER_MAX_TEXTURES][WGF_GFX_PRIV_SHADER_NAME_MAX];
    bool texture_cube[WGF_GFX_PRIV_SHADER_MAX_TEXTURES]; /* a textureCube (a cubemap's), not a texture2D */
    int texture_count;
} wgf_gfx_priv_shader_t;

/* The shader when it is READY; NULL otherwise. The pointer moves when a shader is created:
 * don't keep it across one. */
wgf_gfx_priv_shader_t *wgf_gfx_priv_shader_get(wgf_shader_t shader);

/* Whether `shader` is a shader at all (of any status). */
bool wgf_gfx_priv_shader_is(wgf_shader_t shader);

/* A custom material drawn now (material/wgf_gfx_material_custom.c): its shader when that
 * is READY (NULL otherwise: it draws nothing), `params` its parameter blocks (the fragment
 * block's block_size bytes, then the vertex block's), and `textures[t]` the material's
 * texture for the shader's texture t (NULL: none set). */
struct wgf_gfx_priv_material_texture_t;
const wgf_gfx_priv_shader_t *wgf_gfx_priv_material_custom_bind(
    wgf_material_t material, const unsigned char **params,
    const struct wgf_gfx_priv_material_texture_t *textures[WGF_GFX_PRIV_SHADER_MAX_TEXTURES]);

/* What to bind for the shader's texture `t`, the material's `texture` (NULL: none set): its
 * view and sampler. A texture2D unset, loading, or FAILED is `white`, as libwgt's; a
 * textureCube unset or loading `white_cube`, FAILED a checker. `sampler` comes in as the one
 * for none. */
void wgf_gfx_priv_material_custom_texture_binding(const wgf_gfx_priv_shader_t *shader, int t,
                                                  const struct wgf_gfx_priv_material_texture_t *texture, sg_view white,
                                                  sg_view white_cube, sg_view *view, sg_sampler *sampler);

/* What a cubemap is bound as (texture/wgf_gfx_cubemap.c sets it as its part installs, so a
 * program with none links none of it): its view READY, a checker's FAILED; false while it
 * loads, or for what isn't one. NULL until a cubemap is made. */
extern bool (*wgf_gfx_priv_cubemap_bind)(wgf_handle_t cubemap, sg_view *view);

/* Index of the parameter or texture called `name`, or -1. */
int wgf_gfx_priv_shader_find_param(const wgf_gfx_priv_shader_t *shader, const char *name);
int wgf_gfx_priv_shader_find_texture(const wgf_gfx_priv_shader_t *shader, const char *name);

#endif
