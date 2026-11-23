#ifndef WGF_TILEMAP_LAYER_H
#define WGF_TILEMAP_LAYER_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_tilemap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A placed map's layer (WGF_ACTOR_KIND_TILEMAP_LAYER), made by wgf_tilemap_instantiate.
 *
 * A layer is an actor named for its layer, in the map's pixels from the map's top-left, y
 * down. Place, show, and parent it with the actor calls. Once placed, a tile layer's cells
 * are its own. You read and set them here, and cells you set are drawn from the next frame.
 * The map's file is unchanged.
 *
 * Every call returns false, 0, or -1 for an actor that isn't a placed layer. A layer of
 * objects or an image has no cells.
 *
 * A cell is a tile's gid (0 for empty) and its flips (wgf_tilemap_flip_t's bits). In bulk
 * (get_cells and set_cells), a cell is one int: its gid plus its flips times 0x10000000.
 *
 * Columns and rows are the map's (wgf_tilemap.h):
 * - an isometric map's run along its axes;
 * - a staggered or hexagonal map's are counted as Tiled counts them;
 * - an infinite map's count from its origin. */

/* Returns the map the layer belongs to, and the layer's index in that map. */
WGF_API wgf_tilemap_t wgf_tilemap_layer_get_map(wgf_actor_t layer);
WGF_API int wgf_tilemap_layer_get_index(wgf_actor_t layer);

/* Returns the tile at `column`, `row` (counted from the top-left), and its flips. Returns 0
 * for an empty cell, or a cell outside the layer. */
WGF_API int wgf_tilemap_layer_get_cell(wgf_actor_t layer, int column, int row);
WGF_API int wgf_tilemap_layer_get_cell_flips(wgf_actor_t layer, int column, int row);
/* Sets a cell to tile `gid`, flipped by `flips`. A `gid` of 0 empties the cell.
 *
 * The cell draws so from the next frame. An animated tile's animation starts in step with the
 * others. Returns false for a cell outside the layer, a `gid` that none of the map's tilesets
 * holds, or `flips` past the three bits. */
WGF_API bool wgf_tilemap_layer_set_cell(wgf_actor_t layer, int column, int row, int gid, int flips);

/* Gets every cell, row by row from the top-left, as one int each (see above). This is what a
 * game saves of a layer it changed.
 *
 * The cells are written into `out`, as many as fit in `count`. Returns how many it filled. */
WGF_API int wgf_tilemap_layer_get_cells(wgf_actor_t layer, int *out, int count);
/* Sets every cell at once, in the form get_cells gives them. Returns false, and changes
 * nothing, for a `count` other than the map's columns times its rows, or a cell set_cell
 * refuses. */
WGF_API bool wgf_tilemap_layer_set_cells(wgf_actor_t layer, const int *cells, int count);

/* Returns the column and the row at the point `x`, `y`.
 *
 * The point is in the layer's parent's units: the placed map's, which are the map's pixels
 * from its top-left. It goes through the layer's own place, turn, and scale. It works outside
 * the layer too: below 0, or at its columns or rows and past. Returns 0 for an actor that
 * isn't a placed layer. */
WGF_API int wgf_tilemap_layer_get_column_at(wgf_actor_t layer, float x, float y);
WGF_API int wgf_tilemap_layer_get_row_at(wgf_actor_t layer, float x, float y);
/* Returns the middle of cell `column`, `row`, in the layer's parent's units. It works outside
 * the layer too, and is the reverse of get_column_at and get_row_at. Returns 0, 0 for an
 * actor that isn't a placed layer. */
WGF_API wgf_vec2_t wgf_tilemap_layer_get_cell_middle(wgf_actor_t layer, int column, int row);

/* Paints terrain `terrain` of terrain set `set` into cell `column`, `row`, as Tiled's Terrain
 * brush paints it. Sets and terrains are wgf_tilemap.h's. Terrains are numbered from 1, and 0
 * is none.
 *
 * - A corner set's cell takes the terrain at its four corners.
 * - An edge set's cell takes it at its four edges.
 * - The cell, and the neighbors sharing those corners or edges, take the set's tile that fits
 *   what is around them. That is the first such tile in the set's order. Flips are never
 *   chosen.
 * - Where none of the set's tiles fits, a cell is left as it was.
 * - Where no terrain of the set is left, a cell showing a tile of the set's tileset is emptied.
 *
 * The terrains around a cell are read from the layer's cells the first time a set is painted.
 * They are kept as painted until a cell is set another way (set_cell or set_cells), and then
 * read again.
 *
 * Returns false, and paints nothing, for a cell outside the layer, a set or terrain the map
 * doesn't have, a mixed set, or a staggered or hexagonal map. Returns true once painted,
 * whether every cell found its tile or not. Each cell does when the set has a tile for every
 * arrangement. */
WGF_API bool wgf_tilemap_layer_paint_terrain(wgf_actor_t layer, int column, int row, int set, int terrain);

/* Returns the sprite (wgf_sprite.h) that draws tile object `object` of an object layer, under
 * the layer (see wgf_tilemap_instantiate).
 *
 * Returns 0 for an object that isn't a tile, an `object` past the layer's objects, or a sprite
 * that was destroyed. wgf_scene_spawn_objects destroys a sprite when it spawns a prefab in its
 * place. */
WGF_API wgf_actor_t wgf_tilemap_layer_get_object_actor(wgf_actor_t layer, int object);

#ifdef __cplusplus
}
#endif

#endif /* WGF_TILEMAP_LAYER_H */
