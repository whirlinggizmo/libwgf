#include "wgf_scene.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node/wgf_gfx_node_priv.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */
#include "wgf_behavior.h"
#include "wgf_bounds.h"
#include "wgf_collider.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_ecs_priv.h"
#include "wgf_ecs_scene_priv.h"
#include "wgf_emitter2d.h"
#include "wgf_font.h"
#include "wgf_lifetime.h"
#include "wgf_log.h"
#include "wgf_motion.h"
#include "wgf_shape2d.h"
#include "wgf_sound.h"
#include "wgf_sprite.h"
#include "wgf_text.h"
#include "wgf_texture.h"
#include "wgf_voice.h"

/* Scenes (wgf_scene.h): a resource whose file is read and parsed whole on a worker, every
 * line checked against the table of components and keys below, kept as each line's
 * settings in their text; making an entity applies its lines in order, a prefab's first
 * when it starts as one. The format's values are parsed by the same functions when the
 * file is read and when its lines are applied, so what was checked is what is used. */

#define FILE_MAX (4 << 20)
#define LINE_MAX_BYTES 4096
#define WORDS_MAX 128
#define POINTS_MAX 2048 /* floats in a polygon: 1024 points, as wgf_shape2d_set_polygon takes */

/* ---- the format's table --------------------------------------------------------------- */

enum {
    V_NUM,    /* one number */
    V_NUM2,   /* two, a list */
    V_NUM3,
    V_NUM4,
    V_POINTS, /* an even list of 6 to POINTS_MAX */
    V_INT,
    V_BOOL,
    V_COLOR,
    V_COLOR2,
    V_TEXT,   /* a word or quoted text */
    V_MODE,   /* wrap, clamp, destroy */
    V_ALIGN   /* left|center|right,top|middle|bottom */
};

typedef struct key_t {
    const char *key;
    int value;
} key_t;

typedef struct kind_t {
    const char *name;
    int component; /* wgf_component_t; 0 for the transform */
    key_t keys[16];
    bool open; /* any key: a behavior's parameters */
} kind_t;

static const kind_t kinds[] = {
    {"transform", 0, {{"position", V_NUM3}, {"rotation", V_NUM3}, {"scale", V_NUM3}}, false},
    {"motion",
     WGF_COMPONENT_MOTION,
     {{"velocity", V_NUM3}, {"spin", V_NUM3}, {"damping", V_NUM}, {"max_speed", V_NUM}},
     false},
    {"bounds",
     WGF_COMPONENT_BOUNDS,
     {{"rect", V_NUM4}, {"mode", V_MODE}, {"margin", V_NUM}, {"visible", V_BOOL}},
     false},
    {"lifetime", WGF_COMPONENT_LIFETIME, {{"seconds", V_NUM}}, false},
    {"collider", WGF_COMPONENT_COLLIDER, {{"radius", V_NUM}, {"layer", V_INT}, {"mask", V_INT}, {"enabled", V_BOOL}}, false},
    {"behavior", WGF_COMPONENT_BEHAVIOR, {{"name", V_TEXT}}, true},
    {"shape2d",
     WGF_COMPONENT_SHAPE2D,
     {{"rectangle", V_NUM2},
      {"circle", V_NUM},
      {"line", V_NUM4},
      {"polygon", V_POINTS},
      {"outline", V_NUM},
      {"color", V_COLOR},
      {"pivot", V_NUM2}},
     false},
    {"sprite",
     WGF_COMPONENT_SPRITE,
     {{"texture", V_TEXT}, {"source", V_NUM4}, {"size", V_NUM2}, {"pivot", V_NUM2}, {"tint", V_COLOR}},
     false},
    {"text",
     WGF_COMPONENT_TEXT,
     {{"string", V_TEXT}, {"font", V_TEXT}, {"size", V_NUM}, {"color", V_COLOR}, {"wrap", V_NUM}, {"align", V_ALIGN}},
     false},
    {"emitter2d",
     WGF_COMPONENT_EMITTER2D,
     {{"rate", V_NUM},
      {"emitting", V_BOOL},
      {"capacity", V_INT},
      {"life", V_NUM2},
      {"direction", V_NUM},
      {"spread", V_NUM},
      {"speed", V_NUM2},
      {"radius", V_NUM},
      {"gravity", V_NUM2},
      {"drag", V_NUM},
      {"size", V_NUM2},
      {"color", V_COLOR2},
      {"stretch", V_NUM},
      {"burst", V_INT}},
     false},
    {"voice",
     WGF_COMPONENT_VOICE,
     {{"sound", V_TEXT},
      {"streamed", V_BOOL},
      {"volume", V_NUM},
      {"pitch", V_NUM},
      {"pan", V_NUM},
      {"loop", V_BOOL},
      {"play", V_BOOL}},
     false},
};
#define KIND_COUNT ((int)(sizeof(kinds) / sizeof(kinds[0])))

/* ---- values ------------------------------------------------------------------------- */

typedef struct value_t {
    double n[POINTS_MAX];
    int count; /* numbers in n */
    wgf_color_t colors[2];
    bool truth;
    char text[LINE_MAX_BYTES];
} value_t;

static bool parse_number(const char *text, size_t length, double *out)
{
    char buffer[64], *end;
    if (length == 0 || length >= sizeof(buffer)) return false;
    memcpy(buffer, text, length);
    buffer[length] = '\0';
    errno = 0;
    *out = strtod(buffer, &end);
    return errno == 0 && *end == '\0' && isfinite(*out);
}

static bool parse_list(const char *text, value_t *v, int least, int most)
{
    const char *p = text;
    v->count = 0;
    for (;;) {
        const char *comma = strchr(p, ',');
        const size_t length = comma != NULL ? (size_t)(comma - p) : strlen(p);
        if (v->count == most || !parse_number(p, length, &v->n[v->count])) return false;
        v->count++;
        if (comma == NULL) break;
        p = comma + 1;
    }
    return v->count >= least;
}

static bool parse_color(const char *text, size_t length, wgf_color_t *out)
{
    unsigned value = 0;
    size_t i;
    if ((length != 7 && length != 9) || text[0] != '#') return false;
    for (i = 1; i < length; i++) {
        const char c = text[i];
        const int digit = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                                                         (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
        if (digit < 0) return false;
        value = value * 16u + (unsigned)digit;
    }
    *out = length == 7 ? (value << 8) | 0xFFu : value;
    return true;
}

/* A value's text (already unquoted) as `type` says: false when it isn't one. */
static bool parse_value(int type, const char *text, value_t *v)
{
    switch (type) {
        case V_NUM: return parse_list(text, v, 1, 1);
        case V_NUM2: return parse_list(text, v, 2, 2);
        case V_NUM3: return parse_list(text, v, 3, 3);
        case V_NUM4: return parse_list(text, v, 4, 4);
        case V_POINTS: return parse_list(text, v, 6, POINTS_MAX) && v->count % 2 == 0;
        case V_INT: {
            char *end;
            long value;
            errno = 0;
            value = strtol(text, &end, 0);
            v->n[0] = (double)value;
            v->count = 1;
            return errno == 0 && end != text && *end == '\0' && value >= -2147483647L - 1 && value <= 2147483647L;
        }
        case V_BOOL:
            v->truth = strcmp(text, "true") == 0;
            return v->truth || strcmp(text, "false") == 0;
        case V_COLOR: return parse_color(text, strlen(text), &v->colors[0]);
        case V_COLOR2: {
            const char *comma = strchr(text, ',');
            return comma != NULL && parse_color(text, (size_t)(comma - text), &v->colors[0]) &&
                   parse_color(comma + 1, strlen(comma + 1), &v->colors[1]);
        }
        case V_MODE:
            v->n[0] = strcmp(text, "wrap") == 0 ? WGF_BOUNDS_MODE_WRAP
                                                 : (strcmp(text, "clamp") == 0 ? WGF_BOUNDS_MODE_CLAMP
                                                                               : (strcmp(text, "destroy") == 0
                                                                                      ? WGF_BOUNDS_MODE_DESTROY
                                                                                      : -1));
            return v->n[0] >= 0;
        case V_ALIGN: {
            static const char *const across[] = {"left", "center", "right"}, *const down[] = {"top", "middle", "bottom"};
            const char *comma = strchr(text, ',');
            int i, a = -1, d = -1;
            if (comma == NULL) return false;
            for (i = 0; i < 3; i++) {
                if (strlen(across[i]) == (size_t)(comma - text) && strncmp(text, across[i], (size_t)(comma - text)) == 0) a = i;
                if (strcmp(comma + 1, down[i]) == 0) d = i;
            }
            v->n[0] = a;
            v->n[1] = d;
            return a >= 0 && d >= 0;
        }
        default: /* V_TEXT */
            snprintf(v->text, sizeof(v->text), "%s", text);
            return true;
    }
}

/* ---- the parsed file ---------------------------------------------------------------- */

typedef struct setting_t {
    char key[WGF_ECS_PRIV_NAME_MAX];
    char *value; /* its text, unquoted; malloc'd */
} setting_t;

typedef struct line_t {
    int kind; /* into kinds[] */
    int number;
    setting_t *settings;
    int count;
} line_t;

typedef struct thing_t {
    char name[WGF_ECS_PRIV_NAME_MAX];
    bool prefab;
    int from; /* the prefab it starts as, into things; -1 none */
    line_t *lines;
    int count, capacity;
} thing_t;

struct wgf_ecs_priv_scene_data_t {
    thing_t *things;
    int count, capacity;
    int entities, prefabs;
};

void wgf_ecs_priv_scene_data_free(wgf_ecs_priv_scene_data_t *data)
{
    int i, j, k;
    if (data == NULL) return;
    for (i = 0; i < data->count; i++) {
        for (j = 0; j < data->things[i].count; j++) {
            for (k = 0; k < data->things[i].lines[j].count; k++) free(data->things[i].lines[j].settings[k].value);
            free(data->things[i].lines[j].settings);
        }
        free(data->things[i].lines);
    }
    free(data->things);
    free(data);
}

typedef struct parser_t {
    const char *path;
    int line;
    bool failed;
    value_t *scratch; /* the parse's own: scenes parse on several workers at once natively */
} parser_t;

static void fail(parser_t *parser, const char *what, const char *word)
{
    wgf_log_error("wgf_scene: %s:%d: %s%s%s%s", parser->path, parser->line, what, word != NULL ? " (\"" : "",
                  word != NULL ? word : "", word != NULL ? "\")" : "");
    parser->failed = true;
}

/* The line's words into `words`: split at blanks, a double-quoted run (with \" and \\)
 * one word, unquoted in place; a key=value word's value may be quoted. NULL-terminated.
 * False for an unclosed quote or too many words. */
static bool split(char *line, char **words, int most)
{
    char *p = line;
    int n = 0;
    for (;;) {
        char *out;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        if (n == most - 1) return false;
        words[n++] = out = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            if (*p == '"') { /* a quoted run, unquoted where it is */
                p++;
                while (*p != '"') {
                    if (*p == '\0') return false;
                    if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) p++;
                    *out++ = *p++;
                }
                p++;
            } else {
                *out++ = *p++;
            }
        }
        if (*p != '\0') p++;
        *out = '\0';
    }
    words[n] = NULL;
    return true;
}

static int find_kind(const char *name)
{
    int i;
    for (i = 0; i < KIND_COUNT; i++) {
        if (strcmp(kinds[i].name, name) == 0) return i;
    }
    return -1;
}

static int key_type(int kind, const char *key)
{
    int i;
    for (i = 0; i < 16 && kinds[kind].keys[i].key != NULL; i++) {
        if (strcmp(kinds[kind].keys[i].key, key) == 0) return kinds[kind].keys[i].value;
    }
    return kinds[kind].open ? V_TEXT : -1; /* a behavior's parameter: any text */
}

static int find_prefab(const wgf_ecs_priv_scene_data_t *data, const char *name)
{
    int i;
    for (i = 0; i < data->count; i++) {
        if (data->things[i].prefab && strcmp(data->things[i].name, name) == 0) return i;
    }
    return -1;
}

static bool add_line(parser_t *parser, thing_t *thing, int kind, char **words)
{
    line_t *line;
    int i, n = 0;
    for (n = 0; words[n] != NULL; n++) {
    }
    if (thing->count == thing->capacity) {
        const int capacity = thing->capacity > 0 ? thing->capacity * 2 : 8;
        line_t *grown = (line_t *)realloc(thing->lines, sizeof(line_t) * (size_t)capacity);
        if (grown == NULL) return false;
        thing->lines = grown;
        thing->capacity = capacity;
    }
    line = &thing->lines[thing->count];
    line->kind = kind;
    line->number = parser->line;
    line->count = 0;
    line->settings = n > 0 ? (setting_t *)calloc((size_t)n, sizeof(setting_t)) : NULL;
    if (n > 0 && line->settings == NULL) return false;
    thing->count++;
    for (i = 0; i < n; i++) {
        char *eq = strchr(words[i], '=');
        int type;
        if (eq == NULL || eq == words[i]) {
            fail(parser, "a setting is key=value", words[i]);
            return true;
        }
        *eq = '\0';
        type = key_type(kind, words[i]);
        if (type < 0) {
            fail(parser, "no such key for this component", words[i]);
            return true;
        }
        if ((size_t)(eq - words[i]) >= WGF_ECS_PRIV_NAME_MAX || strlen(eq + 1) >= WGF_ECS_PRIV_VALUE_MAX * 16) {
            fail(parser, "a key or value too long", words[i]);
            return true;
        }
        if (!parse_value(type, eq + 1, parser->scratch)) {
            fail(parser, "a value of the wrong shape for its key", words[i]);
            return true;
        }
        snprintf(line->settings[i].key, sizeof(line->settings[i].key), "%s", words[i]);
        line->settings[i].value = (char *)malloc(strlen(eq + 1) + 1);
        if (line->settings[i].value == NULL) return false;
        memcpy(line->settings[i].value, eq + 1, strlen(eq + 1) + 1);
        line->count++;
    }
    return true;
}

wgf_ecs_priv_scene_data_t *wgf_ecs_priv_scene_parse(const char *text, size_t size, const char *path)
{
    parser_t parser = {path, 0, false, NULL};
    wgf_ecs_priv_scene_data_t *data = (wgf_ecs_priv_scene_data_t *)calloc(1, sizeof(*data));
    const char *p = text, *end = text + size;
    thing_t *open = NULL;
    bool header = false;
    parser.scratch = (value_t *)malloc(sizeof(value_t));
    if (data == NULL || parser.scratch == NULL) {
        free(data);
        free(parser.scratch);
        wgf_log_error("wgf_scene: %s: out of memory", path);
        return NULL;
    }
    if (size > FILE_MAX) {
        wgf_log_error("wgf_scene: %s: larger than 4 MB", path);
        wgf_ecs_priv_scene_data_free(data);
        free(parser.scratch);
        return NULL;
    }
    while (p < end && !parser.failed) {
        char line[LINE_MAX_BYTES], *words[WORDS_MAX], *hash;
        const char *newline = (const char *)memchr(p, '\n', (size_t)(end - p));
        size_t n = newline != NULL ? (size_t)(newline - p) : (size_t)(end - p), i;
        parser.line++;
        if (n >= sizeof(line)) {
            fail(&parser, "a line longer than 4096 bytes", NULL);
            break;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        p += n + (newline != NULL ? 1 : 0);
        if (n > 0 && line[n - 1] == '\r') line[--n] = '\0';
        for (i = 0; i < n; i++) {
            if ((unsigned char)line[i] < 0x20 && line[i] != '\t') {
                fail(&parser, "a control character", NULL);
                break;
            }
        }
        if (parser.failed) break;
        /* a comment: from a # outside quotes */
        {
            bool quoted = false;
            for (hash = line; *hash != '\0'; hash++) {
                if (*hash == '"' && (hash == line || hash[-1] != '\\')) quoted = !quoted;
                if (*hash == '#' && !quoted) {
                    /* a color's # follows = or , : it is a value, not a comment */
                    if (hash > line && (hash[-1] == '=' || hash[-1] == ',')) continue;
                    *hash = '\0';
                    break;
                }
            }
        }
        if (!split(line, words, WORDS_MAX)) {
            fail(&parser, "an unclosed quote, or more than 127 words", NULL);
            break;
        }
        if (words[0] == NULL) continue;
        if (!header) {
            if (strcmp(words[0], "wgf-scene") != 0 || words[1] == NULL || strcmp(words[1], "1") != 0 ||
                words[2] != NULL) {
                fail(&parser, "the first line is `wgf-scene 1`", words[0]);
                break;
            }
            header = true;
            continue;
        }
        if (strcmp(words[0], "prefab") == 0 || strcmp(words[0], "entity") == 0) {
            const bool prefab = words[0][0] == 'p';
            thing_t *thing;
            if (open != NULL) {
                fail(&parser, "an entity or prefab inside another: its `end` is missing", words[0]);
                break;
            }
            if ((prefab && words[1] == NULL) || (words[1] != NULL && words[2] != NULL) ||
                (words[1] != NULL && strlen(words[1]) >= WGF_ECS_PRIV_NAME_MAX)) {
                fail(&parser, prefab ? "prefab takes a name, under 64 bytes" : "entity takes a name or none", NULL);
                break;
            }
            if (prefab && find_prefab(data, words[1]) >= 0) {
                fail(&parser, "a second prefab of that name", words[1]);
                break;
            }
            if (data->count == data->capacity) {
                const int capacity = data->capacity > 0 ? data->capacity * 2 : 16;
                thing_t *grown = (thing_t *)realloc(data->things, sizeof(thing_t) * (size_t)capacity);
                if (grown == NULL) break;
                data->things = grown;
                data->capacity = capacity;
            }
            thing = open = &data->things[data->count++];
            memset(thing, 0, sizeof(*thing));
            thing->prefab = prefab;
            thing->from = -1;
            if (words[1] != NULL) snprintf(thing->name, sizeof(thing->name), "%s", words[1]);
            if (prefab) data->prefabs++;
            else data->entities++;
        } else if (strcmp(words[0], "end") == 0) {
            if (open == NULL || words[1] != NULL) {
                fail(&parser, open == NULL ? "an `end` with nothing to end" : "`end` takes nothing", NULL);
                break;
            }
            open = NULL;
        } else if (strcmp(words[0], "from") == 0) {
            const int prefab = open != NULL && words[1] != NULL ? find_prefab(data, words[1]) : -1;
            if (open == NULL || open->count > 0 || open->from >= 0 || words[1] == NULL || words[2] != NULL) {
                fail(&parser, "`from <prefab>` comes first in an entity or prefab", NULL);
                break;
            }
            if (prefab < 0) {
                fail(&parser, "no prefab of that name above it", words[1]);
                break;
            }
            open->from = prefab;
        } else {
            const int kind = find_kind(words[0]);
            if (open == NULL) {
                fail(&parser, "a line is `prefab`, `entity`, `end`, or a component inside one", words[0]);
                break;
            }
            if (kind < 0) {
                fail(&parser, "no such component", words[0]);
                break;
            }
            if (!add_line(&parser, open, kind, words + 1)) {
                wgf_log_error("wgf_scene: %s: out of memory", path);
                parser.failed = true;
            }
        }
    }
    if (!parser.failed && !header) fail(&parser, "empty: the first line is `wgf-scene 1`", NULL);
    if (!parser.failed && open != NULL) fail(&parser, "the file ended inside an entity or prefab: its `end` is missing", NULL);
    free(parser.scratch);
    if (parser.failed) {
        wgf_ecs_priv_scene_data_free(data);
        return NULL;
    }
    return data;
}

/* ---- applying a line -------------------------------------------------------------- */

typedef struct after_t { /* what happens once the entity is placed: a burst, a voice played */
    int burst;
    bool play;
} after_t;

static float f(const value_t *v, int i)
{
    return (float)v->n[i];
}

static void apply_setting(wgf_entity_t e, int kind, const setting_t *s, value_t *v, after_t *after)
{
    const char *key = s->key;
    const wgf_node_t node = kinds[kind].component >= WGF_COMPONENT_SHAPE2D && kinds[kind].component <= WGF_COMPONENT_EMITTER2D
                                ? wgf_entity_get_component_node(e, (wgf_component_t)kinds[kind].component)
                                : 0;
    const int type = key_type(kind, key);
    if (!parse_value(type, s->value, v)) return; /* checked as the file was read */
    switch (kinds[kind].component) {
        case 0:
            if (strcmp(key, "position") == 0) wgf_entity_set_position(e, f(v, 0), f(v, 1), f(v, 2));
            else if (strcmp(key, "rotation") == 0) wgf_entity_set_rotation(e, f(v, 0), f(v, 1), f(v, 2));
            else wgf_entity_set_scale(e, f(v, 0), f(v, 1), f(v, 2));
            break;
        case WGF_COMPONENT_MOTION:
            if (strcmp(key, "velocity") == 0) wgf_motion_set_velocity(e, f(v, 0), f(v, 1), f(v, 2));
            else if (strcmp(key, "spin") == 0) wgf_motion_set_spin(e, f(v, 0), f(v, 1), f(v, 2));
            else if (strcmp(key, "damping") == 0) wgf_motion_set_damping(e, f(v, 0));
            else wgf_motion_set_max_speed(e, f(v, 0));
            break;
        case WGF_COMPONENT_BOUNDS:
            if (strcmp(key, "rect") == 0) wgf_bounds_set_rect(e, f(v, 0), f(v, 1), f(v, 2), f(v, 3));
            else if (strcmp(key, "mode") == 0) wgf_bounds_set_mode(e, (wgf_bounds_mode_t)(int)v->n[0]);
            else if (strcmp(key, "visible") == 0) wgf_bounds_set_visible(e, v->truth);
            else wgf_bounds_set_margin(e, f(v, 0));
            break;
        case WGF_COMPONENT_LIFETIME: wgf_lifetime_set_seconds(e, f(v, 0)); break;
        case WGF_COMPONENT_COLLIDER:
            if (strcmp(key, "radius") == 0) wgf_collider_set_radius(e, f(v, 0));
            else if (strcmp(key, "layer") == 0) wgf_collider_set_layer(e, (int)v->n[0]);
            else if (strcmp(key, "enabled") == 0) wgf_collider_set_enabled(e, v->truth);
            else wgf_collider_set_mask(e, (int)v->n[0]);
            break;
        case WGF_COMPONENT_BEHAVIOR:
            if (strcmp(key, "name") == 0) wgf_behavior_set_name(e, v->text);
            else wgf_behavior_set_param(e, key, v->text);
            break;
        case WGF_COMPONENT_SHAPE2D:
            if (strcmp(key, "rectangle") == 0) wgf_shape2d_set_rectangle(node, f(v, 0), f(v, 1));
            else if (strcmp(key, "circle") == 0) wgf_shape2d_set_circle(node, f(v, 0));
            else if (strcmp(key, "line") == 0) wgf_shape2d_set_line(node, f(v, 0), f(v, 1), f(v, 2), f(v, 3));
            else if (strcmp(key, "polygon") == 0) {
                float points[POINTS_MAX];
                int i;
                for (i = 0; i < v->count; i++) points[i] = f(v, i);
                wgf_shape2d_set_polygon(node, points, v->count);
            } else if (strcmp(key, "outline") == 0) wgf_shape2d_set_outline(node, f(v, 0));
            else if (strcmp(key, "color") == 0) wgf_shape2d_set_color(node, v->colors[0]);
            else wgf_shape2d_set_pivot(node, f(v, 0), f(v, 1));
            break;
        case WGF_COMPONENT_SPRITE:
            if (strcmp(key, "texture") == 0) {
                const wgf_texture_t texture = wgf_texture_create(v->text);
                wgf_sprite_set_texture(node, texture);
                wgf_resource_release(texture); /* the sprite holds its own */
            } else if (strcmp(key, "source") == 0) wgf_sprite_set_source(node, f(v, 0), f(v, 1), f(v, 2), f(v, 3));
            else if (strcmp(key, "size") == 0) wgf_sprite_set_size(node, f(v, 0), f(v, 1));
            else if (strcmp(key, "pivot") == 0) wgf_sprite_set_pivot(node, f(v, 0), f(v, 1));
            else wgf_sprite_set_tint(node, v->colors[0]);
            break;
        case WGF_COMPONENT_TEXT:
            if (strcmp(key, "string") == 0) wgf_text_set_string(node, v->text);
            else if (strcmp(key, "font") == 0) {
                const wgf_font_t font = v->text[0] != '\0' ? wgf_font_create(v->text) : 0;
                wgf_text_set_font(node, font);
                if (font != 0) wgf_resource_release(font);
            } else if (strcmp(key, "size") == 0) wgf_text_set_font_size(node, f(v, 0));
            else if (strcmp(key, "color") == 0) wgf_text_set_color(node, v->colors[0]);
            else if (strcmp(key, "wrap") == 0) wgf_text_set_wrap_width(node, f(v, 0));
            else wgf_text_set_align(node, (wgf_text_halign_t)(int)v->n[0], (wgf_text_valign_t)(int)v->n[1]);
            break;
        case WGF_COMPONENT_EMITTER2D:
            if (strcmp(key, "rate") == 0) wgf_emitter2d_set_rate(node, f(v, 0));
            else if (strcmp(key, "emitting") == 0) wgf_emitter2d_set_emitting(node, v->truth);
            else if (strcmp(key, "capacity") == 0) wgf_emitter2d_set_capacity(node, (int)v->n[0]);
            else if (strcmp(key, "life") == 0) wgf_emitter2d_set_life(node, f(v, 0), f(v, 1));
            else if (strcmp(key, "direction") == 0) wgf_emitter2d_set_direction(node, f(v, 0), wgf_emitter2d_get_spread(node));
            else if (strcmp(key, "spread") == 0) wgf_emitter2d_set_direction(node, wgf_emitter2d_get_direction(node), f(v, 0));
            else if (strcmp(key, "speed") == 0) wgf_emitter2d_set_speed(node, f(v, 0), f(v, 1));
            else if (strcmp(key, "radius") == 0) wgf_emitter2d_set_radius(node, f(v, 0));
            else if (strcmp(key, "gravity") == 0) wgf_emitter2d_set_gravity(node, f(v, 0), f(v, 1));
            else if (strcmp(key, "drag") == 0) wgf_emitter2d_set_drag(node, f(v, 0));
            else if (strcmp(key, "size") == 0) wgf_emitter2d_set_size(node, f(v, 0), f(v, 1));
            else if (strcmp(key, "color") == 0) wgf_emitter2d_set_color(node, v->colors[0], v->colors[1]);
            else if (strcmp(key, "stretch") == 0) wgf_emitter2d_set_stretch(node, f(v, 0));
            else after->burst = (int)v->n[0];
            break;
        default: { /* the voice */
            const wgf_voice_t voice = wgf_entity_get_voice(e);
            if (strcmp(key, "sound") == 0 || strcmp(key, "streamed") == 0) {
                /* the sound once both are known: a streamed one is another kind */
                break;
            }
            if (strcmp(key, "volume") == 0) wgf_voice_set_volume(voice, f(v, 0));
            else if (strcmp(key, "pitch") == 0) wgf_voice_set_pitch(voice, f(v, 0));
            else if (strcmp(key, "pan") == 0) wgf_voice_set_pan(voice, f(v, 0));
            else if (strcmp(key, "loop") == 0) wgf_voice_set_loop(voice, v->truth);
            else after->play = v->truth;
            break;
        }
    }
}

/* A voice line's sound: its path and whether streamed, both read first. */
static void apply_sound(wgf_entity_t e, const line_t *line, value_t *v)
{
    const char *path = NULL;
    bool streamed = false;
    int i;
    for (i = 0; i < line->count; i++) {
        if (strcmp(line->settings[i].key, "sound") == 0) path = line->settings[i].value;
        if (strcmp(line->settings[i].key, "streamed") == 0 && parse_value(V_BOOL, line->settings[i].value, v)) {
            streamed = v->truth;
        }
    }
    if (path != NULL) {
        const wgf_sound_t sound = path[0] != '\0' ? (streamed ? wgf_sound_create_streamed(path) : wgf_sound_create(path)) : 0;
        wgf_voice_set_sound(wgf_entity_get_voice(e), sound);
        if (sound != 0) wgf_resource_release(sound);
    }
}

static void apply_thing(const wgf_ecs_priv_scene_data_t *data, const thing_t *thing, wgf_entity_t e, after_t *after,
                        int depth)
{
    static value_t v; /* the main thread's */
    int i, j;
    if (thing->from >= 0 && depth < 64) apply_thing(data, &data->things[thing->from], e, after, depth + 1);
    if (!thing->prefab && thing->name[0] != '\0') wgf_entity_set_name(e, thing->name);
    for (i = 0; i < thing->count; i++) {
        const line_t *line = &thing->lines[i];
        const int component = kinds[line->kind].component;
        if (component != 0) wgf_entity_add_component(e, (wgf_component_t)component);
        if (component == WGF_COMPONENT_VOICE) apply_sound(e, line, &v);
        for (j = 0; j < line->count; j++) apply_setting(e, line->kind, &line->settings[j], &v, after);
    }
}

/* The node placed where the entity is, its smoothing ended: so what is made with it (a
 * burst) starts there, not at the origin. */
static void place(wgf_entity_t e)
{
    const wgf_vec3_t p = wgf_entity_get_position(e), r = wgf_entity_get_rotation(e), s = wgf_entity_get_scale(e);
    wgf_entity_snap(e);
    wgf_node_set_transform(wgf_entity_get_node(e), p.x, p.y, p.z, r.x, r.y, r.z, s.x, s.y, s.z);
}

static wgf_entity_t make(const wgf_ecs_priv_scene_data_t *data, const thing_t *thing, wgf_node_t parent)
{
    after_t after = {0, false};
    const wgf_entity_t e = wgf_entity_create(parent);
    if (e == 0) return 0;
    apply_thing(data, thing, e, &after, 0);
    place(e);
    if (after.burst > 0) wgf_emitter2d_burst(wgf_entity_get_component_node(e, WGF_COMPONENT_EMITTER2D), after.burst);
    if (after.play) wgf_voice_play(wgf_entity_get_voice(e));
    return e;
}

/* ---- the resource ---------------------------------------------------------------- */

typedef struct scene_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's */
    wgf_ecs_priv_scene_data_t *data;
} scene_t;

static bool pool_ready;
static wgf_core_priv_handle_pool_t scene_pool;
static scene_t *scenes;
static wgf_core_priv_resource_kind_t resource_kind;

static scene_t *scene_of(wgf_scene_t scene)
{
    uint16_t index;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve(&scene_pool, scene, &index)) return NULL;
    return &scenes[index];
}

static void *prepare(const char *path)
{
    unsigned char *bytes;
    int size;
    wgf_ecs_priv_scene_data_t *data;
    if (!wgf_core_priv_fs_read(path, &bytes, &size)) {
        wgf_log_warn("wgf_scene: %s: couldn't be read", path);
        return NULL;
    }
    data = wgf_ecs_priv_scene_parse((const char *)bytes, (size_t)size, path);
    wgf_core_priv_fs_read_free(bytes);
    return data;
}

static wgf_core_priv_load_step_t finish(void *prepared, wgf_handle_t resource)
{
    scene_t *scene_ptr = scene_of(resource);
    if (scene_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    /* taken out of what core discards after this: the scene's now */
    scene_ptr->data = (wgf_ecs_priv_scene_data_t *)malloc(sizeof(wgf_ecs_priv_scene_data_t));
    if (scene_ptr->data == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    memcpy(scene_ptr->data, prepared, sizeof(wgf_ecs_priv_scene_data_t));
    memset(prepared, 0, sizeof(wgf_ecs_priv_scene_data_t));
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void discard(void *prepared)
{
    wgf_ecs_priv_scene_data_free((wgf_ecs_priv_scene_data_t *)prepared);
}

static void failed(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"scene", prepare, finish, discard, failed, NULL};

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

static void free_scene(wgf_handle_t scene, void *record)
{
    scene_t *scene_ptr = (scene_t *)record;
    (void)scene;
    wgf_ecs_priv_scene_data_free(scene_ptr->data);
    scene_ptr->data = NULL;
}

static wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_scene_create", .free = free_scene};

wgf_scene_t wgf_scene_create(const char *path)
{
    if (!wgf_ecs_priv_start()) return 0; /* the ecs's stop frees scenes too */
    if (!pool_ready) {
        pool_ready = wgf_core_priv_handle_pool_init(&scene_pool, WGF_CORE_PRIV_HANDLE_KIND_SCENE, (void **)&scenes,
                                                    sizeof(scene_t), 8, 4096);
        if (!pool_ready) return 0;
        wgf_core_priv_resource_register(&scene_pool, &resource_kind);
    }
    resource_kind.loader = loader_of;
    return wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_SCENE, path);
}

static const wgf_ecs_priv_scene_data_t *ready(wgf_scene_t scene)
{
    const scene_t *scene_ptr = scene_of(scene);
    return scene_ptr != NULL && scene_ptr->resource.status == WGF_RESOURCE_STATUS_READY ? scene_ptr->data : NULL;
}

int wgf_scene_instantiate(wgf_scene_t scene, wgf_node_t parent)
{
    const wgf_ecs_priv_scene_data_t *data = ready(scene);
    int i, made = 0;
    if (data == NULL || (parent != 0 && wgf_node_get_type(parent) == WGF_NODE_TYPE_NONE)) return 0;
    for (i = 0; i < data->count; i++) {
        data = ready(scene); /* making entities doesn't free the scene, but look again */
        if (!data->things[i].prefab && make(data, &data->things[i], parent) != 0) made++;
    }
    return made;
}

wgf_entity_t wgf_scene_spawn(wgf_scene_t scene, const char *name, wgf_node_t parent)
{
    const wgf_ecs_priv_scene_data_t *data = ready(scene);
    const int prefab = data != NULL && name != NULL ? find_prefab(data, name) : -1;
    if (prefab < 0 || (parent != 0 && wgf_node_get_type(parent) == WGF_NODE_TYPE_NONE)) return 0;
    return make(data, &data->things[prefab], parent);
}

int wgf_scene_get_entity_count(wgf_scene_t scene)
{
    const wgf_ecs_priv_scene_data_t *data = ready(scene);
    return data != NULL ? data->entities : 0;
}

int wgf_scene_get_prefab_count(wgf_scene_t scene)
{
    const wgf_ecs_priv_scene_data_t *data = ready(scene);
    return data != NULL ? data->prefabs : 0;
}

const char *wgf_scene_get_prefab_name(wgf_scene_t scene, int index)
{
    const wgf_ecs_priv_scene_data_t *data = ready(scene);
    int i, n = 0;
    if (data == NULL || index < 0) return "";
    for (i = 0; i < data->count; i++) {
        if (data->things[i].prefab && n++ == index) return data->things[i].name;
    }
    return "";
}

bool wgf_scene_has_prefab(wgf_scene_t scene, const char *name)
{
    const wgf_ecs_priv_scene_data_t *data = ready(scene);
    return data != NULL && name != NULL && find_prefab(data, name) >= 0;
}

void wgf_ecs_priv_scene_shutdown(void)
{
    if (!pool_ready) return;
    wgf_core_priv_resource_unregister(&scene_pool);
    wgf_core_priv_handle_pool_destroy(&scene_pool);
    pool_ready = false;
}
