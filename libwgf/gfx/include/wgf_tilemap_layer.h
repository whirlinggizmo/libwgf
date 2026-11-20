#ifndef WGF_TILEMAP_LAYER_H
#define WGF_TILEMAP_LAYER_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_tilemap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A placed map's layer (WGF_ACTOR_KIND_TILEMAP_LAYER), made by wgf_tilemap_instantiate: an
 * actor named for its layer, in the map's pixels from its top-left (y down), placed, shown,
 * and parented with the actor calls. A tile layer's cells are its own once placed: read and
 * set here, set ones drawn from the next frame (the map's file unchanged). Every call is
 * false, 0, or -1 for an actor that isn't a placed layer; a layer of objects or an image has
 * no cells.
 *
 * A cell is a tile's gid (0 empty) and its flips (wgf_tilemap_flip_t's bits). In bulk
 * (get_cells, set_cells), a cell is one int: its gid plus its flips times 0x10000000. Columns
 * and rows are the map's (wgf_tilemap.h: an isometric map's along its axes, a staggered or
 * hexagonal map's as Tiled counts them, an infinite map's from its origin). */

/* The map it is of, and its layer's index there. */
WGF_API wgf_tilemap_t wgf_tilemap_layer_get_map(wgf_actor_t layer);
WGF_API int wgf_tilemap_layer_get_index(wgf_actor_t layer);

/* The tile at `column`, `row` (from the top-left), and its flips: 0 for an empty cell, or
 * one outside the layer. */
WGF_API int wgf_tilemap_layer_get_cell(wgf_actor_t layer, int column, int row);
WGF_API int wgf_tilemap_layer_get_cell_flips(wgf_actor_t layer, int column, int row);
/* The cell given tile `gid` flipped by `flips` (0 empties it), drawn so from the next frame
 * (an animated tile's animation starting in step with the others). False for a cell outside
 * the layer, a gid none of the map's tilesets holds, or flips past the three bits. */
WGF_API bool wgf_tilemap_layer_set_cell(wgf_actor_t layer, int column, int row, int gid, int flips);

/* Every cell, row by row from the top-left, as one int each (above), into `out`, as many as
 * fit in `count`, returning how many it filled: what a game saves of a layer it changed. */
WGF_API int wgf_tilemap_layer_get_cells(wgf_actor_t layer, int *out, int count);
/* Every cell at once, as get_cells gives them. False, changing nothing, for a count other
 * than the map's columns times its rows, or a cell set_cell refuses. */
WGF_API bool wgf_tilemap_layer_set_cells(wgf_actor_t layer, const int *cells, int count);

/* The column and the row at the point `x`, `y` in its parent's units (the placed map's: the
 * map's pixels from its top-left), through the layer's own place, turn, and scale: outside
 * the layer too (below 0, or its columns or rows and past). 0 for an actor that isn't a
 * placed layer. */
WGF_API int wgf_tilemap_layer_get_column_at(wgf_actor_t layer, float x, float y);
WGF_API int wgf_tilemap_layer_get_row_at(wgf_actor_t layer, float x, float y);
/* The middle of cell `column`, `row` (outside the layer too) in its parent's units: the
 * reverse of get_column_at and get_row_at. 0, 0 for an actor that isn't a placed layer. */
WGF_API wgf_vec2_t wgf_tilemap_layer_get_cell_middle(wgf_actor_t layer, int column, int row);

/* Terrain `terrain` of terrain set `set` (wgf_tilemap.h, numbered from 1; 0 none) painted
 * into cell `column`, `row` as Tiled's Terrain brush paints it (helix-legacy's setTerrain): a
 * corner set's cell takes it at its four corners, an edge set's at its four edges, and the
 * cell and the neighbors sharing them take the set's tile that fits what is around them (the
 * first in the set's order; flips are never chosen). The terrains around are read from the
 * layer's cells the first time a set is painted, and kept as painted until a cell is set
 * another way (set_cell, set_cells), when they are read again. Where none of the set's tiles
 * fits, a cell is left as it was; where nothing of the set is left, a cell showing a tile of
 * the set's tileset is emptied. False, painting nothing, for a cell outside the layer, a set
 * or terrain the map hasn't, a mixed set, or a staggered or hexagonal map; true once painted,
 * whether every cell found its tile or not (each does when the set has a tile for every
 * arrangement). */
WGF_API bool wgf_tilemap_layer_paint_terrain(wgf_actor_t layer, int column, int row, int set, int terrain);

/* A tile object of an object layer drawn as its tile (wgf_tilemap_instantiate): its sprite
 * (wgf_sprite.h), under the layer. 0 for an object that isn't a tile, an object past the
 * layer's, or a sprite destroyed (wgf_scene_spawn_objects destroys one it spawns a prefab in
 * place of). */
WGF_API wgf_actor_t wgf_tilemap_layer_get_object_actor(wgf_actor_t layer, int object);

#ifdef __cplusplus
}
#endif

#endif /* WGF_TILEMAP_LAYER_H */
