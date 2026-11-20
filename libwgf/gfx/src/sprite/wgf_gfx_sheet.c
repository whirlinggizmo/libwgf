/* Sprite sheets and 2D animation (wgf_sheet.h): helix-legacy's SpriteSheet2D and
 * SpriteAnimator2D. Sheets are handles in a pool; what each sprite plays is a record in a
 * table of its own (the sprite's record keeps nothing of it), stepped once a frame by a part
 * installed by the first clip played, which sets the sprite's region (wgf_sprite_set_source)
 * as its frames go by. */
#include "wgf_sheet.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "texture/wgf_gfx_texture_priv.h"

#define CLIPS_MAX 64
#define NAME_MAX 32

typedef struct clip_t {
    char name[NAME_MAX];
    int *frames, count;
    float seconds;
    int direction;
} clip_t;

typedef struct sheet_t {
    wgf_texture_t texture;
    int frame_width, frame_height, count, margin, spacing;
    float (*frames)[4]; /* cut once its texture is READY (a count of 0 waits for its size) */
    int frame_count;
    clip_t clips[CLIPS_MAX];
    int clip_count;
} sheet_t;

typedef struct player_t {
    wgf_actor_t sprite;
    wgf_sheet_t sheet;
    int clip;     /* -1 none */
    int *order;   /* the clip's frames in the order shown, by place in the clip */
    int steps, step;
    float elapsed, speed;
    bool loop;
    wgf_play_state_t state;
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

/* Its frames cut from its texture's size, once it is READY. */
static void cut(sheet_t *s)
{
    const int tw = wgf_texture_get_width(s->texture), th = wgf_texture_get_height(s->texture);
    int columns, rows, n, i;
    if (s->frames != NULL || tw <= 0 || th <= 0) return;
    columns = (tw - 2 * s->margin + s->spacing) / (s->frame_width + s->spacing);
    rows = (th - 2 * s->margin + s->spacing) / (s->frame_height + s->spacing);
    if (columns <= 0 || rows <= 0) return;
    n = s->count > 0 ? s->count : columns * rows;
    s->frames = (float(*)[4])malloc(sizeof(float[4]) * (size_t)n);
    if (s->frames == NULL) return;
    for (i = 0; i < n; i++) {
        const int c = i % columns, r = i / columns;
        s->frames[i][0] = (float)(s->margin + c * (s->frame_width + s->spacing));
        s->frames[i][1] = (float)(s->margin + r * (s->frame_height + s->spacing));
        s->frames[i][2] = (float)s->frame_width;
        s->frames[i][3] = (float)s->frame_height;
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

/* The sprite's region set to the frame showing. */
static void show(player_t *p)
{
    sheet_t *s = sheet_of(p->sheet);
    int frame;
    if (s == NULL || p->clip < 0 || p->steps == 0) return;
    cut(s);
    frame = s->clips[p->clip].frames[p->order[p->step]];
    if (frame < 0 || frame >= s->frame_count) return;
    wgf_sprite_set_source(p->sprite, s->frames[frame][0], s->frames[frame][1], s->frames[frame][2], s->frames[frame][3]);
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
        while (p->elapsed >= s->clips[p->clip].seconds) {
            p->elapsed -= s->clips[p->clip].seconds;
            if (p->step + 1 < p->steps) {
                p->step++;
            } else if (p->loop) {
                p->step = 0;
            } else {
                p->state = WGF_PLAY_STATE_COMPLETE;
                p->elapsed = 0.0f;
                break;
            }
        }
        show(p);
    }
}

static void stop(void)
{
    uint16_t i;
    if (sh.ready) {
        for (i = 1; i < sh.pool.capacity; i++) {
            const wgf_sheet_t handle = wgf_core_priv_handle_pool_handle_from_index(&sh.pool, i);
            if (handle != 0) wgf_sheet_destroy(handle);
        }
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

static void install(void)
{
    if (sh.installed) return;
    wgf_core_priv_part_install(&part);
    sh.installed = true;
}

wgf_sheet_t wgf_sheet_create_grid(wgf_texture_t texture, int frame_width, int frame_height, int count, int margin,
                                  int spacing)
{
    wgf_sheet_t sheet;
    sheet_t *s;
    if (WGF_CORE_PRIV_HANDLE_KIND(texture) != WGF_CORE_PRIV_HANDLE_KIND_TEXTURE ||
        wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_NONE || frame_width <= 0 || frame_height <= 0 ||
        count < 0 || margin < 0 || spacing < 0) {
        return 0;
    }
    if (!sh.ready) {
        sh.ready = wgf_core_priv_handle_pool_init(&sh.pool, WGF_CORE_PRIV_HANDLE_KIND_SHEET, (void **)&sh.sheets,
                                                  sizeof(sheet_t), 16, 4096);
        if (!sh.ready) return 0;
        install();
    }
    sheet = wgf_core_priv_handle_pool_alloc(&sh.pool);
    s = sheet_of(sheet);
    if (s == NULL) return 0;
    memset(s, 0, sizeof(*s));
    wgf_gfx_priv_texture_retain(texture);
    s->texture = texture;
    s->frame_width = frame_width, s->frame_height = frame_height;
    s->count = count, s->margin = margin, s->spacing = spacing;
    cut(s); /* now, if its texture is READY; else when first asked after */
    return sheet;
}

void wgf_sheet_destroy(wgf_sheet_t sheet)
{
    sheet_t *s = sheet_of(sheet);
    int i;
    if (s == NULL) return;
    for (i = 0; i < s->clip_count; i++) free(s->clips[i].frames);
    free(s->frames);
    wgf_resource_release(s->texture);
    wgf_core_priv_handle_pool_free(&sh.pool, sheet);
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
    return wgf_vec4_make(s->frames[index][0], s->frames[index][1], s->frames[index][2], s->frames[index][3]);
}

bool wgf_sheet_add_clip(wgf_sheet_t sheet, const char *name, const int *frames, int count, float seconds,
                        wgf_sheet_direction_t direction)
{
    sheet_t *s = sheet_of(sheet);
    clip_t *c = NULL;
    int *copy, i;
    if (s == NULL || name == NULL || name[0] == '\0' || strlen(name) >= NAME_MAX || frames == NULL || count <= 0 ||
        !(seconds > 0.0f) || !isfinite(seconds) || (int)direction < WGF_SHEET_DIRECTION_FORWARD ||
        direction > WGF_SHEET_DIRECTION_PING_PONG) {
        return false;
    }
    for (i = 0; i < s->clip_count && c == NULL; i++) {
        if (strcmp(s->clips[i].name, name) == 0) c = &s->clips[i];
    }
    if (c == NULL) {
        if (s->clip_count >= CLIPS_MAX) return false;
        c = &s->clips[s->clip_count++];
        memset(c, 0, sizeof(*c));
        strcpy(c->name, name);
    }
    copy = (int *)malloc(sizeof(int) * (size_t)count);
    if (copy == NULL) return false;
    memcpy(copy, frames, sizeof(int) * (size_t)count);
    free(c->frames);
    c->frames = copy;
    c->count = count;
    c->seconds = seconds;
    c->direction = (int)direction;
    return true;
}

int wgf_sheet_get_clip_count(wgf_sheet_t sheet)
{
    const sheet_t *s = sheet_of(sheet);
    return s != NULL ? s->clip_count : 0;
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

bool wgf_sprite_play_clip(wgf_actor_t sprite, const char *name, bool loop)
{
    player_t *p = player_of(sprite);
    sheet_t *s = p != NULL ? sheet_of(p->sheet) : NULL;
    const clip_t *c;
    int i, n, *order;
    if (s == NULL || name == NULL) return false;
    for (i = 0; i < s->clip_count && strcmp(s->clips[i].name, name) != 0; i++) {
    }
    if (i == s->clip_count) return false;
    p->loop = loop;
    if (p->clip == i && p->state == WGF_PLAY_STATE_PLAYING) return true; /* already playing: on it goes */
    c = &s->clips[i];
    n = c->count;
    order = (int *)malloc(sizeof(int) * (size_t)(n * 2));
    if (order == NULL) return false;
    p->steps = 0;
    if (c->direction == WGF_SHEET_DIRECTION_BACKWARD) {
        for (int k = 0; k < n; k++) order[p->steps++] = n - 1 - k;
    } else {
        for (int k = 0; k < n; k++) order[p->steps++] = k;
        if (c->direction == WGF_SHEET_DIRECTION_PING_PONG) {
            for (int k = n - 2; k > 0; k--) order[p->steps++] = k;
        }
    }
    free(p->order);
    p->order = order;
    p->clip = i;
    p->step = 0;
    p->elapsed = 0.0f;
    p->state = WGF_PLAY_STATE_PLAYING;
    show(p);
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
