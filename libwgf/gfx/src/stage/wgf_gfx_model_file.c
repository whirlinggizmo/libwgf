#include "wgf_model.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

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
 * actor for a node with none, and a light under a node that carries one. Every root is kept
 * on a list (the loader's mesh_done goes through it), so a root made while its mesh loads
 * gets its tree when it loads, and a mesh loaded again (wgf_asset_reload) has its roots'
 * trees made again in place: each node's actor kept, found by its path, moved and given its
 * new mesh, so the game's handles, components, and children on it stay; a node the file no
 * longer has goes with its actor; a new one is made. And what a model of a FAILED mesh
 * draws, since only a file's mesh can fail. Linked with glTF's file alone, which installs
 * it (wgf_gfx_priv_model_file_install). */

/* Every file root, its mesh pending, READY, or FAILED. */
static wgf_actor_t *roots;
static int root_count, root_capacity;

static void forget(wgf_actor_t root)
{
    int i;
    for (i = 0; i < root_count; i++) {
        if (roots[i] == root) roots[i] = roots[--root_count];
    }
}

/* `root` kept, its tree made as its mesh loads; false out of memory. */
static bool add(wgf_actor_t root)
{
    int i;
    for (i = 0; i < root_count; i++) {
        if (roots[i] == root) return true;
    }
    if (root_count == root_capacity) {
        const int capacity = root_capacity > 0 ? root_capacity * 2 : 16;
        wgf_actor_t *grown = (wgf_actor_t *)realloc(roots, sizeof(wgf_actor_t) * (size_t)capacity);
        if (grown == NULL) return false;
        roots = grown;
        root_capacity = capacity;
    }
    roots[root_count++] = root;
    return true;
}

/* An sRGB channel (0..1) from linear light: a file light's color as a light's. */
static unsigned char to_srgb(float c)
{
    const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
    return (unsigned char)(s <= 0.0f ? 0 : (s >= 1.0f ? 255 : (int)(s * 255.0f + 0.5f)));
}

/* The light file light `light` is, under `parent`. */
static wgf_actor_t make_light(const wgf_gfx_priv_mesh_light_t *light, wgf_actor_t parent)
{
    const wgf_actor_t actor = wgf_light_create((wgf_light_type_t)light->type);
    if (actor == 0) return 0;
    wgf_light_set_color(actor, wgf_color_make(to_srgb(light->color[0]), to_srgb(light->color[1]),
                                              to_srgb(light->color[2]), 255));
    wgf_light_set_intensity(actor, light->intensity);
    if (light->range > 0.0f) wgf_light_set_range(actor, light->range);
    if (light->type == WGF_LIGHT_TYPE_SPOT) wgf_light_set_spot_cone(actor, light->inner, light->outer);
    wgf_gfx_priv_actor_of(actor)->from_file = true;
    wgf_actor_set_parent(actor, parent);
    return actor;
}

static bool among(const wgf_actor_t *list, int count, wgf_actor_t actor)
{
    int i;
    for (i = 0; i < count; i++) {
        if (list[i] == actor) return true;
    }
    return false;
}

/* The file's actor named `name` directly under `parent`, not yet taken by this build; 0 for
 * none (an unnamed node is never found: it is made again). */
static wgf_actor_t find_kept(wgf_actor_t parent, const char *name, const wgf_actor_t *taken, int taken_count)
{
    int i;
    if (name[0] == '\0') return 0;
    for (i = 0; i < wgf_actor_get_child_count(parent); i++) {
        const wgf_actor_t child = wgf_actor_get_child(parent, i);
        const wgf_gfx_priv_actor_t *child_ptr = wgf_gfx_priv_actor_of(child);
        if (child_ptr != NULL && child_ptr->from_file && child_ptr->type != WGF_ACTOR_KIND_LIGHT &&
            strcmp(wgf_actor_get_name(child), name) == 0 && !among(taken, taken_count, child)) {
            return child;
        }
    }
    return 0;
}

/* The file's actors under `actor` that this build didn't take, destroyed, with what is
 * under them; the ones it took gone through for theirs. */
static void drop_untaken(wgf_actor_t actor, const wgf_actor_t *taken, int taken_count, int depth)
{
    int i = 0;
    while (depth < 64 && i < wgf_actor_get_child_count(actor)) {
        const wgf_actor_t child = wgf_actor_get_child(actor, i);
        const wgf_gfx_priv_actor_t *child_ptr = wgf_gfx_priv_actor_of(child);
        if (child_ptr != NULL && child_ptr->from_file && !among(taken, taken_count, child)) {
            wgf_actor_destroy(child, WGF_ACTOR_DESTROY_CHILDREN); /* leaves the list: the same index next */
            continue;
        }
        if (child_ptr != NULL && child_ptr->from_file) drop_untaken(child, taken, taken_count, depth + 1);
        i++;
    }
}

/* `root`'s tree, made from its file's mesh's nodes, each under its parent's actor: the
 * actor already there for a node kept (found by its name under its parent's, of the kind
 * the node makes), placed and given its mesh again, and its lights made again. */
static void build(wgf_actor_t root, wgf_mesh_t mesh)
{
    const wgf_gfx_priv_mesh_record_t *record = wgf_gfx_priv_mesh_record(mesh);
    wgf_actor_t *built, *taken;
    int i, taken_count = 0;
    if (record == NULL) return;
    built = (wgf_actor_t *)calloc((size_t)record->node_count + 1, sizeof(wgf_actor_t));
    taken = (wgf_actor_t *)calloc(2 * (size_t)record->node_count + 1, sizeof(wgf_actor_t));
    if (built == NULL || taken == NULL) {
        free(built);
        free(taken);
        wgf_log_error("wgf_model: out of memory making a file's tree");
        return;
    }
    for (i = 0; i < record->node_count; i++) {
        const wgf_gfx_priv_mesh_node_t *node = &record->nodes[i];
        const wgf_actor_t parent = node->parent >= 0 ? built[node->parent] : root;
        const bool shows = node->mesh >= 0 && node->mesh < record->part_count && record->parts[node->mesh] != 0;
        const wgf_actor_kind_t kind = shows ? WGF_ACTOR_KIND_MODEL : WGF_ACTOR_KIND_PLAIN;
        wgf_actor_t actor;
        wgf_gfx_priv_actor_t *actor_ptr;
        int c;
        if (parent == 0) continue; /* its parent couldn't be made */
        actor = find_kept(parent, node->name, taken, taken_count);
        if (actor != 0 && wgf_actor_get_kind(actor) != kind) actor = 0; /* it became another kind: made anew */
        if (actor == 0) {
            actor = shows ? wgf_model_create(0) : wgf_actor_create();
            actor_ptr = wgf_gfx_priv_actor_of(actor);
            if (actor_ptr == NULL) continue;
            actor_ptr->from_file = true;
            if (node->name[0] != '\0') wgf_actor_set_name(actor, node->name);
            wgf_actor_set_parent(actor, parent);
        }
        actor_ptr = wgf_gfx_priv_actor_of(actor);
        if (shows && actor_ptr->as.model.mesh != record->parts[node->mesh] &&
            wgf_gfx_priv_mesh_retain(record->parts[node->mesh])) {
            if (actor_ptr->as.model.mesh != 0) wgf_resource_release(actor_ptr->as.model.mesh);
            actor_ptr->as.model.mesh = record->parts[node->mesh]; /* the nodes showing one mesh share it */
        }
        actor_ptr->position = node->position;
        actor_ptr->rotation = node->rotation;
        actor_ptr->scale = node->scale;
        wgf_gfx_priv_actor_transform_changed(actor);
        built[i] = actor;
        taken[taken_count++] = actor;
        for (c = wgf_actor_get_child_count(actor) - 1; c >= 0; c--) { /* its file's lights, made again */
            const wgf_actor_t child = wgf_actor_get_child(actor, c);
            const wgf_gfx_priv_actor_t *child_ptr = wgf_gfx_priv_actor_of(child);
            if (child_ptr != NULL && child_ptr->from_file && child_ptr->type == WGF_ACTOR_KIND_LIGHT) {
                wgf_actor_destroy(child, WGF_ACTOR_DESTROY_CHILDREN);
            }
        }
        if (node->light >= 0 && node->light < record->light_count) {
            const wgf_actor_t light = make_light(&record->lights[node->light], actor);
            if (light != 0) taken[taken_count++] = light;
        }
    }
    drop_untaken(root, taken, taken_count, 0);
    free(built);
    free(taken);
}

void wgf_gfx_priv_model_mesh_done(wgf_mesh_t mesh)
{
    int i;
    for (i = 0; i < root_count; i++) {
        const wgf_actor_t root = roots[i];
        const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(root);
        if (actor_ptr == NULL || actor_ptr->type != WGF_ACTOR_KIND_MODEL || actor_ptr->as.model.mesh != mesh) continue;
        if (wgf_resource_get_status(mesh) == WGF_RESOURCE_STATUS_READY) build(root, mesh);
        wgf_gfx_priv_model_built(root); /* what a scene put under it, made now (the list may grow) */
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

static const wgf_gfx_priv_model_file_t part = {add, forget, build, wgf_mesh_create, placeholder};

void wgf_gfx_priv_model_file_install(void)
{
    wgf_gfx_priv_model_set_file(&part);
}
