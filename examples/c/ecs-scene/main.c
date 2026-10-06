#include <math.h>
#include <stddef.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_behavior.h"
#include "wgf_canvas.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_ecs.h"
#include "wgf_entity.h"
#include "wgf_keyboard.h"
#include "wgf_log.h"
#include "wgf_loop.h"
#include "wgf_motion.h"
#include "wgf_node.h"
#include "wgf_random.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_scene.h"
#include "wgf_shape2d.h"
#include "wgf_window.h"

/* A scene file (examples/assets/scenes/field.scene) loaded and instantiated: rocks that
 * drift and wrap, and a ship held inside the screen, every one moved by the ecs's
 * systems at the tick rate and drawn between ticks. Sparks, a prefab in the same file,
 * are spawned from the ship and die of age or at the edge; a spark that meets a rock
 * turns it red, as a trigger, read from the polled events. Escape quits where quitting
 * means anything. */

static wgf_node_t world;
static wgf_scene_t scene;
static bool made;
static float spawn_clock;

static void init(void *user)
{
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(12, 14, 22, 255));
    wgf_random_set_seed(7);
    world = wgf_canvas_create();
    scene = wgf_scene_create("scenes/field.scene");
}

/* What the systems raised since the last frame: a spark meeting a rock colors the rock. */
static void read_events(void)
{
    int events[3 * 64], n, i;
    while ((n = wgf_ecs_take_events(events, 3 * 64)) > 0) {
        for (i = 0; i < n; i += 3) {
            const wgf_entity_t entity = (wgf_entity_t)events[i + 1];
            if (events[i] == WGF_ECS_EVENT_TRIGGER_ENTER && strcmp(wgf_behavior_get_name(entity), "Rock") == 0) {
                wgf_shape2d_set_color(wgf_entity_get_component_node(entity, WGF_COMPONENT_SHAPE2D),
                                      wgf_color_make(255, 90, 80, 255));
            }
        }
    }
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    const wgf_resource_status_t status = wgf_resource_get_status(scene);
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (status == WGF_RESOURCE_STATUS_READY && !made) {
        made = true;
        wgf_scene_instantiate(scene, world);
    } else if (status == WGF_RESOURCE_STATUS_FAILED && !made) {
        made = true;
        wgf_log_message(WGF_LOG_LEVEL_ERROR, "ecs-scene: scenes/field.scene failed to load (see above)");
    }
    if (made && status == WGF_RESOURCE_STATUS_READY) {
        const wgf_entity_t ship = wgf_entity_find("ship");
        spawn_clock += dt;
        while (ship != 0 && spawn_clock > 0.05f) {
            const wgf_vec3_t at = wgf_entity_get_position(ship);
            const float angle = wgf_random_get_range(0.0f, 6.2831853f), speed = wgf_random_get_range(80, 220);
            const wgf_entity_t spark = wgf_scene_spawn(scene, "spark", world);
            spawn_clock -= 0.05f;
            wgf_entity_set_position(spark, at.x, at.y, 0);
            wgf_entity_snap(spark); /* made here: drawn here, not swept from the origin */
            wgf_motion_set_velocity(spark, cosf(angle) * speed, sinf(angle) * speed, 0);
        }
        read_events();
    }
    wgf_canvas_draw(world);
    wgf_draw_text(0, "libwgf: ecs-scene", 12, 12, 20, WGF_COLOR_WHITE);
}

int main(void)
{
    wgf_window_set_title("ecs-scene");
    wgf_window_set_size(800, 450);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
