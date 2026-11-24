#include <math.h>
#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_image.h"
#include "wgf_input.h"
#include "wgf_log.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_mouse.h"
#include "wgf_platform_input_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_pointer.h"
#include "wgf_resource.h"
#include "wgf_shape2d.h"
#include "wgf_shape3d.h"
#include "wgf_sprite.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_time.h"

/* Picking and the pointer, headless. 3D: the screen's middle ray along the camera's forward;
 * a cube model's hit (point, normal, distance) from the screen and from any ray; the nearest
 * of several, what lets a ray through (not visible, not pickable, not enabled); a 3D shape's
 * sphere; a skinned glTF file hit where its animation has it, reported as its file's root;
 * a miss. 2D: sprites by their quads (the topmost of two), turned and scaled, and by their
 * texels' alpha once the texture's file is read; a rounded rectangle's corner. The pointer,
 * frame by frame as the loop runs it, with mouse and touch events fed as the window's: hover
 * entered and left, the HUD over the world, a press on a 3D actor that doesn't capture and
 * a click, a press on a 2D actor that does, dragged off and let go (no click), the UI's
 * capture, a stage not drawn last frame, a click by touch, and edges a tick sees once. */

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

static void write_file(const char *path, const void *data, int size)
{
    const wgf_fs_task_t task = wgf_fs_write(path, data, size);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

static void settle(wgf_handle_t resource)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(resource) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
}

/* ---- a skinned file: wgf_gfx_draw_model_test's ------------------------------------------ */

static void put32(unsigned char *at, unsigned int value)
{
    at[0] = (unsigned char)(value & 0xFF);
    at[1] = (unsigned char)((value >> 8) & 0xFF);
    at[2] = (unsigned char)((value >> 16) & 0xFF);
    at[3] = (unsigned char)((value >> 24) & 0xFF);
}

#define S2 0.70710678f

/* A triangle (0, 0, 0), (1, 0, 0), (0, 1, 0) weighted to "arm", a unit above "hip"; "raise"
 * moves the arm 1 to 3 up over 2 s. */
static int make_bin(unsigned char *bin)
{
    static const float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    static const unsigned char joints[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    static const float weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    static const float bind[32] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
                                   1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1};
    static const float raise_times[2] = {0, 2};
    static const float raise_places[6] = {0, 1, 0, 0, 3, 0};
    static const float step_times[2] = {0, 1};
    static const float step_turns[8] = {0, 0, 0, 1, 0, 0, S2, S2};
    memcpy(bin + 0, positions, sizeof(positions));
    memcpy(bin + 36, joints, sizeof(joints));
    memcpy(bin + 48, weights, sizeof(weights));
    memcpy(bin + 96, bind, sizeof(bind));
    memcpy(bin + 224, raise_times, sizeof(raise_times));
    memcpy(bin + 232, raise_places, sizeof(raise_places));
    memcpy(bin + 256, step_times, sizeof(step_times));
    memcpy(bin + 264, step_turns, sizeof(step_turns));
    return 296;
}

static const char json[] =
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,2]}],"
    "\"nodes\":[{\"name\":\"hip\",\"children\":[1]},{\"name\":\"arm\",\"translation\":[0,1,0]},"
    "{\"name\":\"body\",\"mesh\":0,\"skin\":0}],"
    "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":3}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2}}]}],"
    "\"animations\":["
    "{\"name\":\"raise\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}},"
    "{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"rotation\"}}],"
    "\"samplers\":[{\"input\":4,\"output\":5},{\"input\":6,\"output\":7,\"interpolation\":\"STEP\"}]}],"
    "\"buffers\":[{\"byteLength\":296}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":296}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],"
    "\"max\":[1,1,0]},"
    "{\"bufferView\":0,\"byteOffset\":36,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
    "{\"bufferView\":0,\"byteOffset\":48,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
    "{\"bufferView\":0,\"byteOffset\":96,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},"
    "{\"bufferView\":0,\"byteOffset\":224,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],"
    "\"max\":[2]},"
    "{\"bufferView\":0,\"byteOffset\":232,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
    "{\"bufferView\":0,\"byteOffset\":256,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],"
    "\"max\":[1]},"
    "{\"bufferView\":0,\"byteOffset\":264,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"}]}";

static void write_glb(const char *path)
{
    static unsigned char glb[8192];
    unsigned char bin[512];
    const int bin_size = make_bin(bin);
    const size_t text_size = strlen(json);
    const unsigned int json_size = (unsigned int)((text_size + 3) & ~3u);
    const unsigned int total = 12 + 8 + json_size + 8 + (unsigned int)bin_size;
    memset(glb, ' ', sizeof(glb));
    memcpy(glb, "glTF", 4);
    put32(glb + 4, 2);
    put32(glb + 8, total);
    put32(glb + 12, json_size);
    memcpy(glb + 16, "JSON", 4);
    memcpy(glb + 20, json, text_size);
    put32(glb + 20 + json_size, (unsigned int)bin_size);
    memcpy(glb + 24 + json_size, "BIN\0", 4);
    memcpy(glb + 28 + json_size, bin, (size_t)bin_size);
    write_file(path, glb, (int)total);
}

/* ---- 3D -------------------------------------------------------------------------------- */

static void picking_3d(void)
{
    wgf_actor_t stage = wgf_stage3d_create(), camera = wgf_camera3d_create(), cube, far_cube, side, ball, root;
    wgf_mesh_t cube_mesh = wgf_mesh_create_cube(2, 2, 2), file_mesh;
    wgf_vec3_t o, d, forward;
    float hit[8];

    wgf_actor_set_parent(camera, stage);
    wgf_actor_set_position(camera, 0, 0, 5);
    wgf_stage3d_set_camera(stage, camera);

    /* the screen's middle: along the camera's forward, from its near plane */
    o = wgf_camera3d_screen_to_origin(camera, 400, 300);
    d = wgf_camera3d_screen_to_direction(camera, 400, 300);
    expect(near(d.x, 0) && near(d.y, 0) && near(d.z, -1) && near(o.z, 4.9f), "the middle's ray: -z, from 0.1 in front");
    wgf_actor_look_at(camera, 1, 2, 0, 0, 1, 0);
    d = wgf_camera3d_screen_to_direction(camera, 400, 300);
    forward = wgf_actor_get_world_direction(camera, 0, 0, -1);
    expect(near(d.x, forward.x) && near(d.y, forward.y) && near(d.z, forward.z), "turned: still the camera's forward");
    wgf_actor_set_rotation(camera, 0, 0, 0);

    /* a cube model in the middle */
    cube = wgf_model_create(cube_mesh);
    wgf_actor_set_parent(cube, stage);
    memset(hit, 0x7f, sizeof(hit));
    expect(wgf_stage3d_pick(stage, 400, 300, hit, 8) == cube, "the middle hits the cube");
    expect(near(hit[0], 0) && near(hit[1], 0) && near(hit[2], 1) && near(hit[3], 0),
           "its normal: +z, toward the camera");
    expect(near(hit[4], 0) && near(hit[5], 0) && near(hit[6], 1), "its point: the front face's middle");
    expect(near(hit[7], 3.9f), "its distance: 3.9 from the near plane");
    expect(wgf_stage3d_pick_ray(stage, 5, 0.5f, 0, -2, 0, 0, hit, 8) == cube && near(hit[0], 1) && near(hit[4], 1) &&
               near(hit[5], 0.5f) && near(hit[7], 2),
           "any ray: from the side, its time in the direction's lengths");
    {
        float some[3] = {9, 9, 9};
        expect(wgf_stage3d_pick(stage, 400, 300, some, 3) == cube && near(some[2], 1), "as many as fit");
    }

    /* turned and scaled: its box first, then its triangles, in its own space */
    wgf_actor_set_scale(cube, 1, 1, 2);
    expect(wgf_stage3d_pick(stage, 400, 300, hit, 8) == cube && near(hit[6], 2) && near(hit[7], 2.9f),
           "scaled along z: the front face at 2");
    wgf_actor_set_scale(cube, 1, 1, 1);

    /* the nearest of several */
    far_cube = wgf_model_create(cube_mesh);
    wgf_actor_set_parent(far_cube, stage);
    wgf_actor_set_position(far_cube, 0, 0, -4);
    side = wgf_model_create(cube_mesh);
    wgf_actor_set_parent(side, stage);
    wgf_actor_set_position(side, 10, 0, 0);
    expect(wgf_stage3d_pick(stage, 400, 300, hit, 8) == cube, "the nearest of two in line");
    wgf_actor_set_position(cube, 0, 0, -8); /* moved behind the other */
    expect(wgf_stage3d_pick(stage, 400, 300, hit, 8) == far_cube && near(hit[6], -3), "moved behind: the other");
    wgf_actor_set_position(cube, 0, 0, 0);
    wgf_actor_set_pickable(cube, false);
    expect(wgf_stage3d_pick(stage, 400, 300, NULL, 0) == far_cube && !wgf_actor_is_pickable(cube),
           "not pickable: the ray goes through");
    expect(wgf_model_pick_ray(cube, 0, 0, 10, 0, 0, -1, hit, 8) && near(hit[6], 1),
           "a model tested alone: pickable or not");
    wgf_actor_set_pickable(cube, true);
    wgf_actor_set_visible(cube, false);
    expect(wgf_stage3d_pick(stage, 400, 300, NULL, 0) == far_cube, "not visible: through");
    wgf_actor_set_visible(cube, true);
    wgf_actor_set_enabled(cube, false);
    expect(wgf_stage3d_pick(stage, 400, 300, NULL, 0) == far_cube, "not enabled: through");
    wgf_actor_set_enabled(cube, true);

    /* a 3D shape's sphere, nearer */
    ball = wgf_shape3d_create();
    wgf_shape3d_set_sphere(ball, 0.5f);
    wgf_actor_set_parent(ball, stage);
    wgf_actor_set_position(ball, 0, 0, 2);
    expect(wgf_stage3d_pick(stage, 400, 300, hit, 8) == ball && near(hit[6], 2.5f) && near(hit[2], 1),
           "a shape's sphere, in front");
    wgf_actor_destroy(ball, WGF_ACTOR_DESTROY_CHILDREN);

    /* a miss */
    memset(hit, 0x7f, sizeof(hit));
    expect(wgf_stage3d_pick_ray(stage, 0, 10, 0, 0, 1, 0, hit, 8) == 0 && hit[0] == 0.0f && hit[4] == 0.0f &&
               hit[7] == 0.0f,
           "a ray up: nothing, its hit all 0");
    expect(wgf_stage3d_pick(stage, 10, 10, NULL, 0) == 0, "a corner of the screen: nothing");
    expect(wgf_stage3d_pick_ray(stage, 0, 0, 5, 0, 0, 0, NULL, 0) == 0 &&
               wgf_stage3d_pick_ray(stage, 0, 0, NAN, 0, 0, -1, NULL, 0) == 0 &&
               wgf_stage3d_pick(camera, 400, 300, NULL, 0) == 0 &&
               !wgf_model_pick_ray(camera, 0, 0, 5, 0, 0, -1, NULL, 0),
           "refused: no direction, not finite, not a stage, not a model");
    wgf_actor_set_enabled(cube, false);
    wgf_actor_set_enabled(far_cube, false);
    wgf_actor_set_enabled(side, false);

    /* a skinned file, hit where its animation has it, reported as its root */
    file_mesh = wgf_mesh_create("pick-test/arm.glb");
    root = wgf_model_create(file_mesh);
    settle(file_mesh);
    expect(wgf_resource_get_status(file_mesh) == WGF_RESOURCE_STATUS_READY, "the skinned file READY");
    wgf_actor_set_parent(root, stage);
    wgf_actor_set_position(root, 10, 0, 0);
    expect(wgf_stage3d_pick_ray(stage, 10.1f, 1.2f, 5, 0, 0, -1, NULL, 0) == 0,
           "at rest, the triangle is below the ray");
    expect(wgf_model_play_animation(root, 0, 0.0f) && wgf_model_set_animation_time(root, 0.5f), "played at 0.5 s");
    expect(wgf_stage3d_pick_ray(stage, 10.1f, 1.2f, 5, 0, 0, -1, hit, 8) == root && near(hit[4], 10.1f) &&
               near(hit[5], 1.2f) && near(hit[6], 0) && near(hit[2], 1) && near(hit[7], 5),
           "posed half a unit up: hit, as the file's root");
    expect(wgf_model_pick_ray(root, 10.1f, 1.2f, 5, 0, 0, -1, NULL, 0), "the root tested alone: its nodes");
    expect(wgf_stage3d_pick_ray(stage, 10.1f, 0.2f, 5, 0, 0, -1, NULL, 0) == 0, "where it was at rest: nothing now");
    wgf_actor_set_visible(root, false);
    expect(wgf_stage3d_pick_ray(stage, 10.1f, 1.2f, 5, 0, 0, -1, NULL, 0) == 0, "its root hidden: through");
    wgf_actor_set_visible(root, true);
    wgf_actor_set_pickable(root, false);
    expect(wgf_stage3d_pick_ray(stage, 10.1f, 1.2f, 5, 0, 0, -1, NULL, 0) == 0, "its root not pickable: through");
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_resource_release(file_mesh);
    wgf_resource_release(cube_mesh);
}

/* ---- 2D -------------------------------------------------------------------------------- */

/* A 4 by 4 picture, its left half solid and its right half clear, as a file. */
static void write_half(const char *path)
{
    const wgf_image_t image = wgf_image_create_color(4, 4, 0x00000000u);
    wgf_fs_task_t task;
    int x, y, i;
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 2; x++) wgf_image_set_pixel(image, x, y, 0xFFFFFFFFu);
    }
    task = wgf_image_save(image, path);
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
    wgf_resource_release(image);
}

static void picking_2d(void)
{
    const wgf_actor_t stage = wgf_stage2d_create();
    wgf_texture_t texture;
    wgf_actor_t back, front, round;
    float hit[4];
    double start;

    write_half("pick-test/half.png");
    texture = wgf_texture_create("pick-test/half.png");
    settle(texture);
    expect(wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_READY, "the picture READY");

    /* two sprites, 40 by 40, centered on (100, 100) and (120, 100): the later one over */
    back = wgf_sprite_create(texture);
    wgf_sprite_set_size(back, 40, 40);
    wgf_actor_set_position(back, 100, 100, 0);
    wgf_actor_set_parent(back, stage);
    front = wgf_sprite_create(texture);
    wgf_sprite_set_size(front, 40, 40);
    wgf_actor_set_position(front, 120, 100, 0);
    wgf_actor_set_parent(front, stage);
    expect(wgf_stage2d_pick(stage, 90, 100, hit, 4) == back && near(hit[0], 90) && near(hit[2], -10) &&
               near(hit[3], 0),
           "the first alone: hit, its own point from its middle");
    expect(wgf_stage2d_pick(stage, 110, 100, NULL, 0) == front, "where both are: the one drawn last");
    expect(wgf_stage2d_pick(stage, 150, 100, hit, 4) == 0 && hit[0] == 0.0f && hit[2] == 0.0f, "outside both: none");
    wgf_actor_set_pickable(front, false);
    expect(wgf_stage2d_pick(stage, 110, 100, NULL, 0) == back, "the front not pickable: the one under it");
    wgf_actor_set_pickable(front, true);

    /* turned a quarter and scaled: (100, 100) + its own (0, -15) turned is (115, 100) */
    wgf_actor_set_rotation(back, 0, 0, 1.5707963f);
    wgf_actor_set_scale(back, 1, 0.5f, 1);
    wgf_actor_set_visible(front, false);
    expect(wgf_stage2d_pick(stage, 109, 100, hit, 4) == back && near(hit[2], 0) && near(hit[3], -18),
           "turned and squashed: its own point turned back");
    expect(wgf_stage2d_pick(stage, 115, 100, NULL, 0) == 0, "and not where it was unturned");
    wgf_actor_set_rotation(back, 0, 0, 0);
    wgf_actor_set_scale(back, 1, 1, 1);
    wgf_actor_set_visible(front, true);

    /* by its texels: the picture's right half is clear */
    expect(wgf_sprite_set_pick_alpha(front, 0.5f) && near(wgf_sprite_get_pick_alpha(front), 0.5f) &&
               !wgf_sprite_set_pick_alpha(front, 1.5f) && !wgf_sprite_set_pick_alpha(front, -0.1f) &&
               !wgf_sprite_set_pick_alpha(stage, 0.5f) && near(wgf_sprite_get_pick_alpha(front), 0.5f),
           "a threshold in 0..1, the rest refused");
    expect(wgf_stage2d_pick(stage, 130, 100, NULL, 0) == front, "before its texels are read: its quad");
    start = wgf_time_get_seconds();
    while (wgf_stage2d_pick(stage, 130, 100, NULL, 0) != 0 && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
    expect(wgf_stage2d_pick(stage, 130, 100, NULL, 0) == 0, "its clear half: through, once read");
    expect(wgf_stage2d_pick(stage, 110, 100, NULL, 0) == front, "its solid half: hit");
    wgf_actor_set_position(back, 140, 100, 0);
    expect(wgf_stage2d_pick(stage, 125, 100, NULL, 0) == back, "clear over the other: the other");
    wgf_actor_set_position(back, 100, 100, 0);

    /* a rounded rectangle's corner */
    round = wgf_shape2d_create();
    wgf_shape2d_set_rectangle(round, 100, 60);
    wgf_shape2d_set_pivot(round, 0, 0);
    wgf_shape2d_set_corner_radius(round, 20);
    wgf_actor_set_position(round, 300, 300, 0);
    wgf_actor_set_parent(round, stage);
    expect(wgf_stage2d_pick(stage, 301, 301, NULL, 0) == 0 && wgf_stage2d_pick(stage, 310, 310, NULL, 0) == round &&
               wgf_stage2d_pick(stage, 350, 301, NULL, 0) == round,
           "a rounded rectangle: its corner's outside missed, inside hit");
    wgf_shape2d_set_corner_radius(round, 0);
    expect(wgf_stage2d_pick(stage, 301, 301, NULL, 0) == round, "square: its corner hit");
    wgf_shape2d_set_pivot(round, 0.5f, 0.5f); /* as a rectangle is made: 250 to 350 across, 270 to 330 down */
    expect(wgf_stage2d_pick(stage, 255, 275, NULL, 0) == round && wgf_stage2d_pick(stage, 345, 325, NULL, 0) == round &&
               wgf_stage2d_pick(stage, 355, 300, NULL, 0) == 0 && wgf_stage2d_pick(stage, 300, 335, NULL, 0) == 0,
           "centered: hit about its actor's position");
    expect(wgf_stage2d_pick(back, 100, 100, NULL, 0) == 0, "not a stage: nothing");
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_resource_release(texture);
}

/* ---- the pointer ----------------------------------------------------------------------- */

static void mouse(sapp_event_type type, float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.mouse_button = SAPP_MOUSEBUTTON_LEFT;
    event.mouse_x = x;
    event.mouse_y = y;
    wgf_platform_priv_input_handle_event(&event, 1.0f);
}

static void touch(sapp_event_type type, float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.num_touches = 1;
    event.touches[0].identifier = 7;
    event.touches[0].pos_x = x;
    event.touches[0].pos_y = y;
    event.touches[0].changed = true;
    wgf_platform_priv_input_handle_event(&event, 1.0f);
}

static wgf_actor_t world, hud;
static bool hud_drawn = true;

/* A frame as the loop runs one: the pointer read before the ticks (the parts' begin_frame),
 * what the test reads then, then the frame drawn and its edges ended. */
static void begin(void)
{
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_FRAME);
    wgf_core_priv_part_begin_frame();
}

static void end(void)
{
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(world);
    if (hud_drawn) wgf_stage2d_draw(hud);
    wgf_gfx_priv_end_frame();
    wgf_platform_priv_input_end_tick();
    wgf_platform_priv_input_end_frame();
}

static void frame(void)
{
    begin();
    end();
}

static void pointer(void)
{
    const wgf_mesh_t cube_mesh = wgf_mesh_create_cube(2, 2, 2);
    wgf_actor_t camera, cube, button, cover;

    world = wgf_stage3d_create();
    camera = wgf_camera3d_create();
    wgf_actor_set_parent(camera, world);
    wgf_actor_set_position(camera, 0, 0, 5);
    wgf_stage3d_set_camera(world, camera);
    cube = wgf_model_create(cube_mesh);
    wgf_actor_set_parent(cube, world);
    hud = wgf_stage2d_create();
    button = wgf_shape2d_create();
    wgf_shape2d_set_rectangle(button, 100, 50);
    wgf_shape2d_set_pivot(button, 0, 0);
    wgf_actor_set_position(button, 50, 50, 0);
    wgf_actor_set_parent(button, hud);
    cover = wgf_shape2d_create(); /* the HUD over the middle of the world */
    wgf_shape2d_set_rectangle(cover, 40, 40);
    wgf_shape2d_set_pivot(cover, 0, 0);
    wgf_actor_set_position(cover, 380, 280, 0);
    wgf_actor_set_parent(cover, hud);

    expect(wgf_pointer_set_interactive(world, true) && wgf_pointer_set_interactive(hud, true) &&
               wgf_pointer_set_interactive(hud, true) && wgf_pointer_is_interactive(world) &&
               !wgf_pointer_set_interactive(cube, true) && !wgf_pointer_is_interactive(cube),
           "stages made interactive, anything else refused");
    mouse(SAPP_EVENTTYPE_MOUSE_MOVE, 75, 75);
    begin();
    expect(wgf_pointer_get_hovered() == 0, "no stage drawn yet: nothing hovered");
    end();

    begin();
    expect(wgf_pointer_get_hovered() == button && wgf_pointer_get_hover_state(button) == WGF_INPUT_STATE_PRESSED,
           "over the button: it entered");
    end();
    begin();
    expect(wgf_pointer_get_hover_state(button) == WGF_INPUT_STATE_DOWN, "the next frame: still under it");
    end();

    mouse(SAPP_EVENTTYPE_MOUSE_MOVE, 400, 300);
    begin();
    expect(wgf_pointer_get_hovered() == cover && wgf_pointer_get_hover_state(button) == WGF_INPUT_STATE_RELEASED &&
               wgf_pointer_get_hover_state(cover) == WGF_INPUT_STATE_PRESSED,
           "the HUD drawn over the world takes the pointer; the button left");
    end();
    wgf_actor_set_pickable(cover, false);
    frame();
    begin();
    expect(wgf_pointer_get_hovered() == cube && wgf_pointer_get_hover_state(cube) == WGF_INPUT_STATE_DOWN,
           "the cover not pickable: the cube under it");
    end();

    /* a press on the cube: not captured; let go over it: a click */
    mouse(SAPP_EVENTTYPE_MOUSE_DOWN, 400, 300);
    begin();
    expect(wgf_pointer_get_press_state(cube) == WGF_INPUT_STATE_PRESSED && wgf_pointer_get_held() == cube &&
               !wgf_input_is_pointer_captured(),
           "pressed on a 3D actor: held, not captured");
    end();
    mouse(SAPP_EVENTTYPE_MOUSE_UP, 400, 300);
    begin();
    expect(wgf_pointer_get_press_state(cube) == WGF_INPUT_STATE_RELEASED && wgf_pointer_is_clicked(cube) &&
               wgf_pointer_get_held() == 0,
           "let go over it: released, and clicked");
    end();
    begin();
    expect(!wgf_pointer_is_clicked(cube) && wgf_pointer_get_press_state(cube) == WGF_INPUT_STATE_UP,
           "a frame later: the edges gone");
    end();

    /* a press on the button captures; dragged off and let go: released, no click */
    mouse(SAPP_EVENTTYPE_MOUSE_MOVE, 75, 75);
    mouse(SAPP_EVENTTYPE_MOUSE_DOWN, 75, 75);
    begin();
    expect(wgf_pointer_get_press_state(button) == WGF_INPUT_STATE_PRESSED && wgf_input_is_pointer_captured(),
           "pressed on a 2D actor: the pointer captured");
    end();
    mouse(SAPP_EVENTTYPE_MOUSE_MOVE, 300, 500);
    begin();
    expect(wgf_pointer_get_press_state(button) == WGF_INPUT_STATE_DOWN && wgf_pointer_get_hovered() == 0 &&
               wgf_pointer_get_held() == button && wgf_input_is_pointer_captured(),
           "dragged off: still held, still captured");
    end();
    mouse(SAPP_EVENTTYPE_MOUSE_UP, 300, 500);
    begin();
    expect(wgf_pointer_get_press_state(button) == WGF_INPUT_STATE_RELEASED && !wgf_pointer_is_clicked(button) &&
               wgf_input_is_pointer_captured(),
           "let go off it: released, no click, captured through the frame");
    end();
    begin();
    expect(!wgf_input_is_pointer_captured(), "the frame after: let go");
    end();

    /* the UI has the pointer: nothing of the game's under it, and no press starts */
    mouse(SAPP_EVENTTYPE_MOUSE_MOVE, 75, 75);
    frame();
    wgf_input_set_pointer_captured(true);
    mouse(SAPP_EVENTTYPE_MOUSE_DOWN, 75, 75);
    begin();
    expect(wgf_pointer_get_hovered() == 0 && wgf_pointer_get_hover_state(button) == WGF_INPUT_STATE_RELEASED &&
               wgf_pointer_get_press_state(button) == WGF_INPUT_STATE_UP && wgf_pointer_get_held() == 0,
           "the UI's capture: the button left, the press not its");
    end();
    mouse(SAPP_EVENTTYPE_MOUSE_UP, 75, 75);
    wgf_input_set_pointer_captured(false);
    begin();
    expect(!wgf_pointer_is_clicked(button) && wgf_pointer_get_hover_state(button) == WGF_INPUT_STATE_PRESSED,
           "let go: no click; the UI's gone, the button entered again");
    end();

    /* a stage not drawn last frame is skipped */
    hud_drawn = false;
    frame();
    begin();
    expect(wgf_pointer_get_hovered() == 0, "the HUD not drawn: its button not hovered");
    end();
    hud_drawn = true;
    frame();

    /* a click by touch (the first finger drives the mouse) */
    touch(SAPP_EVENTTYPE_TOUCHES_BEGAN, 80, 70);
    begin();
    expect(wgf_pointer_get_press_state(button) == WGF_INPUT_STATE_PRESSED && wgf_input_is_pointer_captured(),
           "a finger down on the button: pressed, captured");
    end();
    touch(SAPP_EVENTTYPE_TOUCHES_ENDED, 80, 70);
    begin();
    expect(wgf_pointer_is_clicked(button), "lifted over it: clicked");
    end();
    frame();

    /* a tick sees a click once, a frame sees it too */
    mouse(SAPP_EVENTTYPE_MOUSE_DOWN, 75, 75);
    frame();
    mouse(SAPP_EVENTTYPE_MOUSE_UP, 75, 75);
    begin();
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_TICK);
    expect(wgf_pointer_is_clicked(button), "the first tick: clicked");
    wgf_core_priv_part_tick(1.0f / 60.0f);
    expect(!wgf_pointer_is_clicked(button), "the second: not again");
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_FRAME);
    expect(wgf_pointer_is_clicked(button), "the frame: clicked");
    end();

    /* stages leave the list */
    expect(wgf_pointer_set_interactive(hud, false) && !wgf_pointer_is_interactive(hud), "the HUD taken off");
    wgf_actor_destroy(world, WGF_ACTOR_DESTROY_CHILDREN);
    frame();
    expect(!wgf_pointer_is_interactive(world) && wgf_pointer_get_hovered() == 0, "a destroyed stage: gone");
    wgf_actor_destroy(hud, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_resource_release(cube_mesh);
}

int main(void)
{
    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    write_glb("pick-test/arm.glb");
    wgf_platform_priv_set_size(800, 600);
    wgf_platform_priv_input_reset();
    expect(wgf_gfx_priv_start(), "gfx starts");
    picking_3d();
    picking_2d();
    pointer();
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_gfx_pick_test: ok\n");
    return failures == 0 ? 0 : 1;
}
