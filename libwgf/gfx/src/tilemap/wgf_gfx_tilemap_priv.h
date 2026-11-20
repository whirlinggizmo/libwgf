#ifndef WGF_GFX_TILEMAP_PRIV_H
#define WGF_GFX_TILEMAP_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "wgf_tilemap.h"

/* A Tiled map as read (wgf_gfx_tilemap.c), what its placed layers (wgf_gfx_tilemap_layer.c)
 * draw from. A cell is Tiled's 32 bits: its gid under GID_MASK, its flips above. */

#define WGF_GFX_PRIV_TILEMAP_GID_MASK 0x0fffffffu
#define WGF_GFX_PRIV_TILEMAP_FLIP_X 0x80000000u
#define WGF_GFX_PRIV_TILEMAP_FLIP_Y 0x40000000u
#define WGF_GFX_PRIV_TILEMAP_FLIP_DIAGONAL 0x20000000u

typedef struct wgf_gfx_priv_tilemap_property_t {
    char *name, *text; /* malloc'd */
    double number;
    int type; /* wgf_tilemap_property_type_t */
} wgf_gfx_priv_tilemap_property_t;

typedef struct wgf_gfx_priv_tilemap_properties_t {
    wgf_gfx_priv_tilemap_property_t *items;
    int count;
} wgf_gfx_priv_tilemap_properties_t;

typedef struct wgf_gfx_priv_tilemap_frame_t {
    int tile; /* in its tileset, from 0 */
    float seconds;
} wgf_gfx_priv_tilemap_frame_t;

/* A tile that carries something: a class, properties, an animation. */
typedef struct wgf_gfx_priv_tilemap_tile_t {
    char *class_name; /* malloc'd; NULL none */
    wgf_gfx_priv_tilemap_properties_t properties;
    wgf_gfx_priv_tilemap_frame_t *frames; /* NULL: not animated */
    int frame_count;
    float seconds; /* the animation's whole length */
} wgf_gfx_priv_tilemap_tile_t;

/* A terrain (Wang) set: its terrains' names (numbered from 1), and for each tile that takes
 * part its eight places' terrains, Tiled's order (top, top right, right, bottom right, bottom,
 * bottom left, left, top left: edges even, corners odd). */
typedef struct wgf_gfx_priv_tilemap_terrain_set_t {
    char *name;
    int kind; /* wgf_tilemap_terrain_kind_t */
    int tileset;
    char **terrains;
    int terrain_count;
    int *tiles; /* in its tileset, from 0, in the file's order */
    unsigned char (*places)[8];
    int tile_count;
} wgf_gfx_priv_tilemap_terrain_set_t;

typedef struct wgf_gfx_priv_tilemap_tileset_t {
    char *name;
    int first_gid, tile_count, columns, tile_width, tile_height, margin, spacing, offset_x, offset_y;
    int alignment;         /* a tile object's anchor: ALIGN_*, the map's default resolved */
    char *image;           /* its path, from the root */
    wgf_texture_t texture; /* made as the map finishes */
    wgf_gfx_priv_tilemap_tile_t **tiles; /* tile_count of them, NULL for a tile with nothing */
} wgf_gfx_priv_tilemap_tileset_t;

/* A tile object's anchor, as a fraction of its size across and down (Tiled's
 * objectalignment): the place Tiled gives is that point of its image. */
enum {
    WGF_GFX_PRIV_TILEMAP_ALIGN_BOTTOM_LEFT = 0,
    WGF_GFX_PRIV_TILEMAP_ALIGN_BOTTOM,
    WGF_GFX_PRIV_TILEMAP_ALIGN_BOTTOM_RIGHT,
    WGF_GFX_PRIV_TILEMAP_ALIGN_LEFT,
    WGF_GFX_PRIV_TILEMAP_ALIGN_CENTER,
    WGF_GFX_PRIV_TILEMAP_ALIGN_RIGHT,
    WGF_GFX_PRIV_TILEMAP_ALIGN_TOP_LEFT,
    WGF_GFX_PRIV_TILEMAP_ALIGN_TOP,
    WGF_GFX_PRIV_TILEMAP_ALIGN_TOP_RIGHT
};

typedef struct wgf_gfx_priv_tilemap_object_t {
    int id, shape, gid, flips; /* flips: wgf_tilemap_flip_t's bits, a tile object's */
    char *name, *class_name;
    float x, y, width, height, rotation; /* rotation in radians */
    bool visible;
    float *points; /* x and y in turn */
    int point_count;
    wgf_gfx_priv_tilemap_properties_t properties;
} wgf_gfx_priv_tilemap_object_t;

typedef struct wgf_gfx_priv_tilemap_layer_data_t {
    char *name;
    int kind; /* wgf_tilemap_layer_kind_t */
    bool visible;
    float opacity, offset_x, offset_y; /* with its groups' */
    int columns, rows;
    uint32_t *cells; /* a tile layer's, row by row */
    wgf_gfx_priv_tilemap_object_t *objects;
    int object_count;
    bool topdown;          /* an object layer's objects drawn by their y, not their order */
    char *image;           /* an image layer's path, from the root; NULL none */
    wgf_texture_t texture; /* made as the map finishes */
    wgf_gfx_priv_tilemap_properties_t properties;
} wgf_gfx_priv_tilemap_layer_data_t;

typedef struct wgf_gfx_priv_tilemap_data_t {
    char *file; /* the file's name less its directory and extension: the placed root's */
    int columns, rows, tile_width, tile_height;
    int orientation;           /* wgf_tilemap_orientation_t */
    int render_order;          /* an orthogonal map's: 0 right-down, 1 right-up, 2 left-down, 3 left-up */
    bool stagger_x, stagger_even; /* a staggered or hexagonal map's axis and index */
    int hex_side;              /* a hexagonal map's side length (0 for any other) */
    bool infinite;
    int origin_column, origin_row; /* Tiled's column and row of cell 0, 0: an infinite map's chunks' top-left */
    int file_rows;             /* the height the file gives: an isometric map's middle (Tiled's originX) */
    wgf_color_t background;
    wgf_gfx_priv_tilemap_layer_data_t *layers;
    int layer_count;
    wgf_gfx_priv_tilemap_tileset_t *tilesets; /* by first gid */
    int tileset_count;
    wgf_gfx_priv_tilemap_terrain_set_t *terrain_sets; /* by tileset, then the file's order */
    int terrain_set_count;
    wgf_gfx_priv_tilemap_properties_t properties;
} wgf_gfx_priv_tilemap_data_t;

/* A READY map's data, NULL for anything else. Don't hold it across a create of a map. */
const wgf_gfx_priv_tilemap_data_t *wgf_gfx_priv_tilemap_data(wgf_tilemap_t map);

/* The tileset gid's tile (its flips apart) is in, and the tile's index there; -1 for 0 or a
 * gid none holds. */
int wgf_gfx_priv_tilemap_find_tileset(const wgf_gfx_priv_tilemap_data_t *data, uint32_t gid, int *tile);

/* Cell `column`, `row`'s box (its tile's bottom-left is the box's): its top-left in the
 * map's pixels into x, y (Tiled's tileToScreenCoords), its size into w, h (the map's tile
 * size, made even for a staggered or hexagonal map, as Tiled's). */
void wgf_gfx_priv_tilemap_cell_box(const wgf_gfx_priv_tilemap_data_t *data, int column, int row, float *x, float *y,
                                   float *w, float *h);

/* The cell whose shape (a rectangle, a diamond, a hexagon) holds the point x, y of the map's
 * pixels, outside the map too. */
void wgf_gfx_priv_tilemap_cell_at(const wgf_gfx_priv_tilemap_data_t *data, float x, float y, int *column, int *row);

/* Where cell `column`, `row` is drawn in Tiled's order: its draw row (an orthogonal map's row
 * in its render order, an isometric map's diagonal, a staggered or hexagonal map's row or half
 * of one) and its place along it. How many draw rows a map has. */
void wgf_gfx_priv_tilemap_draw_slot(const wgf_gfx_priv_tilemap_data_t *data, int column, int row, int *draw_row,
                                    int *place);
int wgf_gfx_priv_tilemap_draw_row_count(const wgf_gfx_priv_tilemap_data_t *data);

/* A point an object layer gives (for an isometric map, along its axes in tile heights) in the
 * map's pixels: Tiled's pixelToScreenCoords; the point itself for any other orientation. */
void wgf_gfx_priv_tilemap_project(const wgf_gfx_priv_tilemap_data_t *data, float x, float y, float *out_x,
                                  float *out_y);

/* A tile object's middle as drawn, in the map's pixels. */
void wgf_gfx_priv_tilemap_object_middle(const wgf_gfx_priv_tilemap_data_t *data,
                                        const wgf_gfx_priv_tilemap_object_t *object, float *x, float *y);

/* Told when a map has loaded again in place (wgf_asset_reload), its new data in: the placed
 * layers' part sets it as the first map is placed, so a program placing none links none. */
extern void (*wgf_gfx_priv_tilemap_reloaded)(wgf_tilemap_t map);

/* The index in `set` of its tileset's tile `tile`, -1 for one that takes no part. */
int wgf_gfx_priv_tilemap_terrain_tile(const wgf_gfx_priv_tilemap_terrain_set_t *set, int tile);

/* A placed layer held a reference to its map, and let go of it; let go of when the maps are
 * gone (gfx's stop) is nothing. */
void wgf_gfx_priv_tilemap_retain(wgf_tilemap_t map);
void wgf_gfx_priv_tilemap_release(wgf_tilemap_t map);

#endif /* WGF_GFX_TILEMAP_PRIV_H */
