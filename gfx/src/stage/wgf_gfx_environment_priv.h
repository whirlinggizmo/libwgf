#ifndef WGF_GFX_ENVIRONMENT_PRIV_H
#define WGF_GFX_ENVIRONMENT_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "sokol_gfx.h"
#include "wgf_handle.h"
#include "wgf_mat4.h"
#include "wgf_vec3.h"

/* Environments, for gfx's own code: the CPU preparation of an environment map (libwgt's
 * wgt_gfx_environment, wgrender's wgr_environment), and how a stage reaches one. An
 * optional part (wgf_gfx_environment.c), installed by the first wgf_environment_create,
 * which sets the stage's hooks (below): a 3D program with no environment links none of it.
 *
 * Conventions:
 * - Directions are world space, +y up.
 * - Equirectangular (latitude-longitude) images: u runs around the horizon with u = 0.5
 *   looking toward -z and u increasing toward +x; v = 0 is straight up.
 * - Cubemap faces are ordered +X, -X, +Y, -Y, +Z, -Z with OpenGL's face orientation (s, t
 *   in 0..1, t = 0 at the first row of the face's data).
 * - Spherical harmonics are 9 RGB coefficients, convolved for Lambert diffuse and divided
 *   by pi: evaluated at a normal they give the irradiance over pi. */

#define WGF_GFX_PRIV_ENVIRONMENT_CUBE_SIZE 128 /* the prefiltered cube's face (mip 0) */
#define WGF_GFX_PRIV_ENVIRONMENT_MIP_COUNT 6   /* 128 .. 4: roughness 0, 0.2, .. 1 */

/* A linear RGB float image (3 floats a pixel). */
typedef struct wgf_gfx_priv_env_image_t {
    float *rgb;
    int width;
    int height;
} wgf_gfx_priv_env_image_t;

/* A cubemap with a mip chain: mips[m] holds 6 faces of (size >> m)^2 RGB texels. */
typedef struct wgf_gfx_priv_env_cube_t {
    int size;
    int mip_count;
    float *mips[16];
} wgf_gfx_priv_env_cube_t;

typedef struct wgf_gfx_priv_env_sh_t {
    float c[9][3];
} wgf_gfx_priv_env_sh_t;

wgf_vec3_t wgf_gfx_priv_environment_equirect_dir(float u, float v);
void wgf_gfx_priv_environment_dir_equirect(wgf_vec3_t dir, float *u, float *v);
wgf_vec3_t wgf_gfx_priv_environment_cube_dir(int face, float s, float t);
void wgf_gfx_priv_environment_dir_cube(wgf_vec3_t dir, int *face, float *s, float *t);
wgf_vec3_t wgf_gfx_priv_environment_sample_equirect(const wgf_gfx_priv_env_image_t *image, wgf_vec3_t dir);
wgf_vec3_t wgf_gfx_priv_environment_sample_cube(const wgf_gfx_priv_env_cube_t *cube, wgf_vec3_t dir, float lod);
void wgf_gfx_priv_environment_project_sh(const wgf_gfx_priv_env_image_t *image, wgf_gfx_priv_env_sh_t *out);
wgf_vec3_t wgf_gfx_priv_environment_eval_sh(const wgf_gfx_priv_env_sh_t *sh, wgf_vec3_t normal);
/* A cubemap of `size` from an equirectangular image, its mips box-filtered down to 1. */
bool wgf_gfx_priv_environment_cube_from_equirect(const wgf_gfx_priv_env_image_t *image, int size,
                                                 wgf_gfx_priv_env_cube_t *out);
/* A GGX-prefiltered cubemap: mip m is the source seen through roughness m / (mip_count - 1),
 * `samples` importance samples a texel. */
bool wgf_gfx_priv_environment_prefilter(const wgf_gfx_priv_env_cube_t *source, int size, int mip_count, int samples,
                                        wgf_gfx_priv_env_cube_t *out);
void wgf_gfx_priv_environment_cube_free(wgf_gfx_priv_env_cube_t *cube);
uint16_t wgf_gfx_priv_environment_half_from_float(float value);
/* An image file decoded to linear RGB floats: a .hdr as it is, a PNG or JPEG from sRGB.
 * False when it can't be read or decoded. Freed with free(out->rgb). */
bool wgf_gfx_priv_environment_load_image(const char *path, wgf_gfx_priv_env_image_t *out);

/* Whether `bytes` are a Radiance .hdr environments read: its own decoder's, in rows. */
bool wgf_gfx_priv_environment_is_hdr(const unsigned char *bytes, int size);

/* What the model shader binds for a READY environment: its prefiltered cube, its
 * irradiance, its last mip, and the split-sum BRDF table, with their samplers. */
typedef struct wgf_gfx_priv_env_lighting_t {
    sg_view cube;
    sg_sampler cube_sampler;
    wgf_gfx_priv_env_sh_t sh;
    float max_lod;
    sg_view brdf;
    sg_sampler brdf_sampler;
} wgf_gfx_priv_env_lighting_t;

/* The lighting hook itself, for the tests. */
bool wgf_gfx_priv_environment_get_lighting(wgf_handle_t environment, wgf_gfx_priv_env_lighting_t *out);

/* The stage's ways to environments, set by the environment part as it installs (NULL
 * before): an environment's lighting (false, nothing set, for one not READY), and its
 * background drawn into the open pass behind everything, seen through `view_projection`
 * (GL's -1..1 clip depth), `blur` 0..1, its intensity and rotation about +y, tone mapped
 * as the stage is (nothing for one not READY). The functions live in the stage
 * (wgf_gfx_stage3d.c), which every 3D program links. */
typedef struct wgf_gfx_priv_environment_hooks_t {
    bool (*lighting)(wgf_handle_t environment, wgf_gfx_priv_env_lighting_t *out);
    void (*background)(wgf_handle_t environment, wgf_mat4_t view_projection, float blur, float intensity,
                       float rotation, int tonemap, float exposure);
} wgf_gfx_priv_environment_hooks_t;
void wgf_gfx_priv_set_environment_hooks(const wgf_gfx_priv_environment_hooks_t *hooks);

#endif
