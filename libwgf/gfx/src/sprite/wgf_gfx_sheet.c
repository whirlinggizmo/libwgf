/* Sprite sheets and 2D animation (wgf_sheet.h): helix-legacy's SpriteSheet2D and
 * SpriteAnimator2D. Sheets are resources: a grid made from numbers (READY at once), an atlas
 * loaded from its JSON file (read and parsed on a worker, its image made a texture as it
 * finishes; loaded again in place, what the program added kept and its players going on).
 * What each sprite plays is a record in a table of its own (the sprite's record
 * keeps nothing of it but a trim, wgf_gfx_priv_sprite_set_trim), stepped once a frame by a
 * part installed by the first sheet, which sets the sprite's region (wgf_sprite_set_source) as
 * its frames go by. */
#include "wgf_sheet.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_json_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "texture/wgf_gfx_texture_priv.h"

#define CLIPS_MAX 64
#define NAME_MAX 64
#define FACING_MAX 16
#define EVENTS_KEPT 16
#define DEFAULT_SECONDS 0.1f /* an atlas frame's with no duration: helix-legacy's */

typedef struct frame_t {
    float x, y, w, h;     /* its texels */
    float ox, oy, sw, sh; /* trimmed: its offset into the whole picture, and the picture's size; sw 0: none */
    float seconds;        /* an atlas's duration */
    char name[NAME_MAX];  /* "" for a grid's */
} frame_t;

typedef struct clip_t {
    char name[NAME_MAX];
    int *frames, count;
    float seconds; /* each frame's; 0: the frames' own */
    int direction;
    int *events; /* by place: raised as it shows, 0 none; NULL for none at all */
    bool added;  /* the program's (wgf_sheet_add_clip), not its file's: kept when the file loads again */
} clip_t;

typedef struct sheet_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    wgf_texture_t texture;
    bool grid;
    int frame_width, frame_height, count, margin, spacing; /* a grid's */
    frame_t *frames; /* a grid's cut once its texture is READY (a count of 0 waits for its size) */
    int frame_count;
    int cut_width, cut_height; /* the texture's size a grid's were cut from: cut again when it changes */
    clip_t clips[CLIPS_MAX];
    int clip_count;
} sheet_t;

typedef struct player_t {
    wgf_actor_t sprite;
    wgf_sheet_t sheet;
    int clip;    /* -1 none */
    int *order;  /* the clip's frames in the order shown, by place in the clip */
    int steps, step;
    float elapsed, speed;
    bool loop;
    wgf_play_state_t state;
    char asked[NAME_MAX];     /* the name played, before its facing */
    char facing[FACING_MAX]; /* "" none */
    int events[EVENTS_KEPT], event_count;
} player_t;

static struct {
    bool ready, installed;
    wgf_core_priv_handle_pool_t pool;
    sheet_t *sheets;
    player_t *players;
    int player_count, player_capacity;
} sh;

static sheet_t *sheet_of_at(wgf_sheet_t sheet, const char *caller)
{
    uint16_t index;
    if (!sh.ready || !wgf_core_priv_handle_pool_resolve_at(&sh.pool, sheet, &index, caller)) return NULL;
    return &sh.sheets[index];
}
#define sheet_of(sheet) sheet_of_at((sheet), WGF_CORE_PRIV_CALLER)

static void free_clip(clip_t *c)
{
    free(c->frames);
    free(c->events);
    c->frames = c->events = NULL;
}

/* A grid's frames cut from its texture's size, once it is READY; and again when its texture
 * has loaded again at another size. */
static void cut(sheet_t *s)
{
    const int tw = wgf_texture_get_width(s->texture), th = wgf_texture_get_height(s->texture);
    int columns, rows, n, i;
    if (!s->grid || tw <= 0 || th <= 0) return;
    if (s->frames != NULL) {
        if (tw == s->cut_width && th == s->cut_height) return;
        free(s->frames);
        s->frames = NULL;
        s->frame_count = 0;
    }
    s->cut_width = tw;
    s->cut_height = th;
    columns = (tw - 2 * s->margin + s->spacing) / (s->frame_width + s->spacing);
    rows = (th - 2 * s->margin + s->spacing) / (s->frame_height + s->spacing);
    if (columns <= 0 || rows <= 0) return;
    n = s->count > 0 ? s->count : columns * rows;
    s->frames = (frame_t *)calloc((size_t)n, sizeof(frame_t));
    if (s->frames == NULL) return;
    for (i = 0; i < n; i++) {
        const int c = i % columns, r = i / columns;
        s->frames[i].x = (float)(s->margin + c * (s->frame_width + s->spacing));
        s->frames[i].y = (float)(s->margin + r * (s->frame_height + s->spacing));
        s->frames[i].w = (float)s->frame_width;
        s->frames[i].h = (float)s->frame_height;
    }
    s->frame_count = n;
}

static player_t *player_of(wgf_actor_t sprite)
{
    int i;
    for (i = 0; i < sh.player_count; i++) {
        if (sh.players[i].sprite == sprite) return &sh.players[i];
    }
    return NULL;
}

static void drop_player(int i)
{
    free(sh.players[i].order);
    sh.players[i] = sh.players[--sh.player_count];
}

/* The sprite's region set to the frame showing, trimmed as it was packed; its event raised. */
static void show(player_t *p, bool entered)
{
    sheet_t *s = sheet_of(p->sheet);
    const clip_t *c;
    const frame_t *f;
    int place, frame;
    if (s == NULL || p->clip < 0 || p->steps == 0) return;
    cut(s);
    c = &s->clips[p->clip];
    place = p->order[p->step];
    frame = c->frames[place];
    if (entered && c->events != NULL && c->events[place] != 0) {
        if (p->event_count == EVENTS_KEPT) { /* the oldest dropped */
            memmove(p->events, p->events + 1, sizeof(int) * (EVENTS_KEPT - 1));
            p->event_count--;
        }
        p->events[p->event_count++] = c->events[place];
    }
    if (frame < 0 || frame >= s->frame_count) return;
    f = &s->frames[frame];
    wgf_sprite_set_source(p->sprite, f->x, f->y, f->w, f->h);
    wgf_gfx_priv_sprite_set_trim(p->sprite, f->ox, f->oy, f->sw, f->sh);
}

/* How long the step showing lasts. */
static float step_seconds(const sheet_t *s, const player_t *p)
{
    const clip_t *c = &s->clips[p->clip];
    const int frame = c->frames[p->order[p->step]];
    if (c->seconds > 0.0f) return c->seconds;
    return frame >= 0 && frame < s->frame_count && s->frames[frame].seconds > 0.0f ? s->frames[frame].seconds
                                                                                     : DEFAULT_SECONDS;
}

static void update(float dt)
{
    int i = 0;
    while (i < sh.player_count) {
        player_t *p = &sh.players[i];
        sheet_t *s = sheet_of(p->sheet);
        if (!wgf_handle_is_alive(p->sprite) || s == NULL) { /* its sprite or its sheet gone */
            drop_player(i);
            continue;
        }
        i++;
        if (p->state != WGF_PLAY_STATE_PLAYING || p->clip < 0 || wgf_gfx_priv_actor_is_frozen(p->sprite)) continue;
        p->elapsed += dt > 0.0f ? dt * p->speed : 0.0f;
        while (p->elapsed >= step_seconds(s, p)) {
            p->elapsed -= step_seconds(s, p);
            if (p->step + 1 < p->steps) {
                p->step++;
            } else if (p->loop) {
                p->step = 0;
            } else {
                p->state = WGF_PLAY_STATE_COMPLETE;
                p->elapsed = 0.0f;
                break;
            }
            show(p, true);
        }
    }
}

static void stop(void)
{
    if (sh.ready) {
        wgf_core_priv_resource_unregister(&sh.pool); /* each sheet freed */
        wgf_core_priv_handle_pool_destroy(&sh.pool);
    }
    while (sh.player_count > 0) drop_player(0);
    free(sh.players);
    memset(&sh, 0, sizeof(sh));
}

static wgf_core_priv_part_t part = {.name = "sheets",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_SHEETS,
                                    .update = update,
                                    .stop = stop};

/* ---- an atlas, read ------------------------------------------------------------------ */

typedef struct atlas_t {
    frame_t *frames;
    int frame_count;
    clip_t clips[CLIPS_MAX];
    int clip_count;
    char image[WGF_CORE_PRIV_FS_PATH_MAX]; /* beside the file */
} atlas_t;

static void free_atlas(atlas_t *a)
{
    int i;
    for (i = 0; i < a->clip_count; i++) free_clip(&a->clips[i]);
    free(a->frames);
    free(a);
}

/* One frame of an atlas's: its region, trim, and duration. False for one with no region, or
 * packed turned. */
static bool read_frame(const wgf_core_priv_json_t *data, const char *name, const char *path, frame_t *out)
{
    const wgf_core_priv_json_t *rect = wgf_core_priv_json_get(data, "frame");
    const wgf_core_priv_json_t *trim = wgf_core_priv_json_get(data, "spriteSourceSize");
    const wgf_core_priv_json_t *source = wgf_core_priv_json_get(data, "sourceSize");
    const double ms = wgf_core_priv_json_get_number(data, "duration", 0.0);
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", name);
    if (!wgf_core_priv_json_is_object(rect)) {
        wgf_log_warn("wgf_sheet: %s: frame %s has no rectangle", path, name);
        return false;
    }
    if (wgf_core_priv_json_truth(wgf_core_priv_json_get(data, "rotated"))) {
        wgf_log_warn("wgf_sheet: %s: frame %s is packed turned; pack without rotation", path, name);
        return false;
    }
    out->x = (float)wgf_core_priv_json_get_number(rect, "x", 0);
    out->y = (float)wgf_core_priv_json_get_number(rect, "y", 0);
    out->w = (float)wgf_core_priv_json_get_number(rect, "w", 0);
    out->h = (float)wgf_core_priv_json_get_number(rect, "h", 0);
    if (wgf_core_priv_json_is_object(trim) && wgf_core_priv_json_is_object(source)) {
        const float sw = (float)wgf_core_priv_json_get_number(source, "w", 0);
        const float sh_ = (float)wgf_core_priv_json_get_number(source, "h", 0);
        const float ox = (float)wgf_core_priv_json_get_number(trim, "x", 0);
        const float oy = (float)wgf_core_priv_json_get_number(trim, "y", 0);
        if (sw > 0.0f && sh_ > 0.0f && (ox != 0.0f || oy != 0.0f || sw != out->w || sh_ != out->h)) {
            out->ox = ox, out->oy = oy, out->sw = sw, out->sh = sh_;
        }
    }
    out->seconds = ms > 0.0 ? (float)(ms / 1000.0) : 0.0f;
    return true;
}

static bool add_atlas_clip(atlas_t *a, const char *name, const int *frames, int count, int direction)
{
    clip_t *c;
    int i;
    if (name[0] == '\0' || count <= 0 || a->clip_count == CLIPS_MAX) return false;
    for (i = 0; i < a->clip_count; i++) {
        if (strcmp(a->clips[i].name, name) == 0) return false; /* a tag's comes first */
    }
    c = &a->clips[a->clip_count];
    memset(c, 0, sizeof(*c));
    c->frames = (int *)malloc(sizeof(int) * (size_t)count);
    if (c->frames == NULL) return false;
    memcpy(c->frames, frames, sizeof(int) * (size_t)count);
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->count = count;
    c->direction = direction;
    a->clip_count++;
    return true;
}

/* A frame's name split as a numbered run's: its stem ("run" of "run_01.png") into `stem`, its
 * number; false for a name without one. */
static bool numbered(const char *name, char *stem, size_t stem_size, long *number)
{
    size_t end = strlen(name), digits;
    const char *dot = strrchr(name, '.'), *slash = strrchr(name, '/');
    /* its extension off */
    if (dot != NULL && (slash == NULL || dot > slash) && dot != name) end = (size_t)(dot - name);
    digits = end;
    while (digits > 0 && isdigit((unsigned char)name[digits - 1])) digits--;
    if (digits == end) return false;
    *number = strtol(name + digits, NULL, 10);
    while (digits > 0 && strchr(" _-./", name[digits - 1]) != NULL) digits--;
    if (digits == 0 || digits >= stem_size) return false;
    memcpy(stem, name, digits);
    stem[digits] = '\0';
    return true;
}

/* The clips of frames named alike but for a number, each in its numbers' order, in the order
 * their first frames come. */
static void numbered_clips(atlas_t *a)
{
    int i, j, *frames = (int *)malloc(sizeof(int) * (size_t)(a->frame_count + 1));
    long *numbers = (long *)malloc(sizeof(long) * (size_t)(a->frame_count + 1));
    char stem[NAME_MAX], other[NAME_MAX];
    long number;
    if (frames == NULL || numbers == NULL) {
        free(frames);
        free(numbers);
        return;
    }
    for (i = 0; i < a->frame_count; i++) {
        int count = 0, k;
        bool seen = false;
        if (!numbered(a->frames[i].name, stem, sizeof(stem), &number)) continue;
        for (j = 0; j < i && !seen; j++) { /* its run started earlier: made then */
            long n;
            seen = numbered(a->frames[j].name, other, sizeof(other), &n) && strcmp(other, stem) == 0;
        }
        if (seen) continue;
        for (j = i; j < a->frame_count; j++) {
            long n;
            if (!numbered(a->frames[j].name, other, sizeof(other), &n) || strcmp(other, stem) != 0) continue;
            for (k = count; k > 0 && numbers[k - 1] > n; k--) { /* by number */
                numbers[k] = numbers[k - 1];
                frames[k] = frames[k - 1];
            }
            numbers[k] = n;
            frames[k] = j;
            count++;
        }
        add_atlas_clip(a, stem, frames, count, WGF_SHEET_DIRECTION_FORWARD);
    }
    free(frames);
    free(numbers);
}

/* `name` (a file the atlas names) read beside `path` into `out`. */
static void beside(const char *path, const char *name, char *out, size_t size)
{
    const char *slash = strrchr(path, '/');
    if (name[0] == '/' || slash == NULL) snprintf(out, size, "%s", name[0] == '/' ? name + 1 : name);
    else snprintf(out, size, "%.*s/%s", (int)(slash - path), path, name);
}

static void *prepare(const char *path)
{
    unsigned char *bytes = NULL;
    int size = 0, i;
    wgf_core_priv_json_t *json;
    const wgf_core_priv_json_t *frames, *meta, *tags;
    atlas_t *a;
    if (!wgf_core_priv_fs_read(path, &bytes, &size)) return NULL;
    json = wgf_core_priv_json_parse((const char *)bytes, (size_t)size);
    wgf_core_priv_fs_read_free(bytes);
    frames = wgf_core_priv_json_get(json, "frames");
    if (json == NULL || (!wgf_core_priv_json_is_object(frames) && !wgf_core_priv_json_is_array(frames))) {
        wgf_log_warn("wgf_sheet: %s: not a sprite sheet's atlas (JSON with frames)", path);
        wgf_core_priv_json_free(json);
        return NULL;
    }
    a = (atlas_t *)calloc(1, sizeof(atlas_t));
    a->frames = a != NULL ? (frame_t *)calloc((size_t)wgf_core_priv_json_size(frames) + 1, sizeof(frame_t)) : NULL;
    if (a == NULL || a->frames == NULL) {
        if (a != NULL) free(a);
        wgf_core_priv_json_free(json);
        return NULL;
    }
    for (i = 0; i < wgf_core_priv_json_size(frames); i++) { /* a hash's in its text's order, or an array's */
        const wgf_core_priv_json_t *entry = wgf_core_priv_json_at(frames, i);
        char number[16];
        const char *name = wgf_core_priv_json_is_array(frames) ? wgf_core_priv_json_get_string(entry, "filename")
                                                               : wgf_core_priv_json_key(entry);
        if (name[0] == '\0') {
            snprintf(number, sizeof(number), "%d", i);
            name = number;
        }
        if (!read_frame(entry, name, path, &a->frames[a->frame_count])) {
            free_atlas(a);
            wgf_core_priv_json_free(json);
            return NULL;
        }
        a->frame_count++;
    }
    meta = wgf_core_priv_json_get(json, "meta");
    tags = wgf_core_priv_json_get(meta, "frameTags");
    for (i = 0; i < wgf_core_priv_json_size(tags); i++) { /* Aseprite's tags: its clips */
        const wgf_core_priv_json_t *tag = wgf_core_priv_json_at(tags, i);
        const char *direction = wgf_core_priv_json_get_string(tag, "direction");
        const int from = (int)wgf_core_priv_json_get_number(tag, "from", -1);
        const int to = (int)wgf_core_priv_json_get_number(tag, "to", -1);
        int run[1024], n = 0, f;
        if (from < 0 || to >= a->frame_count || from > to || to - from >= 1024) {
            wgf_log_warn("wgf_sheet: %s: tag %s's frames %d to %d aren't the sheet's %d", path,
                         wgf_core_priv_json_get_string(tag, "name"), from, to, a->frame_count);
            continue;
        }
        for (f = from; f <= to; f++) run[n++] = f;
        add_atlas_clip(a, wgf_core_priv_json_get_string(tag, "name"), run, n,
                       strcmp(direction, "reverse") == 0            ? WGF_SHEET_DIRECTION_BACKWARD
                       : strcmp(direction, "pingpong") == 0         ? WGF_SHEET_DIRECTION_PING_PONG
                       : strcmp(direction, "pingpong_reverse") == 0 ? WGF_SHEET_DIRECTION_PING_PONG_BACKWARD
                                                                    : WGF_SHEET_DIRECTION_FORWARD);
    }
    numbered_clips(a);
    if (wgf_core_priv_json_get_string(meta, "image")[0] == '\0') { /* read before the tree is freed */
        wgf_log_warn("wgf_sheet: %s: its meta names no image", path);
        free_atlas(a);
        wgf_core_priv_json_free(json);
        return NULL;
    }
    beside(path, wgf_core_priv_json_get_string(meta, "image"), a->image, sizeof(a->image));
    wgf_core_priv_json_free(json);
    return a;
}

static int resolve(sheet_t *s, const player_t *p, const char *name);
static bool start(player_t *p, sheet_t *s, int i, int step, float elapsed);

/* What the program set on the sheet carried into the atlas read again: a clip it added (in
 * place of the file's of that name, while there is room), and the events it put on the
 * file's clips, by place. */
static void keep(sheet_t *s, atlas_t *a)
{
    int i, j;
    for (i = 0; i < s->clip_count; i++) {
        clip_t *old = &s->clips[i], *now = NULL;
        for (j = 0; j < a->clip_count && now == NULL; j++) {
            if (strcmp(a->clips[j].name, old->name) == 0) now = &a->clips[j];
        }
        if (old->added) {
            if (now != NULL) {
                free_clip(now);
            } else if (a->clip_count < CLIPS_MAX) {
                now = &a->clips[a->clip_count++];
            } else {
                continue;
            }
            *now = *old; /* the atlas's now */
            old->frames = old->events = NULL;
        } else if (now != NULL && old->events != NULL && now->events == NULL) {
            now->events = (int *)calloc((size_t)now->count, sizeof(int));
            if (now->events != NULL) {
                const int count = old->count < now->count ? old->count : now->count;
                memcpy(now->events, old->events, sizeof(int) * (size_t)count);
            }
        }
    }
}

/* The sprites playing a sheet that has loaded again: each on its new texture, and going on in
 * the clip of the name it played, from the same place; stopped where the sheet has it no
 * more. */
static void replay(wgf_sheet_t sheet, sheet_t *s)
{
    int k;
    for (k = 0; k < sh.player_count; k++) {
        player_t *p = &sh.players[k];
        int i;
        if (p->sheet != sheet) continue;
        wgf_sprite_set_texture(p->sprite, s->texture);
        if (p->clip < 0) continue;
        i = resolve(s, p, p->asked);
        if (i >= 0 && start(p, s, i, p->step, p->elapsed)) {
            show(p, false);
        } else {
            p->state = WGF_PLAY_STATE_STOPPED;
            p->clip = -1;
            p->steps = 0;
            p->asked[0] = '\0';
        }
    }
}

/* The atlas made the sheet's, READY: in place of what it held, when it loads again. */
static wgf_core_priv_load_step_t finish(void *prepared, wgf_handle_t resource)
{
    atlas_t *a = (atlas_t *)prepared;
    sheet_t *s = sheet_of(resource);
    wgf_texture_t texture, old;
    int i;
    if (s == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    /* its image, beside it: loading, drawn once READY. Made before the old one is let go of,
     * so the same image stays loaded */
    texture = wgf_texture_create(a->image);
    s = sheet_of(resource); /* a texture made: sheets don't move, but be sure */
    if (s == NULL) {
        wgf_resource_release(texture);
        return WGF_CORE_PRIV_LOAD_FAILED;
    }
    keep(s, a);
    for (i = 0; i < s->clip_count; i++) free_clip(&s->clips[i]);
    free(s->frames);
    old = s->texture;
    s->texture = texture;
    if (old != 0) wgf_resource_release(old);
    s->frames = a->frames;
    s->frame_count = a->frame_count;
    memcpy(s->clips, a->clips, sizeof(a->clips));
    s->clip_count = a->clip_count;
    a->frames = NULL; /* the sheet's now */
    a->clip_count = 0;
    wgf_core_priv_resource_loaded(resource, NULL);
    replay(resource, s);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void discard(void *prepared)
{
    free_atlas((atlas_t *)prepared);
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"sheet", prepare, finish, discard, fail, NULL, true, NULL};

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

static void free_sheet(wgf_handle_t sheet, void *record)
{
    sheet_t *s = (sheet_t *)record;
    int i;
    (void)sheet;
    for (i = 0; i < s->clip_count; i++) free_clip(&s->clips[i]);
    free(s->frames);
    if (s->texture != 0) wgf_resource_release(s->texture);
    s->frames = NULL;
    s->clip_count = 0;
    s->texture = 0;
}

static const wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_sheet_create",
                                                            .loader = loader_of,
                                                            .free = free_sheet};

static bool ensure_started(void)
{
    if (sh.ready) return true;
    sh.ready = wgf_core_priv_handle_pool_init(&sh.pool, WGF_CORE_PRIV_HANDLE_KIND_SHEET, (void **)&sh.sheets,
                                              sizeof(sheet_t), 16, 4096);
    if (!sh.ready) return false;
    wgf_core_priv_resource_register(&sh.pool, &resource_kind);
    if (!sh.installed) {
        wgf_core_priv_part_install(&part);
        sh.installed = true;
    }
    return true;
}

/* ---- the calls ----------------------------------------------------------------------- */

wgf_sheet_t wgf_sheet_create(const char *path)
{
    if (!ensure_started()) return 0;
    return wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_SHEET, path);
}

wgf_sheet_t wgf_sheet_create_grid(wgf_texture_t texture, int frame_width, int frame_height, int count, int margin,
                                  int spacing)
{
    wgf_sheet_t sheet;
    sheet_t *s;
    if (WGF_CORE_PRIV_HANDLE_KIND(texture) != WGF_CORE_PRIV_HANDLE_KIND_TEXTURE ||
        wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_NONE || frame_width <= 0 || frame_height <= 0 ||
        count < 0 || margin < 0 || spacing < 0 || !ensure_started()) {
        return 0;
    }
    sheet = wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_SHEET);
    s = sheet_of(sheet);
    if (s == NULL) return 0;
    wgf_gfx_priv_texture_retain(texture);
    s->texture = texture;
    s->grid = true;
    s->frame_width = frame_width, s->frame_height = frame_height;
    s->count = count, s->margin = margin, s->spacing = spacing;
    cut(s); /* now, if its texture is READY; else when first asked after */
    return sheet;
}

void wgf_sheet_destroy(wgf_sheet_t sheet)
{
    if (sheet_of(sheet) != NULL) wgf_resource_release(sheet);
}

wgf_texture_t wgf_sheet_get_texture(wgf_sheet_t sheet)
{
    const sheet_t *s = sheet_of(sheet);
    return s != NULL ? s->texture : 0;
}

int wgf_sheet_get_frame_count(wgf_sheet_t sheet)
{
    sheet_t *s = sheet_of(sheet);
    if (s == NULL) return 0;
    cut(s);
    return s->frame_count;
}

wgf_vec4_t wgf_sheet_get_frame(wgf_sheet_t sheet, int index)
{
    sheet_t *s = sheet_of(sheet);
    if (s == NULL) return wgf_vec4_make(0.0f, 0.0f, 0.0f, 0.0f);
    cut(s);
    if (index < 0 || index >= s->frame_count) return wgf_vec4_make(0.0f, 0.0f, 0.0f, 0.0f);
    return wgf_vec4_make(s->frames[index].x, s->frames[index].y, s->frames[index].w, s->frames[index].h);
}

const char *wgf_sheet_get_frame_name(wgf_sheet_t sheet, int index)
{
    const sheet_t *s = sheet_of(sheet);
    return s != NULL && index >= 0 && index < s->frame_count ? s->frames[index].name : "";
}

int wgf_sheet_find_frame(wgf_sheet_t sheet, const char *name)
{
    const sheet_t *s = sheet_of(sheet);
    int i;
    if (s == NULL || name == NULL || name[0] == '\0') return -1;
    for (i = 0; i < s->frame_count; i++) {
        if (strcmp(s->frames[i].name, name) == 0) return i;
    }
    return -1;
}

static clip_t *clip_named(sheet_t *s, const char *name)
{
    int i;
    for (i = 0; s != NULL && name != NULL && i < s->clip_count; i++) {
        if (strcmp(s->clips[i].name, name) == 0) return &s->clips[i];
    }
    return NULL;
}

bool wgf_sheet_add_clip(wgf_sheet_t sheet, const char *name, const int *frames, int count, float seconds,
                        wgf_sheet_direction_t direction)
{
    sheet_t *s = sheet_of(sheet);
    clip_t *c;
    int *copy;
    if (s == NULL || name == NULL || name[0] == '\0' || strlen(name) >= NAME_MAX || frames == NULL || count <= 0 ||
        !(seconds >= 0.0f) || !isfinite(seconds) || (int)direction < WGF_SHEET_DIRECTION_FORWARD ||
        direction > WGF_SHEET_DIRECTION_PING_PONG_BACKWARD) {
        return false;
    }
    c = clip_named(s, name);
    if (c == NULL) {
        if (s->clip_count >= CLIPS_MAX) return false;
        c = &s->clips[s->clip_count++];
        memset(c, 0, sizeof(*c));
        strcpy(c->name, name);
    }
    copy = (int *)malloc(sizeof(int) * (size_t)count);
    if (copy == NULL) return false;
    memcpy(copy, frames, sizeof(int) * (size_t)count);
    free_clip(c);
    c->frames = copy;
    c->count = count;
    c->seconds = seconds;
    c->direction = (int)direction;
    c->added = true;
    return true;
}

int wgf_sheet_get_clip_count(wgf_sheet_t sheet)
{
    const sheet_t *s = sheet_of(sheet);
    return s != NULL ? s->clip_count : 0;
}

const char *wgf_sheet_get_clip_name(wgf_sheet_t sheet, int index)
{
    const sheet_t *s = sheet_of(sheet);
    return s != NULL && index >= 0 && index < s->clip_count ? s->clips[index].name : "";
}

int wgf_sheet_find_clip(wgf_sheet_t sheet, const char *name)
{
    sheet_t *s = sheet_of(sheet);
    const clip_t *c = clip_named(s, name);
    return c != NULL ? (int)(c - s->clips) : -1;
}

bool wgf_sheet_set_clip_event(wgf_sheet_t sheet, const char *clip, int place, int event)
{
    clip_t *c = clip_named(sheet_of(sheet), clip);
    if (c == NULL || place < 0 || place >= c->count || event < 0) return false;
    if (c->events == NULL) {
        if (event == 0) return true;
        c->events = (int *)calloc((size_t)c->count, sizeof(int));
        if (c->events == NULL) return false;
    }
    c->events[place] = event;
    return true;
}

int wgf_sheet_get_clip_event(wgf_sheet_t sheet, const char *clip, int place)
{
    const clip_t *c = clip_named(sheet_of(sheet), clip);
    return c != NULL && c->events != NULL && place >= 0 && place < c->count ? c->events[place] : 0;
}

/* ---- a sprite playing --------------------------------------------------------------- */

static bool is_sprite(wgf_actor_t sprite)
{
    return wgf_actor_get_kind(sprite) == WGF_ACTOR_KIND_SPRITE;
}

bool wgf_sprite_set_sheet(wgf_actor_t sprite, wgf_sheet_t sheet)
{
    sheet_t *s = sheet_of(sheet);
    player_t *p;
    if (!is_sprite(sprite) || s == NULL) return false;
    p = player_of(sprite);
    if (p == NULL) {
        if (sh.player_count == sh.player_capacity) {
            const int capacity = sh.player_capacity > 0 ? sh.player_capacity * 2 : 32;
            player_t *grown = (player_t *)realloc(sh.players, sizeof(player_t) * (size_t)capacity);
            if (grown == NULL) return false;
            sh.players = grown;
            sh.player_capacity = capacity;
        }
        p = &sh.players[sh.player_count++];
        memset(p, 0, sizeof(*p));
        p->sprite = sprite;
        p->speed = 1.0f;
    }
    if (p->sheet != sheet) {
        free(p->order);
        p->order = NULL;
        p->clip = -1;
        p->steps = 0;
        p->state = WGF_PLAY_STATE_STOPPED;
    }
    p->sheet = sheet;
    wgf_sprite_set_texture(sprite, s->texture);
    return true;
}

wgf_sheet_t wgf_sprite_get_sheet(wgf_actor_t sprite)
{
    const player_t *p = player_of(sprite);
    return p != NULL && sheet_of(p->sheet) != NULL ? p->sheet : 0;
}

/* The clip `name` plays as, facing as the player faces: name_facing when the sheet has it. */
static int resolve(sheet_t *s, const player_t *p, const char *name)
{
    clip_t *c = NULL;
    if (p->facing[0] != '\0') {
        char faced[NAME_MAX + FACING_MAX + 1];
        snprintf(faced, sizeof(faced), "%s_%s", name, p->facing);
        c = clip_named(s, faced);
    }
    if (c == NULL) c = clip_named(s, name);
    return c != NULL ? (int)(c - s->clips) : -1;
}

/* Clip `i` started for `p`: its order made, at its start (or kept at `step` and `elapsed`, a
 * facing's change). */
static bool start(player_t *p, sheet_t *s, int i, int step, float elapsed)
{
    const clip_t *c = &s->clips[i];
    const int n = c->count;
    int *order = (int *)malloc(sizeof(int) * (size_t)(n * 2)), k;
    if (order == NULL) return false;
    p->steps = 0;
    if (c->direction == WGF_SHEET_DIRECTION_BACKWARD || c->direction == WGF_SHEET_DIRECTION_PING_PONG_BACKWARD) {
        for (k = 0; k < n; k++) order[p->steps++] = n - 1 - k;
        if (c->direction == WGF_SHEET_DIRECTION_PING_PONG_BACKWARD) {
            for (k = 1; k < n - 1; k++) order[p->steps++] = k;
        }
    } else {
        for (k = 0; k < n; k++) order[p->steps++] = k;
        if (c->direction == WGF_SHEET_DIRECTION_PING_PONG) {
            for (k = n - 2; k > 0; k--) order[p->steps++] = k;
        }
    }
    free(p->order);
    p->order = order;
    p->clip = i;
    p->step = step < p->steps ? step : 0;
    p->elapsed = elapsed;
    return true;
}

bool wgf_sprite_play_clip(wgf_actor_t sprite, const char *name, bool loop)
{
    player_t *p = player_of(sprite);
    sheet_t *s = p != NULL ? sheet_of(p->sheet) : NULL;
    int i;
    if (s == NULL || name == NULL || strlen(name) >= NAME_MAX) return false;
    i = resolve(s, p, name);
    if (i < 0) return false;
    p->loop = loop;
    snprintf(p->asked, sizeof(p->asked), "%s", name);
    if (p->clip == i && p->state == WGF_PLAY_STATE_PLAYING) return true; /* already playing: on it goes */
    if (!start(p, s, i, 0, 0.0f)) return false;
    p->state = WGF_PLAY_STATE_PLAYING;
    show(p, true);
    return true;
}

bool wgf_sprite_pause_clip(wgf_actor_t sprite)
{
    player_t *p = player_of(sprite);
    if (p == NULL || p->state != WGF_PLAY_STATE_PLAYING) return false;
    p->state = WGF_PLAY_STATE_PAUSED;
    return true;
}

bool wgf_sprite_resume_clip(wgf_actor_t sprite)
{
    player_t *p = player_of(sprite);
    if (p == NULL || p->state != WGF_PLAY_STATE_PAUSED) return false;
    p->state = WGF_PLAY_STATE_PLAYING;
    return true;
}

bool wgf_sprite_stop_clip(wgf_actor_t sprite)
{
    player_t *p = player_of(sprite);
    if (p == NULL) return is_sprite(sprite);
    p->state = WGF_PLAY_STATE_STOPPED;
    p->clip = -1;
    p->steps = 0;
    p->asked[0] = '\0';
    return true;
}

wgf_play_state_t wgf_sprite_get_clip_state(wgf_actor_t sprite)
{
    const player_t *p = player_of(sprite);
    return p != NULL ? p->state : WGF_PLAY_STATE_STOPPED;
}

const char *wgf_sprite_get_clip(wgf_actor_t sprite)
{
    const player_t *p = player_of(sprite);
    const sheet_t *s = p != NULL ? sheet_of(p->sheet) : NULL;
    return s != NULL && p->clip >= 0 ? s->clips[p->clip].name : "";
}

int wgf_sprite_get_clip_frame(wgf_actor_t sprite)
{
    const player_t *p = player_of(sprite);
    return p != NULL && p->clip >= 0 && p->steps > 0 ? p->order[p->step] : -1;
}

bool wgf_sprite_set_clip_speed(wgf_actor_t sprite, float speed)
{
    player_t *p = player_of(sprite);
    if (p == NULL || !isfinite(speed)) return false;
    p->speed = speed > 0.0f ? speed : 0.0f;
    return true;
}

float wgf_sprite_get_clip_speed(wgf_actor_t sprite)
{
    const player_t *p = player_of(sprite);
    return p != NULL ? p->speed : 0.0f;
}

int wgf_sprite_take_clip_events(wgf_actor_t sprite, int *out, int count)
{
    player_t *p = player_of(sprite);
    int n;
    if (p == NULL || out == NULL || count <= 0) return 0;
    n = p->event_count < count ? p->event_count : count;
    memcpy(out, p->events, sizeof(int) * (size_t)n);
    memmove(p->events, p->events + n, sizeof(int) * (size_t)(p->event_count - n));
    p->event_count -= n;
    return n;
}

bool wgf_sprite_set_clip_facing(wgf_actor_t sprite, const char *facing)
{
    player_t *p = player_of(sprite);
    sheet_t *s = p != NULL ? sheet_of(p->sheet) : NULL;
    int i;
    if (s == NULL || (facing != NULL && strlen(facing) >= FACING_MAX)) return false;
    snprintf(p->facing, sizeof(p->facing), "%s", facing != NULL ? facing : "");
    if (p->clip < 0 || p->asked[0] == '\0') return true;
    i = resolve(s, p, p->asked);
    if (i >= 0 && i != p->clip && start(p, s, i, p->step, p->elapsed)) show(p, false); /* on from the same place */
    return true;
}

const char *wgf_sprite_get_clip_facing(wgf_actor_t sprite)
{
    const player_t *p = player_of(sprite);
    return p != NULL ? p->facing : "";
}
