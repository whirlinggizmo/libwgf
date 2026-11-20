/* A placed Tiled map (wgf_tilemap_instantiate, wgf_tilemap_layer.h): helix-legacy's
 * TiledInstance2D and TiledTileLayer2D. A plain root actor and a layer actor under it for each
 * of the map's layers (WGF_ACTOR_KIND_TILEMAP_LAYER), its record its own copy of a tile
 * layer's cells; a tile layer's cells are quads of sprite batches under it, one batch for each
 * tileset it uses, each cell its quad's in its batch (moved by a swap with the last when a
 * cell leaves a batch). A part installed by the first map placed steps the layers' animated
 * tiles each frame, a clock a layer, holding still under a paused actor. */
#include "wgf_tilemap_layer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "tilemap/wgf_gfx_tilemap_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_mat4.h"
#include "wgf_sprite_batch.h"

#define FLIPS (WGF_GFX_PRIV_TILEMAP_FLIP_X | WGF_GFX_PRIV_TILEMAP_FLIP_Y | WGF_GFX_PRIV_TILEMAP_FLIP_DIAGONAL)
#define BULK_FLIP_SHIFT 28 /* a cell in bulk: its gid plus its flips times 0x10000000 */

typedef struct batch_t {
    wgf_actor_t actor;
    int tileset;
    int *cells; /* the cell each quad shows */
    int count, capacity;
} batch_t;

typedef struct animated_t {
    int cell;
    int shown; /* the tile it shows, in its tileset */
} animated_t;

typedef struct wgf_gfx_priv_tilemap_layer_t {
    wgf_actor_t actor;
    wgf_tilemap_t map; /* a reference held */
    int index, kind, columns, rows, tile_width, tile_height;
    int alpha; /* its opacity, 0..255 */
    uint32_t *cells;           /* Tiled's, row by row; NULL for a layer without */
    int *batch_of, *quad_of;   /* each cell's batch (-1 none) and quad there */
    batch_t *batches;
    int batch_count;
    animated_t *animated;
    int animated_count, animated_capacity;
    double clock;
    struct wgf_gfx_priv_tilemap_layer_t *next, *prev; /* every placed layer, for the part */
} layer_t;

static struct {
    bool installed;
    layer_t *layers;
} tl;

static layer_t *layer_of_at(wgf_actor_t layer, const char *caller)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(layer, caller);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_TILEMAP_LAYER ? actor_ptr->as.tilemap_layer : NULL;
}
#define layer_of(layer) layer_of_at((layer), WGF_CORE_PRIV_CALLER)

static bool grow(void **items, int *capacity, int needed, size_t size)
{
    void *moved;
    int grown = *capacity > 0 ? *capacity : 16;
    if (needed <= *capacity) return true;
    while (grown < needed) grown *= 2;
    moved = realloc(*items, size * (size_t)grown);
    if (moved == NULL) return false;
    *items = moved;
    *capacity = grown;
    return true;
}

/* The tile a cell's animation shows at `clock`. */
static int frame_at(const wgf_gfx_priv_tilemap_tile_t *tile, double clock)
{
    double at = fmod(clock, (double)tile->seconds);
    int i;
    for (i = 0; i < tile->frame_count; i++) {
        if (at < (double)tile->frames[i].seconds) return tile->frames[i].tile;
        at -= (double)tile->frames[i].seconds;
    }
    return tile->frames[tile->frame_count - 1].tile;
}

static const wgf_gfx_priv_tilemap_tile_t *animation_of(const wgf_gfx_priv_tilemap_tileset_t *t, int tile)
{
    return t->tiles[tile] != NULL && t->tiles[tile]->frames != NULL ? t->tiles[tile] : NULL;
}

static int animated_index(const layer_t *l, int cell)
{
    int i;
    for (i = 0; i < l->animated_count; i++) {
        if (l->animated[i].cell == cell) return i;
    }
    return -1;
}

/* Quad `quad` of batch `b` showing `cell` as tile `shown` of its tileset: Tiled's place for it,
 * its bottom-left on its cell's, turned and mirrored about its middle as its flips say (the
 * diagonal first, then across, then down: helix-legacy's orient). */
static void write_quad(const layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, const batch_t *b, int quad, int cell,
                       int shown)
{
    const wgf_gfx_priv_tilemap_tileset_t *t = &d->tilesets[b->tileset];
    const uint32_t gid = l->cells[cell];
    const int column = cell % l->columns, row = cell / l->columns;
    const float left = (float)(column * l->tile_width + t->offset_x);
    const float top = (float)((row + 1) * l->tile_height - t->tile_height + t->offset_y);
    int a = 1, bb = 0, c = 0, dd = 1;
    float position[2], size[2], rotation, region[4];
    int tint;
    if (!wgf_handle_is_alive(b->actor)) return;
    if (gid & WGF_GFX_PRIV_TILEMAP_FLIP_DIAGONAL) a = 0, bb = 1, c = 1, dd = 0;
    if (gid & WGF_GFX_PRIV_TILEMAP_FLIP_X) a = -a, bb = -bb;
    if (gid & WGF_GFX_PRIV_TILEMAP_FLIP_Y) c = -c, dd = -dd;
    size[0] = (float)t->tile_width;
    size[1] = (float)t->tile_height;
    if (a * dd - bb * c > 0) {
        rotation = atan2f((float)c, (float)a);
    } else { /* a mirror across, then the turn that is left */
        rotation = atan2f((float)-c, (float)-a);
        size[0] = -size[0];
    }
    position[0] = left + (float)t->tile_width * 0.5f;
    position[1] = top + (float)t->tile_height * 0.5f;
    region[0] = (float)(t->margin + (shown % t->columns) * (t->tile_width + t->spacing));
    region[1] = (float)(t->margin + (shown / t->columns) * (t->tile_height + t->spacing));
    region[2] = (float)t->tile_width;
    region[3] = (float)t->tile_height;
    tint = (int)(0xffffff00u | (unsigned)l->alpha);
    wgf_sprite_batch_set_positions(b->actor, quad, position, 2);
    wgf_sprite_batch_set_sizes(b->actor, quad, size, 2);
    wgf_sprite_batch_set_rotations(b->actor, quad, &rotation, 1);
    wgf_sprite_batch_set_regions(b->actor, quad, region, 4);
    wgf_sprite_batch_set_tints(b->actor, quad, &tint, 1);
}

/* The tile `cell` shows now: its animation's frame, or itself. */
static int shown_tile(const layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, int cell)
{
    const int i = animated_index(l, cell);
    int tile = 0;
    if (i >= 0) return l->animated[i].shown;
    wgf_gfx_priv_tilemap_find_tileset(d, l->cells[cell], &tile);
    return tile;
}

/* The batch of `tileset` under the layer, made after the others when it has none; -1 when
 * it can't be. */
static int batch_for(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, int tileset)
{
    batch_t *grown;
    wgf_actor_t made;
    int i;
    for (i = 0; i < l->batch_count; i++) {
        if (l->batches[i].tileset == tileset) return i;
    }
    grown = (batch_t *)realloc(l->batches, sizeof(batch_t) * (size_t)(l->batch_count + 1));
    if (grown == NULL) return -1;
    l->batches = grown;
    made = wgf_sprite_batch_create(d->tilesets[tileset].texture);
    if (made == 0) return -1;
    wgf_actor_set_parent(made, l->actor);
    memset(&l->batches[l->batch_count], 0, sizeof(batch_t));
    l->batches[l->batch_count].actor = made;
    l->batches[l->batch_count].tileset = tileset;
    return l->batch_count++;
}

/* `cell` taken out of its batch: the batch's last quad moved into its place. */
static void take_out(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, int cell)
{
    const int b = l->batch_of[cell], i = animated_index(l, cell);
    batch_t *batch;
    int quad, last;
    if (i >= 0) l->animated[i] = l->animated[--l->animated_count];
    if (b < 0) return;
    batch = &l->batches[b];
    quad = l->quad_of[cell];
    last = batch->count - 1;
    if (quad != last) {
        const int moved = batch->cells[last];
        batch->cells[quad] = moved;
        l->quad_of[moved] = quad;
        write_quad(l, d, batch, quad, moved, shown_tile(l, d, moved));
    }
    batch->count = last;
    if (wgf_handle_is_alive(batch->actor)) wgf_sprite_batch_set_count(batch->actor, batch->count);
    l->batch_of[cell] = -1;
}

/* `cell` drawn as its tile is: a quad at the end of its tileset's batch, its animation
 * stepped with the layer's. */
static void put_in(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, int cell)
{
    const wgf_gfx_priv_tilemap_tile_t *animation;
    batch_t *batch;
    int tile = 0, b, shown;
    const int tileset = wgf_gfx_priv_tilemap_find_tileset(d, l->cells[cell], &tile);
    if (tileset < 0 || (b = batch_for(l, d, tileset)) < 0) return;
    batch = &l->batches[b];
    if (!grow((void **)&batch->cells, &batch->capacity, batch->count + 1, sizeof(int))) return;
    if (!wgf_handle_is_alive(batch->actor) || !wgf_sprite_batch_set_count(batch->actor, batch->count + 1)) return;
    batch->cells[batch->count] = cell;
    l->batch_of[cell] = b;
    l->quad_of[cell] = batch->count++;
    shown = tile;
    animation = animation_of(&d->tilesets[tileset], tile);
    if (animation != NULL &&
        grow((void **)&l->animated, &l->animated_capacity, l->animated_count + 1, sizeof(animated_t))) {
        shown = frame_at(animation, l->clock);
        l->animated[l->animated_count].cell = cell;
        l->animated[l->animated_count].shown = shown;
        l->animated_count++;
    }
    write_quad(l, d, batch, l->quad_of[cell], cell, shown);
}

static void update(float dt)
{
    layer_t *l;
    for (l = tl.layers; l != NULL; l = l->next) {
        const wgf_gfx_priv_tilemap_data_t *d;
        int i;
        if (l->animated_count == 0 || wgf_gfx_priv_actor_is_frozen(l->actor)) continue;
        d = wgf_gfx_priv_tilemap_data(l->map);
        if (d == NULL) continue;
        l->clock += dt > 0.0f ? (double)dt : 0.0;
        for (i = 0; i < l->animated_count; i++) {
            animated_t *a = &l->animated[i];
            const int b = l->batch_of[a->cell];
            int tile = 0, shown;
            const int tileset = wgf_gfx_priv_tilemap_find_tileset(d, l->cells[a->cell], &tile);
            const wgf_gfx_priv_tilemap_tile_t *animation = tileset >= 0 ? animation_of(&d->tilesets[tileset], tile) : NULL;
            if (animation == NULL || b < 0) continue;
            shown = frame_at(animation, l->clock);
            if (shown == a->shown) continue;
            a->shown = shown;
            write_quad(l, d, &l->batches[b], l->quad_of[a->cell], a->cell, shown);
        }
    }
}

static void stop(void)
{
    tl.layers = NULL; /* still owned by their actors, freed after the parts stop */
}

static wgf_core_priv_part_t part = {.name = "tilemap layers",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_TILEMAPS,
                                    .update = update,
                                    .stop = stop};

static void free_layer(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr)
{
    layer_t *l = actor_ptr->as.tilemap_layer;
    layer_t *p;
    int i;
    (void)actor;
    if (l == NULL) return;
    for (p = tl.layers; p != NULL && p != l; p = p->next) {
    }
    if (p == l) { /* still listed: the part hasn't stopped */
        if (l->prev != NULL) l->prev->next = l->next;
        else tl.layers = l->next;
        if (l->next != NULL) l->next->prev = l->prev;
    }
    wgf_gfx_priv_tilemap_release(l->map);
    for (i = 0; i < l->batch_count; i++) free(l->batches[i].cells);
    free(l->batches);
    free(l->cells);
    free(l->batch_of);
    free(l->quad_of);
    free(l->animated);
    free(l);
    actor_ptr->as.tilemap_layer = NULL;
}

static const wgf_gfx_priv_actor_kind_t kind = {free_layer, NULL, NULL, NULL};

/* Layer `index` of the map placed under `root`: its actor and its record, its cells drawn. */
static bool place_layer(wgf_tilemap_t map, const wgf_gfx_priv_tilemap_data_t *d, int index, wgf_actor_t root)
{
    const wgf_gfx_priv_tilemap_layer_data_t *data = &d->layers[index];
    const int cells = data->kind == WGF_TILEMAP_LAYER_KIND_TILES ? data->columns * data->rows : 0;
    const wgf_actor_t actor = wgf_gfx_priv_actor_create(WGF_ACTOR_KIND_TILEMAP_LAYER);
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    layer_t *l;
    int i, t;
    if (actor_ptr == NULL) return false;
    l = (layer_t *)calloc(1, sizeof(*l));
    if (l == NULL) {
        wgf_actor_destroy(actor, WGF_ACTOR_DESTROY_CHILDREN);
        return false;
    }
    actor_ptr->as.tilemap_layer = l;
    l->actor = actor;
    l->map = map;
    wgf_gfx_priv_tilemap_retain(map);
    l->index = index;
    l->kind = data->kind;
    l->columns = data->columns, l->rows = data->rows;
    l->tile_width = d->tile_width, l->tile_height = d->tile_height;
    l->alpha = (int)(data->opacity * 255.0f + 0.5f);
    l->next = tl.layers;
    if (tl.layers != NULL) tl.layers->prev = l;
    tl.layers = l;
    wgf_actor_set_name(actor, data->name[0] != '\0' ? data->name : NULL);
    wgf_actor_set_position(actor, data->offset_x, data->offset_y, 0.0f);
    wgf_actor_set_visible(actor, data->visible);
    wgf_actor_set_parent(actor, root);
    if (cells == 0) return true;
    l->cells = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)cells);
    l->batch_of = (int *)malloc(sizeof(int) * (size_t)cells);
    l->quad_of = (int *)calloc((size_t)cells, sizeof(int));
    if (l->cells == NULL || l->batch_of == NULL || l->quad_of == NULL) return false;
    memcpy(l->cells, data->cells, sizeof(uint32_t) * (size_t)cells);
    for (i = 0; i < cells; i++) l->batch_of[i] = -1;
    for (t = 0; t < d->tileset_count; t++) { /* a batch for each tileset it uses, in their order */
        for (i = 0; i < cells; i++) {
            if (wgf_gfx_priv_tilemap_find_tileset(d, l->cells[i], NULL) == t) break;
        }
        if (i < cells) batch_for(l, d, t);
    }
    for (i = 0; i < cells; i++) put_in(l, d, i);
    return true;
}

wgf_actor_t wgf_tilemap_instantiate(wgf_tilemap_t map, wgf_actor_t parent)
{
    const wgf_gfx_priv_tilemap_data_t *d = wgf_gfx_priv_tilemap_data(map);
    wgf_actor_t root;
    int i;
    if (d == NULL || (parent != 0 && wgf_gfx_priv_actor_of(parent) == NULL)) return 0;
    root = wgf_actor_create();
    if (root == 0) return 0;
    if (!tl.installed) {
        wgf_core_priv_part_install(&part);
        tl.installed = true;
    }
    wgf_gfx_priv_actor_set_kind(WGF_ACTOR_KIND_TILEMAP_LAYER, &kind);
    wgf_actor_set_name(root, d->file[0] != '\0' ? d->file : NULL);
    for (i = 0; i < d->layer_count; i++) {
        d = wgf_gfx_priv_tilemap_data(map); /* the same, looked up again across the creates */
        if (d == NULL || !place_layer(map, d, i, root)) {
            wgf_actor_destroy(root, WGF_ACTOR_DESTROY_CHILDREN);
            return 0;
        }
    }
    if (parent != 0) wgf_actor_set_parent(root, parent);
    return root;
}

/* ---- the calls ----------------------------------------------------------------------- */

wgf_tilemap_t wgf_tilemap_layer_get_map(wgf_actor_t layer)
{
    const layer_t *l = layer_of(layer);
    return l != NULL ? l->map : 0;
}

int wgf_tilemap_layer_get_index(wgf_actor_t layer)
{
    const layer_t *l = layer_of(layer);
    return l != NULL ? l->index : -1;
}

static bool inside(const layer_t *l, int column, int row)
{
    return l != NULL && l->cells != NULL && column >= 0 && row >= 0 && column < l->columns && row < l->rows;
}

int wgf_tilemap_layer_get_cell(wgf_actor_t layer, int column, int row)
{
    const layer_t *l = layer_of(layer);
    return inside(l, column, row) ? (int)(l->cells[row * l->columns + column] & WGF_GFX_PRIV_TILEMAP_GID_MASK) : 0;
}

/* Tiled's flip bits as wgf_tilemap_flip_t's, and back. */
static int flips_of(uint32_t gid)
{
    return ((gid & WGF_GFX_PRIV_TILEMAP_FLIP_X) ? WGF_TILEMAP_FLIP_X : 0) |
           ((gid & WGF_GFX_PRIV_TILEMAP_FLIP_Y) ? WGF_TILEMAP_FLIP_Y : 0) |
           ((gid & WGF_GFX_PRIV_TILEMAP_FLIP_DIAGONAL) ? WGF_TILEMAP_FLIP_DIAGONAL : 0);
}

static uint32_t tiled_bits(int flips)
{
    return ((flips & WGF_TILEMAP_FLIP_X) ? WGF_GFX_PRIV_TILEMAP_FLIP_X : 0u) |
           ((flips & WGF_TILEMAP_FLIP_Y) ? WGF_GFX_PRIV_TILEMAP_FLIP_Y : 0u) |
           ((flips & WGF_TILEMAP_FLIP_DIAGONAL) ? WGF_GFX_PRIV_TILEMAP_FLIP_DIAGONAL : 0u);
}

int wgf_tilemap_layer_get_cell_flips(wgf_actor_t layer, int column, int row)
{
    const layer_t *l = layer_of(layer);
    return inside(l, column, row) ? flips_of(l->cells[row * l->columns + column]) : 0;
}

/* Whether gid and flips make a cell of the map's: an empty one, or a tile one of its
 * tilesets holds, flipped by the three bits at most. */
static bool is_cell(const wgf_gfx_priv_tilemap_data_t *d, int gid, int flips)
{
    if (flips < 0 || flips > 7 || gid < 0 || gid > (int)WGF_GFX_PRIV_TILEMAP_GID_MASK) return false;
    return gid == 0 || wgf_gfx_priv_tilemap_find_tileset(d, (uint32_t)gid, NULL) >= 0;
}

static void set_one(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, int cell, int gid, int flips)
{
    const uint32_t value = gid == 0 ? 0u : ((uint32_t)gid | tiled_bits(flips));
    if (l->cells[cell] == value) return;
    take_out(l, d, cell);
    l->cells[cell] = value;
    put_in(l, d, cell);
}

bool wgf_tilemap_layer_set_cell(wgf_actor_t layer, int column, int row, int gid, int flips)
{
    layer_t *l = layer_of(layer);
    const wgf_gfx_priv_tilemap_data_t *d = l != NULL ? wgf_gfx_priv_tilemap_data(l->map) : NULL;
    if (d == NULL || !inside(l, column, row) || !is_cell(d, gid, flips)) return false;
    set_one(l, d, row * l->columns + column, gid, flips);
    return true;
}

int wgf_tilemap_layer_get_cells(wgf_actor_t layer, int *out, int count)
{
    const layer_t *l = layer_of(layer);
    int i, n;
    if (l == NULL || l->cells == NULL || out == NULL || count <= 0) return 0;
    n = l->columns * l->rows < count ? l->columns * l->rows : count;
    for (i = 0; i < n; i++) {
        out[i] = (int)(l->cells[i] & WGF_GFX_PRIV_TILEMAP_GID_MASK) + (flips_of(l->cells[i]) << BULK_FLIP_SHIFT);
    }
    return n;
}

bool wgf_tilemap_layer_set_cells(wgf_actor_t layer, const int *cells, int count)
{
    layer_t *l = layer_of(layer);
    const wgf_gfx_priv_tilemap_data_t *d = l != NULL ? wgf_gfx_priv_tilemap_data(l->map) : NULL;
    int i;
    if (d == NULL || l->cells == NULL || cells == NULL || count != l->columns * l->rows) return false;
    for (i = 0; i < count; i++) {
        if (cells[i] < 0 || !is_cell(d, cells[i] & (int)WGF_GFX_PRIV_TILEMAP_GID_MASK, cells[i] >> BULK_FLIP_SHIFT)) {
            return false;
        }
    }
    for (i = 0; i < count; i++) {
        set_one(l, d, i, cells[i] & (int)WGF_GFX_PRIV_TILEMAP_GID_MASK, cells[i] >> BULK_FLIP_SHIFT);
    }
    return true;
}

/* The point x, y of the layer's parent in the layer's own units (the map's pixels). */
static wgf_vec3_t local_point(wgf_actor_t layer, float x, float y, const layer_t **found)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(layer, "wgf_tilemap_layer_get_cell_at");
    *found = actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_TILEMAP_LAYER ? actor_ptr->as.tilemap_layer : NULL;
    if (*found == NULL) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    return wgf_mat4_transform_point(wgf_mat4_invert(wgf_gfx_priv_actor_get_local_matrix(actor_ptr)),
                                    wgf_vec3_make(x, y, 0.0f));
}

int wgf_tilemap_layer_get_column_at(wgf_actor_t layer, float x, float y)
{
    const layer_t *l;
    const wgf_vec3_t p = local_point(layer, x, y, &l);
    return l != NULL && l->tile_width > 0 ? (int)floorf(p.x / (float)l->tile_width) : 0;
}

int wgf_tilemap_layer_get_row_at(wgf_actor_t layer, float x, float y)
{
    const layer_t *l;
    const wgf_vec3_t p = local_point(layer, x, y, &l);
    return l != NULL && l->tile_height > 0 ? (int)floorf(p.y / (float)l->tile_height) : 0;
}
