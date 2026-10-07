#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_stage2d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_mouse.h"
#include "wgf_actor.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_texture.h"
#include "wgf_touch.h"
#include "wgf_window.h"

/* Fingers, the two-finger gesture, and the pointer:
 *   - every finger gets a numbered ring (its id) while it's down, and a fading one
 *     where it lifted;
 *   - two fingers pan, pinch, and twist the logo, about the point between them, so it
 *     stays under the fingers;
 *   - one finger is also the mouse: drag the coin. A second finger lets the drag go
 *     (off the window), so a pinch never drops the coin somewhere.
 * Without a touch screen: drag with the mouse, and the wheel zooms the logo. Esc
 * quits, where quitting means anything. The images are in examples/assets/.
 *
 * libwgt's app-touch (wgrender's touch) done 1:1 for the size table. The differences:
 *   - the title says libwgf where libwgt's says libwgt;
 *   - libwgf has no pointer picking (libwgt's wgt_pointer_set_interactive and
 *     wgt_pointer_get_held, and wgt_node_set_pickable), so the coin is picked by hand
 *     as wgrender's touch picks it: a left press within its 96-pixel square starts the
 *     drag, and the button up ends it. The logo is then never picked, as libwgt's,
 *     made unpickable, isn't;
 *   - outlines take a thickness here, 1 logical pixel: libwgt's are GL lines of one
 *     pixel;
 *   - circles get enough segments for their size, where libwgt's have 36. */

#define LOGO_PATH "sprites/logo/wg-logo-white-alpha.png"
#define TILES_PATH "textures/tiles.png"
#define RING 38.0f
#define COIN 96.0f
#define FINGERS 8

static struct {
    wgf_actor_t stage, logo, coin;
    bool coin_held; /* a left press that started on the coin is held */
    float logo_x, logo_y, logo_scale, logo_rotation;
    float lifted[FINGERS][3]; /* x, y, fade (1 down to 0) where a finger lifted */
    wgf_color_t colors[FINGERS];
} g;

/* Scale and turn the logo about (x, y), so the point under the fingers stays put. */
static void transform_logo(float x, float y, float scale, float rotation)
{
    const float c = cosf(rotation), s = sinf(rotation);
    const float ox = (g.logo_x - x) * scale, oy = (g.logo_y - y) * scale;
    g.logo_x = x + ox * c - oy * s;
    g.logo_y = y + ox * s + oy * c;
    g.logo_scale = fminf(fmaxf(g.logo_scale * scale, 0.2f), 8.0f);
    g.logo_rotation += rotation;
}

static void frame(void *user)
{
    const wgf_vec2_t mouse = wgf_mouse_get_position();
    const wgf_vec2_t wheel = wgf_mouse_get_wheel();
    const int count = wgf_touch_get_count();
    char line[96];
    int i;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    /* two fingers move the logo; the wheel zooms it about the mouse */
    if (wgf_touch_is_gesture()) {
        const wgf_vec2_t pan = wgf_touch_get_gesture_pan(), center = wgf_touch_get_gesture_center();
        g.logo_x += pan.x;
        g.logo_y += pan.y;
        transform_logo(center.x, center.y, wgf_touch_get_gesture_scale(), wgf_touch_get_gesture_rotation());
    }
    if (wheel.y != 0.0f) transform_logo(mouse.x, mouse.y, powf(1.1f, wheel.y), 0.0f);
    wgf_actor_set_position(g.logo, g.logo_x, g.logo_y, 0);
    wgf_actor_set_scale(g.logo, g.logo_scale, g.logo_scale, 1);
    wgf_actor_set_rotation(g.logo, 0, 0, g.logo_rotation);

    /* the pointer (the mouse, or one finger) drags the coin: picked where it was before this frame moved it */
    if (wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT)) {
        const wgf_vec3_t coin = wgf_actor_get_position(g.coin);
        g.coin_held = fabsf(mouse.x - coin.x) < COIN * 0.5f && fabsf(mouse.y - coin.y) < COIN * 0.5f;
    } else if (!wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT)) {
        g.coin_held = false;
    }
    if (g.coin_held) wgf_actor_set_position(g.coin, mouse.x, mouse.y, 0);
    wgf_sprite_set_tint(g.coin, g.coin_held ? wgf_color_make(255, 230, 150, 255) : WGF_COLOR_WHITE);

    /* where fingers lifted: a ring that fades */
    for (i = 0; i < FINGERS; i++) g.lifted[i][2] = fmaxf(g.lifted[i][2] - 0.033f, 0.0f);
    for (i = 0; i < count; i++) {
        const int id = wgf_touch_get_id(i);
        if (wgf_touch_get_state(id) == WGF_INPUT_STATE_RELEASED) {
            const wgf_vec2_t at = wgf_touch_get_position(id);
            g.lifted[id][0] = at.x;
            g.lifted[id][1] = at.y;
            g.lifted[id][2] = 1.0f;
        }
    }

    wgf_stage2d_draw(g.stage);
    for (i = 0; i < FINGERS; i++) {
        if (g.lifted[i][2] > 0.0f) {
            wgf_draw_circle_lines(g.lifted[i][0], g.lifted[i][1], RING * (2.0f - g.lifted[i][2]), 1,
                                  wgf_color_with_alpha(g.colors[i], (int)(200 * g.lifted[i][2])));
        }
    }
    for (i = 0; i < count; i++) {
        const int id = wgf_touch_get_id(i);
        const wgf_vec2_t at = wgf_touch_get_position(id);
        if (wgf_touch_get_state(id) == WGF_INPUT_STATE_RELEASED) continue;
        wgf_draw_circle(at.x, at.y, RING, wgf_color_with_alpha(g.colors[id], 90));
        wgf_draw_circle_lines(at.x, at.y, RING, 1, g.colors[id]);
        snprintf(line, sizeof(line), "%d", id);
        wgf_draw_text(0, line, at.x - 6, at.y - RING - 26, 22, g.colors[id]);
    }
    if (wgf_touch_is_gesture()) {
        const wgf_vec2_t center = wgf_touch_get_gesture_center();
        wgf_draw_circle(center.x, center.y, 6, WGF_COLOR_WHITE);
    }

    snprintf(line, sizeof(line), "fingers: %d   pointer: %s", count,
             wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT) ? "down" : "up");
    wgf_draw_text(0, line, 16, 16, 18, WGF_COLOR_WHITE);
    snprintf(line, sizeof(line), "logo: scale %.2f, turn %.0f deg", g.logo_scale, g.logo_rotation * 57.29578f);
    wgf_draw_text(0, line, 16, 40, 18, WGF_COLOR_WHITE);
    wgf_draw_text(0, "two fingers: pan, pinch, twist the logo; one finger drags the coin", 16, 64, 16,
                  wgf_color_make(150, 158, 175, 255));
}

static void init(void *user)
{
    wgf_texture_t logo_texture, tiles;
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    logo_texture = wgf_texture_create(LOGO_PATH);
    tiles = wgf_texture_create(TILES_PATH);
    wgf_render_set_clear_color(wgf_color_make(22, 25, 33, 255));
    g.stage = wgf_stage2d_create();

    g.logo = wgf_sprite_create(logo_texture);
    wgf_sprite_set_size(g.logo, 240, 240);
    wgf_actor_set_parent(g.logo, g.stage);
    g.logo_x = (float)wgf_window_get_width() * 0.5f;
    g.logo_y = (float)wgf_window_get_height() * 0.45f;
    g.logo_scale = 1.0f;

    wgf_texture_set_sampling(tiles, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_FILTER_NEAREST);
    g.coin = wgf_sprite_create(tiles);
    wgf_sprite_set_source(g.coin, 42, 22, 16, 16); /* the coin, in the tile sheet (wgrender tools/gen_tiles.py) */
    wgf_sprite_set_size(g.coin, COIN, COIN);
    wgf_actor_set_position(g.coin, (float)wgf_window_get_width() * 0.5f, (float)wgf_window_get_height() * 0.8f, 0);
    wgf_actor_set_parent(g.coin, g.stage);

    /* the sprites hold their textures */
    wgf_resource_release(logo_texture);
    wgf_resource_release(tiles);
    for (i = 0; i < FINGERS; i++) g.colors[i] = wgf_color_make(90 + 20 * i, 200 - 15 * i, 120 + 17 * i, 255);
}

int main(void)
{
    wgf_window_set_title("libwgf touch");
    wgf_window_set_size(900, 700);
    wgf_window_set_msaa(true);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
