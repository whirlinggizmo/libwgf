#include "wgf_pointer.h"

#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_log.h"
#include "wgf_mouse.h"
#include "wgf_platform_input_priv.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"

/* The pointer over the interactive stages' actors: libwgt's pointer (wgt_gfx_pointer.c),
 * over libwgf's stages, which stack by the order they were drawn on the screen in the last
 * frame. A part of its own, installed by the first stage made interactive: picked at the
 * pointer as a frame begins, before its ticks (begin_frame), its edges kept for the frame and
 * for the ticks, each set cleared as input's are (end_frame, and after each tick). */

#define MAX_EDGE_ACTORS 8 /* entered or left in one read; past that, dropped */

typedef struct edges_t {
    wgf_actor_t entered[MAX_EDGE_ACTORS], left[MAX_EDGE_ACTORS];
    int entered_count, left_count;
    wgf_actor_t pressed, released, clicked;
} edges_t;

static struct {
    bool installed;
    wgf_actor_t *stages; /* the interactive ones */
    int count, capacity;
    wgf_actor_t hovered;
    wgf_actor_t press_target; /* what the held press started on; 0 for none */
    bool press_down;
    bool releasing; /* let go this frame: captured through it */
    edges_t frame, tick;
} pointer;

static int find(wgf_actor_t stage)
{
    int i;
    for (i = 0; i < pointer.count; i++) {
        if (pointer.stages[i] == stage) return i;
    }
    return -1;
}

/* Where a stage was drawn in the last frame; -1 when it wasn't, or is gone. */
static int drawn_order(wgf_actor_t stage)
{
    const wgf_gfx_priv_actor_t *stage_ptr = wgf_gfx_priv_actor_of_silently(stage);
    const unsigned last = wgf_gfx_priv_render_get_last_frame();
    if (stage_ptr == NULL || last == 0) return -1;
    if (stage_ptr->type == WGF_ACTOR_KIND_STAGE2D) {
        return stage_ptr->as.stage2d.drawn_frame == last ? stage_ptr->as.stage2d.drawn_order : -1;
    }
    if (stage_ptr->type == WGF_ACTOR_KIND_STAGE3D) {
        return stage_ptr->as.stage3d.drawn_frame == last ? stage_ptr->as.stage3d.drawn_order : -1;
    }
    return -1;
}

/* What is under (x, y), among the interactive stages drawn last frame, the last drawn
 * first; `*in_2d` says whether it was a 2D stage's. Stages gone leave the list. */
static wgf_actor_t pick(float x, float y, bool *in_2d)
{
    int i = 0, best_order;
    *in_2d = false;
    while (i < pointer.count) {
        const wgf_actor_kind_t kind = wgf_actor_get_kind(pointer.stages[i]);
        if (kind != WGF_ACTOR_KIND_STAGE2D && kind != WGF_ACTOR_KIND_STAGE3D) {
            pointer.stages[i] = pointer.stages[--pointer.count];
        } else {
            i++;
        }
    }
    best_order = 1 << 30;
    for (;;) { /* the stages from the last drawn down: few, so a pass for each */
        wgf_actor_t stage = 0, hit;
        int order = -1;
        for (i = 0; i < pointer.count; i++) {
            const int o = drawn_order(pointer.stages[i]);
            if (o > order && o < best_order) {
                order = o;
                stage = pointer.stages[i];
            }
        }
        if (stage == 0) return 0;
        best_order = order;
        if (wgf_actor_get_kind(stage) == WGF_ACTOR_KIND_STAGE2D) {
            hit = wgf_stage2d_pick(stage, x, y, NULL, 0);
            if (hit != 0) *in_2d = true;
        } else {
            hit = wgf_stage3d_pick(stage, x, y, NULL, 0);
        }
        if (hit != 0) return hit;
    }
}

static void add_edge(wgf_actor_t *list, int *count, wgf_actor_t actor)
{
    if (*count < MAX_EDGE_ACTORS) list[(*count)++] = actor;
}

static void begin_frame(void)
{
    const wgf_vec2_t at = wgf_mouse_get_position();
    const bool ui = wgf_platform_priv_input_is_ui_pointer_captured();
    const bool pressed = wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT);
    const bool released = wgf_mouse_is_released(WGF_MOUSE_BUTTON_LEFT);
    wgf_actor_t hit = 0, entered = 0, left = 0, pressed_on = 0, released_on = 0, clicked_on = 0;
    bool in_2d = false;
    edges_t *sets[2];
    int s;
    if (pointer.releasing) { /* captured through the frame it was let go in */
        wgf_platform_priv_input_set_pointer_held(false);
        pointer.releasing = false;
    }
    if (pointer.press_target != 0 && wgf_gfx_priv_actor_of_silently(pointer.press_target) == NULL) {
        pointer.press_target = 0; /* destroyed while pressed: nothing to release */
        wgf_platform_priv_input_set_pointer_held(false);
    }
    if (pointer.hovered != 0 && wgf_gfx_priv_actor_of_silently(pointer.hovered) == NULL) pointer.hovered = 0;
    if (!ui) hit = pick(at.x, at.y, &in_2d); /* the UI has it: nothing of the game's is under it */
    if (hit != pointer.hovered) {
        left = pointer.hovered;
        entered = hit;
        pointer.hovered = hit;
    }
    if (pressed && !pointer.press_down && !ui) {
        pointer.press_target = hit;
        pointer.press_down = true;
        pressed_on = hit;
        /* a press on a 2D stage's actor captures the pointer, as wgrender's 2D members do;
           one on a 3D stage's doesn't, so the world under a HUD still has its drags */
        if (hit != 0 && in_2d) wgf_platform_priv_input_set_pointer_held(true);
    }
    if (released && pointer.press_down) {
        pointer.press_down = false;
        if (pointer.press_target != 0) {
            released_on = pointer.press_target;
            clicked_on = hit == pointer.press_target ? hit : 0;
            pointer.releasing = true;
        }
        pointer.press_target = 0;
    }
    sets[0] = &pointer.frame;
    sets[1] = &pointer.tick;
    for (s = 0; s < 2; s++) {
        if (entered != 0) add_edge(sets[s]->entered, &sets[s]->entered_count, entered);
        if (left != 0) add_edge(sets[s]->left, &sets[s]->left_count, left);
        if (pressed_on != 0) sets[s]->pressed = pressed_on;
        if (released_on != 0) sets[s]->released = released_on;
        if (clicked_on != 0) sets[s]->clicked = clicked_on;
    }
}

static void tick(float dt)
{
    (void)dt;
    memset(&pointer.tick, 0, sizeof(pointer.tick));
}

static void end_frame(void)
{
    memset(&pointer.frame, 0, sizeof(pointer.frame));
}

static void stop(void)
{
    free(pointer.stages);
    if (pointer.press_down || pointer.releasing) wgf_platform_priv_input_set_pointer_held(false);
    memset(&pointer, 0, sizeof(pointer));
}

static wgf_core_priv_part_t part = {.name = "pointer",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_POINTER,
                                    .begin_frame = begin_frame,
                                    .tick = tick,
                                    .end_frame = end_frame,
                                    .stop = stop};

static const edges_t *current(void)
{
    return wgf_platform_priv_input_get_context() == WGF_PLATFORM_PRIV_INPUT_TICK ? &pointer.tick : &pointer.frame;
}

static bool in_list(const wgf_actor_t *list, int count, wgf_actor_t actor)
{
    int i;
    for (i = 0; i < count; i++) {
        if (list[i] == actor) return true;
    }
    return false;
}

bool wgf_pointer_set_interactive(wgf_actor_t stage, bool interactive)
{
    const wgf_gfx_priv_actor_t *stage_ptr = wgf_gfx_priv_actor_of(stage); /* a dead one warned of */
    const wgf_actor_kind_t kind = stage_ptr != NULL ? stage_ptr->type : WGF_ACTOR_KIND_NONE;
    const int index = find(stage);
    if (kind != WGF_ACTOR_KIND_STAGE2D && kind != WGF_ACTOR_KIND_STAGE3D) return false;
    if (!interactive) {
        if (index >= 0) pointer.stages[index] = pointer.stages[--pointer.count];
        return true;
    }
    if (index >= 0) return true;
    if (pointer.count == pointer.capacity) {
        const int capacity = pointer.capacity > 0 ? pointer.capacity * 2 : 4;
        wgf_actor_t *grown = (wgf_actor_t *)realloc(pointer.stages, sizeof(wgf_actor_t) * (size_t)capacity);
        if (grown == NULL) {
            wgf_log_error("wgf_gfx_pointer: out of memory");
            return false;
        }
        pointer.stages = grown;
        pointer.capacity = capacity;
    }
    if (!pointer.installed) {
        wgf_core_priv_part_install(&part);
        pointer.installed = true;
    }
    pointer.stages[pointer.count++] = stage;
    return true;
}

bool wgf_pointer_is_interactive(wgf_actor_t stage)
{
    const wgf_actor_kind_t kind = wgf_actor_get_kind(stage);
    return (kind == WGF_ACTOR_KIND_STAGE2D || kind == WGF_ACTOR_KIND_STAGE3D) && find(stage) >= 0;
}

wgf_actor_t wgf_pointer_get_hovered(void)
{
    return pointer.hovered;
}

wgf_input_state_t wgf_pointer_get_hover_state(wgf_actor_t actor)
{
    const edges_t *edges = current();
    if (actor == 0) return WGF_INPUT_STATE_UP;
    if (in_list(edges->entered, edges->entered_count, actor)) return WGF_INPUT_STATE_PRESSED;
    if (in_list(edges->left, edges->left_count, actor)) return WGF_INPUT_STATE_RELEASED;
    return pointer.hovered == actor ? WGF_INPUT_STATE_DOWN : WGF_INPUT_STATE_UP;
}

wgf_input_state_t wgf_pointer_get_press_state(wgf_actor_t actor)
{
    const edges_t *edges = current();
    if (actor == 0) return WGF_INPUT_STATE_UP;
    if (edges->pressed == actor) return WGF_INPUT_STATE_PRESSED;
    if (edges->released == actor) return WGF_INPUT_STATE_RELEASED;
    return pointer.press_down && pointer.press_target == actor ? WGF_INPUT_STATE_DOWN : WGF_INPUT_STATE_UP;
}

bool wgf_pointer_is_clicked(wgf_actor_t actor)
{
    return actor != 0 && current()->clicked == actor;
}

wgf_actor_t wgf_pointer_get_held(void)
{
    return pointer.press_down ? pointer.press_target : 0;
}
