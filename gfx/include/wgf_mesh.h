#ifndef WGF_MESH_H
#define WGF_MESH_H

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_material.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A mesh: a shape's triangles on the GPU, with normals, texture coordinates, and
 * tangents at each corner, so any shading can light it, drawn by the models made from
 * it (wgf_model.h), each of its parts with one of its material slots. It is a resource
 * (wgf_resource.h): shared, reference counted, and held by its models.
 *
 * Generated meshes are shapes made in code, ready at once (even before gfx starts, as
 * they go to the GPU the first time they are drawn), centered on the origin, y up, in
 * units. They are shared by their parameters: the same shape asked for again is the
 * same mesh, with one more reference, and a mesh never changes once made; to size a
 * model differently, scale it, or make another mesh. Each has one material slot,
 * white, not metallic, roughness 0.5: draw a model of it with another with
 * wgf_model_set_material. 0 (logged) for a size of 0 or less; counts are clamped to
 * their ranges. Meshes from files are glTF's (milestone 2, step 6).
 *
 *   plane     flat in x and z, facing +y; subdivisions 0..256 more cells each way.
 *             Texture coordinates span it once
 *   cube      each face its own corners (sharp edges), textured 0..1 per face
 *   sphere    rings 2..256 from pole to pole, segments 3..512 around; u around, v
 *             pole to pole
 *   cylinder  capped; segments 3..512 around
 *   cone      tip up, its base capped
 *   capsule   height from end to end (at least 2 x radius; less: a sphere); rings
 *             across both round ends
 *   torus     around y; radius to the middle of the tube, thickness the tube's
 *             radius; rings around the ring, segments around the tube (3..512) */
typedef wgf_handle_t wgf_mesh_t;

WGF_API wgf_mesh_t wgf_mesh_create_plane(float width, float length, int subdivisions);
WGF_API wgf_mesh_t wgf_mesh_create_cube(float width, float height, float length);
WGF_API wgf_mesh_t wgf_mesh_create_sphere(float radius, int rings, int segments);
WGF_API wgf_mesh_t wgf_mesh_create_cylinder(float radius, float height, int segments);
WGF_API wgf_mesh_t wgf_mesh_create_cone(float radius, float height, int segments);
WGF_API wgf_mesh_t wgf_mesh_create_capsule(float radius, float height, int rings, int segments);
WGF_API wgf_mesh_t wgf_mesh_create_torus(float radius, float thickness, int rings, int segments);

/* The mesh's materials, one a slot (wgf_material.h); a generated mesh has one. The
 * handle is borrowed: valid while the mesh is, and changing it changes every model
 * drawing the mesh with it. 0 for a slot it hasn't. */
WGF_API int wgf_mesh_get_material_count(wgf_mesh_t mesh);
WGF_API wgf_material_t wgf_mesh_get_material(wgf_mesh_t mesh, int slot);

#ifdef __cplusplus
}
#endif

#endif
