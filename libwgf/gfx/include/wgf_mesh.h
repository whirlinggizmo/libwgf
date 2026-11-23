#ifndef WGF_MESH_H
#define WGF_MESH_H

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_material.h"
#include "wgf_resource.h"
#include "wgf_vec3.h"

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
 * their ranges. A mesh from a file is glTF's (wgf_mesh_create, below); a mesh made from
 * numbers, a program's own triangles (wgf_mesh_create_triangles) or heights
 * (wgf_mesh_create_heightmap), is its own, never shared, and can be changed in place.
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

/* A terrain from a grid of heights (raylib's GenMeshHeightmap, its heights from anything a
 * game fills an array from): `columns` heights a row along x, rows along z (height_count /
 * columns of them), each a corner, its y the height times `height`; the grid spans `width`
 * in x and `length` in z, centered on the origin in both, two triangles a cell facing up
 * (counterclockwise from +y), the corners shared, their normals smooth (made from the
 * triangles, as wgf_mesh_create_triangles makes them), and texture coordinates spanning it
 * once (u along x, v along z). A mesh made from numbers, as wgf_mesh_create_triangles's is:
 * its own, ready at once, and changed in place by the calls below. 0 (logged) for fewer
 * than 2 columns or rows, more than 4096 of either, a height_count that isn't a whole
 * number of rows, or a width or length of 0 or less. */
WGF_API wgf_mesh_t wgf_mesh_create_heightmap(const float *heights, int height_count, int columns, float width,
                                             float height, float length);

/* A mesh made from numbers (wgf_mesh_create_triangles, wgf_mesh_create_heightmap) changed in
 * place, raylib's UpdateMeshBuffer: its corners' positions, normals, texture coordinates, or
 * colors replaced from the caller's arrays, as many as it has corners, and sent to the GPU
 * as it is next drawn; its triangles stay as they are. A terrain deformed, water, geometry
 * made each frame. Its first change makes its vertices a buffer the GPU expects to change
 * (a mesh never changed keeps one it doesn't); the GPU takes one change a frame, so of
 * several in a frame it draws the last, the next frame. Each true when changed; false (and
 * nothing changed) for a mesh not made from numbers (a generated shape is shared by its
 * parameters, and a file's is its file's) and for a count that isn't the mesh's corners'
 * (3 a corner for positions and normals, 2 for texture coordinates, 4 for colors), but 0,
 * the default its create call gives:
 *
 *   positions  x, y, z each; its box (wgf_mesh_get_bounds_min, _max) made again. A count
 *              of 0 is refused. Its normals stay as they were: set them, or make them
 *              smooth again with a count of 0
 *   normals    x, y, z each, unit length; 0: smooth, made from the triangles as they are
 *   uvs        u, v each (both texture coordinate sets); 0: all 0, 0
 *   colors     red, green, blue, alpha each, 0..1 (clamped), as a wgf_color_t's channels
 *              are (sRGB, stored linear); the built-in shadings multiply the material's
 *              color by them, and a custom shader reads them as wgf_color; 0: white
 *
 * Its tangents follow. A physics body made from the mesh (wgf_body.h) keeps the shape it
 * was made with: make it again to collide with the new one. */
WGF_API bool wgf_mesh_set_positions(wgf_mesh_t mesh, const float *positions, int position_count);
WGF_API bool wgf_mesh_set_normals(wgf_mesh_t mesh, const float *normals, int normal_count);
WGF_API bool wgf_mesh_set_uvs(wgf_mesh_t mesh, const float *uvs, int uv_count);
WGF_API bool wgf_mesh_set_colors(wgf_mesh_t mesh, const float *colors, int color_count);

/* A mesh of one part's corners: how many (0 for a handle that isn't a READY mesh, and for
 * a mesh of several parts or none, as a file's own is), and their positions, normals,
 * texture coordinates (set 0), or colors (sRGB, as wgf_mesh_set_colors takes them; a file's
 * COLOR_0, white where it has none), as many whole corners as fit in the caller's array:
 * each returns how many numbers it filled. Any such mesh's: a generated shape's and a
 * file's too. */
WGF_API int wgf_mesh_get_vertex_count(wgf_mesh_t mesh);
WGF_API int wgf_mesh_get_positions(wgf_mesh_t mesh, float *positions, int position_count);
WGF_API int wgf_mesh_get_normals(wgf_mesh_t mesh, float *normals, int normal_count);
WGF_API int wgf_mesh_get_uvs(wgf_mesh_t mesh, float *uvs, int uv_count);
WGF_API int wgf_mesh_get_colors(wgf_mesh_t mesh, float *colors, int color_count);

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

/* One mesh of several, drawn with fewer draws (helix-raylib's LoadModelFromModels): a
 * character's head, body, legs, and feet as one, posed once and drawn as few primitives.
 * `meshes`, `count` of them (1 to 64), are glTF files' meshes or meshes made in code; each is
 * held until the merged mesh is made, so they may be let go of at once. The merged mesh is
 * PENDING until every one of them (and `skeleton`) is READY, then made; FAILED when one
 * FAILED, or the skeleton has no skin.
 *
 * Primitives whose materials differ only in base color are one primitive, each color
 * multiplied into its vertices' colors and drawn by a copy of the first material made white;
 * any other difference (shading, alpha, double sided, metal, roughness, emission, a texture or
 * its sampling) keeps them apart, and a custom material merges only with itself.
 *
 * With `skeleton`, a glTF file's mesh with a skin (usually one of `meshes`), the merged mesh is
 * a file's: its tree the skeleton's nodes, their names, places, and animations (a model of it
 * is the file's root, wgf_model.h: its joints are actors, wgf_model_find_node finds a socket,
 * wgf_model_set_animations gives it clips), the skeleton's own meshes not drawn but as pieces.
 * Each piece's skinned primitives are bound to the skeleton's first skin by its joints' names:
 * a joint the skeleton hasn't follows its nearest ancestor the skeleton has, and a vertex
 * following none follows the skin's first joint. A piece bound in another bind pose is bound
 * again to the skeleton's. A piece's primitive that isn't skinned follows the joint its node
 * or nearest ancestor is named as (a helmet under "Head"), placed as its file placed it under
 * that node; one under no such node, and a mesh made in code, stays where it is, under the
 * root. With 0, the merged mesh is a plain one with no tree: every primitive placed where its
 * file put it at rest, a skinned one at its bind pose.
 *
 * Lights, and a piece's own animations, aren't carried. A piece loaded again (wgf_asset_reload)
 * doesn't change the merged mesh. 0 (logged) for no meshes or more than 64, a handle that isn't
 * a mesh, or a skeleton that isn't a glTF file's mesh. */
WGF_API wgf_mesh_t wgf_mesh_create_merged(const wgf_mesh_t *meshes, int count, wgf_mesh_t skeleton);

/* The mesh's materials, one a slot (wgf_material.h); a generated mesh has one. The
 * handle is borrowed: valid while the mesh is, and changing it changes every model
 * drawing the mesh with it. 0 for a slot it hasn't. */
WGF_API int wgf_mesh_get_material_count(wgf_mesh_t mesh);
WGF_API wgf_material_t wgf_mesh_get_material(wgf_mesh_t mesh, int slot);

/* The box around the mesh in its own space, its least and greatest corner; 0, 0, 0 for
 * both while it isn't READY, and for a handle that isn't a mesh. */
WGF_API wgf_vec3_t wgf_mesh_get_bounds_min(wgf_mesh_t mesh);
WGF_API wgf_vec3_t wgf_mesh_get_bounds_max(wgf_mesh_t mesh);

#ifdef __cplusplus
}
#endif

#endif
