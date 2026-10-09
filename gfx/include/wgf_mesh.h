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
 * their ranges. A mesh from a file is glTF's (wgf_mesh_create, below); a mesh of a
 * program's own triangles (wgf_mesh_create_triangles) is its own, never shared.
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

/* A mesh of the program's own triangles (a road along a centerline, a terrain): its
 * corners' positions (x, y, z each: position_count floats, a multiple of 3), their normals
 * (as many, or none for smooth ones made from the triangles), their texture coordinates
 * (u, v each, or none for 0, 0), and its triangles as indices into the corners, three
 * each, counterclockwise seen from its front (none: the corners in threes); a count of 0
 * is none, whatever its pointer. Ready at once, as a generated mesh is, with its one
 * material slot, white, not metallic, roughness 0.5; the arrays are copied. 0 (logged)
 * for counts that don't fit, or an index past the corners. */
WGF_API wgf_mesh_t wgf_mesh_create_triangles(const float *positions, int position_count, const float *normals,
                                             int normal_count, const float *uvs, int uv_count, const int *indices,
                                             int index_count);

/* The mesh in a glTF file (.gltf, its buffers and images beside it, or .glb), loaded in the
 * background as a texture is (wgf_resource.h: PENDING, then READY or FAILED, the asset
 * part's search paths finding it), shared by its path. All of its scene's nodes: their
 * meshes (each glTF mesh one mesh, shared by every node showing it, its primitives each
 * with a material slot: glTF's metallic-roughness materials, their textures loaded with
 * it), their names and places, and the lights KHR_lights_punctual gives them. A model of it
 * (wgf_model_set_mesh) is the file's root: once READY, the file's node tree is made its
 * children, a model of the mesh each node shows (wgf_model_get_mesh: one handle for the
 * nodes sharing one) and a plain actor for a node with none, named and placed as in the
 * file, found as any actor is (wgf_actor_find), and a light under a node that carries one.
 * The file's own mesh holds the tree, the materials, and no triangles. A model of a PENDING mesh
 * draws nothing; of a FAILED one, the placeholder checker on a unit cube. Skins,
 * animations, morph targets, cameras, and `extras` (a node's or the file's own data) in the
 * file are not read: a game's data beside its geometry is a file of its own. 0 (logged) for an
 * empty path or one that isn't .gltf or .glb. */
WGF_API wgf_mesh_t wgf_mesh_create(const char *path);

/* The mesh's materials, one a slot (wgf_material.h); a generated mesh has one. The
 * handle is borrowed: valid while the mesh is, and changing it changes every model
 * drawing the mesh with it. 0 for a slot it hasn't. */
WGF_API int wgf_mesh_get_material_count(wgf_mesh_t mesh);
WGF_API wgf_material_t wgf_mesh_get_material(wgf_mesh_t mesh, int slot);

#ifdef __cplusplus
}
#endif

#endif
