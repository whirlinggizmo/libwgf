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

/* A Tiled map (mapeditor.org), loaded as a resource (wgf_resource.h).
 *
 * A map is loaded from its JSON: a `.tmj` file, from Tiled's Save As in the JSON map format.
 * Its tilesets are each embedded, or a `.tsj` file beside it. Each tileset is one image, cut
 * into a grid of tiles, with margin and spacing as Tiled has them. On the web, the tilesets
 * and images that a `.tmj` or `.tsj` names are fetched with it. The images are textures
 * (wgf_texture.h), sampled nearest for pixel art. The map holds a reference to each.
 *
 * What is read:
 * - Orthogonal, isometric, staggered, and hexagonal maps, finite or infinite. This includes
 *   the stagger axis and index, the hexagons' side length, and an orthogonal map's render
 *   order.
 * - Tile layers: CSV, or base64 plain, zlib, or gzip. An infinite map's come in chunks.
 * - Object layers, with their draw order, and image layers, with their image.
 * - Groups, flattened into the layers inside them, in their order. Each layer is named as
 *   its own, and is offset, faded, and hidden with its groups.
 * - Each tileset's tile classes, custom properties, and animations, its object alignment,
 *   and its terrain (Wang) sets.
 * - Every object's shape, place, size, turn, tile, and custom properties.
 * - The custom properties of the map and its layers.
 *
 * What fails the load (logged, naming the file):
 * - image-collection tilesets;
 * - zstd-compressed layers;
 * - a `.tsx` tileset. Save it as JSON, `.tsj`, instead.
 *
 * What isn't read:
 * - an image layer's repeat;
 * - layers' parallax and tint colors;
 * - Tiled's older terrains, from before 1.5. Open and save the map in a newer Tiled.
 *
 * An infinite map is read as a finite one, the size of the box its chunks fill. Its cells
 * count from that box's top-left cell. get_origin_column and get_origin_row give that cell's
 * Tiled column and row. They are negative for chunks left of or above Tiled's first cell. The
 * map is drawn, and its objects placed, where Tiled has them.
 *
 * A map can be loaded again in place (wgf_asset_reload), from its file, or from a tileset file
 * or image it names. It keeps its handle, and stays READY all the while. Once the new file is
 * in, each placed copy of the map is built again (wgf_tilemap_instantiate).
 *
 * The map's data can be read once the map is READY. Before that, and for a handle that isn't a map,
 * the calls return 0, "", or none.
 *
 * - Layers are counted in drawing order, with groups flattened. The first layer is under the
 *   rest.
 * - A tile is named by its global id (gid), apart from its flips. Tiled numbers a map's tiles
 *   from 1, across its tilesets. A gid of 0 is no tile. */

/* A Tiled map: a resource handle of kind "gfx.tilemap". */
typedef wgf_handle_t wgf_tilemap_t;

/* What a layer holds. */
typedef enum wgf_tilemap_layer_kind_t {
    WGF_TILEMAP_LAYER_KIND_NONE = 0, /* not a layer */
    WGF_TILEMAP_LAYER_KIND_TILES = 1,
    WGF_TILEMAP_LAYER_KIND_OBJECTS = 2,
    WGF_TILEMAP_LAYER_KIND_IMAGE = 3
} wgf_tilemap_layer_kind_t;

/* How a map's cells are laid out: Tiled's orientations. */
typedef enum wgf_tilemap_orientation_t {
    WGF_TILEMAP_ORIENTATION_NONE = 0, /* not a map */
    WGF_TILEMAP_ORIENTATION_ORTHOGONAL = 1,
    WGF_TILEMAP_ORIENTATION_ISOMETRIC = 2, /* diamonds, in a diamond */
    WGF_TILEMAP_ORIENTATION_STAGGERED = 3, /* diamonds, in a rectangle, with every other row or column moved */
    WGF_TILEMAP_ORIENTATION_HEXAGONAL = 4  /* hexagons, with every other row or column moved */
} wgf_tilemap_orientation_t;

/* How a terrain (Wang) set's tiles meet: at their corners, their edges, or both. */
typedef enum wgf_tilemap_terrain_kind_t {
    WGF_TILEMAP_TERRAIN_KIND_NONE = 0, /* not a terrain set */
    WGF_TILEMAP_TERRAIN_KIND_CORNER = 1,
    WGF_TILEMAP_TERRAIN_KIND_EDGE = 2,
    WGF_TILEMAP_TERRAIN_KIND_MIXED = 3 /* read and drawn, but not painted (wgf_tilemap_layer_paint_terrain) */
} wgf_tilemap_terrain_kind_t;

/* An object's shape. */
typedef enum wgf_tilemap_shape_t {
    WGF_TILEMAP_SHAPE_NONE = 0, /* not an object */
    WGF_TILEMAP_SHAPE_RECTANGLE = 1,
    WGF_TILEMAP_SHAPE_POINT = 2,
    WGF_TILEMAP_SHAPE_ELLIPSE = 3,
    WGF_TILEMAP_SHAPE_POLYGON = 4,
    WGF_TILEMAP_SHAPE_POLYLINE = 5,
    WGF_TILEMAP_SHAPE_TILE = 6, /* a tile placed as an object, by its gid */
    WGF_TILEMAP_SHAPE_TEXT = 7
} wgf_tilemap_shape_t;

/* A tile's flips, as bits.
 *
 * Tiled applies them in this order: across the diagonal first, then left for right, then top
 * for bottom. With the others, the diagonal flip turns a tile by a quarter. */
typedef enum wgf_tilemap_flip_t {
    WGF_TILEMAP_FLIP_NONE = 0,
    WGF_TILEMAP_FLIP_X = 1,       /* left for right */
    WGF_TILEMAP_FLIP_Y = 2,       /* top for bottom */
    WGF_TILEMAP_FLIP_DIAGONAL = 4 /* across the tile's top-left to bottom-right diagonal */
} wgf_tilemap_flip_t;

/* A custom property's type, as Tiled has it. */
typedef enum wgf_tilemap_property_type_t {
    WGF_TILEMAP_PROPERTY_TYPE_NONE = 0, /* no such property */
    WGF_TILEMAP_PROPERTY_TYPE_STRING = 1,
    WGF_TILEMAP_PROPERTY_TYPE_INT = 2,
    WGF_TILEMAP_PROPERTY_TYPE_FLOAT = 3,
    WGF_TILEMAP_PROPERTY_TYPE_BOOL = 4,
    WGF_TILEMAP_PROPERTY_TYPE_COLOR = 5,  /* text "#AARRGGBB" ("" for none) */
    WGF_TILEMAP_PROPERTY_TYPE_FILE = 6,   /* text: the path as Tiled wrote it, relative to the file it is in */
    WGF_TILEMAP_PROPERTY_TYPE_OBJECT = 7, /* a number: an object's id in the map (0 for none) */
    WGF_TILEMAP_PROPERTY_TYPE_CLASS = 8   /* text: the class's members as JSON */
} wgf_tilemap_property_type_t;

/* Creates the map at `path`, a `.tmj` file, with its tilesets' images.
 *
 * It loads as a resource does: it is PENDING at once, and READY or FAILED in a later update.
 * The same path again returns the same map, with one more reference. Returns 0 only if there
 * is no room for another map. */
WGF_API wgf_tilemap_t wgf_tilemap_create(const char *path);

/* Returns the map's size in tiles, and a tile's size in pixels. */
WGF_API int wgf_tilemap_get_columns(wgf_tilemap_t map);
WGF_API int wgf_tilemap_get_rows(wgf_tilemap_t map);
WGF_API int wgf_tilemap_get_tile_width(wgf_tilemap_t map);
WGF_API int wgf_tilemap_get_tile_height(wgf_tilemap_t map);
/* Returns the map's background color, as Tiled sets it, or 0 if it has none. */
WGF_API wgf_color_t wgf_tilemap_get_background_color(wgf_tilemap_t map);

/* Returns how the map's cells are laid out, and whether it is infinite. Returns NONE and false
 * for a map that isn't READY. */
WGF_API wgf_tilemap_orientation_t wgf_tilemap_get_orientation(wgf_tilemap_t map);
WGF_API bool wgf_tilemap_is_infinite(wgf_tilemap_t map);
/* Returns Tiled's column and row of the map's cell 0, 0. For an infinite map, that is its
 * chunks' top-left. For a finite map, it is 0. */
WGF_API int wgf_tilemap_get_origin_column(wgf_tilemap_t map);
WGF_API int wgf_tilemap_get_origin_row(wgf_tilemap_t map);
/* Returns the size of the box the map's cells cover, in the map's pixels, as Tiled gives a
 * map's size.
 *
 * - For an isometric map, it is the box around its diamond.
 * - For a staggered or hexagonal map, it is figured with the tile size made even. */
WGF_API int wgf_tilemap_get_pixel_width(wgf_tilemap_t map);
WGF_API int wgf_tilemap_get_pixel_height(wgf_tilemap_t map);

/* Converts between cells and points, in the map's pixels from its top-left, y down, as Tiled
 * finds them.
 *
 * - get_cell_middle returns the middle of the shape of cell `column`, `row`: a rectangle, a
 *   diamond, or a hexagon. It works outside the map too.
 * - get_column_at and get_row_at return the column and row whose shape holds the point `x`,
 *   `y`. They work outside the map too: below 0, or at its columns or rows and past.
 *
 * A layer's offset isn't added here. wgf_tilemap_layer.h's calls add it. Returns 0 for a map
 * that isn't READY. */
WGF_API wgf_vec2_t wgf_tilemap_get_cell_middle(wgf_tilemap_t map, int column, int row);
WGF_API int wgf_tilemap_get_column_at(wgf_tilemap_t map, float x, float y);
WGF_API int wgf_tilemap_get_row_at(wgf_tilemap_t map, float x, float y);

/* Returns the map's layers, with groups flattened, in drawing order.
 *
 * - get_layer_count returns how many layers there are.
 * - get_layer_name and get_layer_kind return one layer's name and kind. The kind is NONE past
 *   the end.
 * - find_layer returns the first layer named `name`, or -1 for none. */
WGF_API int wgf_tilemap_get_layer_count(wgf_tilemap_t map);
WGF_API const char *wgf_tilemap_get_layer_name(wgf_tilemap_t map, int layer);
WGF_API wgf_tilemap_layer_kind_t wgf_tilemap_get_layer_kind(wgf_tilemap_t map, int layer);
WGF_API int wgf_tilemap_find_layer(wgf_tilemap_t map, const char *name);
/* Returns an image layer's image: a texture sampled nearest, whose reference is the map's.
 * Returns 0 for a layer of another kind, or one with no image. */
WGF_API wgf_texture_t wgf_tilemap_get_layer_texture(wgf_tilemap_t map, int layer);

/* Returns the map's tilesets, in order of first gid: how many there are, and one tileset's
 * name, first gid, tile count, and image. Past the end, each returns 0, and the name "". */
WGF_API int wgf_tilemap_get_tileset_count(wgf_tilemap_t map);
WGF_API const char *wgf_tilemap_get_tileset_name(wgf_tilemap_t map, int tileset);
WGF_API int wgf_tilemap_get_tileset_first_gid(wgf_tilemap_t map, int tileset);
WGF_API int wgf_tilemap_get_tileset_tile_count(wgf_tilemap_t map, int tileset);
WGF_API wgf_texture_t wgf_tilemap_get_tileset_texture(wgf_tilemap_t map, int tileset);

/* Returns an object layer's objects, in Tiled's order.
 *
 * - get_object_count returns how many there are, or 0 for a layer of another kind.
 * - get_object_id returns the object's id in the map.
 * - get_object_name returns its name.
 * - get_object_class returns its class: Tiled's Class, or Type before 1.9.
 * - get_object_shape returns its shape.
 * - get_object_position returns its place, in the map's pixels from the map's top-left, y
 *   down. That is a tile object's bottom-left corner, a point's point, and any other shape's
 *   top-left. Its layer's offset isn't added.
 * - get_object_size returns its size, or 0 for a point, a polygon, or a polyline.
 * - get_object_rotation returns its turn about its place, in radians, clockwise as Tiled's.
 * - is_object_visible returns whether Tiled shows it.
 * - get_object_tile returns a tile object's gid, or 0 for another shape.
 *
 * Each returns "", 0, or none for an object or layer that isn't one. */
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
/* Returns a tile object's flips, as wgf_tilemap_flip_t's X and Y bits, or 0 for another shape.
 * Tiled doesn't flip a tile object across its diagonal. */
WGF_API int wgf_tilemap_get_object_tile_flips(wgf_tilemap_t map, int layer, int object);
/* Returns an object's middle, in the map's pixels. On an isometric map, the object's place is
 * converted to the map's pixels first.
 *
 * - A tile object's middle is its tile's, as drawn. Its place is the point of the tile that
 *   its tileset's object alignment says. By default that is the bottom-left, or the bottom
 *   middle on an isometric map.
 * - A rectangle's, an ellipse's, or a text's middle is half its size across and down from its
 *   place, turned with it.
 * - Any other shape's middle is its place.
 *
 * Returns 0, 0 for an object that isn't one. */
WGF_API wgf_vec2_t wgf_tilemap_get_object_middle(wgf_tilemap_t map, int layer, int object);
/* Returns a polygon's or a polyline's points, relative to its place and unturned.
 *
 * get_object_points writes x and y in turn into `out`, as many as fit in `count` floats. It
 * returns how many floats it filled, or 0 for another shape. get_object_point_count returns
 * how many points the object has. */
WGF_API int wgf_tilemap_get_object_points(wgf_tilemap_t map, int layer, int object, float *out, int count);
WGF_API int wgf_tilemap_get_object_point_count(wgf_tilemap_t map, int layer, int object);

/* Returns custom properties: the map's own (`layer` -1), a layer's (`object` -1), or an
 * object's in that layer.
 *
 * - get_property_count returns how many there are.
 * - get_property_name returns each one's name, in Tiled's order ("" past the end).
 * - get_property_type returns one's type by name (NONE for a property it doesn't have).
 * - get_property returns its value as text: a number written out, or a bool as "true" or
 *   "false". It returns "" for a property it doesn't have.
 * - get_property_number returns its value as a number: a bool as 1 or 0. It returns 0 for a
 *   property it doesn't have, or for text.
 *
 * The text belongs to the map, and is valid while the map is. */
WGF_API int wgf_tilemap_get_property_count(wgf_tilemap_t map, int layer, int object);
WGF_API const char *wgf_tilemap_get_property_name(wgf_tilemap_t map, int layer, int object, int index);
WGF_API wgf_tilemap_property_type_t wgf_tilemap_get_property_type(wgf_tilemap_t map, int layer, int object,
                                                                  const char *name);
WGF_API const char *wgf_tilemap_get_property(wgf_tilemap_t map, int layer, int object, const char *name);
WGF_API double wgf_tilemap_get_property_number(wgf_tilemap_t map, int layer, int object, const char *name);

/* Returns a tile's class ("" for none), and its custom properties as above. The tile is named
 * by its gid, apart from its flips. */
WGF_API const char *wgf_tilemap_get_tile_class(wgf_tilemap_t map, int gid);
WGF_API int wgf_tilemap_get_tile_property_count(wgf_tilemap_t map, int gid);
WGF_API const char *wgf_tilemap_get_tile_property_name(wgf_tilemap_t map, int gid, int index);
WGF_API wgf_tilemap_property_type_t wgf_tilemap_get_tile_property_type(wgf_tilemap_t map, int gid, const char *name);
WGF_API const char *wgf_tilemap_get_tile_property(wgf_tilemap_t map, int gid, const char *name);
WGF_API double wgf_tilemap_get_tile_property_number(wgf_tilemap_t map, int gid, const char *name);

/* Returns the map's terrain (Wang) sets, by tileset and then as Tiled lists them. To paint
 * them into a layer, see wgf_tilemap_layer_paint_terrain.
 *
 * Sets:
 * - get_terrainset_count returns how many there are.
 * - get_terrainset_name returns one set's name.
 * - get_terrainset_kind returns its kind (NONE past the end).
 * - get_terrainset_tileset returns its tileset's index (-1 past the end).
 * - find_terrainset returns the first set named `name` (-1 for none).
 *
 * A set's terrains are numbered from 1. As in Tiled, 0 is none.
 * - get_terrain_count returns how many a set has.
 * - get_terrain_name returns one terrain's name ("" for 0 or past the end).
 * - find_terrain returns the number of the first terrain named `name` (0 for none).
 *
 * get_tile_terrain returns a tile's terrain at `place`. The tile is named by its gid, apart
 * from its flips. Places go in Tiled's order, clockwise from the top:
 * - 0 is its top edge, 1 its top-right corner, 2 its right edge, and so on, to 7, its
 *   top-left corner.
 * - An edge set's tiles have terrains at the even places, and a corner set's at the odd.
 * - It returns 0 for a tile the set doesn't hold, or a `place` past 7. */
WGF_API int wgf_tilemap_get_terrainset_count(wgf_tilemap_t map);
WGF_API const char *wgf_tilemap_get_terrainset_name(wgf_tilemap_t map, int set);
WGF_API wgf_tilemap_terrain_kind_t wgf_tilemap_get_terrainset_kind(wgf_tilemap_t map, int set);
WGF_API int wgf_tilemap_get_terrainset_tileset(wgf_tilemap_t map, int set);
WGF_API int wgf_tilemap_find_terrainset(wgf_tilemap_t map, const char *name);
WGF_API int wgf_tilemap_get_terrain_count(wgf_tilemap_t map, int set);
WGF_API const char *wgf_tilemap_get_terrain_name(wgf_tilemap_t map, int set, int terrain);
WGF_API int wgf_tilemap_find_terrain(wgf_tilemap_t map, int set, const char *name);
WGF_API int wgf_tilemap_get_tile_terrain(wgf_tilemap_t map, int set, int gid, int place);

/* Places the map under `parent` (0 for none), and returns its root actor.
 *
 * The root is a plain actor named for the map's file: "level" for "maps/level.tmj". It is in
 * the map's pixels from its top-left, y down. Scale it to place the map in other units. Under
 * it is a layer actor (wgf_tilemap_layer.h) for each of the map's layers, in their order. Each
 * layer actor is named for its layer, and shown or hidden and offset as in Tiled.
 *
 * The map draws on a 2D stage. Each layer is faded by its opacity.
 *
 * A tile layer's cells are drawn as sprite batches' quads (wgf_sprite_batch.h) under it:
 * - Each tile is flipped as the map says. Its bottom-left is on its cell's box's bottom-left.
 *   A tile larger than the map's cells reaches up and right, as Tiled draws it.
 * - Animated tiles step each frame. They hold still under a paused actor
 *   (wgf_actor_set_paused).
 * - Where no tile can reach past its cell, there is a batch for each tileset the layer uses.
 *   That is an orthogonal map whose tiles are its cells' size, unmoved by their tilesets'
 *   offsets.
 * - Where a tile can reach past its cell, the cells are drawn in Tiled's order, with a batch
 *   for each run of a tileset's tiles along a row of that order. That is a map with larger or
 *   moved tiles, and every isometric, staggered, or hexagonal map. A tile later in Tiled's
 *   order then draws over one before it, whatever their tilesets.
 *
 * An object layer's tile objects are drawn as sprites (wgf_sprite.h) under it:
 * - Each shows its tile, sized as the object, at its middle (wgf_tilemap_get_object_middle).
 * - Each is turned, flipped, and shown as Tiled has it.
 * - They draw in the layer's draw order: by their y (Tiled's default), or as listed.
 * - wgf_tilemap_layer_get_object_actor finds one.
 *
 * An image layer's image is drawn as a sprite under it, its top-left on the layer's.
 *
 * The layers hold a reference to the map. Returns 0 for a map that isn't READY, a `parent`
 * that isn't an actor (0, for none, is allowed), or no room for the actors.
 *
 * When the map is loaded again in place (wgf_asset_reload), its placed copy is built again:
 * - The root is kept.
 * - A layer actor whose layer the new file still has, by name and kind, is kept, with the
 *   actors put under it. Of layers with the same name, the first is matched. It shows the new
 *   file's layer, so changes from set_cell and paint_terrain are gone.
 * - A layer actor whose layer the file no longer has is destroyed, with the actors under it.
 * - A new layer gets a new layer actor.
 * - Tile objects' sprites are made anew. */
WGF_API wgf_actor_t wgf_tilemap_instantiate(wgf_tilemap_t map, wgf_actor_t parent);

#ifdef __cplusplus
}
#endif

#endif /* WGF_TILEMAP_H */
