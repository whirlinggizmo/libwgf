#include "wgf_screen.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_actor.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_draw.h"
#include "wgf_ecs_priv.h"
#include "wgf_log.h"
#include "wgf_render.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"

/* Screens (wgf_screen.h): the stack, each screen known by its actor with its settings, and the
 * change running, moved on each frame by a part on real time (a paused game still changes
 * screens). helix-legacy's Transition, its callbacks replaced by the change itself: the stack
 * knows what it is to do at the middle. Installed by the first screen call, so a program
 * with none links none of it. */

#define STACK_MAX 32
#define STAGES_MAX 8

typedef struct known_t {
    wgf_actor_t actor;
    bool opaque, pausing, wait_loads;
    wgf_asset_task_t assets;
    wgf_actor_t stages[STAGES_MAX];
    int stage_count;
} known_t;

enum { OP_NONE, OP_PUSH, OP_POP, OP_REPLACE };
enum { PHASE_IDLE, PHASE_COVERING, PHASE_WAITING, PHASE_UNCOVERING };

static struct {
    bool installed;
    known_t *known;
    int known_count, known_capacity;
    wgf_actor_t stack[STACK_MAX];
    int count;
    wgf_transition_t kind;
    float seconds;
    wgf_color_t color;
    wgf_easing_t ease;
    /* the change running */
    int op, phase;
    wgf_actor_t target;
    float t;      /* seconds into its phase */
    int peak;     /* loads pending as its wait began */
    bool counted; /* peak taken */
    wgf_actor_t loading;
} sc;

static void install(void);

/* ---- the screens known ---------------------------------------------------------------- */

static known_t *known_of(wgf_actor_t actor)
{
    int i;
    for (i = 0; i < sc.known_count; i++) {
        if (sc.known[i].actor == actor) return &sc.known[i];
    }
    return NULL;
}

/* `actor` known as a screen (its settings at their defaults the first time); NULL for one
 * that isn't an actor, or out of memory. */
static known_t *know(wgf_actor_t actor)
{
    known_t *k;
    if (wgf_gfx_priv_actor_of(actor) == NULL) return NULL;
    k = known_of(actor);
    if (k != NULL) return k;
    if (sc.known_count == sc.known_capacity) {
        const int capacity = sc.known_capacity > 0 ? sc.known_capacity * 2 : 8;
        known_t *grown = (known_t *)realloc(sc.known, sizeof(known_t) * (size_t)capacity);
        if (grown == NULL) {
            wgf_log_error("wgf_screen: out of memory for a screen");
            return NULL;
        }
        sc.known = grown;
        sc.known_capacity = capacity;
    }
    k = &sc.known[sc.known_count++];
    k->actor = actor;
    k->opaque = k->pausing = true;
    k->wait_loads = false;
    k->assets = 0;
    k->stage_count = 0;
    return k;
}

static int index_of(wgf_actor_t actor)
{
    int i;
    for (i = 0; i < sc.count; i++) {
        if (sc.stack[i] == actor) return i;
    }
    return -1;
}

/* A screen and its stages switched on or off, and paused or not. */
static void switch_screen(const known_t *k, bool enabled, bool paused)
{
    int s;
    wgf_actor_set_enabled(k->actor, enabled);
    wgf_actor_set_paused(k->actor, paused);
    for (s = 0; s < k->stage_count; s++) {
        wgf_actor_set_enabled(k->stages[s], enabled);
        wgf_actor_set_paused(k->stages[s], paused);
    }
}

/* Every known screen switched on and paused as the stack says: on it, on, paused under one
 * that pauses; off it, off and paused. Screens gone are forgotten. */
static void refresh(void)
{
    int i, kept = 0;
    for (i = 0; i < sc.known_count; i++) {
        if (wgf_handle_is_alive(sc.known[i].actor)) sc.known[kept++] = sc.known[i];
    }
    sc.known_count = kept;
    for (i = 0; i < sc.known_count; i++) { /* its stages destroyed leave it */
        known_t *k = &sc.known[i];
        int s, left = 0;
        for (s = 0; s < k->stage_count; s++) {
            if (wgf_handle_is_alive(k->stages[s])) k->stages[left++] = k->stages[s];
        }
        k->stage_count = left;
    }
    kept = 0;
    for (i = 0; i < sc.count; i++) { /* a screen destroyed on the stack leaves it */
        if (wgf_handle_is_alive(sc.stack[i])) sc.stack[kept++] = sc.stack[i];
    }
    sc.count = kept;
    for (i = 0; i < sc.known_count; i++) {
        const wgf_actor_t a = sc.known[i].actor;
        const int at = index_of(a);
        bool paused = at < 0;
        int above;
        for (above = at + 1; at >= 0 && above < sc.count && !paused; above++) {
            const known_t *k = known_of(sc.stack[above]);
            paused = k != NULL && k->pausing;
        }
        if (a == sc.loading && sc.phase == PHASE_WAITING && at < 0) paused = false; /* the loading screen, shown */
        switch_screen(&sc.known[i], at >= 0 || !paused, paused);
    }
}

/* ---- changes ------------------------------------------------------------------------- */

static bool instant(void)
{
    return sc.kind == WGF_TRANSITION_NONE || !(sc.seconds > 0.0f);
}

/* Whether the change's target is ready: its assets settled, and no load pending when it waits
 * for them. */
static bool ready(void)
{
    const known_t *k = sc.op == OP_POP ? NULL : known_of(sc.target);
    if (k == NULL) return true;
    if (k->assets != 0 && wgf_asset_task_get_status(k->assets) == WGF_ASSET_TASK_STATUS_PENDING) return false;
    return !k->wait_loads || wgf_core_priv_load_get_pending_count() == 0;
}

/* The change made: the stack as it says, every screen switched as it then says, and the top's
 * change told. */
static void apply(void)
{
    const wgf_actor_t before = sc.count > 0 ? sc.stack[sc.count - 1] : 0;
    wgf_actor_t after;
    if (sc.op == OP_PUSH && sc.count < STACK_MAX) sc.stack[sc.count++] = sc.target;
    else if (sc.op == OP_POP && sc.count > 0) sc.count--;
    else if (sc.op == OP_REPLACE && sc.count > 0) sc.stack[sc.count - 1] = sc.target;
    after = sc.count > 0 ? sc.stack[sc.count - 1] : 0;
    sc.op = OP_NONE;
    sc.target = 0;
    sc.counted = false;
    refresh();
    if (before != after) {
        if (before != 0) wgf_ecs_priv_raise(WGF_WORLD_EVENT_SCREEN_EXIT, (int)before, (int)after, 0);
        if (after != 0) wgf_ecs_priv_raise(WGF_WORLD_EVENT_SCREEN_ENTER, (int)after, (int)before, 0);
    }
}

/* Asked for: the change starts covering, or, at once, is made now when its target is ready
 * (else it waits, uncovered, with the loading screen over everything). */
static bool request(int op, wgf_actor_t target)
{
    if (sc.phase != PHASE_IDLE) return false;
    sc.op = op;
    sc.target = target;
    sc.t = 0.0f;
    sc.counted = false;
    if (!instant()) {
        sc.phase = PHASE_COVERING;
        return true;
    }
    if (ready()) {
        apply();
        return true;
    }
    sc.phase = PHASE_WAITING;
    refresh();
    return true;
}

bool wgf_screen_push(wgf_actor_t screen)
{
    install();
    if (know(screen) == NULL || index_of(screen) >= 0 || sc.count == STACK_MAX) return false;
    return request(OP_PUSH, screen);
}

bool wgf_screen_pop(void)
{
    install();
    refresh();
    if (sc.count < 2) return false;
    return request(OP_POP, 0);
}

bool wgf_screen_replace(wgf_actor_t screen)
{
    install();
    refresh();
    if (sc.count == 0 || know(screen) == NULL || index_of(screen) >= 0) return false;
    return request(OP_REPLACE, screen);
}

/* ---- the part ------------------------------------------------------------------------ */

static void update(float dt)
{
    const float half = sc.seconds * 0.5f;
    if (sc.phase == PHASE_IDLE) return;
    sc.t += dt;
    if (sc.phase == PHASE_COVERING && sc.t >= half) {
        sc.phase = PHASE_WAITING;
        sc.t = 0.0f;
    }
    if (sc.phase == PHASE_WAITING) {
        if (!sc.counted) {
            sc.peak = wgf_core_priv_load_get_pending_count();
            sc.counted = true;
            refresh(); /* the loading screen shown, while it waits */
        }
        if (!ready()) return;
        apply();
        sc.phase = instant() ? PHASE_IDLE : PHASE_UNCOVERING;
        sc.t = 0.0f;
        refresh(); /* the loading screen put away */
    } else if (sc.phase == PHASE_UNCOVERING && sc.t >= half) {
        sc.phase = PHASE_IDLE;
        sc.t = 0.0f;
    }
}

static void stop(void)
{
    free(sc.known);
    memset(&sc, 0, sizeof(sc));
}

static wgf_core_priv_part_t part = {.name = "screens",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_SCREENS,
                                    .update = update,
                                    .stop = stop};

static void install(void)
{
    if (sc.installed) return;
    wgf_ecs_priv_start(); /* its events are the world's */
    sc.color = wgf_color_make(0, 0, 0, 255);
    sc.ease = WGF_EASING_LINEAR;
    sc.installed = true;
    wgf_core_priv_part_install(&part);
}

/* ---- the calls ----------------------------------------------------------------------- */

wgf_actor_t wgf_screen_create(void)
{
    const wgf_actor_t actor = wgf_actor_create();
    install();
    if (actor == 0) return 0;
    if (know(actor) == NULL) {
        wgf_actor_destroy(actor, WGF_ACTOR_DESTROY_CHILDREN);
        return 0;
    }
    refresh(); /* off the stack: off */
    return actor;
}

bool wgf_screen_add_stage(wgf_actor_t screen, wgf_actor_t stage)
{
    known_t *k = known_of(screen);
    const wgf_actor_kind_t kind = wgf_actor_get_kind(stage);
    int i;
    if (k == NULL || (kind != WGF_ACTOR_KIND_STAGE2D && kind != WGF_ACTOR_KIND_STAGE3D) ||
        k->stage_count == STAGES_MAX) {
        return false;
    }
    for (i = 0; i < k->stage_count; i++) {
        if (k->stages[i] == stage) return false;
    }
    k->stages[k->stage_count++] = stage;
    refresh(); /* switched as its screen is */
    return true;
}

bool wgf_screen_remove_stage(wgf_actor_t screen, wgf_actor_t stage)
{
    known_t *k = known_of(screen);
    int i;
    for (i = 0; k != NULL && i < k->stage_count; i++) {
        if (k->stages[i] != stage) continue;
        memmove(&k->stages[i], &k->stages[i + 1], sizeof(wgf_actor_t) * (size_t)(k->stage_count - i - 1));
        k->stage_count--;
        return true;
    }
    return false;
}

int wgf_screen_get_stage_count(wgf_actor_t screen)
{
    const known_t *k = known_of(screen);
    refresh();
    return k != NULL ? k->stage_count : 0;
}

wgf_actor_t wgf_screen_get_stage(wgf_actor_t screen, int index)
{
    const known_t *k = known_of(screen);
    refresh();
    return k != NULL && index >= 0 && index < k->stage_count ? k->stages[index] : 0;
}

wgf_actor_t wgf_screen_get_top(void)
{
    refresh();
    return sc.count > 0 ? sc.stack[sc.count - 1] : 0;
}

int wgf_screen_get_count(void)
{
    refresh();
    return sc.count;
}

wgf_actor_t wgf_screen_get(int index)
{
    refresh();
    return index >= 0 && index < sc.count ? sc.stack[index] : 0;
}

bool wgf_screen_set_opaque(wgf_actor_t screen, bool opaque)
{
    known_t *k;
    install();
    k = know(screen);
    if (k == NULL) return false;
    k->opaque = opaque;
    return true;
}

bool wgf_screen_is_opaque(wgf_actor_t screen)
{
    const known_t *k = known_of(screen);
    return k != NULL && k->opaque;
}

bool wgf_screen_set_pausing(wgf_actor_t screen, bool pausing)
{
    known_t *k;
    install();
    k = know(screen);
    if (k == NULL) return false;
    k->pausing = pausing;
    refresh();
    return true;
}

bool wgf_screen_is_pausing(wgf_actor_t screen)
{
    const known_t *k = known_of(screen);
    return k != NULL && k->pausing;
}

bool wgf_screen_set_assets(wgf_actor_t screen, wgf_asset_task_t task)
{
    known_t *k;
    install();
    k = know(screen);
    if (k == NULL || (task != 0 && wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_NONE)) return false;
    k->assets = task;
    return true;
}

wgf_asset_task_t wgf_screen_get_assets(wgf_actor_t screen)
{
    const known_t *k = known_of(screen);
    return k != NULL ? k->assets : 0;
}

bool wgf_screen_set_wait_loads(wgf_actor_t screen, bool wait)
{
    known_t *k;
    install();
    k = know(screen);
    if (k == NULL) return false;
    k->wait_loads = wait;
    return true;
}

bool wgf_screen_is_wait_loads(wgf_actor_t screen)
{
    const known_t *k = known_of(screen);
    return k != NULL && k->wait_loads;
}

bool wgf_screen_set_loading_screen(wgf_actor_t screen)
{
    install();
    if (screen != 0 && know(screen) == NULL) return false;
    sc.loading = screen;
    refresh();
    return true;
}

wgf_actor_t wgf_screen_get_loading_screen(void)
{
    return sc.loading;
}

float wgf_screen_get_load_progress(void)
{
    const known_t *k = sc.phase == PHASE_WAITING && sc.op != OP_POP ? known_of(sc.target) : NULL;
    int pending;
    if (sc.phase != PHASE_WAITING) return 1.0f;
    if (k != NULL && k->assets != 0) return wgf_asset_task_get_progress(k->assets);
    pending = wgf_core_priv_load_get_pending_count();
    if (!sc.counted || sc.peak <= 0) return pending == 0 ? 1.0f : 0.0f;
    return pending >= sc.peak ? 0.0f : 1.0f - (float)pending / (float)sc.peak;
}

bool wgf_screen_set_transition(wgf_transition_t kind, float seconds)
{
    install();
    if ((int)kind < WGF_TRANSITION_NONE || kind > WGF_TRANSITION_IRIS || !isfinite(seconds)) return false;
    sc.kind = kind;
    sc.seconds = seconds > 0.0f ? seconds : 0.0f;
    return true;
}

wgf_transition_t wgf_screen_get_transition(void)
{
    return sc.kind;
}

float wgf_screen_get_transition_seconds(void)
{
    return sc.seconds;
}

void wgf_screen_set_transition_color(wgf_color_t color)
{
    install();
    sc.color = color;
}

wgf_color_t wgf_screen_get_transition_color(void)
{
    return sc.installed ? sc.color : wgf_color_make(0, 0, 0, 255);
}

bool wgf_screen_set_transition_ease(wgf_easing_t ease)
{
    install();
    if ((int)ease < WGF_EASING_LINEAR || ease > WGF_EASING_BOUNCE_IN_OUT) return false;
    sc.ease = ease;
    return true;
}

wgf_easing_t wgf_screen_get_transition_ease(void)
{
    return sc.ease;
}

bool wgf_screen_is_changing(void)
{
    return sc.phase != PHASE_IDLE;
}

/* How much of the frame the cover hides, 0 to 1. */
static float covered(void)
{
    const float half = sc.seconds * 0.5f;
    const float u = half > 0.0f ? fminf(sc.t / half, 1.0f) : 1.0f;
    if (sc.phase == PHASE_COVERING) return wgf_ease_value(sc.ease, u);
    if (sc.phase == PHASE_WAITING) return instant() ? 0.0f : 1.0f;
    if (sc.phase == PHASE_UNCOVERING) return 1.0f - wgf_ease_value(sc.ease, u);
    return 0.0f;
}

float wgf_screen_get_change_progress(void)
{
    const float c = covered();
    if (sc.phase == PHASE_COVERING) return 0.5f * c;
    if (sc.phase == PHASE_WAITING) return 0.5f;
    if (sc.phase == PHASE_UNCOVERING) return 1.0f - 0.5f * c;
    return 0.0f;
}

/* ---- drawing ------------------------------------------------------------------------- */

/* `screen`'s stages, in their order. */
static void draw_screen(wgf_actor_t screen)
{
    const known_t *k = known_of(screen);
    int i;
    for (i = 0; k != NULL && i < k->stage_count; i++) {
        const wgf_actor_t stage = k->stages[i];
        if (wgf_actor_get_kind(stage) == WGF_ACTOR_KIND_STAGE2D) wgf_stage2d_draw(stage);
        else wgf_stage3d_draw(stage);
    }
}

/* The cover, `c` of the frame (`w` by `h` logical pixels) hidden as the kind does it. */
static void draw_cover(float c, float w, float h)
{
    const wgf_color_t color = sc.color;
    const bool in = sc.phase != PHASE_UNCOVERING; /* coming on, not going off */
    if (c <= 0.0f) return;
    switch (sc.kind) {
    case WGF_TRANSITION_FADE: {
        const int alpha = (int)lroundf((float)wgf_color_get_alpha(color) * c);
        wgf_draw_rectangle(0, 0, w, h, (color & 0x00FFFFFFu) | ((wgf_color_t)alpha << 24));
        break;
    }
    case WGF_TRANSITION_SLIDE_LEFT: wgf_draw_rectangle(in ? w * (1.0f - c) : -w * (1.0f - c), 0, w, h, color); break;
    case WGF_TRANSITION_SLIDE_RIGHT: wgf_draw_rectangle(in ? -w * (1.0f - c) : w * (1.0f - c), 0, w, h, color); break;
    case WGF_TRANSITION_SLIDE_UP: wgf_draw_rectangle(0, in ? h * (1.0f - c) : -h * (1.0f - c), w, h, color); break;
    case WGF_TRANSITION_SLIDE_DOWN: wgf_draw_rectangle(0, in ? -h * (1.0f - c) : h * (1.0f - c), w, h, color); break;
    case WGF_TRANSITION_WIPE: wgf_draw_rectangle(in ? 0 : w * (1.0f - c), 0, w * c, h, color); break;
    case WGF_TRANSITION_IRIS: {
        const float reach = sqrtf(w * w + h * h) * 0.5f + 2.0f;
        if (c >= 1.0f) wgf_draw_rectangle(0, 0, w, h, color);
        else wgf_draw_ring(w * 0.5f, h * 0.5f, reach * (1.0f - c), reach, 0.0f, 6.2831853f, color);
        break;
    }
    default: break;
    }
}

void wgf_screen_draw(void)
{
    const float dpi = wgf_render_get_dpi_scale() > 0.0f ? wgf_render_get_dpi_scale() : 1.0f;
    int first, i;
    refresh();
    for (first = sc.count - 1; first > 0; first--) { /* from the highest opaque one up */
        const known_t *k = known_of(sc.stack[first]);
        if (k == NULL || k->opaque) break;
    }
    for (i = first < 0 ? 0 : first; i < sc.count; i++) draw_screen(sc.stack[i]);
    draw_cover(covered(), (float)wgf_render_get_width() / dpi, (float)wgf_render_get_height() / dpi);
    if (sc.phase == PHASE_WAITING && sc.loading != 0 && wgf_handle_is_alive(sc.loading)) draw_screen(sc.loading);
}
