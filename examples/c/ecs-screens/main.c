#include <stdio.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_component.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_motion.h"
#include "wgf_render.h"
#include "wgf_screen.h"
#include "wgf_shape2d.h"
#include "wgf_stage2d.h"
#include "wgf_text.h"
#include "wgf_window.h"

/* Screens and scene flow: a title, play (a shape turning by its motion), a pause menu laid
 * over play (not opaque: play shows through it, paused, frozen), and game over, changed
 * through each transition in turn -- fade, slides, wipe, iris. Space goes on to the next
 * screen, P pauses and resumes; with no key it goes on by itself every second and a half.
 * Escape quits where quitting means anything. */

#define W 640
#define H 360

static wgf_actor_t title, play, paused_menu, over;
static float since;
static int next_transition;

/* A screen with one 2D stage: a backdrop in `color` (none when see-through) and a line of
 * text. */
static wgf_actor_t screen(const char *text, wgf_color_t color, bool backdrop)
{
    const wgf_actor_t s = wgf_screen_create(), stage = wgf_stage2d_create(), label = wgf_text_create(0);
    wgf_screen_add_stage(s, stage);
    if (backdrop) {
        const wgf_actor_t back = wgf_shape2d_create();
        wgf_shape2d_set_rectangle(back, W, H);
        wgf_shape2d_set_pivot(back, 0, 0); /* from the screen's top-left */
        wgf_shape2d_set_color(back, color);
        wgf_actor_set_parent(back, stage);
    } else {
        const wgf_actor_t band = wgf_shape2d_create();
        wgf_shape2d_set_rectangle(band, W, 80);
        wgf_shape2d_set_pivot(band, 0, 0);
        wgf_shape2d_set_color(band, wgf_color_make(0, 0, 0, 160));
        wgf_actor_set_position(band, 0, H / 2 - 40, 0);
        wgf_actor_set_parent(band, stage);
    }
    wgf_text_set_string(label, text);
    wgf_text_set_font_size(label, 28);
    wgf_text_set_color(label, WGF_COLOR_WHITE);
    wgf_actor_set_position(label, 40, H / 2 - 14, 0);
    wgf_actor_set_parent(label, stage);
    return s;
}

static void init(void *user)
{
    wgf_actor_t spinner;
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(0, 0, 0, 255));
    title = screen("libwgf: ecs-screens -- the title", wgf_color_make(40, 60, 120, 255), true);
    play = screen("play", wgf_color_make(30, 90, 50, 255), true);
    paused_menu = screen("paused (play frozen under it)", 0, false);
    over = screen("game over", wgf_color_make(120, 30, 30, 255), true);
    wgf_screen_set_opaque(paused_menu, false);

    spinner = wgf_shape2d_create(); /* on play's stage: it turns while play runs */
    wgf_shape2d_set_rectangle(spinner, 80, 80);
    wgf_shape2d_set_color(spinner, WGF_COLOR_GOLD);
    wgf_actor_set_position(spinner, W - 120, H / 2, 0);
    wgf_actor_set_parent(spinner, wgf_screen_get_stage(play, 0));
    wgf_actor_add_component(spinner, WGF_COMPONENT_MOTION);
    wgf_motion_set_spin(spinner, 0, 0, 2.0f);

    wgf_screen_set_transition_color(wgf_color_make(10, 10, 16, 255));
    wgf_screen_push(title);
}

/* The next transition of the kinds, in turn. */
static void next_kind(void)
{
    static const wgf_transition_t kinds[] = {WGF_TRANSITION_FADE,       WGF_TRANSITION_SLIDE_LEFT, WGF_TRANSITION_IRIS,
                                             WGF_TRANSITION_SLIDE_DOWN, WGF_TRANSITION_WIPE,
                                             WGF_TRANSITION_SLIDE_UP,
                                             WGF_TRANSITION_SLIDE_RIGHT};
    wgf_screen_set_transition(kinds[next_transition], 0.8f);
    next_transition = (next_transition + 1) % (int)(sizeof(kinds) / sizeof(kinds[0]));
}

/* On from the top: title to play, play to its pause menu, the menu back to play (the second
 * time to game over), game over to the title. */
static void go_on(void)
{
    static int pauses;
    const wgf_actor_t top = wgf_screen_get_top();
    if (wgf_screen_is_changing()) return;
    next_kind();
    if (top == title) {
        wgf_screen_replace(play);
    } else if (top == play) {
        wgf_screen_set_transition(WGF_TRANSITION_NONE, 0); /* a pause menu opens at once */
        wgf_screen_push(paused_menu);
    } else if (top == paused_menu) {
        wgf_screen_set_transition(WGF_TRANSITION_NONE, 0);
        wgf_screen_pop();
        if (++pauses % 2 == 0) {
            next_kind();
            wgf_screen_replace(over);
        }
    } else {
        wgf_screen_replace(title);
    }
    since = 0;
}

static void frame(void *user)
{
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    since += wgf_loop_get_frame_delta();
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE) || since > 1.5f) go_on();
    if (wgf_keyboard_is_pressed(WGF_KEY_P) && !wgf_screen_is_changing()) {
        if (wgf_screen_get_top() == play) wgf_screen_push(paused_menu);
        else if (wgf_screen_get_top() == paused_menu) wgf_screen_pop();
    }
    wgf_screen_draw();
}

int main(void)
{
    wgf_window_set_title("libwgf: ecs-screens");
    wgf_window_set_size(W, H);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
