#include <stdio.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_arcade2d.h"
#include "wgf_camera2d.h"
#include "wgf_collider.h"
#include "wgf_color.h"
#include "wgf_component.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_shape2d.h"
#include "wgf_stage2d.h"
#include "wgf_text.h"
#include "wgf_window.h"
#include "wgf_world.h"

/* 2D collision on actors: a hero (a 2D body, its collider a rectangle) running and jumping
 * over a grid of solid cells and onto a floating platform (a solid rectangle collider), and
 * picking up coins of every collider shape -- circles, a polygon, a segment -- as triggers.
 * Left and right (or A and D) run, Space jumps (a moment after leaving a ledge too); with no
 * key down it runs back and forth by itself and jumps now and then. Escape quits where
 * quitting means anything. */

#define COLUMNS 25
#define ROWS 14
#define CELL 32.0f
#define LEFT (-COLUMNS * CELL / 2)
#define TOP (-ROWS * CELL / 2)
#define RUN 220.0f
#define JUMP 560.0f
#define COYOTE 0.1f

static const char level[ROWS][COLUMNS + 1] = {
    "#########################",
    "#.......................#",
    "#.......................#",
    "#.......................#",
    "#.......................#",
    "#.......................#",
    "#.......................#",
    "#.......................#",
    "#.......................#",
    "#........##.............#",
    "#.....................###",
    "#...###.................#",
    "#.......................#",
    "#########################",
};

static wgf_actor_t world, hero, label;
static int cells[ROWS * COLUMNS], coins_left, coins_taken, facing = 1;
static float since_jump;
static bool played; /* a key pressed: no longer running by itself */

static wgf_actor_t block(float x, float y, float w, float h, wgf_color_t color)
{
    const wgf_actor_t shape = wgf_shape2d_create();
    wgf_shape2d_set_rectangle(shape, w, h);
    wgf_shape2d_set_color(shape, color);
    wgf_actor_set_position(shape, x, y, 0);
    wgf_actor_set_parent(shape, world);
    return shape;
}

/* A coin: a shape drawn, and a collider of the same shape on layer 2, a trigger. */
static void coin(wgf_actor_t shape, float x, float y)
{
    wgf_actor_set_position(shape, x, y, 0);
    wgf_shape2d_set_color(shape, WGF_COLOR_GOLD);
    wgf_actor_set_parent(shape, world);
    wgf_collider_set_layer(shape, 2);
    coins_left++;
}

static void init(void *user)
{
    static const float diamond[] = {0, -12, 10, 0, 0, 12, -10, 0};
    wgf_actor_t camera, platform, shape;
    int r, c;
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(16, 18, 30, 255));
    world = wgf_stage2d_create();
    camera = wgf_camera2d_create();
    wgf_stage2d_set_camera(world, camera);

    for (r = 0; r < ROWS; r++) { /* the grid: solid cells, drawn */
        for (c = 0; c < COLUMNS; c++) {
            cells[r * COLUMNS + c] = level[r][c] == '#';
            if (cells[r * COLUMNS + c]) {
                block(LEFT + (float)c * CELL, TOP + (float)r * CELL, CELL - 1, CELL - 1,
                      wgf_color_make(70, 80, 110, 255));
            }
        }
    }
    wgf_arcade2d_set_grid(world, cells, ROWS * COLUMNS, COLUMNS, CELL, LEFT, TOP);

    platform = block(120, 108, 128, 16, WGF_COLOR_SKYBLUE); /* a solid collider, not a cell */
    wgf_shape2d_set_pivot(platform, 0.5f, 0.5f);
    wgf_actor_add_component(platform, WGF_COMPONENT_COLLIDER);
    wgf_collider_set_rectangle(platform, 128, 16);

    for (c = 0; c < 4; c++) { /* coins: circles over the floor, a diamond, a bar */
        shape = wgf_shape2d_create();
        wgf_shape2d_set_circle(shape, 8);
        wgf_actor_add_component(shape, WGF_COMPONENT_COLLIDER);
        wgf_collider_set_radius(shape, 8);
        coin(shape, -250.0f + 140.0f * (float)c, 170);
    }
    shape = wgf_shape2d_create();
    wgf_shape2d_set_polygon(shape, diamond, 8);
    wgf_actor_add_component(shape, WGF_COMPONENT_COLLIDER);
    wgf_collider_set_polygon(shape, diamond, 8);
    coin(shape, 120, 76);
    shape = wgf_shape2d_create();
    wgf_shape2d_set_line(shape, 0, -14, 0, 14);
    wgf_shape2d_set_outline(shape, 4);
    wgf_actor_add_component(shape, WGF_COMPONENT_COLLIDER);
    wgf_collider_set_segment(shape, 0, -14, 0, 14);
    coin(shape, -230, 60);

    hero = block(-300, 150, 20, 28, WGF_COLOR_ORANGE);
    wgf_shape2d_set_pivot(hero, 0.5f, 0.5f);
    wgf_actor_add_component(hero, WGF_COMPONENT_ARCADE2D);
    wgf_arcade2d_set_size(hero, 20, 28);
    wgf_arcade2d_set_gravity(hero, 1400);
    wgf_arcade2d_set_max_fall_speed(hero, 900);
    wgf_arcade2d_set_solid_mask(hero, 1); /* the platform; the coins on layer 2 are triggers */
    wgf_actor_add_component(hero, WGF_COMPONENT_COLLIDER);
    wgf_collider_set_rectangle(hero, 20, 28);
    wgf_collider_set_layer(hero, 4);
    wgf_collider_set_mask(hero, 2);

    label = wgf_text_create(0);
    wgf_text_set_font_size(label, 18);
    wgf_text_set_color(label, WGF_COLOR_WHITE);
    wgf_actor_set_position(label, LEFT + 40, TOP + 40, 0);
    wgf_actor_set_parent(label, world);
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    const bool left = wgf_keyboard_is_down(WGF_KEY_LEFT) || wgf_keyboard_is_down(WGF_KEY_A);
    const bool right = wgf_keyboard_is_down(WGF_KEY_RIGHT) || wgf_keyboard_is_down(WGF_KEY_D);
    bool jump = wgf_keyboard_is_pressed(WGF_KEY_SPACE);
    int events[4 * 32], n, i;
    char text[64];
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    since_jump += dt;
    played = played || left || right || jump;
    if (!played) { /* by itself: turned back at a wall, a jump every second and a half */
        if ((facing > 0 && wgf_arcade2d_is_blocked_right(hero)) || (facing < 0 && wgf_arcade2d_is_blocked_left(hero))) {
            facing = -facing;
        }
        jump = since_jump > 1.5f;
    }
    wgf_arcade2d_set_velocity(hero, played ? (right ? RUN : 0) - (left ? RUN : 0) : RUN * (float)facing,
                            wgf_arcade2d_get_velocity(hero).y);
    if (jump && wgf_arcade2d_get_air_time(hero) < COYOTE) { /* grounded, or just off a ledge */
        wgf_arcade2d_set_velocity(hero, wgf_arcade2d_get_velocity(hero).x, -JUMP);
        since_jump = 0;
    }

    n = wgf_world_take_events(events, 4 * 32);
    for (i = 0; i < n; i += 4) { /* a coin touched (the other's layer, 2): taken */
        if (events[i] == WGF_WORLD_EVENT_TRIGGER_ENTER && (wgf_actor_t)events[i + 1] == hero && events[i + 3] == 2) {
            wgf_actor_destroy((wgf_actor_t)events[i + 2], WGF_ACTOR_DESTROY_CHILDREN);
            coins_taken++;
            coins_left--;
        }
    }
    snprintf(text, sizeof(text), "libwgf: ecs-platformer   coins %d of %d%s", coins_taken, coins_taken + coins_left,
             wgf_arcade2d_is_grounded(hero) ? "" : "   (in the air)");
    wgf_text_set_string(label, text);
    wgf_stage2d_draw(world);
}

int main(void)
{
    wgf_window_set_title("libwgf: ecs-platformer");
    wgf_window_set_size(COLUMNS * 32, ROWS * 32);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
