#include "wgf_model.h"

#include <stdlib.h>

#include "material/wgf_gfx_material_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "node/wgf_gfx_node_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_log.h"

/* Models: nodes showing a mesh, holding references to it and to their own materials,
 * libwgt's for generated meshes (a file's node tree comes with glTF, step 6). */

static wgf_gfx_priv_node_t *model_of(wgf_node_t model)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(model);
    return node_ptr != NULL && node_ptr->type == WGF_NODE_TYPE_MODEL ? node_ptr : NULL;
}

static bool is_material(wgf_material_t material)
{
    return wgf_gfx_priv_material_get(material) != NULL;
}

/* A model let go of: its mesh and its own materials. */
static void model_free(wgf_node_t model, wgf_gfx_priv_node_t *node_ptr)
{
    int slot;
    (void)model;
    if (node_ptr->as.model.mesh != 0) wgf_resource_release(node_ptr->as.model.mesh);
    if (node_ptr->as.model.materials != NULL) {
        for (slot = 0; slot < WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS; slot++) {
            if (node_ptr->as.model.materials[slot] != 0) wgf_resource_release(node_ptr->as.model.materials[slot]);
        }
        free(node_ptr->as.model.materials);
    }
    node_ptr->as.model.mesh = 0;
    node_ptr->as.model.materials = NULL;
}

static const wgf_gfx_priv_node_kind_t node_kind = {model_free, NULL};

wgf_node_t wgf_model_create(wgf_mesh_t mesh)
{
    wgf_node_t model;
    wgf_gfx_priv_node_t *node_ptr;
    if (mesh != 0 && WGF_CORE_PRIV_HANDLE_KIND(mesh) != WGF_CORE_PRIV_HANDLE_KIND_MESH) return 0;
    model = wgf_gfx_priv_node_create(WGF_NODE_TYPE_MODEL);
    node_ptr = wgf_gfx_priv_node_of(model);
    if (node_ptr == NULL) return 0;
    wgf_gfx_priv_node_set_kind(WGF_NODE_TYPE_MODEL, &node_kind);
    node_ptr->as.model.tint = 0xFFFFFFFFu;
    if (mesh != 0 && !wgf_model_set_mesh(model, mesh)) {
        wgf_node_destroy(model, WGF_NODE_DESTROY_CHILDREN);
        return 0;
    }
    return model;
}

bool wgf_model_set_mesh(wgf_node_t model, wgf_mesh_t mesh)
{
    wgf_gfx_priv_node_t *node_ptr = model_of(model);
    if (node_ptr == NULL || (mesh != 0 && !wgf_gfx_priv_mesh_retain(mesh))) return false;
    if (node_ptr->as.model.mesh != 0) wgf_resource_release(node_ptr->as.model.mesh);
    node_ptr->as.model.mesh = mesh;
    return true;
}

wgf_mesh_t wgf_model_get_mesh(wgf_node_t model)
{
    const wgf_gfx_priv_node_t *node_ptr = model_of(model);
    return node_ptr != NULL ? node_ptr->as.model.mesh : 0;
}

bool wgf_model_set_tint(wgf_node_t model, wgf_color_t color)
{
    wgf_gfx_priv_node_t *node_ptr = model_of(model);
    if (node_ptr == NULL) return false;
    node_ptr->as.model.tint = color;
    return true;
}

wgf_color_t wgf_model_get_tint(wgf_node_t model)
{
    const wgf_gfx_priv_node_t *node_ptr = model_of(model);
    return node_ptr != NULL ? node_ptr->as.model.tint : 0;
}

bool wgf_model_set_material(wgf_node_t model, int slot, wgf_material_t material)
{
    wgf_gfx_priv_node_t *node_ptr = model_of(model);
    int first, last, s;
    if (node_ptr == NULL || slot < -1 || slot >= WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS) return false;
    if (material != 0 && !is_material(material)) return false;
    if (node_ptr->as.model.materials == NULL) {
        if (material == 0) return true; /* nothing of its own to go back from */
        node_ptr->as.model.materials =
            (wgf_material_t *)calloc(WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS, sizeof(wgf_material_t));
        if (node_ptr->as.model.materials == NULL) {
            wgf_log_error("wgf_model_set_material: out of memory");
            return false;
        }
    }
    first = slot < 0 ? 0 : slot;
    last = slot < 0 ? WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS - 1 : slot;
    for (s = first; s <= last; s++) {
        wgf_material_t *held = &node_ptr->as.model.materials[s];
        if (*held == material) continue;
        if (material != 0) wgf_core_priv_resource_retain(material);
        if (*held != 0) wgf_resource_release(*held);
        *held = material;
    }
    return true;
}

wgf_material_t wgf_model_get_material(wgf_node_t model, int slot)
{
    const wgf_gfx_priv_node_t *node_ptr = model_of(model);
    if (node_ptr == NULL || slot < 0 || slot >= WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS) return 0;
    if (node_ptr->as.model.materials != NULL && node_ptr->as.model.materials[slot] != 0) {
        return node_ptr->as.model.materials[slot];
    }
    return wgf_mesh_get_material(node_ptr->as.model.mesh, slot);
}
