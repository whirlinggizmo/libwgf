/* Tweens (wgf_tween.h): helix-legacy's Tweens, as handles in a pool, stepped by a part of
 * their own on the tick clock (installed by the first tween), after the ecs's systems. */
#include "wgf_tween.h"

#include <math.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"

typedef enum kind_t { KIND_VALUE, KIND_MOVE, KIND_TURN, KIND_SCALE } kind_t;

typedef struct tween_t {
    kind_t kind;
    wgf_actor_t actor;
    float from[3], to[3];
    float seconds, delay, elapsed, value;
    wgf_easing_t ease;
    int repeat, repeat_set; /* runs left after this one (-1 for ever), and as set */
    bool yoyo, backward, begun, paused;
} tween_t;

static struct {
    bool ready;
    wgf_core_priv_handle_pool_t pool;
    tween_t *tweens;
} tw;

static tween_t *tween_of_at(wgf_tween_t tween, const char *caller)
{
    uint16_t index;
    if (!tw.ready || !wgf_core_priv_handle_pool_resolve_at(&tw.pool, tween, &index, caller)) return NULL;
    return &tw.tweens[index];
}
#define tween_of(tween) tween_of_at((tween), WGF_CORE_PRIV_CALLER)

/* The actor's value a tween of `kind` moves, into `out`. */
static void read_actor(kind_t kind, wgf_actor_t actor, float out[3])
{
    const wgf_vec3_t v = kind == KIND_MOVE   ? wgf_actor_get_position(actor)
                         : kind == KIND_TURN ? wgf_actor_get_rotation(actor)
                                             : wgf_actor_get_scale(actor);
    out[0] = v.x, out[1] = v.y, out[2] = v.z;
}

static void write_actor(kind_t kind, wgf_actor_t actor, const float v[3])
{
    if (kind == KIND_MOVE) wgf_actor_set_position(actor, v[0], v[1], v[2]);
    else if (kind == KIND_TURN) wgf_actor_set_rotation(actor, v[0], v[1], v[2]);
    else wgf_actor_set_scale(actor, v[0], v[1], v[2]);
}

/* Where it is at its elapsed time, written; false once its runs are done. */
static bool apply(tween_t *t)
{
    const float share = wgf_ease_value(t->ease, t->elapsed / t->seconds);
    const float progress = t->backward ? 1.0f - share : share;
    int i;
    t->value = t->kind == KIND_VALUE ? t->from[0] + (t->to[0] - t->from[0]) * progress : progress;
    if (t->kind != KIND_VALUE) {
        float v[3];
        for (i = 0; i < 3; i++) v[i] = t->from[i] + (t->to[i] - t->from[i]) * progress;
        write_actor(t->kind, t->actor, v);
    }
    return true;
}

static void tick(float dt)
{
    uint16_t i;
    if (!tw.ready) return;
    for (i = 1; i < tw.pool.capacity; i++) {
        const wgf_tween_t handle = wgf_core_priv_handle_pool_handle_from_index(&tw.pool, i);
        tween_t *t = &tw.tweens[i];
        float step = dt;
        if (handle == 0 || t->paused) continue;
        if (t->kind != KIND_VALUE && !wgf_handle_is_alive(t->actor)) { /* its actor gone: it goes too */
            wgf_core_priv_handle_pool_free(&tw.pool, handle);
            continue;
        }
        if (t->kind != KIND_VALUE && wgf_gfx_priv_actor_is_frozen(t->actor)) continue; /* its actor paused */
        if (t->delay > 0.0f) {
            t->delay -= step;
            if (t->delay > 0.0f) continue;
            step = -t->delay;
            t->delay = 0.0f;
        }
        if (!t->begun) {
            if (t->kind != KIND_VALUE) read_actor(t->kind, t->actor, t->from);
            t->begun = true;
        }
        t->elapsed += step;
        while (t->elapsed >= t->seconds) {
            if (t->repeat == 0) {
                t->elapsed = t->seconds;
                apply(t);
                wgf_core_priv_handle_pool_free(&tw.pool, handle); /* ended: let go of */
                break;
            }
            if (t->repeat > 0) t->repeat--;
            t->elapsed -= t->seconds;
            if (t->yoyo) t->backward = !t->backward;
        }
        if (wgf_core_priv_handle_pool_handle_from_index(&tw.pool, i) == handle) apply(t);
    }
}

static void stop(void)
{
    if (tw.ready) wgf_core_priv_handle_pool_destroy(&tw.pool);
    memset(&tw, 0, sizeof(tw));
}

static wgf_core_priv_part_t part = {.name = "tweens",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_TWEENS,
                                    .tick = tick,
                                    .stop = stop};

static wgf_tween_t make(kind_t kind, wgf_actor_t actor, float x, float y, float z, float to_or_x, float seconds,
                        wgf_easing_t easing)
{
    wgf_tween_t tween;
    tween_t *t;
    if (!(seconds > 0.0f) || !isfinite(seconds)) return 0;
    if (kind != KIND_VALUE && wgf_actor_get_kind(actor) == WGF_ACTOR_KIND_NONE) return 0;
    if (!tw.ready) {
        tw.ready = wgf_core_priv_handle_pool_init(&tw.pool, WGF_CORE_PRIV_HANDLE_KIND_TWEEN, (void **)&tw.tweens,
                                                  sizeof(tween_t), 64, 4096);
        if (!tw.ready) return 0;
        wgf_core_priv_part_install(&part);
    }
    tween = wgf_core_priv_handle_pool_alloc(&tw.pool);
    t = tween_of(tween);
    if (t == NULL) return 0;
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    t->actor = actor;
    t->seconds = seconds;
    t->ease = easing;
    if (kind == KIND_VALUE) {
        t->from[0] = x;
        t->to[0] = to_or_x;
        t->value = x;
    } else {
        t->to[0] = x, t->to[1] = y, t->to[2] = z;
    }
    return tween;
}

wgf_tween_t wgf_tween_create(float from, float to, float seconds, wgf_easing_t easing)
{
    if (!isfinite(from) || !isfinite(to)) return 0;
    return make(KIND_VALUE, 0, from, 0.0f, 0.0f, to, seconds, easing);
}

wgf_tween_t wgf_tween_move(wgf_actor_t actor, float x, float y, float z, float seconds, wgf_easing_t easing)
{
    return make(KIND_MOVE, actor, x, y, z, 0.0f, seconds, easing);
}

wgf_tween_t wgf_tween_turn(wgf_actor_t actor, float x, float y, float z, float seconds, wgf_easing_t easing)
{
    return make(KIND_TURN, actor, x, y, z, 0.0f, seconds, easing);
}

wgf_tween_t wgf_tween_scale(wgf_actor_t actor, float x, float y, float z, float seconds, wgf_easing_t easing)
{
    return make(KIND_SCALE, actor, x, y, z, 0.0f, seconds, easing);
}

void wgf_tween_destroy(wgf_tween_t tween)
{
    if (tween_of(tween) != NULL) wgf_core_priv_handle_pool_free(&tw.pool, tween);
}

float wgf_tween_get_value(wgf_tween_t tween)
{
    const tween_t *t = tween_of(tween);
    return t != NULL ? t->value : 0.0f;
}

bool wgf_tween_set_delay(wgf_tween_t tween, float seconds)
{
    tween_t *t = tween_of(tween);
    if (t == NULL || t->begun || !(seconds >= 0.0f) || !isfinite(seconds)) return false;
    t->delay = seconds;
    return true;
}

float wgf_tween_get_delay(wgf_tween_t tween)
{
    const tween_t *t = tween_of(tween);
    return t != NULL ? t->delay : 0.0f;
}

bool wgf_tween_set_repeat(wgf_tween_t tween, int count)
{
    tween_t *t = tween_of(tween);
    if (t == NULL || count < -1) return false;
    t->repeat = t->repeat_set = count;
    return true;
}

int wgf_tween_get_repeat(wgf_tween_t tween)
{
    const tween_t *t = tween_of(tween);
    return t != NULL ? t->repeat_set : 0;
}

bool wgf_tween_set_yoyo(wgf_tween_t tween, bool yoyo)
{
    tween_t *t = tween_of(tween);
    if (t == NULL) return false;
    t->yoyo = yoyo;
    return true;
}

bool wgf_tween_is_yoyo(wgf_tween_t tween)
{
    const tween_t *t = tween_of(tween);
    return t != NULL && t->yoyo;
}

bool wgf_tween_set_paused(wgf_tween_t tween, bool paused)
{
    tween_t *t = tween_of(tween);
    if (t == NULL) return false;
    t->paused = paused;
    return true;
}

bool wgf_tween_is_paused(wgf_tween_t tween)
{
    const tween_t *t = tween_of(tween);
    return t != NULL && t->paused;
}
