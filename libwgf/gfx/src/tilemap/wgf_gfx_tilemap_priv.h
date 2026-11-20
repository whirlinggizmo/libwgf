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

typedef struct wgf_gfx_priv_tilemap_tileset_t {
    char *name;
    int first_gid, tile_count, columns, tile_width, tile_height, margin, spacing, offset_x, offset_y;
    char *image;           /* its path, from the root */
    wgf_texture_t texture; /* made as the map finishes */
    wgf_gfx_priv_tilemap_tile_t **tiles; /* tile_count of them, NULL for a tile with nothing */
} wgf_gfx_priv_tilemap_tileset_t;

typedef struct wgf_gfx_priv_tilemap_object_t {
    int id, shape, gid;
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
    wgf_gfx_priv_tilemap_properties_t properties;
} wgf_gfx_priv_tilemap_layer_data_t;

typedef struct wgf_gfx_priv_tilemap_data_t {
    char *file; /* the file's name less its directory and extension: the placed root's */
    int columns, rows, tile_width, tile_height;
    wgf_color_t background;
    wgf_gfx_priv_tilemap_layer_data_t *layers;
    int layer_count;
    wgf_gfx_priv_tilemap_tileset_t *tilesets; /* by first gid */
    int tileset_count;
    wgf_gfx_priv_tilemap_properties_t properties;
} wgf_gfx_priv_tilemap_data_t;

/* A READY map's data, NULL for anything else. Don't hold it across a create of a map. */
const wgf_gfx_priv_tilemap_data_t *wgf_gfx_priv_tilemap_data(wgf_tilemap_t map);

/* The tileset gid's tile (its flips apart) is in, and the tile's index there; -1 for 0 or a
 * gid none holds. */
int wgf_gfx_priv_tilemap_find_tileset(const wgf_gfx_priv_tilemap_data_t *data, uint32_t gid, int *tile);

/* A placed layer held a reference to its map, and let go of it; let go of when the maps are
 * gone (gfx's stop) is nothing. */
void wgf_gfx_priv_tilemap_retain(wgf_tilemap_t map);
void wgf_gfx_priv_tilemap_release(wgf_tilemap_t map);

#endif /* WGF_GFX_TILEMAP_PRIV_H */
