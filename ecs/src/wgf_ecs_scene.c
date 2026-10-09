#include "wgf_scene.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */
#include "wgf_behavior.h"
#include "wgf_component.h"
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
 * line checked against the table of kinds, components, and keys below, kept as each
 * line's settings in their text; making an actor makes it of its kind, applies its lines in
 * order (a prefab's first when it starts as one), then makes its children (the prefab's,
 * then its own). The format's values are parsed by the same functions when the file is
 * read and when its lines are applied, so what was checked is what is used. */

#define FILE_MAX (4 << 20)
#define LINE_MAX_BYTES 4096
#define WORDS_MAX 128
#define POINTS_MAX 2048 /* floats in a polygon: 1024 points, as wgf_shape2d_set_polygon takes */
#define DEPTH_MAX 32    /* actors inside actors */

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

enum { ROLE_TRANSFORM, ROLE_KIND, ROLE_COMPONENT, ROLE_BEHAVIOR };

typedef struct kind_t {
    const char *name;
    int role;
    int what; /* a kind's wgf_actor_kind_t, a component's wgf_component_t */
    key_t keys[16];
    bool open; /* any key: a behavior's parameters */
} kind_t;

static const kind_t kinds[] = {
    {"transform", ROLE_TRANSFORM, 0, {{"position", V_NUM3}, {"rotation", V_NUM3}, {"scale", V_NUM3}}, false},
    {"motion",
     ROLE_COMPONENT,
     WGF_COMPONENT_MOTION,
     {{"velocity", V_NUM3}, {"spin", V_NUM3}, {"damping", V_NUM}, {"max_speed", V_NUM}},
     false},
    {"bounds",
     ROLE_COMPONENT,
     WGF_COMPONENT_BOUNDS,
     {{"rect", V_NUM4}, {"mode", V_MODE}, {"margin", V_NUM}, {"visible", V_BOOL}},
     false},
    {"lifetime", ROLE_COMPONENT, WGF_COMPONENT_LIFETIME, {{"seconds", V_NUM}}, false},
    {"collider",
     ROLE_COMPONENT,
     WGF_COMPONENT_COLLIDER,
     {{"radius", V_NUM}, {"layer", V_INT}, {"mask", V_INT}, {"enabled", V_BOOL}},
     false},
    {"behavior", ROLE_BEHAVIOR, 0, {{"name", V_TEXT}}, true},
    /* physics3d's (wgf_body.h, wgf_vehicle.h): any key, its text handed to physics, which
       reads it, so a program without physics carries none of its keys */
    {"body", ROLE_COMPONENT, WGF_COMPONENT_BODY, {{NULL, 0}}, true},
    {"vehicle", ROLE_COMPONENT, WGF_COMPONENT_VEHICLE, {{NULL, 0}}, true},
    {"shape2d",
     ROLE_KIND,
     WGF_ACTOR_KIND_SHAPE2D,
     {{"rectangle", V_NUM2},
      {"circle", V_NUM},
      {"line", V_NUM4},
      {"polygon", V_POINTS},
      {"outline", V_NUM},
      {"color", V_COLOR},
      {"pivot", V_NUM2}},
     false},
    {"sprite",
     ROLE_KIND,
     WGF_ACTOR_KIND_SPRITE,
     {{"texture", V_TEXT}, {"source", V_NUM4}, {"size", V_NUM2}, {"pivot", V_NUM2}, {"tint", V_COLOR}},
     false},
    {"text",
     ROLE_KIND,
     WGF_ACTOR_KIND_TEXT,
     {{"string", V_TEXT}, {"font", V_TEXT}, {"size", V_NUM}, {"color", V_COLOR}, {"wrap", V_NUM}, {"align", V_ALIGN}},
     false},
    {"emitter2d",
     ROLE_KIND,
     WGF_ACTOR_KIND_EMITTER2D,
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
     ROLE_COMPONENT,
     WGF_COMPONENT_VOICE,
     {{"sound", V_TEXT},
      {"streamed", V_BOOL},
      {"volume", V_NUM},
      {"pitch", V_NUM},
      {"pan", V_NUM},
      {"loop", V_BOOL},
      {"play", V_BOOL}},
     false},
    {"model",
     ROLE_KIND,
     WGF_ACTOR_KIND_MODEL,
     {{"plane", V_NUM3},
      {"cube", V_NUM3},
      {"sphere", V_NUM3},
      {"cylinder", V_NUM3},
      {"cone", V_NUM3},
      {"capsule", V_NUM4},
      {"torus", V_NUM4},
      {"path", V_TEXT},
      {"tint", V_COLOR}},
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

#define PATH_MAX_BYTES 256

typedef struct thing_t { /* a block: a prefab's actor or a scene's, its parent named by its path */
    char name[WGF_ECS_PRIV_NAME_MAX]; /* its path's last part */
    char path[PATH_MAX_BYTES];        /* "ship/flame": its parent's path, then its name */
    bool prefab;
    int parent; /* its parent's block, into things; -1 at the top */
    int from;   /* the prefab it starts as, into things; -1 none */
    int kind;   /* its kind line's kind, into kinds[]; -1 none: a plain actor, or its prefab's kind */
    line_t *lines;
    int count, capacity;
} thing_t;

struct wgf_ecs_priv_scene_data_t {
    thing_t *things;
    int count, capacity;
    int actors, prefabs; /* at the top */
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
        if (data->things[i].prefab && data->things[i].parent < 0 && strcmp(data->things[i].name, name) == 0) return i;
    }
    return -1;
}

/* The block above whose path is `path`, a prefab's or a scene's actor's as `prefab` says;
 * -1 for none. */
static int find_path(const wgf_ecs_priv_scene_data_t *data, const char *path, bool prefab)
{
    int i;
    for (i = 0; i < data->count; i++) {
        if (data->things[i].prefab == prefab && strcmp(data->things[i].path, path) == 0) return i;
    }
    return -1;
}

/* The kind a block is: its own kind line's, or its prefab's (-1: a plain actor). */
static int kind_of(const wgf_ecs_priv_scene_data_t *data, int thing)
{
    int depth;
    for (depth = 0; thing >= 0 && depth < DEPTH_MAX; depth++) {
        if (data->things[thing].kind >= 0) return data->things[thing].kind;
        thing = data->things[thing].from;
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
        if (strpbrk(words[i], ".[]") != NULL) {
            fail(parser, "a key with . [ or ] is kept for structured values", words[i]);
            return true;
        }
        type = key_type(kind, words[i]);
        if (type < 0) {
            fail(parser, "no such key for this line", words[i]);
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
    int open = -1; /* the block open, into things */
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
            if (strcmp(words[0], "wgf-scene") == 0 && words[1] != NULL && strcmp(words[1], "1") == 0) {
                fail(&parser, "version 1's entities are actors now: the file's blocks are `actor`, at `wgf-scene 2`",
                     NULL);
                break;
            }
            if (strcmp(words[0], "wgf-scene") != 0 || words[1] == NULL || strcmp(words[1], "2") != 0 ||
                words[2] != NULL) {
                fail(&parser, "the first line is `wgf-scene 2`", words[0]);
                break;
            }
            header = true;
            continue;
        }
        if (strcmp(words[0], "prefab") == 0 || strcmp(words[0], "actor") == 0) {
            const bool prefab = words[0][0] == 'p';
            const char *slash = words[1] != NULL ? strrchr(words[1], '/') : NULL;
            const char *name = slash != NULL ? slash + 1 : words[1];
            int parent = -1, depth = 0;
            thing_t *thing;
            if (open >= 0) {
                fail(&parser, "a block inside another: blocks are flat, a part naming its parent by path", words[0]);
                break;
            }
            if ((prefab && words[1] == NULL) || (words[1] != NULL && words[2] != NULL) ||
                (words[1] != NULL && strlen(words[1]) >= PATH_MAX_BYTES) ||
                (name != NULL && (name[0] == '\0' || strlen(name) >= WGF_ECS_PRIV_NAME_MAX))) {
                fail(&parser, prefab ? "prefab takes a path, its name under 64 bytes"
                                     : "actor takes a path or none, its name under 64 bytes", NULL);
                break;
            }
            if (slash != NULL) { /* a part: its parent a block above, at that path */
                char above[PATH_MAX_BYTES];
                const char *c;
                snprintf(above, sizeof(above), "%.*s", (int)(slash - words[1]), words[1]);
                parent = find_path(data, above, prefab);
                for (c = words[1]; *c != '\0'; c++) depth += *c == '/';
                if (parent < 0) {
                    fail(&parser, "no block above at its parent's path", above);
                    break;
                }
                if (depth >= DEPTH_MAX) {
                    fail(&parser, "a path deeper than 32", words[1]);
                    break;
                }
            }
            if (words[1] != NULL && find_path(data, words[1], prefab) >= 0) {
                fail(&parser, prefab ? "a second prefab block at that path" : "a second actor block at that path",
                     words[1]);
                break;
            }
            if (data->count == data->capacity) {
                const int capacity = data->capacity > 0 ? data->capacity * 2 : 16;
                thing_t *grown = (thing_t *)realloc(data->things, sizeof(thing_t) * (size_t)capacity);
                if (grown == NULL) break;
                data->things = grown;
                data->capacity = capacity;
            }
            thing = &data->things[data->count];
            memset(thing, 0, sizeof(*thing));
            thing->prefab = prefab;
            thing->parent = parent;
            thing->from = -1;
            thing->kind = -1;
            if (words[1] != NULL) {
                snprintf(thing->name, sizeof(thing->name), "%s", name);
                snprintf(thing->path, sizeof(thing->path), "%s", words[1]);
            }
            if (parent < 0 && prefab) data->prefabs++;
            else if (parent < 0) data->actors++;
            open = data->count++;
        } else if (strcmp(words[0], "end") == 0) {
            if (open < 0 || words[1] != NULL) {
                fail(&parser, open < 0 ? "an `end` with nothing to end" : "`end` takes nothing", NULL);
                break;
            }
            open = -1;
        } else if (strcmp(words[0], "from") == 0) {
            thing_t *thing = open >= 0 ? &data->things[open] : NULL;
            const int prefab = thing != NULL && words[1] != NULL ? find_prefab(data, words[1]) : -1;
            if (thing == NULL || thing->count > 0 || thing->from >= 0 || words[1] == NULL || words[2] != NULL) {
                fail(&parser, "`from <prefab>` comes first in an actor or prefab", NULL);
                break;
            }
            if (prefab < 0) {
                fail(&parser, "no prefab of that name above it", words[1]);
                break;
            }
            thing->from = prefab;
        } else {
            const int kind = find_kind(words[0]);
            thing_t *thing = open >= 0 ? &data->things[open] : NULL;
            if (thing == NULL) {
                fail(&parser, "a line is `prefab`, `actor`, `end`, or a line inside one", words[0]);
                break;
            }
            if (kind < 0) {
                fail(&parser, "no such kind or component", words[0]);
                break;
            }
            if (kinds[kind].role == ROLE_KIND) { /* one kind, its own or its prefab's */
                const int inherited = thing->from >= 0 ? kind_of(data, thing->from) : -1;
                if ((thing->kind >= 0 && thing->kind != kind) || (inherited >= 0 && inherited != kind)) {
                    fail(&parser, "an actor is one kind: put the other in a part of it (a block at its path/<name>)", words[0]);
                    break;
                }
                thing->kind = kind;
            }
            if (!add_line(&parser, thing, kind, words + 1)) {
                wgf_log_error("wgf_scene: %s: out of memory", path);
                parser.failed = true;
            }
        }
    }
    if (!parser.failed && !header) fail(&parser, "empty: the first line is `wgf-scene 2`", NULL);
    if (!parser.failed && open >= 0) fail(&parser, "the file ended inside an actor or prefab: its `end` is missing", NULL);
    free(parser.scratch);
    if (parser.failed) {
        wgf_ecs_priv_scene_data_free(data);
        return NULL;
    }
    return data;
}

/* ---- applying a line -------------------------------------------------------------- */

typedef struct after_t { /* what happens once the actor is placed: a burst, a voice played */
    int burst;
    bool play;
} after_t;

static float f(const value_t *v, int i)
{
    return (float)v->n[i];
}

/* What a line's settings set: a kind's (KIND + its actor type), a component's, the
 * transform's, a behavior's. */
#define KIND 100
#define BEHAVIOR (-1)

static int what_of(int kind)
{
    switch (kinds[kind].role) {
        case ROLE_TRANSFORM: return 0;
        case ROLE_KIND: return KIND + kinds[kind].what;
        case ROLE_BEHAVIOR: return BEHAVIOR;
        default: return kinds[kind].what;
    }
}

static void apply_setting(wgf_actor_t actor, int kind, const setting_t *s, value_t *v, after_t *after, int behavior)
{
    const wgf_actor_t e = actor;
    const char *key = s->key;
    const int type = key_type(kind, key);
    if (!parse_value(type, s->value, v)) return; /* checked as the file was read */
    switch (what_of(kind)) {
        case 0:
            if (strcmp(key, "position") == 0) wgf_actor_set_position(e, f(v, 0), f(v, 1), f(v, 2));
            else if (strcmp(key, "rotation") == 0) wgf_actor_set_rotation(e, f(v, 0), f(v, 1), f(v, 2));
            else wgf_actor_set_scale(e, f(v, 0), f(v, 1), f(v, 2));
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
        case WGF_COMPONENT_BODY:
        case WGF_COMPONENT_VEHICLE: { /* through physics' hooks: none was added without them */
            const wgf_ecs_priv_part_component_t *part = wgf_ecs_priv_get_part_component((wgf_component_t)what_of(kind));
            if (part != NULL && wgf_actor_has_component(e, (wgf_component_t)what_of(kind))) {
                part->set(e, key, v->text);
            }
            break;
        }
        case BEHAVIOR: /* its name, which added it, is its line's */
            if (strcmp(key, "name") != 0) wgf_behavior_set_param(e, behavior, key, v->text);
            break;
        case KIND + WGF_ACTOR_KIND_SHAPE2D:
            if (strcmp(key, "rectangle") == 0) wgf_shape2d_set_rectangle(actor, f(v, 0), f(v, 1));
            else if (strcmp(key, "circle") == 0) wgf_shape2d_set_circle(actor, f(v, 0));
            else if (strcmp(key, "line") == 0) wgf_shape2d_set_line(actor, f(v, 0), f(v, 1), f(v, 2), f(v, 3));
            else if (strcmp(key, "polygon") == 0) {
                float points[POINTS_MAX];
                int i;
                for (i = 0; i < v->count; i++) points[i] = f(v, i);
                wgf_shape2d_set_polygon(actor, points, v->count);
            } else if (strcmp(key, "outline") == 0) wgf_shape2d_set_outline(actor, f(v, 0));
            else if (strcmp(key, "color") == 0) wgf_shape2d_set_color(actor, v->colors[0]);
            else wgf_shape2d_set_pivot(actor, f(v, 0), f(v, 1));
            break;
        case KIND + WGF_ACTOR_KIND_SPRITE:
            if (strcmp(key, "texture") == 0) {
                const wgf_texture_t texture = wgf_texture_create(v->text);
                wgf_sprite_set_texture(actor, texture);
                wgf_resource_release(texture); /* the sprite holds its own */
            } else if (strcmp(key, "source") == 0) wgf_sprite_set_source(actor, f(v, 0), f(v, 1), f(v, 2), f(v, 3));
            else if (strcmp(key, "size") == 0) wgf_sprite_set_size(actor, f(v, 0), f(v, 1));
            else if (strcmp(key, "pivot") == 0) wgf_sprite_set_pivot(actor, f(v, 0), f(v, 1));
            else wgf_sprite_set_tint(actor, v->colors[0]);
            break;
        case KIND + WGF_ACTOR_KIND_TEXT:
            if (strcmp(key, "string") == 0) wgf_text_set_string(actor, v->text);
            else if (strcmp(key, "font") == 0) {
                const wgf_font_t font = v->text[0] != '\0' ? wgf_font_create(v->text) : 0;
                wgf_text_set_font(actor, font);
                if (font != 0) wgf_resource_release(font);
            } else if (strcmp(key, "size") == 0) wgf_text_set_font_size(actor, f(v, 0));
            else if (strcmp(key, "color") == 0) wgf_text_set_color(actor, v->colors[0]);
            else if (strcmp(key, "wrap") == 0) wgf_text_set_wrap_width(actor, f(v, 0));
            else wgf_text_set_align(actor, (wgf_text_halign_t)(int)v->n[0], (wgf_text_valign_t)(int)v->n[1]);
            break;
        case KIND + WGF_ACTOR_KIND_MODEL: { /* through a stage's hooks: no model was made without one */
            const wgf_gfx_priv_model_hooks_t *hooks = wgf_gfx_priv_get_model_hooks();
            float params[4];
            int i;
            if (hooks == NULL || wgf_actor_get_kind(actor) != WGF_ACTOR_KIND_MODEL) break;
            if (strcmp(key, "tint") == 0) {
                hooks->set_tint(actor, v->colors[0]);
                break;
            }
            if (strcmp(key, "path") == 0) {
                hooks->set_path(actor, v->text);
                break;
            }
            for (i = 0; i < 4; i++) params[i] = i < v->count ? f(v, i) : 0.0f;
            hooks->set_shape(actor, key, params, v->count);
            break;
        }
        case KIND + WGF_ACTOR_KIND_EMITTER2D:
            if (strcmp(key, "rate") == 0) wgf_emitter2d_set_rate(actor, f(v, 0));
            else if (strcmp(key, "emitting") == 0) wgf_emitter2d_set_emitting(actor, v->truth);
            else if (strcmp(key, "capacity") == 0) wgf_emitter2d_set_capacity(actor, (int)v->n[0]);
            else if (strcmp(key, "life") == 0) wgf_emitter2d_set_life(actor, f(v, 0), f(v, 1));
            else if (strcmp(key, "direction") == 0) wgf_emitter2d_set_direction(actor, f(v, 0), wgf_emitter2d_get_spread(actor));
            else if (strcmp(key, "spread") == 0) wgf_emitter2d_set_direction(actor, wgf_emitter2d_get_direction(actor), f(v, 0));
            else if (strcmp(key, "speed") == 0) wgf_emitter2d_set_speed(actor, f(v, 0), f(v, 1));
            else if (strcmp(key, "radius") == 0) wgf_emitter2d_set_radius(actor, f(v, 0));
            else if (strcmp(key, "gravity") == 0) wgf_emitter2d_set_gravity(actor, f(v, 0), f(v, 1));
            else if (strcmp(key, "drag") == 0) wgf_emitter2d_set_drag(actor, f(v, 0));
            else if (strcmp(key, "size") == 0) wgf_emitter2d_set_size(actor, f(v, 0), f(v, 1));
            else if (strcmp(key, "color") == 0) wgf_emitter2d_set_color(actor, v->colors[0], v->colors[1]);
            else if (strcmp(key, "stretch") == 0) wgf_emitter2d_set_stretch(actor, f(v, 0));
            else after->burst = (int)v->n[0];
            break;
        default: { /* the voice */
            const wgf_voice_t voice = wgf_actor_get_voice(e);
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
static void apply_sound(wgf_actor_t e, const line_t *line, value_t *v)
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
        wgf_voice_set_sound(wgf_actor_get_voice(e), sound);
        if (sound != 0) wgf_resource_release(sound);
    }
}

/* A block's lines on `actor`, a prefab's it starts as first. */
static void apply_thing(const wgf_ecs_priv_scene_data_t *data, const thing_t *thing, wgf_actor_t actor, after_t *after,
                        int *behavior, int depth)
{
    static value_t v; /* the main thread's */
    int i, j;
    if (thing->from >= 0 && depth < DEPTH_MAX) {
        apply_thing(data, &data->things[thing->from], actor, after, behavior, depth + 1);
    }
    for (i = 0; i < thing->count; i++) {
        const line_t *line = &thing->lines[i];
        if (kinds[line->kind].role == ROLE_COMPONENT) wgf_actor_add_component(actor, (wgf_component_t)kinds[line->kind].what);
        if (kinds[line->kind].role == ROLE_BEHAVIOR) {
            for (j = 0; j < line->count; j++) { /* name= adds one; without it, the last one's parameters */
                if (strcmp(line->settings[j].key, "name") == 0) *behavior = wgf_actor_add_behavior(actor, line->settings[j].value);
            }
            if (*behavior == 0) {
                wgf_log_warn("wgf_scene: line %d: parameters for a behavior, but the actor has none yet", line->number);
                continue;
            }
        }
        if (kinds[line->kind].what == WGF_COMPONENT_VOICE && kinds[line->kind].role == ROLE_COMPONENT) {
            apply_sound(actor, line, &v);
        }
        for (j = 0; j < line->count; j++) apply_setting(actor, line->kind, &line->settings[j], &v, after, *behavior);
    }
}

/* An actor of a block's kind (its own kind line's, or its prefab's): a plain actor for none,
 * and for a model before any stage is made (logged by the hooks' absence). */
static wgf_actor_t make_kind(const wgf_ecs_priv_scene_data_t *data, int thing)
{
    const int kind = kind_of(data, thing);
    switch (kind >= 0 ? kinds[kind].what : -1) {
        case WGF_ACTOR_KIND_SHAPE2D: return wgf_shape2d_create();
        case WGF_ACTOR_KIND_SPRITE: return wgf_sprite_create(0);
        case WGF_ACTOR_KIND_TEXT: return wgf_text_create(0);
        case WGF_ACTOR_KIND_EMITTER2D: return wgf_emitter2d_create();
        case WGF_ACTOR_KIND_MODEL: { /* through a stage's hooks: a program with no stage links no 3D */
            const wgf_gfx_priv_model_hooks_t *hooks = wgf_gfx_priv_get_model_hooks();
            if (hooks != NULL) return hooks->create();
            wgf_log_warn("wgf_scene: a model is drawn on a stage: make one first (a plain actor made in its place)");
            return wgf_actor_create();
        }
        default: return wgf_actor_create();
    }
}

static wgf_actor_t make(const wgf_ecs_priv_scene_data_t *data, int thing, wgf_actor_t parent, int depth);
static void overlay(const wgf_ecs_priv_scene_data_t *data, int thing, wgf_actor_t node, int depth);
static bool defer(int thing, wgf_actor_t root, int depth);

/* Where a spawn_at puts the top actor it makes, before it is snapped; none otherwise. */
static struct {
    bool set;
    float x, y, z, angle;
} placement;

/* The scene whose blocks are being made: what a glTF file's root waits with. */
static wgf_scene_t making;

/* The node of a glTF file named `name` directly under `actor` (a file's root or one of its
 * nodes); 0 for none, and for any other actor. */
static wgf_actor_t file_node(wgf_actor_t actor, const char *name)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    int i;
    if (actor_ptr == NULL || name[0] == '\0' ||
        !(actor_ptr->from_file || (actor_ptr->type == WGF_ACTOR_KIND_MODEL && actor_ptr->as.model.file_root))) {
        return 0;
    }
    for (i = 0; i < wgf_actor_get_child_count(actor); i++) {
        const wgf_actor_t child = wgf_actor_get_child(actor, i);
        const wgf_gfx_priv_actor_t *child_ptr = wgf_gfx_priv_actor_of(child);
        if (child_ptr != NULL && child_ptr->from_file && strcmp(wgf_actor_get_name(child), name) == 0) return child;
    }
    return 0;
}

/* Whether `actor` is a glTF file's root whose file is still loading: its tree isn't made yet. */
static bool file_loading(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_MODEL && actor_ptr->as.model.file_root &&
           wgf_resource_get_status(actor_ptr->as.model.mesh) == WGF_RESOURCE_STATUS_PENDING;
}

/* The blocks inside block `thing`, each made under `actor`: a block naming one of a glTF
 * file's nodes there is that node, its lines applied to it (overlay); any other a new actor. */
static void make_children(const wgf_ecs_priv_scene_data_t *data, int thing, wgf_actor_t actor, int depth)
{
    int i;
    if (depth >= DEPTH_MAX) return;
    if (data->things[thing].from >= 0) make_children(data, data->things[thing].from, actor, depth + 1);
    for (i = thing + 1; i < data->count; i++) {
        if (data->things[i].parent == thing) {
            const wgf_actor_t node = file_node(actor, data->things[i].name);
            if (node != 0) overlay(data, i, node, depth + 1);
            else make(data, i, actor, depth + 1);
        }
    }
}

/* The blocks inside block `thing`, under `actor`: made now, or, `actor` a glTF file's root
 * still loading, once its tree is (its nodes are what they name). */
static void make_children_of(const wgf_ecs_priv_scene_data_t *data, int thing, wgf_actor_t actor, int depth)
{
    if (file_loading(actor) && defer(thing, actor, depth)) return;
    make_children(data, thing, actor, depth);
}

/* Block `thing`'s lines on a glTF file's node, which the file made: its kind the file's. */
static void overlay(const wgf_ecs_priv_scene_data_t *data, int thing, wgf_actor_t node, int depth)
{
    after_t after = {0, false};
    int behavior = 0;
    const int kind = kind_of(data, thing);
    if (kind >= 0 && kinds[kind].role == ROLE_KIND && (int)kinds[kind].what != (int)wgf_actor_get_kind(node)) {
        wgf_log_warn("wgf_scene: %s: a glTF file's node is the kind its file makes it, not a %s",
                     data->things[thing].path, kinds[kind].name);
    }
    apply_thing(data, &data->things[thing], node, &after, &behavior, 0);
    make_children_of(data, thing, node, depth);
    wgf_actor_snap(node);
    if (after.play) wgf_voice_play(wgf_actor_get_voice(node));
}

/* Block `thing`'s actor, its lines applied, then its children made, then it placed where it
 * is, its smoothing ended: so what is made with it (a burst) starts there. */
static wgf_actor_t make(const wgf_ecs_priv_scene_data_t *data, int thing, wgf_actor_t parent, int depth)
{
    after_t after = {0, false};
    int behavior = 0;
    const thing_t *block = &data->things[thing];
    const wgf_actor_t actor = make_kind(data, thing);
    if (actor == 0) return 0;
    if (parent != 0) wgf_actor_set_parent(actor, parent);
    if (block->name[0] != '\0' && (!block->prefab || block->parent >= 0)) wgf_actor_set_name(actor, block->name);
    apply_thing(data, block, actor, &after, &behavior, 0);
    make_children_of(data, thing, actor, depth);
    if (depth == 0 && placement.set) {
        wgf_vec3_t angles = wgf_actor_get_rotation(actor);
        if (wgf_gfx_priv_actor_get_root_type(actor) == WGF_ACTOR_KIND_STAGE3D) {
            angles.y = placement.angle;
        } else {
            angles.z = placement.angle;
        }
        wgf_actor_set_position(actor, placement.x, placement.y, placement.z);
        wgf_actor_set_rotation(actor, angles.x, angles.y, angles.z);
    }
    wgf_actor_snap(actor);
    if (after.burst > 0 && wgf_actor_get_kind(actor) == WGF_ACTOR_KIND_EMITTER2D) wgf_emitter2d_burst(actor, after.burst);
    if (after.play) wgf_voice_play(wgf_actor_get_voice(actor));
    return actor;
}

/* ---- the resource ---------------------------------------------------------------- */

typedef struct scene_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's */
    wgf_ecs_priv_scene_data_t *data;
    wgf_prefab_t *prefabs; /* by block, its prefab's handle once found; NULL until the first */
} scene_t;

/* A prefab: its scene, and its block there. */
typedef struct prefab_t {
    wgf_scene_t scene;
    int thing;
} prefab_t;

static bool prefabs_ready;
static wgf_core_priv_handle_pool_t prefab_pool;
static prefab_t *prefab_records;

static bool pool_ready;
static wgf_core_priv_handle_pool_t scene_pool;
static scene_t *scenes;
static wgf_core_priv_resource_kind_t resource_kind;

static scene_t *scene_of_at(wgf_scene_t scene, const char *caller)
{
    uint16_t index;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve_at(&scene_pool, scene, &index, caller)) return NULL;
    return &scenes[index];
}
#define scene_of(...) scene_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

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
    int i;
    (void)scene;
    for (i = 0; scene_ptr->prefabs != NULL && scene_ptr->data != NULL && i < scene_ptr->data->count; i++) {
        if (scene_ptr->prefabs[i] != 0) wgf_core_priv_handle_pool_free(&prefab_pool, scene_ptr->prefabs[i]);
    }
    free(scene_ptr->prefabs);
    scene_ptr->prefabs = NULL;
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

/* ---- blocks under a glTF file still loading ---------------------------------------- */

/* A file's root whose file was loading as its scene made it: the blocks inside block `thing`
 * are made under it once its tree is (the file part's hook, wgf_gfx_priv_model_built), the
 * scene held until then. */
typedef struct deferred_t {
    wgf_scene_t scene;
    int thing, depth;
    wgf_actor_t root;
} deferred_t;

static deferred_t *deferred;
static int deferred_count, deferred_capacity;

static void resolve_tree(wgf_actor_t actor);

static bool waits(wgf_actor_t actor)
{
    int i;
    for (i = 0; i < deferred_count; i++) {
        if (deferred[i].root == actor) return true;
    }
    return false;
}

/* Let go of the waits whose roots went before their files loaded. */
static void forget_gone(void)
{
    int i = 0;
    while (i < deferred_count) {
        if (wgf_handle_is_alive(deferred[i].root)) {
            i++;
            continue;
        }
        wgf_resource_release(deferred[i].scene);
        deferred[i] = deferred[--deferred_count];
    }
}

/* The ecs's hook: `root`'s file loaded (its tree made) or FAILED; what waited for it made, or
 * dropped with a warning. A block made now may wait for another file, growing the list. */
static void file_built(wgf_actor_t root)
{
    int i = 0;
    while (i < deferred_count) {
        deferred_t wait;
        const wgf_ecs_priv_scene_data_t *data;
        const wgf_scene_t before = making;
        const wgf_gfx_priv_actor_t *root_ptr;
        if (deferred[i].root != root) {
            i++;
            continue;
        }
        wait = deferred[i];
        deferred[i] = deferred[--deferred_count];
        data = ready(wait.scene);
        root_ptr = wgf_gfx_priv_actor_of(root);
        if (data != NULL && root_ptr != NULL &&
            wgf_resource_get_status(root_ptr->as.model.mesh) == WGF_RESOURCE_STATUS_READY) {
            making = wait.scene;
            make_children(data, wait.thing, root, wait.depth);
            making = before;
            if (!waits(root)) resolve_tree(root);
        } else if (data != NULL) {
            wgf_log_warn("wgf_scene: %s: what the scene puts under it isn't made: its file failed",
                         data->things[wait.thing].path[0] != '\0' ? data->things[wait.thing].path : "a model");
        }
        wgf_resource_release(wait.scene);
        i = 0; /* making may have changed the list */
    }
    forget_gone();
}

static bool defer(int thing, wgf_actor_t root, int depth)
{
    if (making == 0) return false;
    forget_gone();
    if (deferred_count == deferred_capacity) {
        const int capacity = deferred_capacity > 0 ? deferred_capacity * 2 : 8;
        deferred_t *grown = (deferred_t *)realloc(deferred, sizeof(deferred_t) * (size_t)capacity);
        if (grown == NULL) return false; /* made now, beside the file's nodes */
        deferred = grown;
        deferred_capacity = capacity;
    }
    wgf_core_priv_resource_retain(making);
    deferred[deferred_count].scene = making;
    deferred[deferred_count].thing = thing;
    deferred[deferred_count].depth = depth;
    deferred[deferred_count].root = root;
    deferred_count++;
    wgf_gfx_priv_set_model_built_hook(file_built);
    return true;
}

/* The references in the behaviors of `actor` and everything under it found, as it and all
 * made with it are there: the actors a scene just made, so no other tree is walked. A file's
 * root that waits for its file is left until its tree is made, so it can refer into it. */
static void resolve_tree(wgf_actor_t actor)
{
    int i;
    if (waits(actor)) return;
    wgf_ecs_priv_behaviors_resolve(actor);
    for (i = 0; i < wgf_actor_get_child_count(actor); i++) resolve_tree(wgf_actor_get_child(actor, i));
}

int wgf_scene_instantiate(wgf_scene_t scene, wgf_actor_t parent)
{
    const wgf_ecs_priv_scene_data_t *data = ready(scene);
    wgf_actor_t *made_actors;
    int i, made = 0;
    if (data == NULL || (parent != 0 && wgf_actor_get_kind(parent) == WGF_ACTOR_KIND_NONE)) return 0;
    made_actors = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * (size_t)(data->count > 0 ? data->count : 1));
    if (made_actors == NULL) {
        wgf_log_error("wgf_scene: out of memory instantiating");
        return 0;
    }
    making = scene;
    for (i = 0; i < data->count; i++) {
        data = ready(scene); /* making actors doesn't free the scene, but look again */
        if (!data->things[i].prefab && data->things[i].parent < 0) {
            const wgf_actor_t actor = make(data, i, parent, 0);
            if (actor != 0) made_actors[made++] = actor;
        }
    }
    making = 0;
    for (i = 0; i < made; i++) resolve_tree(made_actors[i]); /* all made, so each can refer to any */
    free(made_actors);
    return made;
}

wgf_prefab_t wgf_scene_find_prefab(wgf_scene_t scene, const char *name)
{
    const wgf_ecs_priv_scene_data_t *data = ready(scene);
    scene_t *scene_ptr = scene_of(scene);
    const int thing = data != NULL && name != NULL ? find_prefab(data, name) : -1;
    uint16_t index;
    if (thing < 0) return 0;
    if (scene_ptr->prefabs == NULL) {
        scene_ptr->prefabs = (wgf_prefab_t *)calloc((size_t)data->count, sizeof(wgf_prefab_t));
        if (scene_ptr->prefabs == NULL) return 0;
    }
    if (scene_ptr->prefabs[thing] != 0) return scene_ptr->prefabs[thing];
    if (!prefabs_ready) {
        prefabs_ready = wgf_core_priv_handle_pool_init(&prefab_pool, WGF_CORE_PRIV_HANDLE_KIND_PREFAB,
                                                       (void **)&prefab_records, sizeof(prefab_t), 16, 65535);
        if (!prefabs_ready) return 0;
    }
    scene_ptr->prefabs[thing] = wgf_core_priv_handle_pool_alloc(&prefab_pool);
    if (scene_ptr->prefabs[thing] == 0 ||
        !wgf_core_priv_handle_pool_resolve(&prefab_pool, scene_ptr->prefabs[thing], &index)) {
        wgf_log_error("wgf_scene_find_prefab: no room for another prefab");
        return 0;
    }
    prefab_records[index].scene = scene;
    prefab_records[index].thing = thing;
    return scene_ptr->prefabs[thing];
}

wgf_actor_t wgf_prefab_spawn(wgf_prefab_t prefab, wgf_actor_t parent)
{
    const wgf_ecs_priv_scene_data_t *data;
    const prefab_t *prefab_ptr;
    wgf_actor_t actor;
    uint16_t index;
    if (!prefabs_ready || !wgf_core_priv_handle_pool_resolve(&prefab_pool, prefab, &index)) return 0;
    prefab_ptr = &prefab_records[index];
    data = ready(prefab_ptr->scene);
    if (data == NULL || (parent != 0 && wgf_actor_get_kind(parent) == WGF_ACTOR_KIND_NONE)) return 0;
    making = prefab_ptr->scene;
    actor = make(data, prefab_ptr->thing, parent, 0);
    making = 0;
    resolve_tree(actor);
    return actor;
}

wgf_actor_t wgf_prefab_spawn_at(wgf_prefab_t prefab, wgf_actor_t parent, float x, float y, float z, float angle)
{
    wgf_actor_t actor;
    placement.set = true;
    placement.x = x;
    placement.y = y;
    placement.z = z;
    placement.angle = angle;
    actor = wgf_prefab_spawn(prefab, parent);
    placement.set = false;
    return actor;
}

int wgf_scene_get_actor_count(wgf_scene_t scene)
{
    const wgf_ecs_priv_scene_data_t *data = ready(scene);
    return data != NULL ? data->actors : 0;
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
        if (data->things[i].prefab && data->things[i].parent < 0 && n++ == index) return data->things[i].name;
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
    int i;
    for (i = 0; i < deferred_count; i++) wgf_resource_release(deferred[i].scene);
    free(deferred);
    deferred = NULL;
    deferred_count = deferred_capacity = 0;
    if (!pool_ready) return;
    wgf_core_priv_resource_unregister(&scene_pool);
    wgf_core_priv_handle_pool_destroy(&scene_pool);
    pool_ready = false;
    if (prefabs_ready) wgf_core_priv_handle_pool_destroy(&prefab_pool);
    prefabs_ready = false;
}
