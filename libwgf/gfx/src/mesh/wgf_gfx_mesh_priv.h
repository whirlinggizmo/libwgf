#ifndef WGF_GFX_MESH_PRIV_H
#define WGF_GFX_MESH_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "sokol_gfx.h"
#include "wgf_mesh.h"
#include "wgf_vec3.h"

/* Meshes, for the stage's drawing: libwgt's, its generated meshes' half (glTF's comes
 * at milestone 2, step 6). A mesh is primitives, each its own triangles drawn with one
 * of the mesh's material slots. A vertex is wgrender's 18 floats, so one layout serves
 * every shader: position 3, normal 3, texture coordinates 2 and 2 (a second set),
 * tangent 4 (w the bitangent's sign), color 4. Indices are 32 bits. */

enum { WGF_GFX_PRIV_MESH_VERTEX_FLOATS = 18 };

/* What drawing a primitive takes: its buffers, uploaded the first time they're asked
 * for, how many indices to draw, its material slot, and its box in the mesh's space. */
typedef struct wgf_gfx_priv_mesh_primitive_t {
    sg_buffer vertices, indices;
    int index_count;
    int material;
    wgf_vec3_t bounds_min, bounds_max;
} wgf_gfx_priv_mesh_primitive_t;

/* One more reference to `mesh`; false when it isn't a mesh. */
bool wgf_gfx_priv_mesh_retain(wgf_mesh_t mesh);

/* The box around the whole mesh, in its own space; false when it isn't a ready mesh. */
bool wgf_gfx_priv_mesh_get_bounds(wgf_mesh_t mesh, wgf_vec3_t *min, wgf_vec3_t *max);

/* How many primitives it has (0 for a handle that isn't a ready mesh), and primitive
 * `index`, its buffers uploaded: false when it hasn't that one, gfx isn't running, or
 * the GPU didn't take them. */
int wgf_gfx_priv_mesh_get_primitive_count(wgf_mesh_t mesh);
bool wgf_gfx_priv_mesh_get_primitive(wgf_mesh_t mesh, int index, wgf_gfx_priv_mesh_primitive_t *primitive);

/* Tangents (4 floats a vertex) from positions, normals, and texture coordinates, in
 * glTF's convention. */
void wgf_gfx_priv_mesh_generate_tangents(const float *positions, const float *normals, const float *uvs,
                                        int vertex_count, const uint32_t *indices, int index_count, float *tangents);


/* What a generated mesh was made as -- "plane", "cube", "sphere", "cylinder", "cone",
 * "capsule", or "torus" -- and its create call's parameters after their clamps, in its
 * order, into `params` (how many: `count`); NULL for anything else. The ecs's dump writes
 * a model's mesh with it. */
const char *wgf_gfx_priv_mesh_describe(wgf_mesh_t mesh, float params[4], int *count);

/* For tests: primitive 0's vertices and indices as made, and how many of each; NULL
 * when it isn't a mesh. */
const float *wgf_gfx_priv_mesh_get_vertices(wgf_mesh_t mesh, int *vertex_count);
const uint32_t *wgf_gfx_priv_mesh_get_indices(wgf_mesh_t mesh, int *index_count);

/* Primitive `primitive`'s vertices (WGF_GFX_PRIV_MESH_VERTEX_FLOATS each) and indices, as
 * made, kept after they are uploaded: what physics makes a convex or mesh body of, a model's
 * mesh at a time. False for a primitive it hasn't. */
bool wgf_gfx_priv_mesh_get_triangles(wgf_mesh_t mesh, int primitive, const float **vertices, int *vertex_count,
                                     const uint32_t **indices, int *index_count);

#endif
