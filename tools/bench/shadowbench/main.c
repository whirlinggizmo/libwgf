#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_camera3d.h"
#include "wgf_light.h"
#include "wgf_log.h"
#include "wgf_loop.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* The shadow benchmark: what a casting light costs a frame, libwgt's shadowbench (ported
 * case for case from wgrender's), run in a browser by tools/bench/measure_frames.py
 * (`bench:shadowbench`), which traces each frame's main-thread work and reads the cases
 * apart by the marks this program leaves in the trace (console.timeStamp): "shadowbench
 * <case> <models>" as a case's measured frames begin, "shadowbench rest" as they end.
 *
 * The same stage each time -- a floor and a grid of generated shapes, half of them turning,
 * under a perspective camera -- drawn with:
 *
 *   off                no light casts: the baseline
 *   1024, 2048, 4096   the sun casts, at three map sizes: the pass over every caster is
 *                      the same CPU work each time, so what changes is the map's fill
 *   two 1024           the sun and a spot cast: two passes, two layers
 *   no receive         the sun casts but nothing receives: the pass is skipped, so this
 *                      should land on "off"
 *   shared             every model the same mesh and material, as a forest or a crowd:
 *                      libwgt batches them into one instanced draw; libwgf draws each until
 *                      step 11, so here it is what batching would be worth
 *   wide, each/shared  the sun's shadows reach the whole grid instead of 40 units, so every
 *                      model is drawn into the map as well as to the screen
 *   look away          the sun casts, the camera faces away from the grid: culling should
 *                      submit almost nothing
 *   away, no cull      the same with the stage's culling off: what culling is worth
 *
 * at four model counts, to see what scales with casters and what doesn't. Nothing is loaded:
 * the shapes are generated, so the numbers are the renderer's. Natively it runs too, and
 * prints its own frame times (no trace there), so a desktop run can be read beside libwgt's. */

#define WARMUP_FRAMES 20
#define MEASURE_FRAMES 100
#define MAX_MODELS 4000
#define SPACING 2.2f
#define DEGREES (3.14159265f / 180.0f)

typedef enum {
    CASE_OFF,
    CASE_1024,
    CASE_2048,
    CASE_4096,
    CASE_TWO_LIGHTS,
    CASE_NO_RECEIVE,
    CASE_SHARED,
    CASE_WIDE_EACH,
    CASE_WIDE_SHARED,
    CASE_AWAY,
    CASE_AWAY_NO_CULL,
    CASES
} bench_case_t;

static const char *CASE_NAMES[CASES] = {"off",        "sun 1024", "sun 2048",     "sun 4096",  "two 1024",
                                        "no receive", "shared",   "wide, each",   "wide, shared", "look away",
                                        "away, no cull"};
enum { COUNT_STEPS = 4 };
static const int MODEL_COUNTS[COUNT_STEPS] = {100, 400, 1000, 4000};

static struct {
    wgf_actor_t stage, camera, sun, spot, floor;
    wgf_actor_t models[MAX_MODELS];
    int model_count;
    bench_case_t which;
    int step; /* case * COUNT_STEPS + count step */
    int frame;
    double frame_sum;
    float angle;
} b;

/* A mark in the browser's trace (and the log, natively), where measure_frames reads the
 * cases apart. */
static void mark(const char *text)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ console.timeStamp(UTF8ToString($0)); }, text);
#else
    (void)text;
#endif
}

static void place_in_grid(wgf_actor_t model, int i, int side)
{
    const float x = ((float)(i % side) - (float)side * 0.5f) * SPACING;
    const float z = ((float)(i / side) - (float)side * 0.5f) * SPACING;
    wgf_actor_set_position(model, x, 0.8f, z);
}

/* A plain lit material: no metal, a little rough. */
static wgf_material_t lit_material(float r, float g, float bl)
{
    const wgf_material_t material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(material, "base_color", r, g, bl, 1.0f);
    wgf_material_set_float(material, "metallic", 0.0f);
    wgf_material_set_float(material, "roughness", 0.55f);
    return material;
}

/* One model of `mesh` on the stage, with a material of its own. */
static wgf_actor_t place(wgf_mesh_t mesh, float r, float g, float bl)
{
    const wgf_actor_t model = wgf_model_create(mesh);
    const wgf_material_t material = lit_material(r, g, bl);
    wgf_resource_release(mesh);
    wgf_model_set_material(model, -1, material);
    wgf_resource_release(material);
    wgf_actor_set_parent(model, b.stage);
    return model;
}

static void setup(void)
{
    const bench_case_t which = (bench_case_t)(b.step / COUNT_STEPS);
    const int count = MODEL_COUNTS[b.step % COUNT_STEPS];
    const int side = (int)ceilf(sqrtf((float)count));
    const bool shared = which == CASE_SHARED || which == CASE_WIDE_SHARED;
    int i;

    b.which = which;
    b.model_count = count;
    b.floor = place(wgf_mesh_create_plane((float)side * SPACING * 1.6f, (float)side * SPACING * 1.6f, 0), 0.4f, 0.42f,
                    0.45f);
    if (shared) { /* one mesh, one material */
        const wgf_mesh_t mesh = wgf_mesh_create_sphere(0.55f, 16, 32);
        const wgf_material_t material = lit_material(0.6f, 0.5f, 0.4f);
        for (i = 0; i < count; i++) {
            b.models[i] = wgf_model_create(mesh);
            wgf_model_set_material(b.models[i], -1, material);
            place_in_grid(b.models[i], i, side);
            wgf_actor_set_parent(b.models[i], b.stage);
        }
        wgf_resource_release(mesh);
        wgf_resource_release(material);
    }
    for (i = 0; !shared && i < count; i++) {
        const float hue = (float)i / (float)count;
        /* a mix of shapes, so the depth pass sees a range of triangle counts */
        const wgf_mesh_t mesh = (i % 3 == 0)   ? wgf_mesh_create_sphere(0.55f, 16, 32)
                                : (i % 3 == 1) ? wgf_mesh_create_cube(0.9f, 1.2f, 0.9f)
                                               : wgf_mesh_create_torus(0.5f, 0.18f, 32, 16);
        b.models[i] = place(mesh, 0.3f + hue * 0.6f, 0.5f, 0.9f - hue * 0.6f);
        place_in_grid(b.models[i], i, side);
    }

    wgf_light_set_shadow_casting(b.sun, which != CASE_OFF);
    /* "wide" reaches the whole grid, so nothing is culled out of the map */
    wgf_light_set_shadow_distance(b.sun, which == CASE_WIDE_EACH || which == CASE_WIDE_SHARED ? 400.0f : 40.0f);
    wgf_light_set_shadow_casting(b.spot, which == CASE_TWO_LIGHTS);
    wgf_stage3d_set_culling(b.stage, which != CASE_AWAY_NO_CULL);
    wgf_light_set_shadow_map_size(b.sun, which == CASE_2048 ? 2048 : which == CASE_4096 ? 4096 : 1024);
    wgf_light_set_shadow_map_size(b.spot, 1024);
    for (i = 0; i < count; i++) wgf_model_set_shadow_receiving(b.models[i], which != CASE_NO_RECEIVE);
    wgf_model_set_shadow_receiving(b.floor, which != CASE_NO_RECEIVE);
    b.frame = 0;
    b.frame_sum = 0.0;
}

static void teardown(void)
{
    int i;
    for (i = 0; i < b.model_count; i++) wgf_actor_destroy(b.models[i], WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(b.floor, WGF_ACTOR_DESTROY_CHILDREN);
    b.model_count = 0;
}

/* Half the models turn, so a frame's casters aren't all as they were; the camera keeps
   moving, so the shadow fit snaps again as it would in a game. */
static void update(float dt)
{
    const float radius = 6.0f + (float)b.model_count * 0.06f;
    const float x = radius * sinf(b.angle * 0.5f), z = radius * cosf(b.angle * 0.5f);
    int i;
    b.angle += dt * 0.3f;
    for (i = 0; i < b.model_count; i += 2) {
        wgf_actor_set_rotation(b.models[i], 0, b.angle * 40.0f * DEGREES, b.angle * 25.0f * DEGREES);
    }
    wgf_actor_set_position(b.camera, x, radius * 0.45f, z);
    if (b.which == CASE_AWAY || b.which == CASE_AWAY_NO_CULL) { /* outward: every model behind the camera */
        wgf_actor_look_at(b.camera, x * 2.0f, radius * 0.9f, z * 2.0f, 0, 1, 0);
        return;
    }
    wgf_actor_look_at(b.camera, 0, 0.8f, 0, 0, 1, 0);
}

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(0x7896C8FFu);
    b.stage = wgf_stage3d_create();
    b.camera = wgf_camera3d_create();
    wgf_actor_set_parent(b.camera, b.stage);
    wgf_stage3d_set_camera(b.stage, b.camera);
    wgf_stage3d_set_ambient(b.stage, 0x8CAAE1FFu, 0.25f);

    b.sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(b.sun, -0.6f, -1.0f, -0.35f, 0, 1, 0);
    wgf_light_set_intensity(b.sun, 3.0f);
    wgf_actor_set_parent(b.sun, b.stage);

    b.spot = wgf_light_create(WGF_LIGHT_TYPE_SPOT);
    wgf_actor_set_position(b.spot, 6.0f, 9.0f, 6.0f);
    wgf_actor_look_at(b.spot, 0, 0, 0, 0, 1, 0); /* down and in, as wgrender's */
    wgf_light_set_intensity(b.spot, 240.0f);
    wgf_light_set_range(b.spot, 30.0f);
    wgf_light_set_spot_cone(b.spot, 0.35f, 0.5f);
    wgf_light_set_shadow_distance(b.spot, 30.0f);
    wgf_actor_set_parent(b.spot, b.stage);
    setup();
}

static void frame(void *user)
{
    char text[64];
    (void)user;
    if (b.frame == WARMUP_FRAMES) {
        snprintf(text, sizeof(text), "shadowbench %s %d", CASE_NAMES[b.which], b.model_count);
        mark(text);
    }
    if (b.frame > WARMUP_FRAMES) b.frame_sum += wgf_loop_get_frame_delta() * 1000.0;
    if (b.frame == WARMUP_FRAMES + MEASURE_FRAMES) {
        mark("shadowbench rest");
        printf("shadowbench: %s %d: %.2f ms a frame (paced)\n", CASE_NAMES[b.which], b.model_count,
               b.frame_sum / (MEASURE_FRAMES - 1));
        teardown();
        if (++b.step == CASES * COUNT_STEPS) {
            wgf_log_message(WGF_LOG_LEVEL_INFO, "shadowbench: done");
            if (wgf_app_can_quit()) wgf_app_quit();
            return;
        }
        setup();
        return;
    }
    if (b.step >= CASES * COUNT_STEPS) return;
    update(wgf_loop_get_frame_delta());
    wgf_stage3d_draw(b.stage);
    b.frame++;
}

int main(void)
{
    wgf_window_set_title("libwgf shadowbench");
    wgf_window_set_size(960, 600);
    wgf_window_set_vsync(false);
    wgf_window_set_high_dpi(false);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
