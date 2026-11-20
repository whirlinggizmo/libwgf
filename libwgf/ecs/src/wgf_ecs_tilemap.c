/* A placed Tiled map in the ecs (wgf_scene_spawn_objects, wgf_body2d_set_grid_from_layer):
 * its objects spawned as a scene's prefabs by their class, and a tile layer's cells as a 2D
 * body's solid grid. Through the map's public calls (wgf_tilemap.h, wgf_tilemap_layer.h); in
 * a file of its own, so a program that places no map links no map through the ecs. */
#include "wgf_body2d.h"
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
#include "wgf_trig.h"

/* The object's middle in its layer's units: half its size across and down from its place,
 * turned with it (a tile object's place is its bottom-left, so up); a point's, a polygon's,
 * or a polyline's place itself. */
static void middle_of(wgf_tilemap_t map, int layer, int object, float *x, float *y)
{
    const wgf_tilemap_shape_t shape = wgf_tilemap_get_object_shape(map, layer, object);
    const wgf_vec2_t place = wgf_tilemap_get_object_position(map, layer, object);
    const wgf_vec2_t size = wgf_tilemap_get_object_size(map, layer, object);
    const float turn = wgf_tilemap_get_object_rotation(map, layer, object);
    const float c = wgf_trig_cos(turn), s = wgf_trig_sin(turn);
    float across = 0.0f, down = 0.0f;
    if (shape == WGF_TILEMAP_SHAPE_RECTANGLE || shape == WGF_TILEMAP_SHAPE_ELLIPSE || shape == WGF_TILEMAP_SHAPE_TEXT) {
        across = size.x * 0.5f, down = size.y * 0.5f;
    } else if (shape == WGF_TILEMAP_SHAPE_TILE) {
        across = size.x * 0.5f, down = -size.y * 0.5f;
    }
    *x = place.x + across * c - down * s;
    *y = place.y + across * s + down * c;
}

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
        wgf_actor_t actor;
        float x, y;
        if (class_name[0] == '\0' || !wgf_scene_has_prefab(scene, class_name)) continue;
        prefab = wgf_scene_find_prefab(scene, class_name);
        middle_of(map, index, object, &x, &y);
        actor = wgf_prefab_spawn_at(prefab, layer, x, y, 0.0f, wgf_tilemap_get_object_rotation(map, index, object));
        if (actor == 0) continue;
        if (wgf_tilemap_get_object_name(map, index, object)[0] != '\0') {
            wgf_actor_set_name(actor, wgf_tilemap_get_object_name(map, index, object));
        }
        if (!wgf_tilemap_is_object_visible(map, index, object)) wgf_actor_set_visible(actor, false);
        set_params(map, index, object, actor);
        spawned++;
    }
    return spawned;
}

bool wgf_body2d_set_grid_from_layer(wgf_actor_t parent, wgf_actor_t layer)
{
    const wgf_tilemap_t map = wgf_tilemap_layer_get_map(layer);
    const int columns = wgf_tilemap_get_columns(map), rows = wgf_tilemap_get_rows(map);
    const int size = wgf_tilemap_get_tile_width(map);
    wgf_vec3_t corner;
    int *cells, count, i;
    bool set;
    if (map == 0 || wgf_gfx_priv_actor_of(parent) == NULL || columns <= 0 || rows <= 0 ||
        size != wgf_tilemap_get_tile_height(map)) {
        return false;
    }
    cells = (int *)malloc(sizeof(int) * (size_t)columns * (size_t)rows);
    if (cells == NULL) return false;
    count = wgf_tilemap_layer_get_cells(layer, cells, columns * rows);
    for (i = 0; i < count; i++) cells[i] = cells[i] != 0 ? 1 : 0;
    /* The layer's top-left in the parent's space. */
    corner = wgf_mat4_transform_point(wgf_mat4_mul(wgf_mat4_invert(wgf_gfx_priv_actor_get_world_matrix(parent)),
                                                   wgf_gfx_priv_actor_get_world_matrix(layer)),
                                      wgf_vec3_make(0.0f, 0.0f, 0.0f));
    set = count == columns * rows && wgf_body2d_set_grid(parent, cells, count, columns, (float)size, corner.x, corner.y);
    free(cells);
    return set;
}
