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
 * What is read: an orthogonal, isometric, staggered, or hexagonal map (its stagger axis and
 * index, its hexagons' side length, an orthogonal map's render order), finite or infinite;
 * its tile layers (CSV, or base64 plain, zlib, or gzip; an infinite map's in chunks), object
 * layers (their draw order), and image layers (their image); groups, flattened into the
 * layers inside them in their order, each named as its own and offset, faded, and hidden
 * with its groups; each tileset's tiles' classes, custom properties, and animations, its
 * object alignment, and its terrain (Wang) sets; every object's shape, place, size, turn,
 * tile, and custom properties; and the custom properties of the map and its layers. What
 * fails the load, logged naming the file: image-collection tilesets, zstd-compressed layers,
 * a tileset `.tsx` (save it as JSON, `.tsj`). Not read: an image layer's repeat, layers'
 * parallax and tint colors, and Tiled's older terrains (before 1.5: open and save the map in
 * a newer Tiled).
 *
 * An infinite map is read as a finite one the size of the box its chunks fill: its cells
 * counted from that box's top-left cell, which is Tiled's column and row
 * get_origin_column and get_origin_row give (negative, for chunks left of or above Tiled's
 * first cell). It is drawn and its objects placed where Tiled has them.
 *
 * A map loaded again in place (wgf_asset_reload, of its file, or of a tileset file or an
 * image it names) keeps its handle and is READY all the while; once the new file is in, each
 * placed copy of it is built again (wgf_tilemap_instantiate).
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
    WGF_TILEMAP_LAYER_KIND_IMAGE = 3
} wgf_tilemap_layer_kind_t;

/* How its cells are laid out: Tiled's orientations. */
typedef enum wgf_tilemap_orientation_t {
    WGF_TILEMAP_ORIENTATION_NONE = 0, /* not a map */
    WGF_TILEMAP_ORIENTATION_ORTHOGONAL = 1,
    WGF_TILEMAP_ORIENTATION_ISOMETRIC = 2, /* diamonds, in a diamond */
    WGF_TILEMAP_ORIENTATION_STAGGERED = 3, /* diamonds, in a rectangle: every other row or column moved */
    WGF_TILEMAP_ORIENTATION_HEXAGONAL = 4  /* hexagons: every other row or column moved */
} wgf_tilemap_orientation_t;

/* How a terrain (Wang) set's tiles meet: at their corners, their edges, or both. */
typedef enum wgf_tilemap_terrain_kind_t {
    WGF_TILEMAP_TERRAIN_KIND_NONE = 0, /* not a terrain set */
    WGF_TILEMAP_TERRAIN_KIND_CORNER = 1,
    WGF_TILEMAP_TERRAIN_KIND_EDGE = 2,
    WGF_TILEMAP_TERRAIN_KIND_MIXED = 3 /* read and drawn, not painted (wgf_tilemap_layer_paint_terrain) */
} wgf_tilemap_terrain_kind_t;

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

/* How its cells are laid out, and whether it is infinite (NONE and false for a map not READY). */
WGF_API wgf_tilemap_orientation_t wgf_tilemap_get_orientation(wgf_tilemap_t map);
WGF_API bool wgf_tilemap_is_infinite(wgf_tilemap_t map);
/* Tiled's column and row of its cell 0, 0: an infinite map's chunks' top-left, 0 for a
 * finite map. */
WGF_API int wgf_tilemap_get_origin_column(wgf_tilemap_t map);
WGF_API int wgf_tilemap_get_origin_row(wgf_tilemap_t map);
/* The size of the box its cells cover, in its pixels, as Tiled gives a map's size (an
 * isometric map's diamond's box; a staggered or hexagonal map's, its tile size made even). */
WGF_API int wgf_tilemap_get_pixel_width(wgf_tilemap_t map);
WGF_API int wgf_tilemap_get_pixel_height(wgf_tilemap_t map);

/* Where a cell is, in the map's pixels from its top-left (y down; a layer's offset not
 * added, wgf_tilemap_layer.h's calls add it): the middle of cell `column`, `row`'s shape (a
 * rectangle, a diamond, or a hexagon), outside the map too; and the column and the row whose
 * shape holds the point `x`, `y` (outside the map too: below 0, or its columns or rows and
 * past), as Tiled finds them. 0 for a map that isn't READY. */
WGF_API wgf_vec2_t wgf_tilemap_get_cell_middle(wgf_tilemap_t map, int column, int row);
WGF_API int wgf_tilemap_get_column_at(wgf_tilemap_t map, float x, float y);
WGF_API int wgf_tilemap_get_row_at(wgf_tilemap_t map, float x, float y);

/* Its layers, groups flattened, in drawing order: how many, one's name and kind (NONE past
 * the end), and the first named `name` (-1 for none). */
WGF_API int wgf_tilemap_get_layer_count(wgf_tilemap_t map);
WGF_API const char *wgf_tilemap_get_layer_name(wgf_tilemap_t map, int layer);
WGF_API wgf_tilemap_layer_kind_t wgf_tilemap_get_layer_kind(wgf_tilemap_t map, int layer);
WGF_API int wgf_tilemap_find_layer(wgf_tilemap_t map, const char *name);
/* An image layer's image (a texture sampled nearest, the map's reference), 0 for a layer of
 * another kind or one with no image. */
WGF_API wgf_texture_t wgf_tilemap_get_layer_texture(wgf_tilemap_t map, int layer);

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
/* A tile object's flips (wgf_tilemap_flip_t's X and Y; Tiled doesn't turn one by its
 * diagonal), 0 for another shape. */
WGF_API int wgf_tilemap_get_object_tile_flips(wgf_tilemap_t map, int layer, int object);
/* An object's middle in the map's pixels (an isometric map's objects' places taken to its
 * pixels): a tile object's tile's as drawn (its place the point of it its tileset's object
 * alignment says: the bottom-left, or an isometric map's bottom middle, by default), a
 * rectangle's, an ellipse's, or a text's half its size across and down from its place, turned
 * with it, and any other shape's place. 0, 0 for an object that isn't one. */
WGF_API wgf_vec2_t wgf_tilemap_get_object_middle(wgf_tilemap_t map, int layer, int object);
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

/* Its terrain (Wang) sets, by tileset and then as Tiled lists them: how many, one's name,
 * kind (NONE past the end), and tileset's index (-1 past the end), and the first named
 * `name` (-1 for none). A set's terrains are numbered from 1 (0 is none, as Tiled's): how
 * many, one's name ("" for 0 or past the end), and the number of the first named `name` (0
 * for none). A tile's terrain (by gid, its flips apart) at `place`, Tiled's order from its
 * top clockwise: 0 its top edge, 1 its top-right corner, 2 its right edge, and so on to 7 its
 * top-left corner (an edge set's tiles have terrains at the even places, a corner set's at
 * the odd); 0 for a tile the set doesn't hold, or a place past 7. Painted into a layer with
 * wgf_tilemap_layer_paint_terrain. */
WGF_API int wgf_tilemap_get_terrainset_count(wgf_tilemap_t map);
WGF_API const char *wgf_tilemap_get_terrainset_name(wgf_tilemap_t map, int set);
WGF_API wgf_tilemap_terrain_kind_t wgf_tilemap_get_terrainset_kind(wgf_tilemap_t map, int set);
WGF_API int wgf_tilemap_get_terrainset_tileset(wgf_tilemap_t map, int set);
WGF_API int wgf_tilemap_find_terrainset(wgf_tilemap_t map, const char *name);
WGF_API int wgf_tilemap_get_terrain_count(wgf_tilemap_t map, int set);
WGF_API const char *wgf_tilemap_get_terrain_name(wgf_tilemap_t map, int set, int terrain);
WGF_API int wgf_tilemap_find_terrain(wgf_tilemap_t map, int set, const char *name);
WGF_API int wgf_tilemap_get_tile_terrain(wgf_tilemap_t map, int set, int gid, int place);

/* The map placed under `parent` (0: none): a plain actor named for its file ("level" for
 * "maps/level.tmj"), in the map's pixels from its top-left, y down (scale it to place the map
 * in other units), with a layer actor under it for each of the map's layers in their order
 * (wgf_tilemap_layer.h): named for its layer, shown or hidden and at its offset as in Tiled.
 * Drawn on a 2D stage, each faded by its layer's opacity:
 *   - a tile layer's cells as sprite batches' quads (wgf_sprite_batch.h) under it, each tile
 *     flipped as the map says, its bottom-left on its cell's box's (a tile larger than the
 *     map's cells reaches up and right, as Tiled draws it), and its animated tiles stepped each
 *     frame, held still under a paused actor (wgf_actor_set_paused). Where no tile can reach
 *     past its cell (an orthogonal map whose tiles are its cells' size, unmoved by their
 *     tilesets' offsets), a batch for each tileset the layer uses; where one can (larger or
 *     moved tiles, and every isometric, staggered, or hexagonal map), the cells are drawn in
 *     Tiled's order, a batch for each run of a tileset's tiles along a row of that order, so a
 *     tile drawn later in Tiled's order is drawn over one before it whatever their tilesets;
 *   - an object layer's tile objects as sprites (wgf_sprite.h) under it, each its tile sized
 *     as the object, at its middle (wgf_tilemap_get_object_middle), turned, flipped, and shown
 *     as Tiled has it, in the layer's draw order (by their y, Tiled's default, or as listed);
 *     wgf_tilemap_layer_get_object_actor finds one;
 *   - an image layer's image as a sprite under it, its top-left on the layer's.
 * The layers hold a reference to the map. 0 for a map that isn't READY, a `parent` that isn't
 * an actor (0 is), or no room for the actors.
 *
 * Built again when the map is loaded again in place (wgf_asset_reload): the root kept, and
 * each layer actor whose layer the new file still has (by name and kind; the first of a
 * name) kept with what was put under it, showing the new file's layer (what set_cell and
 * paint_terrain changed gone with the rest); a layer actor the file no longer has destroyed
 * with what is under it, and a new layer's made; tile objects' sprites made anew. */
WGF_API wgf_actor_t wgf_tilemap_instantiate(wgf_tilemap_t map, wgf_actor_t parent);

#ifdef __cplusplus
}
#endif

#endif /* WGF_TILEMAP_H */
