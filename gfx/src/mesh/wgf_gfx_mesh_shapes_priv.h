#ifndef WGF_GFX_MESH_SHAPES_PRIV_H
#define WGF_GFX_MESH_SHAPES_PRIV_H

#include <stdbool.h>
#include <stdint.h>

/* Geometry of the generated meshes (wgf_mesh_create_plane, ...): positions, unit
 * normals and texture coordinates per vertex, and triangles front-facing
 * counterclockwise (glTF's convention, y up). Centered on the origin. Pure: the mesh
 * section makes meshes of them. libwgt's (wgrender's wgr_mesh_shapes). Texture
 * coordinates start at the top-left (v down), as glTF's do. */

typedef struct wgf_gfx_priv_mesh_shape_t {
    float *positions; /* 3 per vertex */
    float *normals;   /* 3 per vertex */
    float *uvs;       /* 2 per vertex */
    uint32_t *indices;
    int vertex_count;
    int index_count;
} wgf_gfx_priv_mesh_shape_t;

/* Parameter limits: counts outside them are clamped (sizes aren't: <= 0 fails). */
#define WGF_GFX_PRIV_MESH_MAX_SUBDIVISIONS 256
#define WGF_GFX_PRIV_MESH_MIN_RINGS 2
#define WGF_GFX_PRIV_MESH_MAX_RINGS 256
#define WGF_GFX_PRIV_MESH_MIN_SEGMENTS 3
#define WGF_GFX_PRIV_MESH_MAX_SEGMENTS 512

/* false (and *out empty) for sizes <= 0 or out of memory. */
bool wgf_gfx_priv_mesh_shape_plane(float width, float length, int subdivisions, wgf_gfx_priv_mesh_shape_t *out);
bool wgf_gfx_priv_mesh_shape_cube(float width, float height, float length, wgf_gfx_priv_mesh_shape_t *out);
bool wgf_gfx_priv_mesh_shape_sphere(float radius, int rings, int segments, wgf_gfx_priv_mesh_shape_t *out);
bool wgf_gfx_priv_mesh_shape_cylinder(float radius, float height, int segments, wgf_gfx_priv_mesh_shape_t *out);
bool wgf_gfx_priv_mesh_shape_cone(float radius, float height, int segments, wgf_gfx_priv_mesh_shape_t *out);
bool wgf_gfx_priv_mesh_shape_capsule(float radius, float height, int rings, int segments,
                                     wgf_gfx_priv_mesh_shape_t *out);
bool wgf_gfx_priv_mesh_shape_torus(float radius, float thickness, int rings, int segments,
                                   wgf_gfx_priv_mesh_shape_t *out);
void wgf_gfx_priv_mesh_shape_free(wgf_gfx_priv_mesh_shape_t *shape);

#endif
