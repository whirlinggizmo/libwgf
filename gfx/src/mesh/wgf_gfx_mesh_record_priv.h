#ifndef WGF_GFX_MESH_RECORD_PRIV_H
#define WGF_GFX_MESH_RECORD_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "sokol_gfx.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_mesh.h"
#include "wgf_quat.h"
#include "wgf_vec3.h"

/* A mesh's record, shared by the mesh's own file (wgf_gfx_mesh.c: generated meshes, a
 * program's triangles, and the calls on every mesh) and glTF's (wgf_gfx_mesh_gltf.c, which
 * fills one from a file), as libwgt's wgt_gfx_mesh_record_priv.h. */

#define WGF_GFX_PRIV_MESH_KEY_MAX 64 /* a generated mesh's: its shape and its parameters' bits */

typedef struct wgf_gfx_priv_mesh_record_primitive_t {
    float *vertices;
    uint32_t *indices;
    int vertex_count, index_count;
    int material; /* its slot in the mesh's materials */
    wgf_vec3_t bounds_min, bounds_max;
    sg_buffer vertex_buffer, index_buffer; /* SG_INVALID_ID until uploaded */
} wgf_gfx_priv_mesh_record_primitive_t;

/* A light a file's node carries (KHR_lights_punctual): what a model's tree makes a light
 * actor of. */
typedef struct wgf_gfx_priv_mesh_light_t {
    int type;        /* wgf_light_type_t */
    float color[3];  /* linear */
    float intensity; /* as the file gives it */
    float range;     /* 0: none */
    float inner, outer; /* a spot's cone, radians */
} wgf_gfx_priv_mesh_light_t;

/* A file's node, its tree depth first, each node's parent before it: what a model of the
 * file makes an actor of (wgf_model.h). */
typedef struct wgf_gfx_priv_mesh_node_t {
    char *name;
    int parent; /* -1: the file's top */
    wgf_vec3_t position;
    wgf_quat_t rotation;
    wgf_vec3_t scale;
    int mesh;  /* what it shows, into the file's meshes; -1 none: a plain actor */
    int light; /* into the file's lights; -1 none */
} wgf_gfx_priv_mesh_node_t;

typedef struct wgf_gfx_priv_mesh_record_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    char key[WGF_GFX_PRIV_MESH_KEY_MAX]; /* a generated mesh's parameters, which find it again; "" for any other */
    const char *shape;                 /* what it was made as ("cube"), and with what: its call's parameters */
    float params[4];
    int param_count;
    wgf_gfx_priv_mesh_record_primitive_t *primitives;
    int primitive_count;
    wgf_material_t *materials; /* one a slot, referenced */
    int material_count;
    wgf_vec3_t bounds_min, bounds_max;
    wgf_gfx_priv_mesh_node_t *nodes; /* a file's; NULL for a mesh made in code */
    int node_count;
    /* A file's meshes, one a glTF mesh, each a mesh of its own (READY, its primitives, the
     * file's materials), referenced: the nodes showing one share it. The file's own record
     * has no primitives. */
    wgf_mesh_t *parts;
    int part_count;
    wgf_gfx_priv_mesh_light_t *lights;
    int light_count;
    bool from_file; /* made by wgf_mesh_create: a model of it is its file's tree */
} wgf_gfx_priv_mesh_record_t;

/* The meshes' pool made and their part installed, once; false out of memory. */
bool wgf_gfx_priv_mesh_ensure_pool(void);
/* A mesh's record, without logging; NULL for a handle that isn't a mesh. */
wgf_gfx_priv_mesh_record_t *wgf_gfx_priv_mesh_record(wgf_mesh_t mesh);
/* What a mesh from a path loads through, set by glTF's file (its only reference). */
void wgf_gfx_priv_mesh_set_loader(const wgf_core_priv_loader_t *loader);
/* A primitive's buffers made on the GPU: false when it didn't take them. */
bool wgf_gfx_priv_mesh_upload(wgf_gfx_priv_mesh_record_primitive_t *primitive);
/* A READY mesh of `count` primitives (the array and what it holds taken) drawn with
 * `materials` (each referenced), not shared by a key: one of a file's meshes. 0 (logged) out
 * of memory or room, the primitives freed. */
wgf_mesh_t wgf_gfx_priv_mesh_create_from(wgf_gfx_priv_mesh_record_primitive_t *primitives, int count,
                                         const wgf_material_t *materials, int material_count);
/* Everything a record (or a load's record, not yet the mesh's) holds, let go of. */
void wgf_gfx_priv_mesh_free_data(wgf_gfx_priv_mesh_record_t *record);

#endif
