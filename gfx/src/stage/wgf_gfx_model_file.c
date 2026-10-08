#include "wgf_model.h"

#include <math.h>
#include <stdlib.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "mesh/wgf_gfx_mesh_record_priv.h"
#include "stage/wgf_gfx_model_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_actor.h"
#include "wgf_handle.h"
#include "wgf_light.h"
#include "wgf_log.h"
#include "wgf_resource.h"

/* A glTF file's models (libwgt's build_tree, its nodes made actors): a model of a file's
 * mesh is the file's root, and once the mesh is READY its node tree is made actors under it,
 * each named and placed as in the file: a model of the node's part of the mesh, a plain
 * actor for a node with none, and a light under a node that carries one. A root made while
 * its mesh loads waits for it (the waiting list, gone through as the loader's mesh_done);
 * and what a model of a FAILED mesh draws, since only a file's mesh can fail. Linked with
 * glTF's file alone, which installs it (wgf_gfx_priv_model_file_install). */

/* The file roots waiting for their meshes. */
static wgf_actor_t *waiting;
static int waiting_count, waiting_capacity;

static void forget(wgf_actor_t root)
{
    int i;
    for (i = 0; i < waiting_count; i++) {
        if (waiting[i] == root) waiting[i] = waiting[--waiting_count];
    }
}

/* `root` waiting for its mesh; false out of memory. */
static bool wait(wgf_actor_t root)
{
    if (waiting_count == waiting_capacity) {
        const int capacity = waiting_capacity > 0 ? waiting_capacity * 2 : 16;
        wgf_actor_t *grown = (wgf_actor_t *)realloc(waiting, sizeof(wgf_actor_t) * (size_t)capacity);
        if (grown == NULL) return false;
        waiting = grown;
        waiting_capacity = capacity;
    }
    waiting[waiting_count++] = root;
    return true;
}

/* An sRGB channel (0..1) from linear light: a file light's color as a light's. */
static unsigned char to_srgb(float c)
{
    const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
    return (unsigned char)(s <= 0.0f ? 0 : (s >= 1.0f ? 255 : (int)(s * 255.0f + 0.5f)));
}

/* The light file light `light` is, under `parent`. */
static void make_light(const wgf_gfx_priv_mesh_light_t *light, wgf_actor_t parent)
{
    const wgf_actor_t actor = wgf_light_create((wgf_light_type_t)light->type);
    if (actor == 0) return;
    wgf_light_set_color(actor, wgf_color_make(to_srgb(light->color[0]), to_srgb(light->color[1]),
                                              to_srgb(light->color[2]), 255));
    wgf_light_set_intensity(actor, light->intensity);
    if (light->range > 0.0f) wgf_light_set_range(actor, light->range);
    if (light->type == WGF_LIGHT_TYPE_SPOT) wgf_light_set_spot_cone(actor, light->inner, light->outer);
    wgf_gfx_priv_actor_of(actor)->from_file = true;
    wgf_actor_set_parent(actor, parent);
}

/* `root`'s tree, made from its file's mesh's nodes, each under its parent's actor. */
static void build(wgf_actor_t root, wgf_mesh_t mesh)
{
    const wgf_gfx_priv_mesh_record_t *record = wgf_gfx_priv_mesh_record(mesh);
    wgf_actor_t *built;
    int i;
    if (record == NULL || record->node_count == 0) return;
    built = (wgf_actor_t *)calloc((size_t)record->node_count, sizeof(wgf_actor_t));
    if (built == NULL) {
        wgf_log_error("wgf_model: out of memory making a file's tree");
        return;
    }
    for (i = 0; i < record->node_count; i++) {
        const wgf_gfx_priv_mesh_node_t *node = &record->nodes[i];
        const wgf_actor_t parent = node->parent >= 0 ? built[node->parent] : root;
        const bool shows = node->mesh >= 0 && node->mesh < record->part_count && record->parts[node->mesh] != 0;
        wgf_actor_t actor;
        wgf_gfx_priv_actor_t *actor_ptr;
        if (parent == 0) continue; /* its parent couldn't be made */
        actor = shows ? wgf_model_create(0) : wgf_actor_create();
        actor_ptr = wgf_gfx_priv_actor_of(actor);
        if (actor_ptr == NULL) continue;
        if (shows && wgf_gfx_priv_mesh_retain(record->parts[node->mesh])) {
            actor_ptr->as.model.mesh = record->parts[node->mesh]; /* the nodes showing one mesh share it */
        }
        actor_ptr->from_file = true;
        if (node->name[0] != '\0') wgf_actor_set_name(actor, node->name);
        actor_ptr->position = node->position;
        actor_ptr->rotation = node->rotation;
        actor_ptr->scale = node->scale;
        wgf_gfx_priv_actor_transform_changed(actor);
        wgf_actor_set_parent(actor, parent);
        if (node->light >= 0 && node->light < record->light_count) make_light(&record->lights[node->light], actor);
        built[i] = actor;
    }
    free(built);
}

void wgf_gfx_priv_model_mesh_done(wgf_mesh_t mesh)
{
    int i = 0;
    while (i < waiting_count) {
        const wgf_actor_t root = waiting[i];
        const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(root);
        if (actor_ptr == NULL || actor_ptr->type != WGF_ACTOR_KIND_MODEL || actor_ptr->as.model.mesh != mesh) {
            i++;
            continue;
        }
        waiting[i] = waiting[--waiting_count];
        if (wgf_resource_get_status(mesh) == WGF_RESOURCE_STATUS_READY) build(root, mesh);
    }
}

/* What a failed model draws: a unit cube of the placeholder checker, unlit, made the first
 * time one is drawn and let go of with every mesh and material at gfx's stop (made again
 * after). */
static wgf_mesh_t placeholder_mesh;
static wgf_material_t placeholder_material;

static bool placeholder(wgf_mesh_t *mesh, wgf_material_t *material)
{
    if (!wgf_handle_is_alive(placeholder_mesh) || !wgf_handle_is_alive(placeholder_material)) {
        placeholder_mesh = wgf_mesh_create_cube(1.0f, 1.0f, 1.0f);
        placeholder_material = wgf_material_create(WGF_MATERIAL_SHADING_UNLIT);
        wgf_material_set_texture(placeholder_material, "base_color_texture", wgf_gfx_priv_texture_get_placeholder());
    }
    *mesh = placeholder_mesh;
    *material = placeholder_material;
    return *mesh != 0 && *material != 0;
}

static const wgf_gfx_priv_model_file_t part = {wait, forget, build, wgf_mesh_create, placeholder};

void wgf_gfx_priv_model_file_install(void)
{
    wgf_gfx_priv_model_set_file(&part);
}
