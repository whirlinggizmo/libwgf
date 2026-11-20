#include <math.h>
#include <stdio.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_screen.h"
#include "wgf_resource.h"
#include "wgf_stage2d.h"
#include "wgf_texture.h"
#include "wgf_world.h"

/* Screens headless: pushed, popped, and replaced at once, each switched on, off, and paused as
 * the stack says (opaque and pausing), the top's changes told as events; a change through a
 * fade, made at its middle, its progress, a change refused while one runs; the transition's
 * settings, the loading screen, and the refusals. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-3f;
}

/* A screen with a 2D stage on it. */
static wgf_actor_t screen_with_stage(void)
{
    const wgf_actor_t screen = wgf_screen_create(), stage = wgf_stage2d_create();
    expect(wgf_screen_add_stage(screen, stage) && !wgf_screen_add_stage(screen, stage) && !wgf_screen_add_stage(screen, screen),
           "a stage added; again, or an actor that isn't a stage: refused");
    return screen;
}

/* The world's events taken: how many of `kind` for `actor` with `other`. */
static int told(int kind, wgf_actor_t actor, wgf_actor_t other)
{
    int out[4 * 64], n, i, found = 0;
    n = wgf_world_take_events(out, 4 * 64);
    for (i = 0; i < n; i += 4) {
        if (out[i] == kind && (wgf_actor_t)out[i + 1] == actor && (wgf_actor_t)out[i + 2] == other) found++;
    }
    return found;
}

int main(void)
{
    wgf_actor_t title, play, pause, hud, loading;

    wgf_core_priv_init();
    expect(wgf_gfx_priv_start(), "gfx");
    title = screen_with_stage();
    play = screen_with_stage();
    pause = screen_with_stage();
    hud = screen_with_stage();
    loading = screen_with_stage();
    expect(title != 0 && !wgf_actor_is_enabled(title) && wgf_actor_is_paused(title) && wgf_screen_get_count() == 0 &&
               wgf_screen_get_top() == 0,
           "made off the stack: off and paused, none on it");
    expect(wgf_screen_is_opaque(title) && wgf_screen_is_pausing(title), "opaque and pausing to start");

    /* at once */
    expect(wgf_screen_push(title) && wgf_screen_get_top() == title && wgf_screen_get_count() == 1, "pushed");
    expect(wgf_actor_is_enabled(title) && !wgf_actor_is_paused(title), "on top: on, running");
    expect(told(WGF_WORLD_EVENT_SCREEN_ENTER, title, 0) == 1, "its entering told");
    expect(wgf_screen_replace(play) && wgf_screen_get_top() == play && wgf_screen_get_count() == 1 &&
               !wgf_actor_is_enabled(title) && wgf_actor_is_paused(title),
           "replaced: the old off the stack, off and paused");
    {
        int out[4 * 8];
        const int n = wgf_world_take_events(out, 4 * 8);
        expect(n == 8 && out[0] == WGF_WORLD_EVENT_SCREEN_EXIT && (wgf_actor_t)out[1] == title &&
                   (wgf_actor_t)out[2] == play && out[4] == WGF_WORLD_EVENT_SCREEN_ENTER && (wgf_actor_t)out[5] == play &&
                   (wgf_actor_t)out[6] == title,
               "the one leaving told, then the one coming");
    }
    expect(wgf_screen_set_opaque(pause, false) && !wgf_screen_is_opaque(pause), "a pause menu, not opaque");
    expect(wgf_screen_push(pause) && wgf_screen_get_count() == 2 && wgf_screen_get(0) == play && wgf_screen_get(1) == pause,
           "pushed over play");
    expect(wgf_actor_is_enabled(play) && wgf_actor_is_paused(play) && wgf_actor_is_frozen(wgf_screen_get_stage(play, 0)) && wgf_screen_get_stage_count(play) == 1 &&
               !wgf_actor_is_paused(pause),
           "play under it: on, paused, frozen with its stage");
    told(0, 0, 0);
    expect(wgf_screen_pop() && wgf_screen_get_top() == play && !wgf_actor_is_paused(play) && !wgf_actor_is_enabled(pause),
           "popped: play running again, the menu off");
    expect(told(WGF_WORLD_EVENT_SCREEN_ENTER, play, pause) == 1, "play's coming back told, with the menu it was under");
    expect(wgf_screen_set_pausing(hud, false) && wgf_screen_push(hud) && !wgf_actor_is_paused(play),
           "a HUD that doesn't pause: play runs on under it");
    wgf_screen_draw(); /* outside a frame: nothing, and nothing goes wrong */
    expect(wgf_screen_pop() && wgf_screen_get_top() == play, "the HUD popped");

    /* refused */
    expect(!wgf_screen_push(play) && !wgf_screen_pop() && !wgf_screen_push(0) && !wgf_screen_push(12345) &&
               !wgf_screen_replace(play) && !wgf_screen_get(5) && !wgf_screen_set_opaque(0, true),
           "one on the stack already, the bottom one alone, not an actor: refused");

    /* through a fade: covered over half its seconds, changed at the middle, uncovered */
    expect(wgf_screen_set_transition(WGF_TRANSITION_FADE, 1.0f) && wgf_screen_get_transition() == WGF_TRANSITION_FADE &&
               near(wgf_screen_get_transition_seconds(), 1.0f),
           "a fade of a second");
    expect(wgf_screen_set_transition_ease(WGF_EASING_QUAD_IN) && wgf_screen_get_transition_ease() == WGF_EASING_QUAD_IN &&
               wgf_screen_set_transition_ease(WGF_EASING_LINEAR),
           "its ease");
    wgf_screen_set_transition_color(wgf_color_make(255, 255, 255, 255));
    expect(wgf_screen_get_transition_color() == wgf_color_make(255, 255, 255, 255), "its color");
    expect(wgf_screen_push(pause) && wgf_screen_is_changing() && wgf_screen_get_top() == play, "pushed: not yet");
    wgf_core_priv_part_update(0.25f);
    expect(near(wgf_screen_get_change_progress(), 0.25f) && wgf_screen_get_top() == play, "covering, halfway");
    expect(!wgf_screen_pop() && !wgf_screen_push(title), "another change while it runs: refused");
    wgf_core_priv_part_update(0.25f);
    expect(wgf_screen_get_top() == pause && near(wgf_screen_get_change_progress(), 0.5f), "covered: changed at the middle");
    wgf_core_priv_part_update(0.25f);
    expect(near(wgf_screen_get_change_progress(), 0.75f), "uncovering");
    wgf_core_priv_part_update(0.3f);
    expect(!wgf_screen_is_changing() && near(wgf_screen_get_change_progress(), 0.0f), "done");

    /* the loading screen, and progress with nothing waiting */
    expect(wgf_screen_set_loading_screen(loading) && wgf_screen_get_loading_screen() == loading &&
               near(wgf_screen_get_load_progress(), 1.0f) && !wgf_actor_is_enabled(loading),
           "a loading screen, off while nothing waits");
    expect(!wgf_screen_set_assets(play, 12345) && wgf_screen_set_assets(play, 0) && wgf_screen_get_assets(play) == 0,
           "assets: a task, or none");
    expect(!wgf_screen_set_transition((wgf_transition_t)99, 1) && !wgf_screen_set_transition(WGF_TRANSITION_FADE, NAN) &&
               !wgf_screen_set_transition_ease((wgf_easing_t)99),
           "a kind or an ease that isn't one, seconds that aren't: refused");

    { /* a change waiting on a load: held, the loading screen shown and running, until it settles */
        const wgf_texture_t pending = wgf_texture_create("screen_test/nothing.png");
        int i;
        wgf_screen_set_transition(WGF_TRANSITION_NONE, 0);
        expect(!wgf_screen_is_wait_loads(title) && wgf_screen_set_wait_loads(title, true) && wgf_screen_is_wait_loads(title),
               "set to wait for every load pending");
        expect(wgf_screen_push(title) && wgf_screen_is_changing() && wgf_screen_get_top() == pause,
               "a load pending: the change waits");
        wgf_core_priv_part_update(0.0f);
        expect(wgf_actor_is_enabled(loading) && !wgf_actor_is_paused(loading) && near(wgf_screen_get_change_progress(), 0.5f),
               "the loading screen shown, running");
        for (i = 0; i < 1000 && wgf_screen_is_changing(); i++) { /* loads moved on, and the screens' part */
            wgf_core_priv_update();
            wgf_core_priv_part_update(0.0f);
        }
        expect(!wgf_screen_is_changing() && wgf_screen_get_top() == title && !wgf_actor_is_enabled(loading),
               "the load settled: changed, the loading screen put away");
        wgf_resource_release(pending);
        wgf_screen_pop();
    }

    /* a screen destroyed on the stack leaves it */
    wgf_screen_set_transition(WGF_TRANSITION_NONE, 0);
    wgf_actor_destroy(pause, WGF_ACTOR_DESTROY_CHILDREN);
    expect(wgf_screen_get_count() == 1 && wgf_screen_get_top() == play, "destroyed: off the stack");

    wgf_world_clear();
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_ecs_screen_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
