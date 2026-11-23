/* A placed Tiled map (wgf_tilemap_instantiate, wgf_tilemap_layer.h): helix-legacy's
 * TiledInstance2D and TiledTileLayer2D. A plain root actor and a layer actor under it for each
 * of the map's layers (WGF_ACTOR_KIND_TILEMAP_LAYER), its record its own copy of a tile
 * layer's cells. A tile layer's cells are quads of sprite batches under it:
 *   - loose, where no tile reaches past its cell (an orthogonal map of tiles its cells' size):
 *     one batch for each tileset it uses, each cell its quad's in its batch (moved by a swap
 *     with the last when a cell leaves a batch), so a set costs a few writes;
 *   - ordered, where one does (and every isometric, staggered, or hexagonal map): Tiled's
 *     drawing order a row of it at a time (wgf_gfx_priv_tilemap_draw_slot), each row a batch
 *     for each run of one tileset's tiles along it, so later tiles draw over earlier ones
 *     whatever their tilesets; a set cell builds its row's runs again.
 * An object layer's tile objects are sprites under it, and an image layer's image one. A part
 * installed by the first map placed steps the layers' animated tiles each frame, a clock a
 * layer, holding still under a paused actor, and builds every placed copy of a map loaded
 * again in place. */
#include "wgf_tilemap_layer.h"
#include "wgf_trig.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "tilemap/wgf_gfx_tilemap_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_mat4.h"
#include "wgf_sprite.h"
#include "wgf_sprite_batch.h"

#define BULK_FLIP_SHIFT 28 /* a cell in bulk: its gid plus its flips times 0x10000000 */
#define PI_F 3.14159265358979f

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

/* An ordered layer's row of Tiled's drawing order: its runs, in order. */
typedef struct draw_row_t {
    batch_t *runs;
    int run_count;
} draw_row_t;

typedef struct wgf_gfx_priv_tilemap_layer_t {
    wgf_actor_t actor, root; /* root: the placed map's, for building it again */
    wgf_tilemap_t map;       /* a reference held */
    int index, kind, columns, rows;
    int alpha;               /* its opacity, 0..255 */
    uint32_t *cells;         /* Tiled's, row by row; NULL for a layer without */
    int *batch_of, *quad_of; /* each cell's batch (loose) or run in its row (ordered), -1 none, and quad there */
    int *animated_of;        /* each cell's place in `animated`, -1 none */
    bool ordered;
    batch_t *batches; /* loose */
    int batch_count;
    draw_row_t *draw_rows; /* ordered: Tiled's order's rows, and each one's cells in order */
    int draw_row_count;
    int *row_of, *row_start, *row_cells;
    animated_t *animated;
    int animated_count, animated_capacity;
    double clock;
    int **terrains; /* by terrain set: its grid of corners or edges as painted, NULL until painted */
    int terrain_count;
    wgf_actor_t *objects; /* an object layer's tile objects' sprites, by object (0 none) */
    int object_count;
    wgf_actor_t image; /* an image layer's sprite */
    struct wgf_gfx_priv_tilemap_layer_t *next, *prev; /* every placed layer, for the part */
} layer_t;

typedef struct instance_t {
    wgf_actor_t root;
    wgf_tilemap_t map;
} instance_t;

static struct {
    bool installed;
    layer_t *layers;
    instance_t *instances; /* each map placed, to be built again when it loads again */
    int instance_count, instance_capacity;
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

static int tint_of(const layer_t *l)
{
    return (int)(0xffffff00u | (unsigned)l->alpha);
}

/* ---- animated cells ------------------------------------------------------------------ */

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

static void animated_remove(layer_t *l, int cell)
{
    const int i = l->animated_of[cell];
    if (i < 0) return;
    l->animated[i] = l->animated[--l->animated_count];
    if (i < l->animated_count) l->animated_of[l->animated[i].cell] = i;
    l->animated_of[cell] = -1;
}

/* The tile `cell` shows now (tile `tile` of tileset `t`, from 0): its animation's frame, the
 * cell stepped with the layer's others from here; or itself. */
static int animated_add(layer_t *l, const wgf_gfx_priv_tilemap_tileset_t *t, int cell, int tile)
{
    const wgf_gfx_priv_tilemap_tile_t *animation = animation_of(t, tile);
    if (animation == NULL ||
        !grow((void **)&l->animated, &l->animated_capacity, l->animated_count + 1, sizeof(animated_t))) {
        return tile;
    }
    l->animated[l->animated_count].cell = cell;
    l->animated[l->animated_count].shown = frame_at(animation, l->clock);
    l->animated_of[cell] = l->animated_count;
    return l->animated[l->animated_count++].shown;
}

/* The tile `cell` shows now: its animation's frame, or itself. */
static int shown_tile(const layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, int cell)
{
    int tile = 0;
    if (l->animated_of[cell] >= 0) return l->animated[l->animated_of[cell]].shown;
    wgf_gfx_priv_tilemap_find_tileset(d, l->cells[cell], &tile);
    return tile;
}

/* ---- quads --------------------------------------------------------------------------- */

/* Tile `tile`'s region of tileset `t`'s image: x, y, width, height. */
static void region_of(const wgf_gfx_priv_tilemap_tileset_t *t, int tile, float region[4])
{
    region[0] = (float)(t->margin + (tile % t->columns) * (t->tile_width + t->spacing));
    region[1] = (float)(t->margin + (tile / t->columns) * (t->tile_height + t->spacing));
    region[2] = (float)t->tile_width;
    region[3] = (float)t->tile_height;
}

/* Quad `quad` of batch `b` showing `cell` as tile `shown` of its tileset: Tiled's place for it,
 * its bottom-left on its cell's box's, turned and mirrored about its middle as its flips say
 * (the diagonal first, then across, then down: helix-legacy's orient). */
static void write_quad(const layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, const batch_t *b, int quad, int cell,
                       int shown)
{
    const wgf_gfx_priv_tilemap_tileset_t *t = &d->tilesets[b->tileset];
    const uint32_t gid = l->cells[cell];
    float box_x = 0.0f, box_y = 0.0f, box_w = 0.0f, box_h = 0.0f, left, top;
    int a = 1, bb = 0, c = 0, dd = 1;
    float position[2], size[2], rotation, region[4];
    int tint;
    if (!wgf_handle_is_alive(b->actor)) return;
    wgf_gfx_priv_tilemap_cell_box(d, cell % l->columns, cell / l->columns, &box_x, &box_y, &box_w, &box_h);
    left = box_x + (float)t->offset_x;
    top = box_y + box_h - (float)t->tile_height + (float)t->offset_y;
    if (gid & WGF_GFX_PRIV_TILEMAP_FLIP_DIAGONAL) a = 0, bb = 1, c = 1, dd = 0;
    if (gid & WGF_GFX_PRIV_TILEMAP_FLIP_X) a = -a, bb = -bb;
    if (gid & WGF_GFX_PRIV_TILEMAP_FLIP_Y) c = -c, dd = -dd;
    size[0] = (float)t->tile_width;
    size[1] = (float)t->tile_height;
    if (a * dd - bb * c > 0) {
        rotation = wgf_trig_atan2((float)c, (float)a);
    } else { /* a mirror across, then the turn that is left */
        rotation = wgf_trig_atan2((float)-c, (float)-a);
        size[0] = -size[0];
    }
    position[0] = left + (float)t->tile_width * 0.5f;
    position[1] = top + (float)t->tile_height * 0.5f;
    region_of(t, shown, region);
    tint = tint_of(l);
    wgf_sprite_batch_set_positions(b->actor, quad, position, 2);
    wgf_sprite_batch_set_sizes(b->actor, quad, size, 2);
    wgf_sprite_batch_set_rotations(b->actor, quad, &rotation, 1);
    wgf_sprite_batch_set_regions(b->actor, quad, region, 4);
    wgf_sprite_batch_set_tints(b->actor, quad, &tint, 1);
}

/* The batch drawing `cell`, NULL for none. */
static batch_t *batch_at(const layer_t *l, int cell)
{
    const int b = l->batch_of[cell];
    if (b < 0) return NULL;
    return l->ordered ? &l->draw_rows[l->row_of[cell]].runs[b] : &l->batches[b];
}

/* Whether a tileset's tiles can reach past their cells, so cells must be drawn in Tiled's
 * order. */
static bool reaches_out(const wgf_gfx_priv_tilemap_data_t *d, int tileset)
{
    const wgf_gfx_priv_tilemap_tileset_t *t = &d->tilesets[tileset];
    return d->orientation != WGF_TILEMAP_ORIENTATION_ORTHOGONAL || t->tile_width > d->tile_width ||
           t->tile_height > d->tile_height || t->offset_x != 0 || t->offset_y != 0;
}

/* ---- a loose layer: a batch a tileset -------------------------------------------------- */

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
    const int b = l->batch_of[cell];
    batch_t *batch;
    int quad, last;
    animated_remove(l, cell);
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
    shown = animated_add(l, &d->tilesets[tileset], cell, tile);
    write_quad(l, d, batch, l->quad_of[cell], cell, shown);
}

/* ---- an ordered layer: Tiled's order, a batch a run ------------------------------------ */

/* The last batch actor of the rows before `row`, 0 for none. */
static wgf_actor_t actor_before(const layer_t *l, int row)
{
    while (--row >= 0) {
        if (l->draw_rows[row].run_count > 0) return l->draw_rows[row].runs[l->draw_rows[row].run_count - 1].actor;
    }
    return 0;
}

/* Draw row `row` built again from its cells: its runs of a tileset's tiles, each a batch (its
 * batches kept where they can be, new ones placed after the one before them), its quads in
 * Tiled's order. `appending`: every row after it is empty, so a new batch goes last. */
static void build_row(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, int row, bool appending)
{
    draw_row_t *dr = &l->draw_rows[row];
    const int first = l->row_start[row], end = l->row_start[row + 1];
    int k, runs = 0, last = -1, run = -1;
    for (k = first; k < end; k++) { /* how many runs */
        const int cell = l->row_cells[k];
        const int tileset = wgf_gfx_priv_tilemap_find_tileset(d, l->cells[cell], NULL);
        animated_remove(l, cell);
        l->batch_of[cell] = -1;
        if (tileset < 0) continue;
        if (tileset != last) runs++;
        last = tileset;
    }
    while (dr->run_count > runs) { /* fewer: the last let go of */
        batch_t *gone = &dr->runs[--dr->run_count];
        if (wgf_handle_is_alive(gone->actor)) wgf_actor_destroy(gone->actor, WGF_ACTOR_DESTROY_CHILDREN);
        free(gone->cells);
    }
    if (runs > dr->run_count) { /* more: each made after the one before */
        batch_t *grown = (batch_t *)realloc(dr->runs, sizeof(batch_t) * (size_t)runs);
        if (grown == NULL) return;
        dr->runs = grown;
        while (dr->run_count < runs) {
            batch_t *made = &dr->runs[dr->run_count];
            memset(made, 0, sizeof(*made));
            made->tileset = -1;
            made->actor = wgf_sprite_batch_create(0);
            if (made->actor == 0) return;
            wgf_actor_set_parent(made->actor, l->actor);
            if (!appending) {
                const wgf_actor_t before = dr->run_count > 0 ? dr->runs[dr->run_count - 1].actor : actor_before(l, row);
                wgf_actor_set_index(made->actor, before != 0 ? wgf_actor_get_index(before) + 1 : 0);
            }
            dr->run_count++;
        }
    }
    for (k = first; k < end; k++) { /* each cell its run's next quad */
        const int cell = l->row_cells[k];
        const int tileset = wgf_gfx_priv_tilemap_find_tileset(d, l->cells[cell], NULL);
        batch_t *b;
        if (tileset < 0) continue;
        if (run < 0 || dr->runs[run].tileset != tileset) {
            b = &dr->runs[++run];
            if (b->tileset != tileset) {
                b->tileset = tileset;
                wgf_sprite_batch_set_texture(b->actor, d->tilesets[tileset].texture);
            }
            b->count = 0;
        }
        b = &dr->runs[run];
        if (!grow((void **)&b->cells, &b->capacity, b->count + 1, sizeof(int))) continue;
        b->cells[b->count] = cell;
        l->batch_of[cell] = run;
        l->quad_of[cell] = b->count++;
    }
    for (k = 0; k < dr->run_count; k++) {
        batch_t *b = &dr->runs[k];
        int q;
        if (!wgf_handle_is_alive(b->actor) || !wgf_sprite_batch_set_count(b->actor, b->count)) continue;
        for (q = 0; q < b->count; q++) {
            const int cell = b->cells[q];
            int tile = 0;
            wgf_gfx_priv_tilemap_find_tileset(d, l->cells[cell], &tile);
            write_quad(l, d, b, q, cell, animated_add(l, &d->tilesets[b->tileset], cell, tile));
        }
    }
}

typedef struct slot_t {
    int row, place, cell;
} slot_t;

static int by_slot(const void *a, const void *b)
{
    const slot_t *x = (const slot_t *)a, *y = (const slot_t *)b;
    if (x->row != y->row) return x->row < y->row ? -1 : 1;
    return x->place < y->place ? -1 : x->place > y->place ? 1 : 0;
}

/* The layer's cells in Tiled's drawing order, a row of it at a time. False for no memory. */
static bool order_cells(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d)
{
    const int cells = l->columns * l->rows;
    slot_t *slots = (slot_t *)malloc(sizeof(slot_t) * (size_t)cells);
    int i;
    l->draw_row_count = wgf_gfx_priv_tilemap_draw_row_count(d);
    l->row_of = (int *)malloc(sizeof(int) * (size_t)cells);
    l->row_cells = (int *)malloc(sizeof(int) * (size_t)cells);
    l->row_start = (int *)calloc((size_t)l->draw_row_count + 1, sizeof(int));
    l->draw_rows = (draw_row_t *)calloc((size_t)l->draw_row_count, sizeof(draw_row_t));
    if (slots == NULL || l->row_of == NULL || l->row_cells == NULL || l->row_start == NULL || l->draw_rows == NULL) {
        free(slots);
        return false;
    }
    for (i = 0; i < cells; i++) {
        slots[i].cell = i;
        wgf_gfx_priv_tilemap_draw_slot(d, i % l->columns, i / l->columns, &slots[i].row, &slots[i].place);
    }
    qsort(slots, (size_t)cells, sizeof(slot_t), by_slot);
    for (i = 0; i < cells; i++) {
        l->row_cells[i] = slots[i].cell;
        l->row_of[slots[i].cell] = slots[i].row;
        l->row_start[slots[i].row + 1]++;
    }
    for (i = 0; i < l->draw_row_count; i++) l->row_start[i + 1] += l->row_start[i];
    free(slots);
    return true;
}

/* ---- what a layer draws, made and let go of -------------------------------------------- */

static void free_terrains(layer_t *l)
{
    int i;
    for (i = 0; i < l->terrain_count; i++) free(l->terrains[i]);
    free(l->terrains);
    l->terrains = NULL;
    l->terrain_count = 0;
}

static void destroy_batch(batch_t *b)
{
    if (wgf_handle_is_alive(b->actor)) wgf_actor_destroy(b->actor, WGF_ACTOR_DESTROY_CHILDREN);
    free(b->cells);
}

static void free_rows(layer_t *l, bool destroy)
{
    int i, k;
    for (i = 0; l->draw_rows != NULL && i < l->draw_row_count; i++) {
        for (k = 0; k < l->draw_rows[i].run_count; k++) {
            if (destroy) destroy_batch(&l->draw_rows[i].runs[k]);
            else free(l->draw_rows[i].runs[k].cells);
        }
        free(l->draw_rows[i].runs);
    }
    free(l->draw_rows);
    free(l->row_of);
    free(l->row_start);
    free(l->row_cells);
    l->draw_rows = NULL;
    l->row_of = l->row_start = l->row_cells = NULL;
    l->draw_row_count = 0;
}

/* The actors it made under it, and what drew its tiles, let go of. */
static void clear_drawn(layer_t *l)
{
    int i;
    for (i = 0; i < l->batch_count; i++) destroy_batch(&l->batches[i]);
    free(l->batches);
    l->batches = NULL;
    l->batch_count = 0;
    free_rows(l, true);
    l->animated_count = 0;
    for (i = 0; i < l->object_count; i++) {
        if (l->objects[i] != 0 && wgf_handle_is_alive(l->objects[i])) {
            wgf_actor_destroy(l->objects[i], WGF_ACTOR_DESTROY_CHILDREN);
        }
    }
    free(l->objects);
    l->objects = NULL;
    l->object_count = 0;
    if (l->image != 0 && wgf_handle_is_alive(l->image)) wgf_actor_destroy(l->image, WGF_ACTOR_DESTROY_CHILDREN);
    l->image = 0;
}

/* Its cells drawn: ordered where a tile can reach past its cell, loose where none can. */
static void draw_cells(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d)
{
    const int cells = l->columns * l->rows;
    int i, t;
    for (i = 0; i < cells; i++) l->batch_of[i] = -1, l->animated_of[i] = -1;
    l->animated_count = 0;
    l->ordered = d->orientation != WGF_TILEMAP_ORIENTATION_ORTHOGONAL;
    for (i = 0; i < cells && !l->ordered; i++) {
        t = wgf_gfx_priv_tilemap_find_tileset(d, l->cells[i], NULL);
        l->ordered = t >= 0 && reaches_out(d, t);
    }
    if (l->ordered && order_cells(l, d)) {
        for (i = 0; i < l->draw_row_count; i++) build_row(l, d, i, true);
        return;
    }
    if (l->ordered) { /* no memory for the order: drawn loose */
        free_rows(l, false);
        l->ordered = false;
    }
    for (t = 0; t < d->tileset_count; t++) { /* a batch for each tileset it uses, in their order */
        for (i = 0; i < cells; i++) {
            if (wgf_gfx_priv_tilemap_find_tileset(d, l->cells[i], NULL) == t) break;
        }
        if (i < cells) batch_for(l, d, t);
    }
    for (i = 0; i < cells; i++) put_in(l, d, i);
}

typedef struct by_y_t {
    float y;
    int object;
} by_y_t;

/* By y, and as listed where two are level: Tiled's topdown order. */
static int by_y(const void *a, const void *b)
{
    const by_y_t *x = (const by_y_t *)a, *y = (const by_y_t *)b;
    if (x->y != y->y) return x->y < y->y ? -1 : 1;
    return x->object < y->object ? -1 : x->object > y->object ? 1 : 0;
}

/* An object layer's tile objects, each a sprite of its tile under the layer, in the layer's
 * draw order. */
static void draw_objects(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d,
                         const wgf_gfx_priv_tilemap_layer_data_t *data)
{
    by_y_t *order;
    int i;
    if (data->object_count == 0) return;
    l->objects = (wgf_actor_t *)calloc((size_t)data->object_count, sizeof(wgf_actor_t));
    order = (by_y_t *)malloc(sizeof(by_y_t) * (size_t)data->object_count);
    if (l->objects == NULL || order == NULL) {
        free(order);
        return;
    }
    l->object_count = data->object_count;
    for (i = 0; i < data->object_count; i++) order[i].y = data->objects[i].y, order[i].object = i;
    if (data->topdown) qsort(order, (size_t)data->object_count, sizeof(by_y_t), by_y);
    for (i = 0; i < data->object_count; i++) {
        const wgf_gfx_priv_tilemap_object_t *o = &data->objects[order[i].object];
        int tile = 0;
        const int tileset = o->shape == WGF_TILEMAP_SHAPE_TILE
                                ? wgf_gfx_priv_tilemap_find_tileset(d, (uint32_t)o->gid, &tile)
                                : -1;
        const wgf_gfx_priv_tilemap_tileset_t *t;
        float region[4], x = 0.0f, y = 0.0f;
        wgf_actor_t sprite;
        if (tileset < 0) continue;
        t = &d->tilesets[tileset];
        sprite = wgf_sprite_create(t->texture);
        if (sprite == 0) break;
        l->objects[order[i].object] = sprite;
        region_of(t, tile, region);
        wgf_sprite_set_source(sprite, region[0], region[1], region[2], region[3]);
        wgf_sprite_set_size(sprite, o->width > 0.0f ? o->width : (float)t->tile_width,
                            o->height > 0.0f ? o->height : (float)t->tile_height);
        wgf_sprite_set_tint(sprite, (wgf_color_t)tint_of(l));
        wgf_gfx_priv_tilemap_object_middle(d, o, &x, &y);
        wgf_actor_set_position(sprite, x, y, 0.0f);
        /* Flipped down is a half turn of one flipped across; flipped one way, mirrored. */
        wgf_actor_set_rotation(sprite, 0.0f, 0.0f, o->rotation + ((o->flips & WGF_TILEMAP_FLIP_Y) ? PI_F : 0.0f));
        if (((o->flips & WGF_TILEMAP_FLIP_X) != 0) != ((o->flips & WGF_TILEMAP_FLIP_Y) != 0)) {
            wgf_actor_set_scale(sprite, -1.0f, 1.0f, 1.0f);
        }
        wgf_actor_set_visible(sprite, o->visible);
        if (o->name[0] != '\0') wgf_actor_set_name(sprite, o->name);
        wgf_actor_set_parent(sprite, l->actor);
    }
    free(order);
}

/* The layer's record and what it draws made from layer `index` of `d`: its name, place, and
 * look, and its cells, tile objects, or image. False for no memory. */
static bool fill_layer(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, int index)
{
    const wgf_gfx_priv_tilemap_layer_data_t *data = &d->layers[index];
    const int cells = data->kind == WGF_TILEMAP_LAYER_KIND_TILES ? data->columns * data->rows : 0;
    l->index = index;
    l->kind = data->kind;
    l->columns = d->columns, l->rows = d->rows;
    l->alpha = (int)(data->opacity * 255.0f + 0.5f);
    wgf_actor_set_name(l->actor, data->name[0] != '\0' ? data->name : NULL);
    wgf_actor_set_position(l->actor, data->offset_x, data->offset_y, 0.0f);
    wgf_actor_set_visible(l->actor, data->visible);
    if (data->kind == WGF_TILEMAP_LAYER_KIND_OBJECTS) draw_objects(l, d, data);
    if (data->kind == WGF_TILEMAP_LAYER_KIND_IMAGE && data->texture != 0) {
        l->image = wgf_sprite_create(data->texture);
        if (l->image == 0) return false;
        wgf_sprite_set_pivot(l->image, 0.0f, 0.0f);
        wgf_sprite_set_tint(l->image, (wgf_color_t)tint_of(l));
        wgf_actor_set_parent(l->image, l->actor);
    }
    if (cells == 0) return true;
    l->cells = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)cells);
    l->batch_of = (int *)malloc(sizeof(int) * (size_t)cells);
    l->quad_of = (int *)calloc((size_t)cells, sizeof(int));
    l->animated_of = (int *)malloc(sizeof(int) * (size_t)cells);
    if (l->cells == NULL || l->batch_of == NULL || l->quad_of == NULL || l->animated_of == NULL) return false;
    memcpy(l->cells, data->cells, sizeof(uint32_t) * (size_t)cells);
    draw_cells(l, d);
    return true;
}

/* What it holds of its layer let go of: what it drew, its cells, its terrains. */
static void empty_layer(layer_t *l)
{
    clear_drawn(l);
    free_terrains(l);
    free(l->cells);
    free(l->batch_of);
    free(l->quad_of);
    free(l->animated_of);
    l->cells = NULL;
    l->batch_of = l->quad_of = l->animated_of = NULL;
}

/* ---- the part ------------------------------------------------------------------------ */

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
            const batch_t *b = batch_at(l, a->cell);
            int tile = 0, shown;
            const int tileset = wgf_gfx_priv_tilemap_find_tileset(d, l->cells[a->cell], &tile);
            const wgf_gfx_priv_tilemap_tile_t *animation =
                tileset >= 0 ? animation_of(&d->tilesets[tileset], tile) : NULL;
            if (animation == NULL || b == NULL) continue;
            shown = frame_at(animation, l->clock);
            if (shown == a->shown) continue;
            a->shown = shown;
            write_quad(l, d, b, l->quad_of[a->cell], a->cell, shown);
        }
    }
}

static void stop(void)
{
    tl.layers = NULL; /* still owned by their actors, freed after the parts stop */
    free(tl.instances);
    tl.instances = NULL;
    tl.instance_count = tl.instance_capacity = 0;
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
    /* What it made under it goes as its children do: only what it holds here is freed. */
    for (i = 0; i < l->batch_count; i++) free(l->batches[i].cells);
    free(l->batches);
    free_rows(l, false);
    free(l->objects);
    free_terrains(l);
    free(l->cells);
    free(l->batch_of);
    free(l->quad_of);
    free(l->animated_of);
    free(l->animated);
    free(l);
    actor_ptr->as.tilemap_layer = NULL;
}

static const wgf_gfx_priv_actor_kind_t kind = {free_layer, NULL, NULL, NULL};

/* Layer `index` of the map placed under `root`: its actor and its record, what it draws made.
 * Its actor, 0 when it can't be made. */
static wgf_actor_t place_layer(wgf_tilemap_t map, const wgf_gfx_priv_tilemap_data_t *d, int index, wgf_actor_t root)
{
    const wgf_actor_t actor = wgf_gfx_priv_actor_create(WGF_ACTOR_KIND_TILEMAP_LAYER);
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    layer_t *l;
    if (actor_ptr == NULL) return 0;
    l = (layer_t *)calloc(1, sizeof(*l));
    if (l == NULL) {
        wgf_actor_destroy(actor, WGF_ACTOR_DESTROY_CHILDREN);
        return 0;
    }
    actor_ptr->as.tilemap_layer = l;
    l->actor = actor;
    l->root = root;
    l->map = map;
    wgf_gfx_priv_tilemap_retain(map);
    l->next = tl.layers;
    if (tl.layers != NULL) tl.layers->prev = l;
    tl.layers = l;
    wgf_actor_set_parent(actor, root);
    if (!fill_layer(l, d, index)) {
        wgf_actor_destroy(actor, WGF_ACTOR_DESTROY_CHILDREN);
        return 0;
    }
    return actor;
}

/* `root`'s layers of `map` built again from its new data: each kept by name and kind, made
 * anew, or destroyed (wgf_tilemap.h). */
static void rebuild(wgf_tilemap_t map, wgf_actor_t root)
{
    layer_t **old = NULL;
    layer_t *l;
    int k, n, old_count = 0;
    for (l = tl.layers; l != NULL; l = l->next) old_count += l->root == root && l->map == map;
    if (old_count > 0 && (old = (layer_t **)malloc(sizeof(layer_t *) * (size_t)old_count)) == NULL) return;
    old_count = 0;
    for (l = tl.layers; l != NULL; l = l->next) { /* listed newest first: put back in their order */
        if (l->root == root && l->map == map) old[old_count++] = l;
    }
    for (k = 0, n = old_count - 1; k < n; k++, n--) {
        layer_t *swap = old[k];
        old[k] = old[n];
        old[n] = swap;
    }
    for (k = 0; wgf_gfx_priv_tilemap_data(map) != NULL && k < wgf_gfx_priv_tilemap_data(map)->layer_count; k++) {
        const wgf_gfx_priv_tilemap_data_t *d = wgf_gfx_priv_tilemap_data(map);
        wgf_actor_t actor;
        for (n = 0; n < old_count; n++) {
            const char *name = old[n] != NULL ? wgf_actor_get_name(old[n]->actor) : NULL;
            if (name != NULL && old[n]->kind == d->layers[k].kind && strcmp(name, d->layers[k].name) == 0) break;
        }
        if (n < old_count) { /* kept: shown as the new file has it */
            l = old[n];
            old[n] = NULL;
            actor = l->actor;
            empty_layer(l);
            l->clock = 0.0;
            if (!fill_layer(l, d, k)) empty_layer(l);
        } else {
            actor = place_layer(map, d, k, root);
        }
        if (actor != 0) wgf_actor_set_index(actor, k);
    }
    for (n = 0; n < old_count; n++) { /* a layer the file no longer has */
        if (old[n] != NULL) wgf_actor_destroy(old[n]->actor, WGF_ACTOR_DESTROY_CHILDREN);
    }
    free(old);
}

/* Every placed copy of `map`, loaded again in place, built again. */
static void reloaded(wgf_tilemap_t map)
{
    int i;
    for (i = 0; i < tl.instance_count;) { /* those whose root is gone, forgotten */
        if (!wgf_handle_is_alive(tl.instances[i].root)) {
            tl.instances[i] = tl.instances[--tl.instance_count];
        } else {
            i++;
        }
    }
    for (i = 0; i < tl.instance_count; i++) {
        if (tl.instances[i].map == map) rebuild(map, tl.instances[i].root);
    }
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
    wgf_gfx_priv_tilemap_reloaded = reloaded;
    wgf_gfx_priv_actor_set_kind(WGF_ACTOR_KIND_TILEMAP_LAYER, &kind);
    wgf_actor_set_name(root, d->file[0] != '\0' ? d->file : NULL);
    for (i = 0; i < d->layer_count; i++) {
        d = wgf_gfx_priv_tilemap_data(map); /* the same, looked up again across the creates */
        if (d == NULL || place_layer(map, d, i, root) == 0) {
            wgf_actor_destroy(root, WGF_ACTOR_DESTROY_CHILDREN);
            return 0;
        }
    }
    if (parent != 0) wgf_actor_set_parent(root, parent);
    if (grow((void **)&tl.instances, &tl.instance_capacity, tl.instance_count + 1, sizeof(instance_t))) {
        tl.instances[tl.instance_count].root = root;
        tl.instances[tl.instance_count].map = map;
        tl.instance_count++;
    }
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

/* A bulk cell (get_cells) as Tiled's 32 bits. */
static uint32_t from_bulk(int cell)
{
    const int gid = cell & (int)WGF_GFX_PRIV_TILEMAP_GID_MASK;
    return gid == 0 ? 0u : ((uint32_t)gid | tiled_bits(cell >> BULK_FLIP_SHIFT));
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

/* `cell` shown as Tiled's `value` (a gid and its flip bits). */
static void put(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, int cell, uint32_t value)
{
    const int tileset = wgf_gfx_priv_tilemap_find_tileset(d, value, NULL);
    if (l->cells[cell] == value) return;
    if (l->ordered) {
        l->cells[cell] = value;
        build_row(l, d, l->row_of[cell], false);
    } else if (tileset >= 0 && reaches_out(d, tileset)) { /* a tile reaching past its cell: in order from now */
        l->cells[cell] = value;
        clear_drawn(l);
        draw_cells(l, d);
    } else {
        take_out(l, d, cell);
        l->cells[cell] = value;
        put_in(l, d, cell);
    }
}

bool wgf_tilemap_layer_set_cell(wgf_actor_t layer, int column, int row, int gid, int flips)
{
    layer_t *l = layer_of(layer);
    const wgf_gfx_priv_tilemap_data_t *d = l != NULL ? wgf_gfx_priv_tilemap_data(l->map) : NULL;
    if (d == NULL || !inside(l, column, row) || !is_cell(d, gid, flips)) return false;
    put(l, d, row * l->columns + column, gid == 0 ? 0u : ((uint32_t)gid | tiled_bits(flips)));
    free_terrains(l); /* read again from the cells when next painted */
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
    if (l->ordered) { /* every cell, then drawn once */
        for (i = 0; i < count; i++) l->cells[i] = from_bulk(cells[i]);
        clear_drawn(l);
        draw_cells(l, d);
    } else {
        for (i = 0; i < count; i++) put(l, d, i, from_bulk(cells[i]));
    }
    free_terrains(l);
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

/* The cell at x, y of the layer's parent: its column and row. */
static void cell_at(wgf_actor_t layer, float x, float y, int *column, int *row)
{
    const layer_t *l;
    const wgf_vec3_t p = local_point(layer, x, y, &l);
    const wgf_gfx_priv_tilemap_data_t *d = l != NULL ? wgf_gfx_priv_tilemap_data(l->map) : NULL;
    *column = 0, *row = 0;
    if (d != NULL) wgf_gfx_priv_tilemap_cell_at(d, p.x, p.y, column, row);
}

int wgf_tilemap_layer_get_column_at(wgf_actor_t layer, float x, float y)
{
    int column = 0, row = 0;
    cell_at(layer, x, y, &column, &row);
    return column;
}

int wgf_tilemap_layer_get_row_at(wgf_actor_t layer, float x, float y)
{
    int column = 0, row = 0;
    cell_at(layer, x, y, &column, &row);
    return row;
}

wgf_vec2_t wgf_tilemap_layer_get_cell_middle(wgf_actor_t layer, int column, int row)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(layer);
    const layer_t *l = actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_TILEMAP_LAYER
                           ? actor_ptr->as.tilemap_layer
                           : NULL;
    const wgf_gfx_priv_tilemap_data_t *d = l != NULL ? wgf_gfx_priv_tilemap_data(l->map) : NULL;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    wgf_vec3_t p;
    if (d == NULL) return wgf_vec2_make(0.0f, 0.0f);
    wgf_gfx_priv_tilemap_cell_box(d, column, row, &x, &y, &w, &h);
    p = wgf_mat4_transform_point(wgf_gfx_priv_actor_get_local_matrix(wgf_gfx_priv_actor_of(layer)),
                                 wgf_vec3_make(x + w * 0.5f, y + h * 0.5f, 0.0f));
    return wgf_vec2_make(p.x, p.y);
}

wgf_actor_t wgf_tilemap_layer_get_object_actor(wgf_actor_t layer, int object)
{
    const layer_t *l = layer_of(layer);
    if (l == NULL || object < 0 || object >= l->object_count || l->objects[object] == 0) return 0;
    return wgf_handle_is_alive(l->objects[object]) ? l->objects[object] : 0;
}

/* ---- terrain ------------------------------------------------------------------------- */

/* Set `set`'s grid as painted, made from the layer's cells when it isn't yet: a corner set's
 * (columns + 1) by (rows + 1) corners; an edge set's horizontal edges ((rows + 1) by columns)
 * then its vertical ones (rows by (columns + 1)). NULL for no memory. */
static int *grid_of(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, int set)
{
    const wgf_gfx_priv_tilemap_terrain_set_t *s = &d->terrain_sets[set];
    const int columns = l->columns, rows = l->rows, vertical = (rows + 1) * columns;
    const bool corner = s->kind == WGF_TILEMAP_TERRAIN_KIND_CORNER;
    int *grid;
    int row, column;
    if (l->terrains == NULL) {
        l->terrains = (int **)calloc((size_t)d->terrain_set_count, sizeof(int *));
        if (l->terrains == NULL) return NULL;
        l->terrain_count = d->terrain_set_count;
    }
    if (l->terrains[set] != NULL) return l->terrains[set];
    grid = (int *)calloc(corner ? (size_t)(columns + 1) * (size_t)(rows + 1)
                                : (size_t)vertical + (size_t)rows * (size_t)(columns + 1),
                         sizeof(int));
    if (grid == NULL) return NULL;
    for (row = 0; row < rows; row++) {
        for (column = 0; column < columns; column++) {
            int tile = 0, found;
            if (wgf_gfx_priv_tilemap_find_tileset(d, l->cells[row * columns + column], &tile) != s->tileset) continue;
            found = wgf_gfx_priv_tilemap_terrain_tile(s, tile);
            if (found < 0) continue;
            if (corner) {
                grid[row * (columns + 1) + column] = s->places[found][7];
                grid[row * (columns + 1) + column + 1] = s->places[found][1];
                grid[(row + 1) * (columns + 1) + column + 1] = s->places[found][3];
                grid[(row + 1) * (columns + 1) + column] = s->places[found][5];
            } else {
                grid[row * columns + column] = s->places[found][0];
                grid[(row + 1) * columns + column] = s->places[found][4];
                grid[vertical + row * (columns + 1) + column] = s->places[found][6];
                grid[vertical + row * (columns + 1) + column + 1] = s->places[found][2];
            }
        }
    }
    l->terrains[set] = grid;
    return grid;
}

/* Cell `column`, `row` given set `set`'s tile that fits its corners or edges in `grid`:
 * emptied when none has a terrain (and its tile was its tileset's), left as it was when no
 * tile fits. */
static void fit(layer_t *l, const wgf_gfx_priv_tilemap_data_t *d, int set, const int *grid, int column, int row)
{
    const wgf_gfx_priv_tilemap_terrain_set_t *s = &d->terrain_sets[set];
    const int columns = l->columns, vertical = (l->rows + 1) * columns;
    int eight[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int i, p, cell;
    if (!inside(l, column, row)) return;
    cell = row * columns + column;
    if (s->kind == WGF_TILEMAP_TERRAIN_KIND_CORNER) {
        eight[7] = grid[row * (columns + 1) + column];
        eight[1] = grid[row * (columns + 1) + column + 1];
        eight[3] = grid[(row + 1) * (columns + 1) + column + 1];
        eight[5] = grid[(row + 1) * (columns + 1) + column];
    } else {
        eight[0] = grid[row * columns + column];
        eight[4] = grid[(row + 1) * columns + column];
        eight[6] = grid[vertical + row * (columns + 1) + column];
        eight[2] = grid[vertical + row * (columns + 1) + column + 1];
    }
    for (p = 0; p < 8 && eight[p] == 0; p++) {
    }
    if (p == 8) {
        if (wgf_gfx_priv_tilemap_find_tileset(d, l->cells[cell], NULL) == s->tileset) put(l, d, cell, 0u);
        return;
    }
    for (i = 0; i < s->tile_count; i++) {
        for (p = 0; p < 8 && s->places[i][p] == eight[p]; p++) {
        }
        if (p == 8) {
            put(l, d, cell, (uint32_t)(d->tilesets[s->tileset].first_gid + s->tiles[i]));
            return;
        }
    }
}

bool wgf_tilemap_layer_paint_terrain(wgf_actor_t layer, int column, int row, int set, int terrain)
{
    layer_t *l = layer_of(layer);
    const wgf_gfx_priv_tilemap_data_t *d = l != NULL ? wgf_gfx_priv_tilemap_data(l->map) : NULL;
    const wgf_gfx_priv_tilemap_terrain_set_t *s;
    int *grid;
    int r, c;
    if (d == NULL || !inside(l, column, row) || set < 0 || set >= d->terrain_set_count) return false;
    s = &d->terrain_sets[set];
    if (terrain < 0 || terrain > s->terrain_count || s->kind == WGF_TILEMAP_TERRAIN_KIND_MIXED ||
        d->orientation == WGF_TILEMAP_ORIENTATION_STAGGERED || d->orientation == WGF_TILEMAP_ORIENTATION_HEXAGONAL) {
        return false;
    }
    grid = grid_of(l, d, set);
    if (grid == NULL) return false;
    if (s->kind == WGF_TILEMAP_TERRAIN_KIND_CORNER) {
        const int columns = l->columns + 1;
        grid[row * columns + column] = terrain;
        grid[row * columns + column + 1] = terrain;
        grid[(row + 1) * columns + column] = terrain;
        grid[(row + 1) * columns + column + 1] = terrain;
        for (r = row - 1; r <= row + 1; r++) {
            for (c = column - 1; c <= column + 1; c++) fit(l, d, set, grid, c, r);
        }
    } else {
        const int vertical = (l->rows + 1) * l->columns;
        grid[row * l->columns + column] = terrain;
        grid[(row + 1) * l->columns + column] = terrain;
        grid[vertical + row * (l->columns + 1) + column] = terrain;
        grid[vertical + row * (l->columns + 1) + column + 1] = terrain;
        fit(l, d, set, grid, column, row);
        fit(l, d, set, grid, column - 1, row);
        fit(l, d, set, grid, column + 1, row);
        fit(l, d, set, grid, column, row - 1);
        fit(l, d, set, grid, column, row + 1);
    }
    return true;
}
