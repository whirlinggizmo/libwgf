/* A placed Tiled map in the ecs (wgf_scene_spawn_objects, wgf_arcade2d_set_grid_from_layer):
 * its objects spawned as a scene's prefabs by their class, and a tile layer's cells as a 2D
 * body's solid grid. Through the map's public calls (wgf_tilemap.h, wgf_tilemap_layer.h); in
 * a file of its own, so a program that places no map links no map through the ecs. */
#include "wgf_arcade2d.h"
#include "wgf_scene.h"

#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_behavior.h"
#include "wgf_log.h"
#include "wgf_mat4.h"
#include "wgf_resource.h"
#include "wgf_tilemap.h"
#include "wgf_tilemap_layer.h"

/* The object's properties set on each behavior of `actor`'s, as text. */
static void set_params(wgf_tilemap_t map, int layer, int object, wgf_actor_t actor)
{
    const int behaviors = wgf_actor_get_behavior_count(actor);
    const int properties = wgf_tilemap_get_property_count(map, layer, object);
    int b, p;
    for (b = 0; b < behaviors; b++) {
        const int id = wgf_actor_get_behavior(actor, b);
        for (p = 0; p < properties; p++) {
            const char *name = wgf_tilemap_get_property_name(map, layer, object, p);
            if (!wgf_behavior_set_param(actor, id, name, wgf_tilemap_get_property(map, layer, object, name))) {
                wgf_log_warn("wgf_scene_spawn_objects: object %d's property %s isn't a parameter its behavior %s "
                             "can take (a key of 1 to 63 bytes, text under 256, 32 at most)",
                             wgf_tilemap_get_object_id(map, layer, object), name,
                             wgf_behavior_get_name(actor, id));
            }
        }
    }
}

int wgf_scene_spawn_objects(wgf_scene_t scene, wgf_actor_t layer)
{
    const wgf_tilemap_t map = wgf_tilemap_layer_get_map(layer);
    const int index = wgf_tilemap_layer_get_index(layer);
    int object, spawned = 0;
    if (map == 0 || wgf_tilemap_get_layer_kind(map, index) != WGF_TILEMAP_LAYER_KIND_OBJECTS ||
        wgf_resource_get_status(scene) != WGF_RESOURCE_STATUS_READY) {
        return 0;
    }
    for (object = 0; object < wgf_tilemap_get_object_count(map, index); object++) {
        const char *class_name = wgf_tilemap_get_object_class(map, index, object);
        wgf_prefab_t prefab;
        wgf_actor_t actor, drawn;
        wgf_vec2_t middle;
        if (class_name[0] == '\0' || !wgf_scene_has_prefab(scene, class_name)) continue;
        prefab = wgf_scene_find_prefab(scene, class_name);
        middle = wgf_tilemap_get_object_middle(map, index, object);
        actor = wgf_prefab_spawn_at(prefab, layer, middle.x, middle.y, 0.0f,
                                    wgf_tilemap_get_object_rotation(map, index, object));
        if (actor == 0) continue;
        drawn = wgf_tilemap_layer_get_object_actor(layer, object);
        if (drawn != 0) wgf_actor_destroy(drawn, WGF_ACTOR_DESTROY_CHILDREN); /* the prefab in its place */
        if (wgf_tilemap_get_object_name(map, index, object)[0] != '\0') {
            wgf_actor_set_name(actor, wgf_tilemap_get_object_name(map, index, object));
        }
        if (!wgf_tilemap_is_object_visible(map, index, object)) wgf_actor_set_visible(actor, false);
        set_params(map, index, object, actor);
        spawned++;
    }
    return spawned;
}

bool wgf_arcade2d_set_grid_from_layer(wgf_actor_t parent, wgf_actor_t layer)
{
    const wgf_tilemap_t map = wgf_tilemap_layer_get_map(layer);
    const int columns = wgf_tilemap_get_columns(map), rows = wgf_tilemap_get_rows(map);
    const int size = wgf_tilemap_get_tile_width(map);
    wgf_vec3_t corner;
    int *cells, count, i;
    bool set;
    if (map == 0 || wgf_gfx_priv_actor_of(parent) == NULL || columns <= 0 || rows <= 0 ||
        size != wgf_tilemap_get_tile_height(map) ||
        wgf_tilemap_get_orientation(map) != WGF_TILEMAP_ORIENTATION_ORTHOGONAL) {
        return false;
    }
    cells = (int *)malloc(sizeof(int) * (size_t)columns * (size_t)rows);
    if (cells == NULL) return false;
    count = wgf_tilemap_layer_get_cells(layer, cells, columns * rows);
    for (i = 0; i < count; i++) cells[i] = cells[i] != 0 ? 1 : 0;
    /* The layer's top-left cell's corner in the parent's space (an infinite map's from its origin). */
    corner = wgf_mat4_transform_point(wgf_mat4_mul(wgf_mat4_invert(wgf_gfx_priv_actor_get_world_matrix(parent)),
                                                   wgf_gfx_priv_actor_get_world_matrix(layer)),
                                      wgf_vec3_make((float)(wgf_tilemap_get_origin_column(map) * size),
                                                    (float)(wgf_tilemap_get_origin_row(map) * size), 0.0f));
    set = count == columns * rows &&
          wgf_arcade2d_set_grid(parent, cells, count, columns, (float)size, corner.x, corner.y);
    free(cells);
    return set;
}
