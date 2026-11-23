#include <math.h>
#include <stdio.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_ease.h"
#include "wgf_handle.h"
#include "wgf_log.h"
#include "wgf_stage2d.h"
#include "wgf_tween.h"

/* Tweens and easing, headless, the tick stepped by the test: the eases' ends and shapes; a
 * number's tween, its delay, repeats and yoyo, paused; an actor moved, turned, and scaled
 * from where it was; a tween ended let go of, and one whose actor goes; what is refused. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void ticks(int n)
{
    while (n-- > 0) {
        wgf_core_priv_part_tick_begin();
        wgf_core_priv_part_tick(0.1f);
    }
}

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-3f;
}

int main(void)
{
    wgf_tween_t t;
    wgf_actor_t stage, box;
    int e, ends = 1;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(wgf_gfx_priv_start(), "setup");

    for (e = WGF_EASING_LINEAR; e <= WGF_EASING_BOUNCE_IN_OUT; e++) {
        ends = ends && near(wgf_ease_value((wgf_easing_t)e, 0), 0) && near(wgf_ease_value((wgf_easing_t)e, 1), 1);
    }
    expect(ends, "every ease 0 at 0 and 1 at 1");
    expect(near(wgf_ease_value(WGF_EASING_QUAD_IN, 0.5f), 0.25f) &&
               near(wgf_ease_value(WGF_EASING_QUAD_OUT, 0.5f), 0.75f) &&
               wgf_ease_value(WGF_EASING_BACK_IN, 0.2f) < 0.0f && near(wgf_ease_value(WGF_EASING_LINEAR, 2), 1),
           "their shapes: quad in slow, out fast, back going past; t clamped");

    t = wgf_tween_create(10, 20, 1, WGF_EASING_LINEAR);
    expect(t != 0 && near(wgf_tween_get_value(t), 10), "a number's tween, at its start");
    ticks(5);
    expect(near(wgf_tween_get_value(t), 15), "halfway");
    ticks(5);
    expect(!wgf_handle_is_alive(t) && wgf_tween_get_value(t) == 0.0f, "ended: let go of");

    t = wgf_tween_create(0, 1, 1, WGF_EASING_LINEAR);
    expect(wgf_tween_set_delay(t, 0.5f) && near(wgf_tween_get_delay(t), 0.5f) && !wgf_tween_set_delay(t, -1),
           "a delay; a negative one refused");
    ticks(5);
    expect(near(wgf_tween_get_value(t), 0), "waiting");
    ticks(3);
    expect(near(wgf_tween_get_value(t), 0.3f) && !wgf_tween_set_delay(t, 1), "begun: on; its delay fixed");
    expect(wgf_tween_set_paused(t, true) && wgf_tween_is_paused(t), "paused");
    ticks(5);
    expect(near(wgf_tween_get_value(t), 0.3f), "held where it was");
    wgf_tween_set_paused(t, false);
    wgf_tween_destroy(t);
    expect(!wgf_handle_is_alive(t), "destroyed");

    t = wgf_tween_create(0, 1, 1, WGF_EASING_LINEAR);
    expect(wgf_tween_set_repeat(t, 1) && wgf_tween_get_repeat(t) == 1 && wgf_tween_set_yoyo(t, true) &&
               wgf_tween_is_yoyo(t) &&
               !wgf_tween_set_repeat(t, -2),
           "a repeat and a yoyo; a count below -1 refused");
    ticks(13);
    expect(near(wgf_tween_get_value(t), 0.7f), "its second run going back");
    ticks(7);
    expect(!wgf_handle_is_alive(t), "and ended");
    t = wgf_tween_create(0, 1, 1, WGF_EASING_LINEAR);
    wgf_tween_set_repeat(t, -1);
    ticks(55);
    expect(wgf_handle_is_alive(t) && near(wgf_tween_get_value(t), 0.5f), "for ever");
    wgf_tween_destroy(t);

    stage = wgf_stage2d_create();
    box = wgf_actor_create();
    wgf_actor_set_parent(box, stage);
    wgf_actor_set_position(box, 100, 0, 0);
    t = wgf_tween_move(box, 200, 50, 0, 1, WGF_EASING_LINEAR);
    ticks(5);
    expect(near(wgf_actor_get_position(box).x, 150) && near(wgf_actor_get_position(box).y, 25) &&
               near(wgf_tween_get_value(t), 0.5f),
           "an actor moved from where it was, halfway; its value its progress");
    ticks(5);
    expect(near(wgf_actor_get_position(box).x, 200) && !wgf_handle_is_alive(t), "there, and ended");
    t = wgf_tween_scale(box, 3, 3, 1, 1, WGF_EASING_LINEAR);
    ticks(10);
    expect(near(wgf_actor_get_scale(box).x, 3), "scaled");
    t = wgf_tween_turn(box, 0, 0, 1.5f, 1, WGF_EASING_LINEAR);
    ticks(10);
    expect(near(wgf_actor_get_rotation(box).z, 1.5f), "turned");
    t = wgf_tween_move(box, 0, 0, 0, 1, WGF_EASING_LINEAR);
    ticks(2);
    wgf_actor_destroy(box, WGF_ACTOR_DESTROY_CHILDREN);
    ticks(1);
    expect(!wgf_handle_is_alive(t), "its actor gone: let go of");

    expect(wgf_tween_create(0, 1, 0, WGF_EASING_LINEAR) == 0 &&
               wgf_tween_move(stage + 1, 0, 0, 0, 1, WGF_EASING_LINEAR) == 0 &&
               !wgf_tween_set_paused(12345, true),
           "no time, an actor that isn't one, a handle that isn't a tween: refused");

    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
