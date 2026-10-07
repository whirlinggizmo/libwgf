#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_debug.h"
#include "wgf_stage2d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_emitter2d.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_mouse.h"
#include "wgf_actor.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* Particle emitters: a fountain (drops under gravity), sparks (from a point circling
 * the fountain: they trail behind it, slowed by drag, and stretched along their
 * motion), and a campfire: flames under smoke (a dark puff that spreads, lightens, and
 * fades as it rises, blown by a breeze), over a ground grid, seen by a camera turning
 * slowly around them. Click or tap anywhere for a confetti burst there, over the rest,
 * each piece one of five colors.
 *
 *   click / tap   confetti
 *   Space         pause the steady emitters (the particles alive finish their lives)
 *   O             stop or restart the camera
 *   Esc           quit, where quitting means anything
 *
 * libwgt's gfx-particles (wgrender's particles) done 1:1, so the two compare in the size
 * table: the same window, emitters, settings, keys, and text, as near as 2D comes.
 * Where it differs, and why:
 *   - "libwgt" reads "libwgf" in the window's title and the heading: the library's name.
 *   - libwgf is 2D only, with CPU 2D emitters (wgf_emitter2d.h): no 3D scene, camera,
 *     emitters, or grid. So the example projects the 3D world itself, with libwgt's
 *     camera (16 units out, 6 up, looking at 0, 3, 0, a quarter turn of field of view,
 *     turning 0.15 radians a second), and each frame places a 2D emitter where its 3D
 *     one would be on the screen and sets it in pixels from its world settings there:
 *     its velocity's screen direction and speed, its gravity, size, and birth radius,
 *     each at its depth. The grid is drawn as projected lines. A particle born stays
 *     where it is on the screen as the camera turns, where libwgt's stays in the world.
 *   - The emitters draw squares, untextured: libwgf's particles have no texture, so no
 *     particle.png dot, no flame.png flipbook, and no confetti cut from the dot's middle;
 *     those two files aren't loaded, so the example loads no file at all.
 *   - No additive blending (libwgf has no alpha modes): the sparks and flames blend. They
 *     draw after the fountain and smoke, which draw farther first, as libwgt orders them.
 *   - Size and color run from start to end (libwgf has no curves): the smoke and the
 *     flames take their first and last size keys, and their colors from the key where
 *     they are fully in (the smoke's at 0.1, the flames' at 0.08) to the last.
 *   - No prewarm (the steady emitters start empty), spin, size variance, inherited
 *     velocity, or spawn box (the fountain's box is a disc of its half size): libwgf's
 *     emitters have none. libwgt's speed variance is the speed range; its drag k is
 *     1 - e^-k, libwgf's share lost a second.
 *   - The confetti is five emitters of one color each, bursting 60 apiece: libwgf's have
 *     no palette. A full emitter drops a new particle, where libwgt's replaces the
 *     oldest.
 *   - The readout is libwgf's overlay (wgf_debug_show_fps), as libwgt's is its
 *     wgt_loop_draw_fps: the same place, font, size, and color, with the frame's
 *     cost in milliseconds beside the rate. */

#define ORBIT_RADIUS 16.0f
#define ORBIT_SPEED 0.15f /* radians a second */
#define FOV 0.78539816f   /* the camera's vertical field of view, radians */
#define NEAR 0.1f         /* nothing nearer the camera is drawn */

enum { STEADY = 4, FOUNTAIN = 0, SMOKE = 1, SPARKS = 2, FLAME = 3, CONFETTI_COLORS = 5 };

/* A point or direction in the world, y up. */
typedef struct v3_t {
    float x, y, z;
} v3_t;

/* A steady emitter's settings in the world, as libwgt's example sets its 3D emitter. */
typedef struct steady_t {
    int capacity;
    float rate, life_min, life_max;
    float radius;                 /* born within it, world units */
    v3_t velocity;                /* world units a second */
    float spread, speed_variance; /* radians either side; a share of the speed either way */
    v3_t gravity;
    float drag; /* libwgt's: a second */
    float size_start, size_end;
    wgf_color_t color_start, color_end;
    float stretch;
} steady_t;

static struct {
    wgf_actor_t scene, overlay;
    wgf_actor_t steady[STEADY];
    wgf_actor_t confetti[CONFETTI_COLORS];
    steady_t settings[STEADY];
    v3_t where[STEADY]; /* each steady emitter's place in the world */
    v3_t eye, forward, right, up;
    float focal; /* pixels from the eye to the screen */
    bool paused, orbit;
    float time, orbit_angle; /* radians around the fountain; holds while the orbit is stopped */
} g;

static v3_t v3(float x, float y, float z)
{
    v3_t v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

static v3_t v3_sub(v3_t a, v3_t b)
{
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static v3_t v3_add_scaled(v3_t a, v3_t b, float s)
{
    return v3(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s);
}

static float v3_dot(v3_t a, v3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static v3_t v3_cross(v3_t a, v3_t b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static v3_t v3_normalize(v3_t a)
{
    const float length = sqrtf(v3_dot(a, a));
    return length > 0.0f ? v3(a.x / length, a.y / length, a.z / length) : a;
}

/* The camera at `eye` looking at `target`, y up, for the window's size. */
static void look_at(v3_t eye, v3_t target)
{
    g.eye = eye;
    g.forward = v3_normalize(v3_sub(target, eye));
    g.right = v3_normalize(v3_cross(g.forward, v3(0, 1, 0)));
    g.up = v3_cross(g.right, g.forward);
    g.focal = (float)wgf_window_get_height() * 0.5f / tanf(FOV * 0.5f);
}

/* How far `p` is in front of the camera. */
static float depth_of(v3_t p)
{
    return v3_dot(v3_sub(p, g.eye), g.forward);
}

/* Where `p` is on the screen, in logical pixels, y down; false behind the near plane. */
static bool project(v3_t p, float *x, float *y)
{
    const v3_t d = v3_sub(p, g.eye);
    const float z = v3_dot(d, g.forward);
    if (z < NEAR) return false;
    *x = (float)wgf_window_get_width() * 0.5f + v3_dot(d, g.right) / z * g.focal;
    *y = (float)wgf_window_get_height() * 0.5f - v3_dot(d, g.up) / z * g.focal;
    return true;
}

/* The screen's pixels for `v`, a world vector at `p`: its direction and length there. */
static void project_vector(v3_t p, v3_t v, float *x, float *y)
{
    const float step = 0.01f;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    *x = *y = 0.0f;
    if (project(p, &x0, &y0) && project(v3_add_scaled(p, v, step), &x1, &y1)) {
        *x = (x1 - x0) / step;
        *y = (y1 - y0) / step;
    }
}

/* The line from `a` to `b` in the world, cut at the near plane. */
static void draw_line_3d(v3_t a, v3_t b, wgf_color_t color)
{
    const float da = depth_of(a), db = depth_of(b);
    float ax, ay, bx, by;
    if (da < NEAR && db < NEAR) return;
    if (da < NEAR) a = v3_add_scaled(a, v3_sub(b, a), (NEAR - da) / (db - da));
    if (db < NEAR) b = v3_add_scaled(b, v3_sub(a, b), (NEAR - db) / (da - db));
    if (project(a, &ax, &ay) && project(b, &bx, &by)) wgf_draw_line(ax, ay, bx, by, 0, color);
}

/* The ground grid, `slices` cells across, `spacing` apart, as libwgt's wgt_draw_grid. */
static void draw_grid(int slices, float spacing, wgf_color_t color)
{
    const float half = (float)slices * spacing * 0.5f;
    int i;
    for (i = 0; i <= slices; i++) {
        const float p = -half + (float)i * spacing;
        draw_line_3d(v3(p, 0, -half), v3(p, 0, half), color);
        draw_line_3d(v3(-half, 0, p), v3(half, 0, p), color);
    }
}

/* A steady emitter on the scene stage, at `where` in the world. */
static void add_steady(int which, v3_t where, const steady_t *settings)
{
    const wgf_actor_t emitter = wgf_emitter2d_create();
    g.steady[which] = emitter;
    g.where[which] = where;
    g.settings[which] = *settings;
    wgf_emitter2d_set_capacity(emitter, settings->capacity);
    wgf_emitter2d_set_rate(emitter, settings->rate);
    wgf_emitter2d_set_life(emitter, settings->life_min, settings->life_max);
    wgf_emitter2d_set_drag(emitter, 1.0f - expf(-settings->drag));
    wgf_emitter2d_set_color(emitter, settings->color_start, settings->color_end);
    wgf_emitter2d_set_stretch(emitter, settings->stretch);
    wgf_actor_set_parent(emitter, g.scene);
}

/* Each steady emitter where its 3D one is on the screen, its settings in pixels there. */
static void place_steady(void)
{
    int i;
    for (i = 0; i < STEADY; i++) {
        const steady_t *s = &g.settings[i];
        const wgf_actor_t emitter = g.steady[i];
        const float z = depth_of(g.where[i]);
        const float scale = z > NEAR ? g.focal / z : 0.0f; /* pixels a world unit, at its depth */
        float x = 0, y = 0, vx, vy, gx, gy, speed;
        wgf_actor_set_visible(emitter, project(g.where[i], &x, &y));
        wgf_actor_set_position(emitter, x, y, 0);
        project_vector(g.where[i], s->velocity, &vx, &vy);
        project_vector(g.where[i], s->gravity, &gx, &gy);
        speed = sqrtf(vx * vx + vy * vy);
        wgf_emitter2d_set_direction(emitter, atan2f(vy, vx), s->spread);
        wgf_emitter2d_set_speed(emitter, speed * (1.0f - s->speed_variance), speed * (1.0f + s->speed_variance));
        wgf_emitter2d_set_gravity(emitter, gx, gy);
        wgf_emitter2d_set_size(emitter, s->size_start * scale, s->size_end * scale);
        wgf_emitter2d_set_radius(emitter, s->radius * scale);
    }
    /* The blended ones farther first, then the added ones, as libwgt draws them. */
    if (depth_of(g.where[FOUNTAIN]) >= depth_of(g.where[SMOKE])) {
        wgf_actor_set_index(g.steady[FOUNTAIN], 0);
    } else {
        wgf_actor_set_index(g.steady[SMOKE], 0);
    }
}

static void make_fountain(void)
{
    steady_t s = {0};
    s.capacity = 4096;
    s.rate = 1200.0f;
    s.life_min = 1.4f;
    s.life_max = 1.9f;
    s.radius = 0.15f; /* libwgt's spawn box, 0.15 by 0 by 0.15 */
    s.velocity = v3(0.0f, 9.0f, 0.0f);
    s.spread = 0.22f;
    s.speed_variance = 0.15f;
    s.gravity = v3(0.0f, -9.8f, 0.0f);
    s.size_start = 0.22f;
    s.size_end = 0.12f;
    s.color_start = wgf_color_make(150, 210, 255, 230);
    s.color_end = wgf_color_make(60, 120, 255, 0);
    add_steady(FOUNTAIN, v3(0.0f, 0.2f, 0.0f), &s);
}

static void make_sparks(void)
{
    steady_t s = {0};
    s.capacity = 2048;
    s.rate = 600.0f;
    s.life_min = 0.5f;
    s.life_max = 1.2f;
    s.velocity = v3(0.0f, 4.0f, 0.0f);
    s.spread = 1.2f;
    s.speed_variance = 0.6f;
    s.gravity = v3(0.0f, -6.0f, 0.0f);
    s.drag = 1.5f;     /* they slow down */
    s.stretch = 0.04f; /* streaks along their motion */
    s.size_start = 0.08f;
    s.size_end = 0.02f;
    s.color_start = wgf_color_make(255, 220, 120, 255);
    s.color_end = wgf_color_make(255, 60, 10, 0);
    add_steady(SPARKS, v3(3.5f, 1.5f, 0.0f), &s);
}

static void make_smoke(void)
{
    steady_t s = {0};
    s.capacity = 512;
    s.rate = 30.0f;
    s.life_min = 3.0f;
    s.life_max = 4.5f;
    s.radius = 0.3f;
    s.velocity = v3(0.0f, 1.4f, 0.0f);
    s.spread = 0.35f;
    s.speed_variance = 0.3f;
    s.gravity = v3(0.35f, 0.0f, 0.0f); /* a breeze */
    /* A puff that keeps spreading; dark, then light, then gone. */
    s.size_start = 0.3f;
    s.size_end = 3.6f;
    s.color_start = wgf_color_make(50, 46, 44, 190);
    s.color_end = wgf_color_make(170, 170, 180, 0);
    add_steady(SMOKE, v3(-5.0f, 1.5f, -2.0f), &s); /* above the fire */
}

/* A campfire's flames: puffs glowing white-yellow, going red and out as they rise. */
static void make_flame(void)
{
    steady_t s = {0};
    s.capacity = 1024; /* libwgt's default */
    s.rate = 40.0f;
    s.life_min = 0.7f;
    s.life_max = 1.1f;
    s.radius = 0.3f;
    s.velocity = v3(0.0f, 1.8f, 0.0f);
    s.spread = 0.2f;
    s.speed_variance = 0.3f;
    s.drag = 0.8f;
    s.size_start = 0.6f;
    s.size_end = 0.4f;
    s.color_start = wgf_color_make(255, 235, 190, 110);
    s.color_end = wgf_color_make(80, 15, 5, 0);
    add_steady(FLAME, v3(-5.0f, 0.3f, -2.0f), &s);
}

static void make_confetti(void)
{
    const wgf_color_t colors[CONFETTI_COLORS] = {WGF_COLOR_RED, WGF_COLOR_GOLD, WGF_COLOR_LIME, WGF_COLOR_SKYBLUE,
                                                 WGF_COLOR_VIOLET};
    int i;
    for (i = 0; i < CONFETTI_COLORS; i++) {
        const wgf_actor_t confetti = wgf_emitter2d_create();
        g.confetti[i] = confetti;
        wgf_actor_set_parent(confetti, g.overlay);
        wgf_emitter2d_set_capacity(confetti, 4096 / CONFETTI_COLORS);
        wgf_emitter2d_set_life(confetti, 1.2f, 2.2f);
        wgf_emitter2d_set_direction(confetti, -1.5707963f, 1.3f); /* up the screen */
        wgf_emitter2d_set_speed(confetti, 420.0f * 0.3f, 420.0f * 1.7f);
        wgf_emitter2d_set_gravity(confetti, 0.0f, 700.0f);
        wgf_emitter2d_set_drag(confetti, 1.0f - expf(-0.8f)); /* flutters down instead of dropping */
        wgf_emitter2d_set_size(confetti, 12.0f, 6.0f);
        wgf_emitter2d_set_color(confetti, colors[i], wgf_color_with_alpha(colors[i], 0));
    }
}

/* A burst where (x, y) is. */
static void burst_confetti(float x, float y)
{
    int i;
    for (i = 0; i < CONFETTI_COLORS; i++) {
        wgf_actor_set_position(g.confetti[i], x, y, 0.0f);
        wgf_emitter2d_burst(g.confetti[i], 300 / CONFETTI_COLORS);
    }
}

static int confetti_count(void)
{
    int i, count = 0;
    for (i = 0; i < CONFETTI_COLORS; i++) count += wgf_emitter2d_get_count(g.confetti[i]);
    return count;
}


static void place_camera(void)
{
    look_at(v3(ORBIT_RADIUS * sinf(g.orbit_angle), 6.0f, ORBIT_RADIUS * cosf(g.orbit_angle)), v3(0.0f, 3.0f, 0.0f));
}

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(14, 16, 24, 255));
    g.orbit = true;
    g.scene = wgf_stage2d_create();  /* the steady emitters, placed as the camera sees them */
    g.overlay = wgf_stage2d_create(); /* the confetti's, over the scene */

    /* The emitters run at once, in their drawing order. */
    make_fountain();
    make_smoke();
    make_sparks();
    make_flame();
    make_confetti();
    place_camera();
    place_steady();

    burst_confetti((float)wgf_window_get_width() * 0.5f, (float)wgf_window_get_height() * 0.4f); /* one to start */
}

static void set_paused(bool paused)
{
    int i;
    g.paused = paused;
    for (i = 0; i < STEADY; i++) wgf_emitter2d_set_emitting(g.steady[i], !paused);
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    char line[128];
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    g.time += dt;
    /* The sparks' source circles the fountain; the sparks stay where they were born. */
    g.where[SPARKS] = v3(3.5f * cosf(g.time * 1.3f), 1.5f + 0.8f * sinf(g.time * 2.1f), 3.5f * sinf(g.time * 1.3f));
    if (wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT)) {
        const wgf_vec2_t mouse = wgf_mouse_get_position();
        burst_confetti(mouse.x, mouse.y);
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) set_paused(!g.paused);
    if (wgf_keyboard_is_pressed(WGF_KEY_O)) g.orbit = !g.orbit;
    if (g.orbit) g.orbit_angle += dt * ORBIT_SPEED;
    place_camera();
    place_steady();

    draw_grid(24, 1.0f, WGF_COLOR_DARKGRAY);
    wgf_stage2d_draw(g.scene);
    wgf_stage2d_draw(g.overlay);

    wgf_draw_text(0, "libwgf particles", 12, 36, 24, WGF_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "click / tap: confetti   space: %s   O: %s the camera", g.paused ? "resume" : "pause",
             g.orbit ? "stop" : "turn");
    wgf_draw_text(0, line, 12, 70, 16, WGF_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "fountain %d   sparks %d   flame %d   smoke %d   confetti %d",
             wgf_emitter2d_get_count(g.steady[FOUNTAIN]), wgf_emitter2d_get_count(g.steady[SPARKS]),
             wgf_emitter2d_get_count(g.steady[FLAME]), wgf_emitter2d_get_count(g.steady[SMOKE]), confetti_count());
    wgf_draw_text(0, line, 12, 94, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf particles");
    wgf_window_set_size(1000, 700);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255)); /* libwgt's draw_fps */
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
