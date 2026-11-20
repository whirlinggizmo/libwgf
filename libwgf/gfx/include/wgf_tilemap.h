#ifndef WGF_TILEMAP_H
#define WGF_TILEMAP_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_handle.h"
#include "wgf_texture.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A Tiled map (mapeditor.org), helix-legacy's TiledMap2D: a resource (wgf_resource.h) loaded
 * from its JSON, a `.tmj` file (Tiled's Save As, JSON map format), with its tilesets, each
 * embedded or a `.tsj` file beside it, and each one image cut into a grid of tiles (margin
 * and spacing as Tiled's). On the web the tilesets and their images named by a `.tmj` or a
 * `.tsj` are fetched with it. Its images are textures (wgf_texture.h) sampled nearest, for
 * pixel art; the map holds a reference to each.
 *
 * What is read: an orthogonal, finite map; its tile layers (CSV, or base64 plain, zlib, or
 * gzip), object layers, and image layers (listed, not drawn); groups, flattened into the
 * layers inside them in their order, each named as its own and offset, faded, and hidden
 * with its groups; each tileset's tiles' classes, custom properties, and animations; every
 * object's shape, place, size, turn, tile, and custom properties; and the custom properties
 * of the map and its layers. What fails the load, logged naming the file: isometric,
 * staggered, or hexagonal maps, infinite ones, image-collection tilesets, zstd-compressed
 * layers, a tileset `.tsx` (save it as JSON, `.tsj`). Terrain (Wang) sets are not read.
 *
 * Everything is read once READY, and 0, "", or none before (and for a handle that isn't a
 * map). Layers are counted in drawing order, groups flattened (the first under the rest);
 * a tile is named by its global id (gid), as Tiled numbers a map's tiles across its
 * tilesets from 1, its flips apart (0 is no tile). */

/* A Tiled map, a resource: a handle of kind "gfx.tilemap". */
typedef wgf_handle_t wgf_tilemap_t;

/* What a layer holds. */
typedef enum wgf_tilemap_layer_kind_t {
    WGF_TILEMAP_LAYER_KIND_NONE = 0, /* not a layer */
    WGF_TILEMAP_LAYER_KIND_TILES = 1,
    WGF_TILEMAP_LAYER_KIND_OBJECTS = 2,
    WGF_TILEMAP_LAYER_KIND_IMAGE = 3 /* listed, not drawn */
} wgf_tilemap_layer_kind_t;

/* An object's shape. */
typedef enum wgf_tilemap_shape_t {
    WGF_TILEMAP_SHAPE_NONE = 0, /* not an object */
    WGF_TILEMAP_SHAPE_RECTANGLE = 1,
    WGF_TILEMAP_SHAPE_POINT = 2,
    WGF_TILEMAP_SHAPE_ELLIPSE = 3,
    WGF_TILEMAP_SHAPE_POLYGON = 4,
    WGF_TILEMAP_SHAPE_POLYLINE = 5,
    WGF_TILEMAP_SHAPE_TILE = 6, /* a tile placed as an object (its gid) */
    WGF_TILEMAP_SHAPE_TEXT = 7
} wgf_tilemap_shape_t;

/* A tile's flips, as bits: Tiled flips it across its diagonal first (with the others, its
 * turns by a quarter), then left for right, then top for bottom. */
typedef enum wgf_tilemap_flip_t {
    WGF_TILEMAP_FLIP_NONE = 0,
    WGF_TILEMAP_FLIP_X = 1,       /* left for right */
    WGF_TILEMAP_FLIP_Y = 2,       /* top for bottom */
    WGF_TILEMAP_FLIP_DIAGONAL = 4 /* across its top-left to bottom-right diagonal */
} wgf_tilemap_flip_t;

/* A custom property's type, Tiled's. */
typedef enum wgf_tilemap_property_type_t {
    WGF_TILEMAP_PROPERTY_TYPE_NONE = 0, /* no such property */
    WGF_TILEMAP_PROPERTY_TYPE_STRING = 1,
    WGF_TILEMAP_PROPERTY_TYPE_INT = 2,
    WGF_TILEMAP_PROPERTY_TYPE_FLOAT = 3,
    WGF_TILEMAP_PROPERTY_TYPE_BOOL = 4,
    WGF_TILEMAP_PROPERTY_TYPE_COLOR = 5,  /* text "#AARRGGBB" ("" for none) */
    WGF_TILEMAP_PROPERTY_TYPE_FILE = 6,   /* text: the path as Tiled wrote it, from the file it is in */
    WGF_TILEMAP_PROPERTY_TYPE_OBJECT = 7, /* a number: an object's id in the map (0 none) */
    WGF_TILEMAP_PROPERTY_TYPE_CLASS = 8   /* text: its members as JSON */
} wgf_tilemap_property_type_t;

/* The map at `path` (a `.tmj` file): loaded as a resource is (PENDING at once, READY or FAILED
 * in a later update), its tilesets' images too. The same path again gives the same map, with
 * one more reference. 0 only when there is no room for another. */
WGF_API wgf_tilemap_t wgf_tilemap_create(const char *path);

/* Its size in tiles, and a tile's size in pixels. */
WGF_API int wgf_tilemap_get_columns(wgf_tilemap_t map);
WGF_API int wgf_tilemap_get_rows(wgf_tilemap_t map);
WGF_API int wgf_tilemap_get_tile_width(wgf_tilemap_t map);
WGF_API int wgf_tilemap_get_tile_height(wgf_tilemap_t map);
/* Its background color (Tiled's), 0 when it has none. */
WGF_API wgf_color_t wgf_tilemap_get_background_color(wgf_tilemap_t map);

/* Its layers, groups flattened, in drawing order: how many, one's name and kind (NONE past
 * the end), and the first named `name` (-1 for none). */
WGF_API int wgf_tilemap_get_layer_count(wgf_tilemap_t map);
WGF_API const char *wgf_tilemap_get_layer_name(wgf_tilemap_t map, int layer);
WGF_API wgf_tilemap_layer_kind_t wgf_tilemap_get_layer_kind(wgf_tilemap_t map, int layer);
WGF_API int wgf_tilemap_find_layer(wgf_tilemap_t map, const char *name);

/* Its tilesets by first gid: how many, one's name, its first gid, how many tiles it has, and
 * its image (0 past the end). */
WGF_API int wgf_tilemap_get_tileset_count(wgf_tilemap_t map);
WGF_API const char *wgf_tilemap_get_tileset_name(wgf_tilemap_t map, int tileset);
WGF_API int wgf_tilemap_get_tileset_first_gid(wgf_tilemap_t map, int tileset);
WGF_API int wgf_tilemap_get_tileset_tile_count(wgf_tilemap_t map, int tileset);
WGF_API wgf_texture_t wgf_tilemap_get_tileset_texture(wgf_tilemap_t map, int tileset);

/* An object layer's objects, in Tiled's order: how many (0 for a layer of another kind), and
 * each one's id in the map, name, class (Tiled's Class, or Type before 1.9), shape, place in
 * the map's pixels from its top-left (y down; a tile object's bottom-left corner, any other
 * shape's top-left, a point's point; its layer's offset not added), size (0 for a point, a
 * polygon, or a polyline), turn about its place (radians, clockwise as Tiled's), whether
 * Tiled shows it, and a tile object's gid (0 for another shape). "", 0, or none for an
 * object or layer that isn't one. */
WGF_API int wgf_tilemap_get_object_count(wgf_tilemap_t map, int layer);
WGF_API int wgf_tilemap_get_object_id(wgf_tilemap_t map, int layer, int object);
WGF_API const char *wgf_tilemap_get_object_name(wgf_tilemap_t map, int layer, int object);
WGF_API const char *wgf_tilemap_get_object_class(wgf_tilemap_t map, int layer, int object);
WGF_API wgf_tilemap_shape_t wgf_tilemap_get_object_shape(wgf_tilemap_t map, int layer, int object);
WGF_API wgf_vec2_t wgf_tilemap_get_object_position(wgf_tilemap_t map, int layer, int object);
WGF_API wgf_vec2_t wgf_tilemap_get_object_size(wgf_tilemap_t map, int layer, int object);
WGF_API float wgf_tilemap_get_object_rotation(wgf_tilemap_t map, int layer, int object);
WGF_API bool wgf_tilemap_is_object_visible(wgf_tilemap_t map, int layer, int object);
WGF_API int wgf_tilemap_get_object_tile(wgf_tilemap_t map, int layer, int object);
/* A polygon's or a polyline's points, from its place (unturned), x and y in turn, into `out`
 * as many as fit in `count` floats, returning how many floats it filled; 0 for another
 * shape. get_object_point_count is how many points it has. */
WGF_API int wgf_tilemap_get_object_points(wgf_tilemap_t map, int layer, int object, float *out, int count);
WGF_API int wgf_tilemap_get_object_point_count(wgf_tilemap_t map, int layer, int object);

/* Custom properties: the map's own (`layer` -1), a layer's (`object` -1), or an object's of
 * that layer. How many, each one's name in Tiled's order ("" past the end), one's type by
 * name (NONE for one it hasn't), its value as text (a number written out, a bool "true" or
 * "false"; "" for one it hasn't), and as a number (a bool 1 or 0; 0 for one it hasn't, or
 * text). Text is the map's, valid while it is. */
WGF_API int wgf_tilemap_get_property_count(wgf_tilemap_t map, int layer, int object);
WGF_API const char *wgf_tilemap_get_property_name(wgf_tilemap_t map, int layer, int object, int index);
WGF_API wgf_tilemap_property_type_t wgf_tilemap_get_property_type(wgf_tilemap_t map, int layer, int object,
                                                                  const char *name);
WGF_API const char *wgf_tilemap_get_property(wgf_tilemap_t map, int layer, int object, const char *name);
WGF_API double wgf_tilemap_get_property_number(wgf_tilemap_t map, int layer, int object, const char *name);

/* A tile's (by gid, its flips apart): its class ("" none), and its custom properties as
 * above. */
WGF_API const char *wgf_tilemap_get_tile_class(wgf_tilemap_t map, int gid);
WGF_API int wgf_tilemap_get_tile_property_count(wgf_tilemap_t map, int gid);
WGF_API const char *wgf_tilemap_get_tile_property_name(wgf_tilemap_t map, int gid, int index);
WGF_API wgf_tilemap_property_type_t wgf_tilemap_get_tile_property_type(wgf_tilemap_t map, int gid, const char *name);
WGF_API const char *wgf_tilemap_get_tile_property(wgf_tilemap_t map, int gid, const char *name);
WGF_API double wgf_tilemap_get_tile_property_number(wgf_tilemap_t map, int gid, const char *name);

/* The map placed under `parent` (0: none): a plain actor named for its file ("level" for
 * "maps/level.tmj"), in the map's pixels from its top-left, y down (scale it to place the map
 * in other units), with a layer actor under it for each of the map's layers in their order
 * (wgf_tilemap_layer.h): named for its layer, shown or hidden and at its offset as in Tiled.
 * A tile layer's cells are drawn on a 2D stage, faded by its opacity, one sprite batch
 * (wgf_sprite_batch.h) under it for each tileset it uses, each tile flipped as the map says,
 * its bottom-left on its cell's (a tile larger than the map's cells reaches up and right, as
 * Tiled draws it), and its animated tiles stepped each frame, held still under a paused actor
 * (wgf_actor_set_paused). The layers hold a reference to the map. 0 for a map that isn't
 * READY, a `parent` that isn't an actor (0 is), or no room for the actors. */
WGF_API wgf_actor_t wgf_tilemap_instantiate(wgf_tilemap_t map, wgf_actor_t parent);

#ifdef __cplusplus
}
#endif

#endif /* WGF_TILEMAP_H */
