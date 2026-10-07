#include "wgf_model.h"

#include <stdlib.h>
#include <string.h>

#include "material/wgf_gfx_material_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_log.h"

/* Models: actors showing a mesh, holding references to it and to their own materials,
 * libwgt's for generated meshes (a file's actor tree comes with glTF, step 6). */

static wgf_gfx_priv_actor_t *model_of(wgf_actor_t model)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(model);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_MODEL ? actor_ptr : NULL;
}

static bool is_material(wgf_material_t material)
{
    return wgf_gfx_priv_material_get(material) != NULL;
}

/* A model let go of: its mesh and its own materials. */
static void model_free(wgf_actor_t model, wgf_gfx_priv_actor_t *actor_ptr)
{
    int slot;
    (void)model;
    if (actor_ptr->as.model.mesh != 0) wgf_resource_release(actor_ptr->as.model.mesh);
    if (actor_ptr->as.model.materials != NULL) {
        for (slot = 0; slot < WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS; slot++) {
            if (actor_ptr->as.model.materials[slot] != 0) wgf_resource_release(actor_ptr->as.model.materials[slot]);
        }
        free(actor_ptr->as.model.materials);
    }
    actor_ptr->as.model.mesh = 0;
    actor_ptr->as.model.materials = NULL;
}

static const wgf_gfx_priv_actor_kind_t actor_kind = {model_free, NULL};

wgf_actor_t wgf_model_create(wgf_mesh_t mesh)
{
    wgf_actor_t model;
    wgf_gfx_priv_actor_t *actor_ptr;
    if (mesh != 0 && WGF_CORE_PRIV_HANDLE_KIND(mesh) != WGF_CORE_PRIV_HANDLE_KIND_MESH) return 0;
    model = wgf_gfx_priv_actor_create(WGF_ACTOR_KIND_MODEL);
    actor_ptr = wgf_gfx_priv_actor_of(model);
    if (actor_ptr == NULL) return 0;
    wgf_gfx_priv_actor_set_kind(WGF_ACTOR_KIND_MODEL, &actor_kind);
    actor_ptr->as.model.tint = 0xFFFFFFFFu;
    if (mesh != 0 && !wgf_model_set_mesh(model, mesh)) {
        wgf_actor_destroy(model, WGF_ACTOR_DESTROY_CHILDREN);
        return 0;
    }
    return model;
}

static wgf_actor_t create_empty(void)
{
    return wgf_model_create(0);
}

/* The generated mesh `shape` names, made with `params` as its create call takes them. */
static bool set_shape(wgf_actor_t model, const char *shape, const float *params, int count)
{
    wgf_mesh_t mesh = 0;
    bool done;
    if (count >= 3 && strcmp(shape, "plane") == 0) mesh = wgf_mesh_create_plane(params[0], params[1], (int)params[2]);
    else if (count >= 3 && strcmp(shape, "cube") == 0) mesh = wgf_mesh_create_cube(params[0], params[1], params[2]);
    else if (count >= 3 && strcmp(shape, "sphere") == 0) {
        mesh = wgf_mesh_create_sphere(params[0], (int)params[1], (int)params[2]);
    } else if (count >= 3 && strcmp(shape, "cylinder") == 0) {
        mesh = wgf_mesh_create_cylinder(params[0], params[1], (int)params[2]);
    } else if (count >= 3 && strcmp(shape, "cone") == 0) {
        mesh = wgf_mesh_create_cone(params[0], params[1], (int)params[2]);
    } else if (count >= 4 && strcmp(shape, "capsule") == 0) {
        mesh = wgf_mesh_create_capsule(params[0], params[1], (int)params[2], (int)params[3]);
    } else if (count >= 4 && strcmp(shape, "torus") == 0) {
        mesh = wgf_mesh_create_torus(params[0], params[1], (int)params[2], (int)params[3]);
    } else {
        return false;
    }
    done = wgf_model_set_mesh(model, mesh);
    if (mesh != 0) wgf_resource_release(mesh); /* the model holds its own */
    return done && mesh != 0;
}

static const char *describe(wgf_actor_t model, float params[4], int *count)
{
    return wgf_gfx_priv_mesh_describe(wgf_model_get_mesh(model), params, count);
}

static const wgf_gfx_priv_model_hooks_t hooks = {create_empty, set_shape, describe, wgf_model_set_tint,
                                                  wgf_model_get_tint};

void wgf_gfx_priv_model_install(void)
{
    wgf_gfx_priv_set_model_hooks(&hooks);
}

bool wgf_model_set_mesh(wgf_actor_t model, wgf_mesh_t mesh)
{
    wgf_gfx_priv_actor_t *actor_ptr = model_of(model);
    if (actor_ptr == NULL || (mesh != 0 && !wgf_gfx_priv_mesh_retain(mesh))) return false;
    if (actor_ptr->as.model.mesh != 0) wgf_resource_release(actor_ptr->as.model.mesh);
    actor_ptr->as.model.mesh = mesh;
    return true;
}

wgf_mesh_t wgf_model_get_mesh(wgf_actor_t model)
{
    const wgf_gfx_priv_actor_t *actor_ptr = model_of(model);
    return actor_ptr != NULL ? actor_ptr->as.model.mesh : 0;
}

bool wgf_model_set_tint(wgf_actor_t model, wgf_color_t color)
{
    wgf_gfx_priv_actor_t *actor_ptr = model_of(model);
    if (actor_ptr == NULL) return false;
    actor_ptr->as.model.tint = color;
    return true;
}

wgf_color_t wgf_model_get_tint(wgf_actor_t model)
{
    const wgf_gfx_priv_actor_t *actor_ptr = model_of(model);
    return actor_ptr != NULL ? actor_ptr->as.model.tint : 0;
}

bool wgf_model_set_material(wgf_actor_t model, int slot, wgf_material_t material)
{
    wgf_gfx_priv_actor_t *actor_ptr = model_of(model);
    int first, last, s;
    if (actor_ptr == NULL || slot < -1 || slot >= WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS) return false;
    if (material != 0 && !is_material(material)) return false;
    if (actor_ptr->as.model.materials == NULL) {
        if (material == 0) return true; /* nothing of its own to go back from */
        actor_ptr->as.model.materials =
            (wgf_material_t *)calloc(WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS, sizeof(wgf_material_t));
        if (actor_ptr->as.model.materials == NULL) {
            wgf_log_error("wgf_model_set_material: out of memory");
            return false;
        }
    }
    first = slot < 0 ? 0 : slot;
    last = slot < 0 ? WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS - 1 : slot;
    for (s = first; s <= last; s++) {
        wgf_material_t *held = &actor_ptr->as.model.materials[s];
        if (*held == material) continue;
        if (material != 0) wgf_core_priv_resource_retain(material);
        if (*held != 0) wgf_resource_release(*held);
        *held = material;
    }
    return true;
}

wgf_material_t wgf_model_get_material(wgf_actor_t model, int slot)
{
    const wgf_gfx_priv_actor_t *actor_ptr = model_of(model);
    if (actor_ptr == NULL || slot < 0 || slot >= WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS) return 0;
    if (actor_ptr->as.model.materials != NULL && actor_ptr->as.model.materials[slot] != 0) {
        return actor_ptr->as.model.materials[slot];
    }
    return wgf_mesh_get_material(actor_ptr->as.model.mesh, slot);
}
