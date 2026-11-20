/* Tiled maps (wgf_tilemap.h): helix-legacy's TiledMap2D, TiledTileset2D, TiledLayer2D,
 * TiledObject2D, and TiledProperties2D, read in C. A map is a resource: its `.tmj` read and
 * parsed on a worker with the `.tsj` tilesets it names (through core's cJSON wrapper), its
 * images made textures as it finishes on the main thread. A lister for `.tmj` and `.tsj`
 * names the tilesets and images to the asset part, so on the web they are fetched with the
 * map before it is read. What is placed of a map is wgf_gfx_tilemap_layer.c's. */
#include "wgf_tilemap.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h" /* its inflate: a layer's zlib or gzip data (compiled in wgf_gfx_stb_impl.c) */
#include "tilemap/wgf_gfx_tilemap_priv.h"
#include "wgf_asset_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_json_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"
#include "wgf_resource.h"

#define MAX_SIDE 65536          /* a map's columns or rows */
#define MAX_CELLS (1 << 24)     /* a map's cells */
#define MAX_TILE_SIDE 4096      /* a tile's pixels */
#define MAX_TILES (1 << 20)     /* a tileset's tiles */
#define MAX_GROUP_DEPTH 32
#define PI_F 3.14159265358979f

typedef struct map_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's */
    wgf_gfx_priv_tilemap_data_t *data; /* NULL until READY */
} map_t;

static struct {
    bool ready, installed;
    wgf_core_priv_handle_pool_t pool;
    map_t *maps;
} tm;

static map_t *map_of_at(wgf_tilemap_t map, const char *caller)
{
    uint16_t index;
    if (!tm.ready || !wgf_core_priv_handle_pool_resolve_at(&tm.pool, map, &index, caller)) return NULL;
    return &tm.maps[index];
}
#define map_of(map) map_of_at((map), WGF_CORE_PRIV_CALLER)

static const wgf_gfx_priv_tilemap_data_t *data_of_at(wgf_tilemap_t map, const char *caller)
{
    const map_t *m = map_of_at(map, caller);
    return m != NULL ? m->data : NULL;
}
#define data_of(map) data_of_at((map), WGF_CORE_PRIV_CALLER)

/* ---- freeing ------------------------------------------------------------------------- */

static void free_properties(wgf_gfx_priv_tilemap_properties_t *p)
{
    int i;
    for (i = 0; i < p->count; i++) {
        free(p->items[i].name);
        free(p->items[i].text);
    }
    free(p->items);
    p->items = NULL;
    p->count = 0;
}

static void free_data(wgf_gfx_priv_tilemap_data_t *d)
{
    int i, k;
    if (d == NULL) return;
    for (i = 0; i < d->layer_count; i++) {
        wgf_gfx_priv_tilemap_layer_data_t *l = &d->layers[i];
        free(l->name);
        free(l->cells);
        for (k = 0; k < l->object_count; k++) {
            free(l->objects[k].name);
            free(l->objects[k].class_name);
            free(l->objects[k].points);
            free_properties(&l->objects[k].properties);
        }
        free(l->objects);
        free_properties(&l->properties);
    }
    free(d->layers);
    for (i = 0; i < d->tileset_count; i++) {
        wgf_gfx_priv_tilemap_tileset_t *t = &d->tilesets[i];
        free(t->name);
        free(t->image);
        if (t->texture != 0) wgf_resource_release(t->texture);
        for (k = 0; t->tiles != NULL && k < t->tile_count; k++) {
            if (t->tiles[k] == NULL) continue;
            free(t->tiles[k]->class_name);
            free_properties(&t->tiles[k]->properties);
            free(t->tiles[k]->frames);
            free(t->tiles[k]);
        }
        free(t->tiles);
    }
    free(d->tilesets);
    free_properties(&d->properties);
    free(d->file);
    free(d);
}

/* ---- reading ------------------------------------------------------------------------- */

static char *copy(const char *text)
{
    const size_t n = strlen(text != NULL ? text : "");
    char *out = (char *)malloc(n + 1);
    if (out != NULL) memcpy(out, text != NULL ? text : "", n + 1);
    return out;
}

/* A number as text: whole ones without a point, others as short as read back the same. */
static void number_text(double value, char *out, size_t size)
{
    int precision;
    if (value == floor(value) && fabs(value) < 1e15) {
        snprintf(out, size, "%.0f", value);
        return;
    }
    for (precision = 6; precision < 17; precision++) {
        snprintf(out, size, "%.*g", precision, value);
        if (strtod(out, NULL) == value) return;
    }
    snprintf(out, size, "%.17g", value);
}

/* A color as Tiled writes it ("#AARRGGBB", or "#RRGGBB" opaque) as 0xRRGGBBAA; 0 for anything
 * else. */
static wgf_color_t parse_color(const char *text)
{
    unsigned long v;
    char *end;
    const size_t n = strlen(text);
    if (text[0] != '#' || (n != 7 && n != 9)) return 0;
    v = strtoul(text + 1, &end, 16);
    if (*end != '\0') return 0;
    if (n == 7) return (wgf_color_t)(((v & 0xffffffUL) << 8) | 0xffUL);
    return (wgf_color_t)(((v & 0xffffffUL) << 8) | ((v >> 24) & 0xffUL));
}

static bool read_properties(const wgf_core_priv_json_t *list, wgf_gfx_priv_tilemap_properties_t *out)
{
    const int n = wgf_core_priv_json_size(list);
    int i;
    out->items = NULL;
    out->count = 0;
    if (n <= 0 || !wgf_core_priv_json_is_array(list)) return true;
    out->items = (wgf_gfx_priv_tilemap_property_t *)calloc((size_t)n, sizeof(*out->items));
    if (out->items == NULL) return false;
    for (i = 0; i < n; i++) {
        const wgf_core_priv_json_t *entry = wgf_core_priv_json_at(list, i);
        const wgf_core_priv_json_t *value = wgf_core_priv_json_get(entry, "value");
        const char *type = wgf_core_priv_json_get_string(entry, "type");
        wgf_gfx_priv_tilemap_property_t *p = &out->items[out->count];
        char text[64];
        char *printed = NULL;
        if (wgf_core_priv_json_get_string(entry, "name")[0] == '\0') continue;
        p->type = strcmp(type, "int") == 0      ? WGF_TILEMAP_PROPERTY_TYPE_INT
                  : strcmp(type, "float") == 0  ? WGF_TILEMAP_PROPERTY_TYPE_FLOAT
                  : strcmp(type, "bool") == 0   ? WGF_TILEMAP_PROPERTY_TYPE_BOOL
                  : strcmp(type, "color") == 0  ? WGF_TILEMAP_PROPERTY_TYPE_COLOR
                  : strcmp(type, "file") == 0   ? WGF_TILEMAP_PROPERTY_TYPE_FILE
                  : strcmp(type, "object") == 0 ? WGF_TILEMAP_PROPERTY_TYPE_OBJECT
                  : strcmp(type, "class") == 0  ? WGF_TILEMAP_PROPERTY_TYPE_CLASS
                                                : WGF_TILEMAP_PROPERTY_TYPE_STRING;
        text[0] = '\0';
        if (wgf_core_priv_json_is_number(value)) {
            p->number = wgf_core_priv_json_number(value, 0.0);
            number_text(p->number, text, sizeof(text));
        } else if (p->type == WGF_TILEMAP_PROPERTY_TYPE_BOOL) {
            p->number = wgf_core_priv_json_truth(value) ? 1.0 : 0.0;
            snprintf(text, sizeof(text), "%s", p->number != 0.0 ? "true" : "false");
        } else if (wgf_core_priv_json_is_object(value) || wgf_core_priv_json_is_array(value)) {
            printed = wgf_core_priv_json_print(value);
        }
        p->name = copy(wgf_core_priv_json_get_string(entry, "name"));
        p->text = printed != NULL ? printed : copy(text[0] != '\0' ? text : wgf_core_priv_json_string(value));
        if (p->name == NULL || p->text == NULL) {
            free(p->name);
            free(p->text);
            return false;
        }
        out->count++;
    }
    return true;
}

/* A path a file names, read against the file at `base`, into `out`. */
static bool beside(const char *base, const char *uri, char *out, size_t size)
{
    return uri[0] != '\0' && wgf_asset_priv_is_relative_uri(uri) && wgf_asset_priv_join_relative(base, uri, out, size);
}

static bool in_range(double value, double low, double high)
{
    return value >= low && value <= high && value == floor(value);
}

/* One tileset, embedded in the map at `path` or the `.tsj` it names. False (logged) for one
 * that can't be used. */
static bool read_tileset(const wgf_core_priv_json_t *json, const char *path, int first_gid, const char *map_path,
                         wgf_gfx_priv_tilemap_tileset_t *t)
{
    const wgf_core_priv_json_t *tiles = wgf_core_priv_json_get(json, "tiles");
    const wgf_core_priv_json_t *offset = wgf_core_priv_json_get(json, "tileoffset");
    const double tw = wgf_core_priv_json_get_number(json, "tilewidth", 0), th = wgf_core_priv_json_get_number(json, "tileheight", 0);
    const double count = wgf_core_priv_json_get_number(json, "tilecount", 0);
    const double columns = wgf_core_priv_json_get_number(json, "columns", 0);
    const double margin = wgf_core_priv_json_get_number(json, "margin", 0);
    const double spacing = wgf_core_priv_json_get_number(json, "spacing", 0);
    char image[WGF_CORE_PRIV_FS_PATH_MAX];
    int i;
    t->first_gid = first_gid;
    t->name = copy(wgf_core_priv_json_get_string(json, "name"));
    if (t->name == NULL) return false;
    if (wgf_core_priv_json_get_string(json, "image")[0] == '\0') {
        wgf_log_warn("wgf_tilemap: %s: tileset %s is an image collection (an image a tile), which isn't read: one "
                     "image for the tileset",
                     map_path, t->name);
        return false;
    }
    if (!in_range(tw, 1, MAX_TILE_SIDE) || !in_range(th, 1, MAX_TILE_SIDE) || !in_range(count, 1, MAX_TILES) ||
        !in_range(columns, 1, MAX_TILES) || !in_range(margin, 0, 65535) || !in_range(spacing, 0, 65535)) {
        wgf_log_warn("wgf_tilemap: %s: tileset %s's tile size, count, columns, margin, or spacing is out of range",
                     map_path, t->name);
        return false;
    }
    if (!beside(path, wgf_core_priv_json_get_string(json, "image"), image, sizeof(image))) {
        wgf_log_warn("wgf_tilemap: %s: tileset %s names an image outside the host: %s", map_path, t->name,
                     wgf_core_priv_json_get_string(json, "image"));
        return false;
    }
    t->image = copy(image);
    t->tile_width = (int)tw, t->tile_height = (int)th, t->tile_count = (int)count, t->columns = (int)columns;
    t->margin = (int)margin, t->spacing = (int)spacing;
    t->offset_x = (int)wgf_core_priv_json_get_number(offset, "x", 0);
    t->offset_y = (int)wgf_core_priv_json_get_number(offset, "y", 0);
    t->tiles = (wgf_gfx_priv_tilemap_tile_t **)calloc((size_t)t->tile_count, sizeof(*t->tiles));
    if (t->image == NULL || t->tiles == NULL) return false;
    for (i = 0; i < wgf_core_priv_json_size(tiles); i++) {
        const wgf_core_priv_json_t *tile = wgf_core_priv_json_at(tiles, i);
        const wgf_core_priv_json_t *frames = wgf_core_priv_json_get(tile, "animation");
        const double id = wgf_core_priv_json_get_number(tile, "id", -1);
        const char *class_name = wgf_core_priv_json_get_string(tile, "type");
        wgf_gfx_priv_tilemap_tile_t *made;
        int f;
        if (!in_range(id, 0, t->tile_count - 1)) continue; /* a tile it hasn't */
        if (class_name[0] == '\0') class_name = wgf_core_priv_json_get_string(tile, "class");
        made = (wgf_gfx_priv_tilemap_tile_t *)calloc(1, sizeof(*made));
        if (made == NULL) return false;
        t->tiles[(int)id] = made;
        if (class_name[0] != '\0' && (made->class_name = copy(class_name)) == NULL) return false;
        if (!read_properties(wgf_core_priv_json_get(tile, "properties"), &made->properties)) return false;
        if (wgf_core_priv_json_size(frames) <= 0 || !wgf_core_priv_json_is_array(frames)) continue;
        made->frames = (wgf_gfx_priv_tilemap_frame_t *)calloc((size_t)wgf_core_priv_json_size(frames),
                                                              sizeof(*made->frames));
        if (made->frames == NULL) return false;
        for (f = 0; f < wgf_core_priv_json_size(frames); f++) {
            const wgf_core_priv_json_t *frame = wgf_core_priv_json_at(frames, f);
            const double tile_id = wgf_core_priv_json_get_number(frame, "tileid", -1);
            const double ms = wgf_core_priv_json_get_number(frame, "duration", 0);
            if (!in_range(tile_id, 0, t->tile_count - 1) || !(ms >= 0.0 && ms < 1e9)) continue;
            made->frames[made->frame_count].tile = (int)tile_id;
            made->frames[made->frame_count].seconds = (float)(ms / 1000.0);
            made->seconds += made->frames[made->frame_count].seconds;
            made->frame_count++;
        }
        if (made->frame_count == 0 || made->seconds <= 0.0f) { /* nothing to step through */
            free(made->frames);
            made->frames = NULL;
            made->frame_count = 0;
        }
    }
    return true;
}

static int base64_value(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Base64 text decoded into a new buffer, its size in `*size`; NULL for text that isn't. */
static unsigned char *base64_decode(const char *text, int *size)
{
    const size_t n = strlen(text);
    unsigned char *out = (unsigned char *)malloc(n / 4 * 3 + 3);
    unsigned bits = 0;
    int have = 0, made = 0;
    size_t i;
    if (out == NULL) return NULL;
    for (i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)text[i];
        int v;
        if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        v = base64_value(c);
        if (v < 0) {
            free(out);
            return NULL;
        }
        bits = (bits << 6) | (unsigned)v;
        have += 6;
        if (have >= 8) {
            have -= 8;
            out[made++] = (unsigned char)((bits >> have) & 0xffu);
        }
    }
    *size = made;
    return out;
}

/* Gzip's deflated data, past its header: where it starts, -1 for data that isn't gzip. */
static int gzip_start(const unsigned char *data, int size)
{
    int at = 10;
    unsigned flags;
    if (size < 18 || data[0] != 0x1f || data[1] != 0x8b || data[2] != 8) return -1;
    flags = data[3];
    if (flags & 4u) {
        if (at + 2 > size) return -1;
        at += 2 + (data[at] | (data[at + 1] << 8));
    }
    if (flags & 8u) {
        while (at < size && data[at] != 0) at++;
        at++;
    }
    if (flags & 16u) {
        while (at < size && data[at] != 0) at++;
        at++;
    }
    if (flags & 2u) at += 2;
    return at < size ? at : -1;
}

/* A tile layer's `count` cells from its data: an array of ids, or base64 of little-endian
 * 32-bit ids, plain or zlib or gzip compressed. */
static uint32_t *read_cells(const wgf_core_priv_json_t *json, int count, const char *map_path, const char *name)
{
    const wgf_core_priv_json_t *data = wgf_core_priv_json_get(json, "data");
    const char *encoding = wgf_core_priv_json_get_string(json, "encoding");
    const char *compression = wgf_core_priv_json_get_string(json, "compression");
    uint32_t *cells = (uint32_t *)calloc((size_t)count, sizeof(uint32_t));
    int i;
    if (cells == NULL) return NULL;
    if (strcmp(encoding, "base64") != 0) {
        if (!wgf_core_priv_json_is_array(data) || wgf_core_priv_json_size(data) != count) {
            wgf_log_warn("wgf_tilemap: %s: layer %s has %d cells for its %d", map_path, name,
                         wgf_core_priv_json_size(data), count);
            free(cells);
            return NULL;
        }
        for (i = 0; i < count; i++) {
            const double v = wgf_core_priv_json_number(wgf_core_priv_json_at(data, i), -1);
            if (!in_range(v, 0, 4294967295.0)) {
                wgf_log_warn("wgf_tilemap: %s: layer %s's cell %d isn't a tile's id", map_path, name, i);
                free(cells);
                return NULL;
            }
            cells[i] = (uint32_t)v;
        }
        return cells;
    }
    {
        int size = 0, inflated = 0, start = -1;
        unsigned char *bytes = base64_decode(wgf_core_priv_json_string(data), &size), *raw = bytes;
        unsigned char *out = NULL;
        if (bytes != NULL && compression[0] != '\0') {
            out = (unsigned char *)malloc((size_t)count * 4);
            if (out != NULL && strcmp(compression, "zlib") == 0) {
                inflated = stbi_zlib_decode_buffer((char *)out, count * 4, (const char *)bytes, size);
            } else if (out != NULL && strcmp(compression, "gzip") == 0 && (start = gzip_start(bytes, size)) >= 0) {
                inflated = stbi_zlib_decode_noheader_buffer((char *)out, count * 4, (const char *)bytes + start,
                                                            size - start);
            } else {
                wgf_log_warn("wgf_tilemap: %s: layer %s is compressed with %s, which isn't read: CSV, base64, or base64 "
                             "with zlib or gzip",
                             map_path, name, compression);
                inflated = -2;
            }
            raw = out;
            size = inflated;
        }
        if (raw == NULL || size < count * 4) {
            if (inflated != -2) {
                wgf_log_warn("wgf_tilemap: %s: layer %s's data doesn't hold its %d cells", map_path, name, count);
            }
            free(bytes);
            free(out);
            free(cells);
            return NULL;
        }
        for (i = 0; i < count; i++) {
            const unsigned char *b = raw + 4 * i;
            cells[i] = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        }
        free(bytes);
        free(out);
        return cells;
    }
}

static bool read_object(const wgf_core_priv_json_t *json, wgf_gfx_priv_tilemap_object_t *o)
{
    const wgf_core_priv_json_t *points = wgf_core_priv_json_get(json, "polygon");
    const char *class_name = wgf_core_priv_json_get_string(json, "type");
    int i;
    o->id = (int)wgf_core_priv_json_get_number(json, "id", 0);
    if (class_name[0] == '\0') class_name = wgf_core_priv_json_get_string(json, "class");
    o->name = copy(wgf_core_priv_json_get_string(json, "name"));
    o->class_name = copy(class_name);
    o->x = (float)wgf_core_priv_json_get_number(json, "x", 0);
    o->y = (float)wgf_core_priv_json_get_number(json, "y", 0);
    o->width = (float)wgf_core_priv_json_get_number(json, "width", 0);
    o->height = (float)wgf_core_priv_json_get_number(json, "height", 0);
    o->rotation = (float)wgf_core_priv_json_get_number(json, "rotation", 0) * PI_F / 180.0f;
    o->visible = wgf_core_priv_json_get(json, "visible") == NULL || wgf_core_priv_json_truth(wgf_core_priv_json_get(json, "visible"));
    if (o->name == NULL || o->class_name == NULL) return false;
    if (!read_properties(wgf_core_priv_json_get(json, "properties"), &o->properties)) return false;
    o->shape = WGF_TILEMAP_SHAPE_RECTANGLE;
    if (in_range(wgf_core_priv_json_get_number(json, "gid", -1), 1, 4294967295.0)) {
        o->shape = WGF_TILEMAP_SHAPE_TILE;
        o->gid = (int)((uint32_t)wgf_core_priv_json_get_number(json, "gid", 0) & WGF_GFX_PRIV_TILEMAP_GID_MASK);
    } else if (wgf_core_priv_json_truth(wgf_core_priv_json_get(json, "point"))) {
        o->shape = WGF_TILEMAP_SHAPE_POINT;
    } else if (wgf_core_priv_json_truth(wgf_core_priv_json_get(json, "ellipse"))) {
        o->shape = WGF_TILEMAP_SHAPE_ELLIPSE;
    } else if (wgf_core_priv_json_is_array(points) ||
               wgf_core_priv_json_is_array(wgf_core_priv_json_get(json, "polyline"))) {
        o->shape = wgf_core_priv_json_is_array(points) ? WGF_TILEMAP_SHAPE_POLYGON : WGF_TILEMAP_SHAPE_POLYLINE;
        if (!wgf_core_priv_json_is_array(points)) points = wgf_core_priv_json_get(json, "polyline");
        if (wgf_core_priv_json_size(points) > 0) {
            o->points = (float *)calloc((size_t)wgf_core_priv_json_size(points) * 2, sizeof(float));
            if (o->points == NULL) return false;
        }
        for (i = 0; i < wgf_core_priv_json_size(points); i++) {
            const wgf_core_priv_json_t *p = wgf_core_priv_json_at(points, i);
            o->points[2 * i] = (float)wgf_core_priv_json_get_number(p, "x", 0);
            o->points[2 * i + 1] = (float)wgf_core_priv_json_get_number(p, "y", 0);
        }
        o->point_count = wgf_core_priv_json_size(points);
    } else if (wgf_core_priv_json_is_object(wgf_core_priv_json_get(json, "text"))) {
        o->shape = WGF_TILEMAP_SHAPE_TEXT;
    }
    return true;
}

typedef struct reading_t {
    const char *path; /* the map's */
    wgf_gfx_priv_tilemap_data_t *data;
    int layer_capacity;
} reading_t;

/* A list of layers, groups flattened: each appended with its groups' offset, opacity, and
 * visibility. False (logged) for one that can't be read. */
static bool read_layers(reading_t *r, const wgf_core_priv_json_t *list, float offset_x, float offset_y, float opacity,
                        bool visible, int depth)
{
    int i;
    if (depth > MAX_GROUP_DEPTH) {
        wgf_log_warn("wgf_tilemap: %s: groups nested more than %d deep", r->path, MAX_GROUP_DEPTH);
        return false;
    }
    for (i = 0; i < wgf_core_priv_json_size(list); i++) {
        const wgf_core_priv_json_t *json = wgf_core_priv_json_at(list, i);
        const char *type = wgf_core_priv_json_get_string(json, "type");
        const float x = offset_x + (float)wgf_core_priv_json_get_number(json, "offsetx", 0);
        const float y = offset_y + (float)wgf_core_priv_json_get_number(json, "offsety", 0);
        const float alpha = opacity * (float)wgf_core_priv_json_get_number(json, "opacity", 1);
        const bool shown = visible && (wgf_core_priv_json_get(json, "visible") == NULL ||
                                       wgf_core_priv_json_truth(wgf_core_priv_json_get(json, "visible")));
        wgf_gfx_priv_tilemap_layer_data_t *l;
        int k;
        if (strcmp(type, "group") == 0) {
            if (!read_layers(r, wgf_core_priv_json_get(json, "layers"), x, y, alpha, shown, depth + 1)) return false;
            continue;
        }
        if (r->data->layer_count == r->layer_capacity) {
            const int grown = r->layer_capacity > 0 ? r->layer_capacity * 2 : 8;
            void *moved = realloc(r->data->layers, sizeof(*r->data->layers) * (size_t)grown);
            if (moved == NULL) return false;
            r->data->layers = (wgf_gfx_priv_tilemap_layer_data_t *)moved;
            r->layer_capacity = grown;
        }
        l = &r->data->layers[r->data->layer_count++];
        memset(l, 0, sizeof(*l));
        l->name = copy(wgf_core_priv_json_get_string(json, "name"));
        l->visible = shown;
        l->opacity = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
        l->offset_x = x, l->offset_y = y;
        if (l->name == NULL || !read_properties(wgf_core_priv_json_get(json, "properties"), &l->properties)) return false;
        if (strcmp(type, "tilelayer") == 0) {
            l->kind = WGF_TILEMAP_LAYER_KIND_TILES;
            if (wgf_core_priv_json_get(json, "chunks") != NULL) {
                wgf_log_warn("wgf_tilemap: %s: layer %s is an infinite map's, which isn't read: untick Infinite in the "
                             "map's properties",
                             r->path, l->name);
                return false;
            }
            if (wgf_core_priv_json_get_number(json, "width", 0) != r->data->columns ||
                wgf_core_priv_json_get_number(json, "height", 0) != r->data->rows) {
                wgf_log_warn("wgf_tilemap: %s: layer %s isn't the map's size", r->path, l->name);
                return false;
            }
            l->columns = r->data->columns, l->rows = r->data->rows;
            l->cells = read_cells(json, l->columns * l->rows, r->path, l->name);
            if (l->cells == NULL) return false;
        } else if (strcmp(type, "objectgroup") == 0) {
            const wgf_core_priv_json_t *objects = wgf_core_priv_json_get(json, "objects");
            l->kind = WGF_TILEMAP_LAYER_KIND_OBJECTS;
            if (wgf_core_priv_json_size(objects) > 0) {
                l->objects = (wgf_gfx_priv_tilemap_object_t *)calloc((size_t)wgf_core_priv_json_size(objects),
                                                                     sizeof(*l->objects));
                if (l->objects == NULL) return false;
            }
            for (k = 0; k < wgf_core_priv_json_size(objects); k++) {
                l->object_count++;
                if (!read_object(wgf_core_priv_json_at(objects, k), &l->objects[k])) return false;
            }
        } else if (strcmp(type, "imagelayer") == 0) {
            l->kind = WGF_TILEMAP_LAYER_KIND_IMAGE;
        } else {
            wgf_log_warn("wgf_tilemap: %s: layer %s is of a kind not read: %s", r->path, l->name, type);
            return false;
        }
    }
    return true;
}

/* The file's name less its directory and extension. */
static char *file_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *start = slash != NULL ? slash + 1 : path;
    const char *dot = strrchr(start, '.');
    const size_t n = dot != NULL && dot > start ? (size_t)(dot - start) : strlen(start);
    char *out = (char *)malloc(n + 1);
    if (out == NULL) return NULL;
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

static int by_first_gid(const void *a, const void *b)
{
    return ((const wgf_gfx_priv_tilemap_tileset_t *)a)->first_gid - ((const wgf_gfx_priv_tilemap_tileset_t *)b)->first_gid;
}

static bool read_map(const wgf_core_priv_json_t *json, const char *path, wgf_gfx_priv_tilemap_data_t *d)
{
    const char *orientation = wgf_core_priv_json_get_string(json, "orientation");
    const wgf_core_priv_json_t *tilesets = wgf_core_priv_json_get(json, "tilesets");
    const double columns = wgf_core_priv_json_get_number(json, "width", 0);
    const double rows = wgf_core_priv_json_get_number(json, "height", 0);
    const double tw = wgf_core_priv_json_get_number(json, "tilewidth", 0);
    const double th = wgf_core_priv_json_get_number(json, "tileheight", 0);
    reading_t r;
    int i;
    if (orientation[0] != '\0' && strcmp(orientation, "orthogonal") != 0) {
        wgf_log_warn("wgf_tilemap: %s is %s: only orthogonal maps are read", path, orientation);
        return false;
    }
    if (wgf_core_priv_json_truth(wgf_core_priv_json_get(json, "infinite"))) {
        wgf_log_warn("wgf_tilemap: %s is infinite, which isn't read: untick Infinite in its properties", path);
        return false;
    }
    if (!in_range(columns, 1, MAX_SIDE) || !in_range(rows, 1, MAX_SIDE) || columns * rows > MAX_CELLS ||
        !in_range(tw, 1, MAX_TILE_SIDE) || !in_range(th, 1, MAX_TILE_SIDE)) {
        wgf_log_warn("wgf_tilemap: %s: its size or tile size is out of range", path);
        return false;
    }
    d->columns = (int)columns, d->rows = (int)rows, d->tile_width = (int)tw, d->tile_height = (int)th;
    d->background = parse_color(wgf_core_priv_json_get_string(json, "backgroundcolor"));
    d->file = file_name(path);
    if (d->file == NULL || !read_properties(wgf_core_priv_json_get(json, "properties"), &d->properties)) return false;
    if (wgf_core_priv_json_size(tilesets) > 0) {
        d->tilesets = (wgf_gfx_priv_tilemap_tileset_t *)calloc((size_t)wgf_core_priv_json_size(tilesets),
                                                               sizeof(*d->tilesets));
        if (d->tilesets == NULL) return false;
    }
    for (i = 0; i < wgf_core_priv_json_size(tilesets); i++) {
        const wgf_core_priv_json_t *entry = wgf_core_priv_json_at(tilesets, i);
        const char *source = wgf_core_priv_json_get_string(entry, "source");
        const double first = wgf_core_priv_json_get_number(entry, "firstgid", 0);
        wgf_gfx_priv_tilemap_tileset_t *t = &d->tilesets[d->tileset_count++];
        bool ok;
        if (!in_range(first, 1, WGF_GFX_PRIV_TILEMAP_GID_MASK)) {
            wgf_log_warn("wgf_tilemap: %s: a tileset's first gid is out of range", path);
            return false;
        }
        if (source[0] == '\0') {
            ok = read_tileset(entry, path, (int)first, path, t);
        } else {
            char file[WGF_CORE_PRIV_FS_PATH_MAX];
            unsigned char *bytes = NULL;
            int size = 0;
            wgf_core_priv_json_t *tileset;
            const size_t n = strlen(source);
            if (n > 4 && strcmp(source + n - 4, ".tsx") == 0) {
                wgf_log_warn("wgf_tilemap: %s names the tileset %s, which isn't read: save it as JSON (.tsj)", path,
                             source);
                return false;
            }
            if (!beside(path, source, file, sizeof(file)) || !wgf_core_priv_fs_read(file, &bytes, &size)) {
                wgf_log_warn("wgf_tilemap: %s: its tileset %s can't be read", path, source);
                return false;
            }
            tileset = wgf_core_priv_json_parse((const char *)bytes, (size_t)size);
            wgf_core_priv_fs_read_free(bytes);
            if (!wgf_core_priv_json_is_object(tileset)) {
                wgf_log_warn("wgf_tilemap: %s: its tileset %s isn't JSON", path, source);
                wgf_core_priv_json_free(tileset);
                return false;
            }
            ok = read_tileset(tileset, file, (int)first, path, t);
            wgf_core_priv_json_free(tileset);
        }
        if (!ok) return false;
    }
    if (d->tileset_count > 1) qsort(d->tilesets, (size_t)d->tileset_count, sizeof(*d->tilesets), by_first_gid);
    r.path = path;
    r.data = d;
    r.layer_capacity = 0;
    return read_layers(&r, wgf_core_priv_json_get(json, "layers"), 0.0f, 0.0f, 1.0f, true, 0);
}

/* What prepare hands finish: the map read, which finish takes. */
typedef struct prepared_t {
    wgf_gfx_priv_tilemap_data_t *data;
} prepared_t;

static void *prepare(const char *path)
{
    unsigned char *bytes = NULL;
    int size = 0;
    wgf_core_priv_json_t *json;
    wgf_gfx_priv_tilemap_data_t *d;
    prepared_t *p;
    if (!wgf_core_priv_fs_read(path, &bytes, &size)) return NULL;
    json = wgf_core_priv_json_parse((const char *)bytes, (size_t)size);
    wgf_core_priv_fs_read_free(bytes);
    if (!wgf_core_priv_json_is_object(json)) {
        wgf_log_warn("wgf_tilemap: %s: not JSON (a Tiled map, .tmj)", path);
        wgf_core_priv_json_free(json);
        return NULL;
    }
    d = (wgf_gfx_priv_tilemap_data_t *)calloc(1, sizeof(*d));
    if (d == NULL || !read_map(json, path, d)) {
        free_data(d);
        wgf_core_priv_json_free(json);
        return NULL;
    }
    wgf_core_priv_json_free(json);
    p = (prepared_t *)malloc(sizeof(*p));
    if (p == NULL) {
        free_data(d);
        return NULL;
    }
    p->data = d;
    return p;
}

static wgf_core_priv_load_step_t finish(void *prepared, wgf_handle_t resource)
{
    wgf_gfx_priv_tilemap_data_t *d = ((prepared_t *)prepared)->data;
    map_t *m = map_of(resource);
    int i;
    if (m == NULL || d == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    ((prepared_t *)prepared)->data = NULL; /* the map's now */
    for (i = 0; i < d->tileset_count; i++) { /* its images, loading; drawn once READY */
        wgf_gfx_priv_tilemap_tileset_t *t = &d->tilesets[i];
        t->texture = wgf_texture_create(t->image);
        wgf_texture_set_sampling(t->texture, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP,
                                 WGF_TEXTURE_FILTER_NEAREST);
    }
    m = map_of(resource); /* maps don't move as textures are made, but be sure */
    if (m == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    free_data(m->data);
    m->data = d;
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void discard(void *prepared)
{
    prepared_t *p = (prepared_t *)prepared;
    free_data(p->data); /* NULL once finish took it */
    free(p);
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"tilemap", prepare, finish, discard, fail, NULL, false, NULL};

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

static void free_map(wgf_handle_t map, void *record)
{
    map_t *m = (map_t *)record;
    (void)map;
    free_data(m->data);
    m->data = NULL;
}

static const wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_tilemap_create",
                                                            .loader = loader_of,
                                                            .free = free_map};

static void stop(void)
{
    if (tm.ready) {
        wgf_core_priv_resource_unregister(&tm.pool); /* each map freed */
        wgf_core_priv_handle_pool_destroy(&tm.pool);
    }
    memset(&tm, 0, sizeof(tm));
}

static wgf_core_priv_part_t part = {.name = "tilemaps",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_TILEMAPS,
                                    .stop = stop};

static bool ensure_started(void)
{
    if (tm.ready) return true;
    tm.ready = wgf_core_priv_handle_pool_init(&tm.pool, WGF_CORE_PRIV_HANDLE_KIND_TILEMAP, (void **)&tm.maps,
                                              sizeof(map_t), 8, 4096);
    if (!tm.ready) return false;
    wgf_core_priv_resource_register(&tm.pool, &resource_kind);
    if (!tm.installed) {
        wgf_core_priv_part_install(&part);
        tm.installed = true;
    }
    return true;
}

/* ---- the files a map names, for the web ---------------------------------------------- */

/* A `.tmj`'s tilesets in files of their own (required) and its embedded tilesets' images,
 * and a `.tsj`'s image (neither required: a missing image draws the checker). */
static void list_map_files(const unsigned char *data, int size, wgf_core_priv_load_add_fn add, void *context)
{
    wgf_core_priv_json_t *json = wgf_core_priv_json_parse((const char *)data, (size_t)size);
    const wgf_core_priv_json_t *tilesets = wgf_core_priv_json_get(json, "tilesets");
    int i;
    if (wgf_core_priv_json_get_string(json, "image")[0] != '\0') {
        add(wgf_core_priv_json_get_string(json, "image"), NULL, false, context);
    }
    for (i = 0; i < wgf_core_priv_json_size(tilesets); i++) {
        const wgf_core_priv_json_t *t = wgf_core_priv_json_at(tilesets, i);
        if (wgf_core_priv_json_get_string(t, "source")[0] != '\0') {
            add(wgf_core_priv_json_get_string(t, "source"), NULL, true, context);
        } else if (wgf_core_priv_json_get_string(t, "image")[0] != '\0') {
            add(wgf_core_priv_json_get_string(t, "image"), NULL, false, context);
        }
    }
    wgf_core_priv_json_free(json);
}

WGF_CORE_PRIV_ON_LINK(register_tilemap_listers)
{
    wgf_core_priv_load_set_lister(".tmj", list_map_files);
    wgf_core_priv_load_set_lister(".tsj", list_map_files);
}

/* ---- for the placed layers ----------------------------------------------------------- */

const wgf_gfx_priv_tilemap_data_t *wgf_gfx_priv_tilemap_data(wgf_tilemap_t map)
{
    return data_of(map);
}

int wgf_gfx_priv_tilemap_find_tileset(const wgf_gfx_priv_tilemap_data_t *data, uint32_t gid, int *tile)
{
    const int id = (int)(gid & WGF_GFX_PRIV_TILEMAP_GID_MASK);
    int i, found = -1;
    if (data == NULL || id == 0) return -1;
    for (i = 0; i < data->tileset_count && data->tilesets[i].first_gid <= id; i++) found = i;
    if (found < 0 || id - data->tilesets[found].first_gid >= data->tilesets[found].tile_count) return -1;
    if (tile != NULL) *tile = id - data->tilesets[found].first_gid;
    return found;
}

void wgf_gfx_priv_tilemap_retain(wgf_tilemap_t map)
{
    if (map_of(map) != NULL) wgf_core_priv_resource_retain(map);
}

void wgf_gfx_priv_tilemap_release(wgf_tilemap_t map)
{
    if (tm.ready && wgf_handle_is_alive(map)) wgf_resource_release(map);
}

/* ---- the calls ----------------------------------------------------------------------- */

wgf_tilemap_t wgf_tilemap_create(const char *path)
{
    if (!ensure_started()) return 0;
    return wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TILEMAP, path);
}

int wgf_tilemap_get_columns(wgf_tilemap_t map)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of(map);
    return d != NULL ? d->columns : 0;
}

int wgf_tilemap_get_rows(wgf_tilemap_t map)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of(map);
    return d != NULL ? d->rows : 0;
}

int wgf_tilemap_get_tile_width(wgf_tilemap_t map)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of(map);
    return d != NULL ? d->tile_width : 0;
}

int wgf_tilemap_get_tile_height(wgf_tilemap_t map)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of(map);
    return d != NULL ? d->tile_height : 0;
}

wgf_color_t wgf_tilemap_get_background_color(wgf_tilemap_t map)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of(map);
    return d != NULL ? d->background : 0;
}

static const wgf_gfx_priv_tilemap_layer_data_t *layer_at(const wgf_gfx_priv_tilemap_data_t *d, int layer)
{
    return d != NULL && layer >= 0 && layer < d->layer_count ? &d->layers[layer] : NULL;
}

int wgf_tilemap_get_layer_count(wgf_tilemap_t map)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of(map);
    return d != NULL ? d->layer_count : 0;
}

const char *wgf_tilemap_get_layer_name(wgf_tilemap_t map, int layer)
{
    const wgf_gfx_priv_tilemap_layer_data_t *l = layer_at(data_of(map), layer);
    return l != NULL ? l->name : "";
}

wgf_tilemap_layer_kind_t wgf_tilemap_get_layer_kind(wgf_tilemap_t map, int layer)
{
    const wgf_gfx_priv_tilemap_layer_data_t *l = layer_at(data_of(map), layer);
    return l != NULL ? (wgf_tilemap_layer_kind_t)l->kind : WGF_TILEMAP_LAYER_KIND_NONE;
}

int wgf_tilemap_find_layer(wgf_tilemap_t map, const char *name)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of(map);
    int i;
    for (i = 0; d != NULL && name != NULL && i < d->layer_count; i++) {
        if (strcmp(d->layers[i].name, name) == 0) return i;
    }
    return -1;
}

static const wgf_gfx_priv_tilemap_tileset_t *tileset_at(wgf_tilemap_t map, int tileset)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of_at(map, "wgf_tilemap_get_tileset");
    return d != NULL && tileset >= 0 && tileset < d->tileset_count ? &d->tilesets[tileset] : NULL;
}

int wgf_tilemap_get_tileset_count(wgf_tilemap_t map)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of(map);
    return d != NULL ? d->tileset_count : 0;
}

const char *wgf_tilemap_get_tileset_name(wgf_tilemap_t map, int tileset)
{
    const wgf_gfx_priv_tilemap_tileset_t *t = tileset_at(map, tileset);
    return t != NULL ? t->name : "";
}

int wgf_tilemap_get_tileset_first_gid(wgf_tilemap_t map, int tileset)
{
    const wgf_gfx_priv_tilemap_tileset_t *t = tileset_at(map, tileset);
    return t != NULL ? t->first_gid : 0;
}

int wgf_tilemap_get_tileset_tile_count(wgf_tilemap_t map, int tileset)
{
    const wgf_gfx_priv_tilemap_tileset_t *t = tileset_at(map, tileset);
    return t != NULL ? t->tile_count : 0;
}

wgf_texture_t wgf_tilemap_get_tileset_texture(wgf_tilemap_t map, int tileset)
{
    const wgf_gfx_priv_tilemap_tileset_t *t = tileset_at(map, tileset);
    return t != NULL ? t->texture : 0;
}

static const wgf_gfx_priv_tilemap_object_t *object_at(wgf_tilemap_t map, int layer, int object, const char *caller)
{
    const wgf_gfx_priv_tilemap_layer_data_t *l = layer_at(data_of_at(map, caller), layer);
    return l != NULL && object >= 0 && object < l->object_count ? &l->objects[object] : NULL;
}
#define object_of(map, layer, object) object_at((map), (layer), (object), WGF_CORE_PRIV_CALLER)

int wgf_tilemap_get_object_count(wgf_tilemap_t map, int layer)
{
    const wgf_gfx_priv_tilemap_layer_data_t *l = layer_at(data_of(map), layer);
    return l != NULL ? l->object_count : 0;
}

int wgf_tilemap_get_object_id(wgf_tilemap_t map, int layer, int object)
{
    const wgf_gfx_priv_tilemap_object_t *o = object_of(map, layer, object);
    return o != NULL ? o->id : 0;
}

const char *wgf_tilemap_get_object_name(wgf_tilemap_t map, int layer, int object)
{
    const wgf_gfx_priv_tilemap_object_t *o = object_of(map, layer, object);
    return o != NULL ? o->name : "";
}

const char *wgf_tilemap_get_object_class(wgf_tilemap_t map, int layer, int object)
{
    const wgf_gfx_priv_tilemap_object_t *o = object_of(map, layer, object);
    return o != NULL ? o->class_name : "";
}

wgf_tilemap_shape_t wgf_tilemap_get_object_shape(wgf_tilemap_t map, int layer, int object)
{
    const wgf_gfx_priv_tilemap_object_t *o = object_of(map, layer, object);
    return o != NULL ? (wgf_tilemap_shape_t)o->shape : WGF_TILEMAP_SHAPE_NONE;
}

wgf_vec2_t wgf_tilemap_get_object_position(wgf_tilemap_t map, int layer, int object)
{
    const wgf_gfx_priv_tilemap_object_t *o = object_of(map, layer, object);
    return o != NULL ? wgf_vec2_make(o->x, o->y) : wgf_vec2_make(0.0f, 0.0f);
}

wgf_vec2_t wgf_tilemap_get_object_size(wgf_tilemap_t map, int layer, int object)
{
    const wgf_gfx_priv_tilemap_object_t *o = object_of(map, layer, object);
    return o != NULL ? wgf_vec2_make(o->width, o->height) : wgf_vec2_make(0.0f, 0.0f);
}

float wgf_tilemap_get_object_rotation(wgf_tilemap_t map, int layer, int object)
{
    const wgf_gfx_priv_tilemap_object_t *o = object_of(map, layer, object);
    return o != NULL ? o->rotation : 0.0f;
}

bool wgf_tilemap_is_object_visible(wgf_tilemap_t map, int layer, int object)
{
    const wgf_gfx_priv_tilemap_object_t *o = object_of(map, layer, object);
    return o != NULL && o->visible;
}

int wgf_tilemap_get_object_tile(wgf_tilemap_t map, int layer, int object)
{
    const wgf_gfx_priv_tilemap_object_t *o = object_of(map, layer, object);
    return o != NULL ? o->gid : 0;
}

int wgf_tilemap_get_object_points(wgf_tilemap_t map, int layer, int object, float *out, int count)
{
    const wgf_gfx_priv_tilemap_object_t *o = object_of(map, layer, object);
    int n;
    if (o == NULL || out == NULL || count <= 0) return 0;
    n = o->point_count * 2 < count ? o->point_count * 2 : count;
    if (n > 0) memcpy(out, o->points, sizeof(float) * (size_t)n);
    return n;
}

int wgf_tilemap_get_object_point_count(wgf_tilemap_t map, int layer, int object)
{
    const wgf_gfx_priv_tilemap_object_t *o = object_of(map, layer, object);
    return o != NULL ? o->point_count : 0;
}

/* The properties of the map (`layer` -1), a layer (`object` -1), or an object; NULL for none. */
static const wgf_gfx_priv_tilemap_properties_t *properties_at(wgf_tilemap_t map, int layer, int object,
                                                              const char *caller)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of_at(map, caller);
    const wgf_gfx_priv_tilemap_layer_data_t *l;
    if (d == NULL) return NULL;
    if (layer == -1) return object == -1 ? &d->properties : NULL;
    l = layer_at(d, layer);
    if (l == NULL) return NULL;
    if (object == -1) return &l->properties;
    return object >= 0 && object < l->object_count ? &l->objects[object].properties : NULL;
}

static const wgf_gfx_priv_tilemap_properties_t *tile_properties_at(wgf_tilemap_t map, int gid, const char *caller)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of_at(map, caller);
    int tile = 0;
    const int t = gid > 0 ? wgf_gfx_priv_tilemap_find_tileset(d, (uint32_t)gid, &tile) : -1;
    return t >= 0 && d->tilesets[t].tiles[tile] != NULL ? &d->tilesets[t].tiles[tile]->properties : NULL;
}

static const wgf_gfx_priv_tilemap_property_t *property_named(const wgf_gfx_priv_tilemap_properties_t *p,
                                                             const char *name)
{
    int i;
    for (i = 0; p != NULL && name != NULL && i < p->count; i++) {
        if (strcmp(p->items[i].name, name) == 0) return &p->items[i];
    }
    return NULL;
}

int wgf_tilemap_get_property_count(wgf_tilemap_t map, int layer, int object)
{
    const wgf_gfx_priv_tilemap_properties_t *p = properties_at(map, layer, object, WGF_CORE_PRIV_CALLER);
    return p != NULL ? p->count : 0;
}

const char *wgf_tilemap_get_property_name(wgf_tilemap_t map, int layer, int object, int index)
{
    const wgf_gfx_priv_tilemap_properties_t *p = properties_at(map, layer, object, WGF_CORE_PRIV_CALLER);
    return p != NULL && index >= 0 && index < p->count ? p->items[index].name : "";
}

wgf_tilemap_property_type_t wgf_tilemap_get_property_type(wgf_tilemap_t map, int layer, int object, const char *name)
{
    const wgf_gfx_priv_tilemap_property_t *p =
        property_named(properties_at(map, layer, object, WGF_CORE_PRIV_CALLER), name);
    return p != NULL ? (wgf_tilemap_property_type_t)p->type : WGF_TILEMAP_PROPERTY_TYPE_NONE;
}

const char *wgf_tilemap_get_property(wgf_tilemap_t map, int layer, int object, const char *name)
{
    const wgf_gfx_priv_tilemap_property_t *p =
        property_named(properties_at(map, layer, object, WGF_CORE_PRIV_CALLER), name);
    return p != NULL ? p->text : "";
}

double wgf_tilemap_get_property_number(wgf_tilemap_t map, int layer, int object, const char *name)
{
    const wgf_gfx_priv_tilemap_property_t *p =
        property_named(properties_at(map, layer, object, WGF_CORE_PRIV_CALLER), name);
    return p != NULL ? p->number : 0.0;
}

const char *wgf_tilemap_get_tile_class(wgf_tilemap_t map, int gid)
{
    const wgf_gfx_priv_tilemap_data_t *d = data_of(map);
    int tile = 0;
    const int t = gid > 0 ? wgf_gfx_priv_tilemap_find_tileset(d, (uint32_t)gid, &tile) : -1;
    const wgf_gfx_priv_tilemap_tile_t *found = t >= 0 ? d->tilesets[t].tiles[tile] : NULL;
    return found != NULL && found->class_name != NULL ? found->class_name : "";
}

int wgf_tilemap_get_tile_property_count(wgf_tilemap_t map, int gid)
{
    const wgf_gfx_priv_tilemap_properties_t *p = tile_properties_at(map, gid, WGF_CORE_PRIV_CALLER);
    return p != NULL ? p->count : 0;
}

const char *wgf_tilemap_get_tile_property_name(wgf_tilemap_t map, int gid, int index)
{
    const wgf_gfx_priv_tilemap_properties_t *p = tile_properties_at(map, gid, WGF_CORE_PRIV_CALLER);
    return p != NULL && index >= 0 && index < p->count ? p->items[index].name : "";
}

wgf_tilemap_property_type_t wgf_tilemap_get_tile_property_type(wgf_tilemap_t map, int gid, const char *name)
{
    const wgf_gfx_priv_tilemap_property_t *p = property_named(tile_properties_at(map, gid, WGF_CORE_PRIV_CALLER), name);
    return p != NULL ? (wgf_tilemap_property_type_t)p->type : WGF_TILEMAP_PROPERTY_TYPE_NONE;
}

const char *wgf_tilemap_get_tile_property(wgf_tilemap_t map, int gid, const char *name)
{
    const wgf_gfx_priv_tilemap_property_t *p = property_named(tile_properties_at(map, gid, WGF_CORE_PRIV_CALLER), name);
    return p != NULL ? p->text : "";
}

double wgf_tilemap_get_tile_property_number(wgf_tilemap_t map, int gid, const char *name)
{
    const wgf_gfx_priv_tilemap_property_t *p = property_named(tile_properties_at(map, gid, WGF_CORE_PRIV_CALLER), name);
    return p != NULL ? p->number : 0.0;
}
