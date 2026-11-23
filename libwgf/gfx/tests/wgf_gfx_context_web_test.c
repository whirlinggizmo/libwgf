#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GLES3/gl3.h>
#include <emscripten.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_core_load_priv.h"
#include "wgf_draw.h"
#include "wgf_emitter2d.h"
#include "wgf_fs.h"
#include "wgf_image.h"
#include "wgf_light.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shader.h"
#include "wgf_sprite_batch.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_window.h"
#include "wgf_web_canvas.h"

/* A lost WebGL context, in a browser (tools/run_in_browser.py): a scene of every part it is
 * practical to show -- a texture from a file and the same image as a glTF file's, each over
 * 16 KB (so neither is one sokol's restore keeps), a blank texture written in code, a mesh
 * made from numbers, lit by a directional light whose shadow a cube casts on a plane, a
 * custom shader's material (shaders/flat.wgfshader) with the file's texture, text, a sprite
 * batch, particles, and a render target drawn once -- read whole, then the context lost
 * (WEBGL_lose_context) from outside a frame. While lost: frames and ticks go on, nothing is
 * drawn, a target and a blank texture made then are READY, and a texture loading then waits.
 * Given back: the frame it is back in redraws the target (wgf_render_is_context_restored),
 * the texture that waited loads, and once every load is done the canvas reads as before,
 * every pixel, with no GL error. */

#define WIDTH 128
#define HEIGHT 96

static int failures, frames, ticks, step, waited;
static int lost_frames, lost_ticks;
static wgf_fs_task_t tasks[5];
static wgf_texture_t checked, written, target, lost_target, lost_blank, lost_load;
static wgf_mesh_t square_file;
static wgf_material_t flat;
static wgf_actor_t stage3d, stage2d;
static unsigned char before[WIDTH * HEIGHT * 4], after[WIDTH * HEIGHT * 4];
static unsigned char code_pixels[128 * 64 * 4]; /* 32 KB: more than sokol's restore keeps */

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The context lost, and given back, by the extension the browser has for it, each from a
 * task of its own: outside any frame, as a real loss arrives. */
EM_JS(int, lose_context, (void), {
    var ext = GLctx.getExtension("WEBGL_lose_context");
    if (!ext) return 0;
    Module["wgfTestLoseContext"] = ext;
    setTimeout(function() { ext.loseContext(); }, 0);
    return 1;
})
EM_JS(void, restore_context, (void), {
    var ext = Module["wgfTestLoseContext"];
    setTimeout(function() { ext.restoreContext(); }, 0);
})

/* A unit square facing +z, textured by the image beside it (the checked PNG). */
static unsigned char square[92];
static const char gltf[] =
    "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"KHR_materials_unlit\"],"
    "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"name\":\"square\",\"mesh\":0}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"indices\":2,"
    "\"material\":0}]}],\"materials\":[{\"extensions\":{\"KHR_materials_unlit\":{}},"
    "\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
    "\"textures\":[{\"source\":0,\"sampler\":0}],\"samplers\":[{\"magFilter\":9728,\"minFilter\":9728}],"
    "\"images\":[{\"uri\":\"checked.png\"}],"
    "\"buffers\":[{\"uri\":\"square.bin\",\"byteLength\":92}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":48},"
    "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":80,\"byteLength\":12}],"
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\","
    "\"min\":[-0.5,-0.5,0],\"max\":[0.5,0.5,0]},{\"bufferView\":1,\"componentType\":5126,\"count\":4,"
    "\"type\":\"VEC2\"},{\"bufferView\":2,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}]}";

static void make_square(void)
{
    static const float positions[12] = {-0.5f, -0.5f, 0, 0.5f, -0.5f, 0, 0.5f, 0.5f, 0, -0.5f, 0.5f, 0};
    static const float uvs[8] = {0, 1, 1, 1, 1, 0, 0, 0};
    static const unsigned short indices[6] = {0, 1, 2, 0, 2, 3};
    memcpy(square, positions, sizeof(positions));
    memcpy(square + 48, uvs, sizeof(uvs));
    memcpy(square + 80, indices, sizeof(indices));
}

static void finish_test(void)
{
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

/* The 3D stage: the glTF's square, the custom shader's, a mesh from numbers, and a cube's
 * shadow on a plane, under a directional light that casts. */
static void make_stage3d(void)
{
    static const float quad[18] = {-0.5f, -0.5f, 0, 0.5f, -0.5f, 0, 0.5f, 0.5f, 0,
                                   -0.5f, -0.5f, 0, 0.5f, 0.5f,  0, -0.5f, 0.5f, 0};
    wgf_actor_t camera, light, model;
    wgf_shader_t shader;
    stage3d = wgf_stage3d_create();
    wgf_stage3d_set_tonemap(stage3d, WGF_STAGE3D_TONEMAP_NONE, 0.0f);
    camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(camera, 0.9f);
    wgf_actor_set_position(camera, 0.0f, 1.0f, 5.0f);
    wgf_stage3d_set_camera(stage3d, camera);
    light = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(light, 1, -4, -1, 0, 1, 0);
    wgf_light_set_intensity(light, 3.0f);
    wgf_light_set_shadow_casting(light, true);
    wgf_light_set_shadow_map_size(light, 512);
    wgf_light_set_shadow_distance(light, 20.0f);
    wgf_actor_set_parent(light, stage3d);

    model = wgf_model_create(wgf_mesh_create_plane(3.0f, 2.0f, 0)); /* the floor, receiving */
    wgf_actor_set_position(model, 0.0f, -1.2f, 0.0f);
    wgf_actor_set_parent(model, stage3d);
    model = wgf_model_create(wgf_mesh_create_cube(0.4f, 0.4f, 0.4f)); /* casting */
    wgf_actor_set_position(model, 0.0f, -0.6f, 0.0f);
    wgf_model_set_shadow_casting(model, true);
    wgf_actor_set_parent(model, stage3d);

    model = wgf_model_create(wgf_mesh_create_triangles(quad, 18, NULL, 0, NULL, 0, NULL, 0)); /* from numbers */
    wgf_actor_set_position(model, 1.6f, 0.0f, 0.0f);
    wgf_actor_set_parent(model, stage3d);

    shader = wgf_shader_create("shaders/flat.wgfshader");
    flat = wgf_material_create_custom(shader);
    wgf_resource_release(shader); /* the material holds it */
    wgf_material_set_vec4(flat, "color", 1.0f, 1.0f, 0.5f, 1.0f);
    wgf_material_set_texture(flat, "tex", checked);
    model = wgf_model_create(wgf_mesh_create_plane(1.0f, 1.0f, 0));
    wgf_actor_set_rotation(model, 1.5707963f, 0.0f, 0.0f); /* facing the camera */
    wgf_actor_set_position(model, -1.6f, 0.0f, 0.0f);
    wgf_model_set_material(model, 0, flat);
    wgf_actor_set_parent(model, stage3d);

    model = wgf_model_create(square_file);
    wgf_actor_set_position(model, 0.0f, 0.4f, 0.0f);
    wgf_actor_set_parent(model, stage3d);
}

/* The 2D stage: a sprite batch of two quads of the file's texture, and particles: one burst,
 * standing still, living long, so every frame shows the same. */
static void make_stage2d(void)
{
    static const float positions[4] = {84, 8, 100, 8};
    static const float sizes[4] = {12, 12, 12, 12};
    wgf_actor_t batch, emitter;
    stage2d = wgf_stage2d_create();
    batch = wgf_sprite_batch_create(checked);
    wgf_sprite_batch_set_count(batch, 2);
    wgf_sprite_batch_set_positions(batch, 0, positions, 2);
    wgf_sprite_batch_set_sizes(batch, 0, sizes, 2);
    wgf_actor_set_parent(batch, stage2d);
    emitter = wgf_emitter2d_create();
    wgf_emitter2d_set_rate(emitter, 0.0f);
    wgf_emitter2d_set_life(emitter, 10000.0f, 10000.0f);
    wgf_emitter2d_set_velocity(emitter, 0.0f, 0.0f, 0.0f, 0.0f);
    wgf_emitter2d_set_size(emitter, 6.0f, 6.0f, 0.0f);
    wgf_emitter2d_set_color(emitter, WGF_COLOR_WHITE, WGF_COLOR_WHITE);
    wgf_actor_set_position(emitter, 118.0f, 8.0f, 0.0f);
    wgf_actor_set_parent(emitter, stage2d);
    wgf_emitter2d_burst(emitter, 1);
}

/* The target: yellow with a green square, drawn when asked (once, and again when the
 * context is back), shown every frame. */
static void draw_target(void)
{
    wgf_render_begin_target(target);
    wgf_draw_rectangle(4, 4, 8, 8, WGF_COLOR_GREEN);
    wgf_render_end_target();
}

/* The frame's drawing: begun by app's runtime (or not, while lost), ended here so it can be
 * read. */
static void draw(bool target_too)
{
    if (target_too) draw_target();
    wgf_stage3d_draw(stage3d);
    wgf_draw_texture(checked, 0, 0, 24, 24, WGF_COLOR_WHITE);
    wgf_draw_texture(written, 24, 0, 24, 24, WGF_COLOR_WHITE);
    wgf_draw_text(0, "Ab", 50, 2, 16, WGF_COLOR_WHITE);
    wgf_draw_texture(target, 0, 26, 16, 16, WGF_COLOR_WHITE);
    wgf_stage2d_draw(stage2d);
    wgf_gfx_priv_end_frame();
}

static void read_canvas(unsigned char *rgba)
{
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static void compare(void)
{
    int x, y, differ = 0;
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            const unsigned char *a = &before[(y * WIDTH + x) * 4], *b = &after[(y * WIDTH + x) * 4];
            if (abs(a[0] - b[0]) > 2 || abs(a[1] - b[1]) > 2 || abs(a[2] - b[2]) > 2) {
                if (differ < 8) {
                    printf("FAIL: at %d, %d (from the top) read %d %d %d, before the loss %d %d %d\n", x,
                           HEIGHT - 1 - y, b[0], b[1], b[2], a[0], a[1], a[2]);
                }
                differ++;
            }
        }
    }
    if (differ > 0) printf("FAIL: %d pixels differ from before the loss\n", differ);
    failures += differ > 0 ? 1 : 0;
}

static bool any_drawn(const unsigned char *rgba)
{
    int i;
    for (i = 0; i < WIDTH * HEIGHT * 4; i += 4) {
        if (rgba[i] > 120 || rgba[i + 1] > 120 || rgba[i + 2] > 120) return true;
    }
    return false;
}

static void on_tick(void *user)
{
    (void)user;
    ticks++;
}

static void on_frame(void *user)
{
    int i, pending = 0;
    (void)user;
    if (++frames > 3000) {
        printf("FAIL: still waiting at step %d\n", step);
        failures++;
        finish_test();
        return;
    }
    switch (step) {
    case 0: /* the files */
        make_square();
        {
            const wgf_image_t image = wgf_image_create_checked(128, 128, 16, 16, WGF_COLOR_RED, WGF_COLOR_BLUE);
            tasks[0] = wgf_image_save(image, "ctx/checked.png");
            tasks[1] = wgf_image_save(image, "ctx/later.png");
        }
        tasks[2] = wgf_fs_write("ctx/square.gltf", (const unsigned char *)gltf, (int)sizeof(gltf) - 1);
        tasks[3] = wgf_fs_write("ctx/square.bin", square, (int)sizeof(square));
        step++;
        return;
    case 1: /* loaded */
        for (i = 0; i < 4; i++) pending += wgf_fs_task_get_status(tasks[i]) == WGF_FS_TASK_STATUS_PENDING;
        if (pending > 0) return;
        for (i = 0; i < 4; i++) {
            expect(wgf_fs_task_get_status(tasks[i]) == WGF_FS_TASK_STATUS_DONE, "a file written");
            wgf_fs_task_destroy(tasks[i]);
        }
        checked = wgf_texture_create("ctx/checked.png");
        square_file = wgf_mesh_create("ctx/square.gltf");
        written = wgf_texture_create_blank(128, 64);
        for (i = 0; i < 128 * 64; i++) {
            code_pixels[i * 4 + 0] = (unsigned char)(i % 128 * 2);
            code_pixels[i * 4 + 1] = (unsigned char)(i / 128 * 4);
            code_pixels[i * 4 + 2] = 200;
            code_pixels[i * 4 + 3] = 255;
        }
        expect(wgf_texture_write(written, code_pixels, (int)sizeof(code_pixels)), "the code texture written");
        target = wgf_texture_create_target(16, 16);
        wgf_render_set_target_clear_color(target, WGF_COLOR_YELLOW);
        make_stage3d();
        make_stage2d();
        step++;
        return;
    case 2:
        if (wgf_resource_get_status(checked) == WGF_RESOURCE_STATUS_PENDING ||
            wgf_resource_get_status(square_file) == WGF_RESOURCE_STATUS_PENDING ||
            wgf_resource_get_status(wgf_material_get_shader(flat)) == WGF_RESOURCE_STATUS_PENDING) {
            return;
        }
        expect(wgf_resource_get_status(checked) == WGF_RESOURCE_STATUS_READY, "the file's texture loaded");
        expect(wgf_resource_get_status(square_file) == WGF_RESOURCE_STATUS_READY, "the glTF file loaded");
        expect(wgf_resource_get_status(wgf_material_get_shader(flat)) == WGF_RESOURCE_STATUS_READY,
               "the custom shader loaded");
        draw(true);
        waited = 0;
        step++;
        return;
    case 3: /* a few frames, the shadows' and particles' settled, then read */
        draw(false);
        if (++waited < 10) return;
        read_canvas(before);
        expect(any_drawn(before), "the scene drawn");
        expect(!wgf_render_is_context_lost() && !wgf_render_is_context_restored(), "neither lost nor restored");
        if (!lose_context()) {
            printf("wgf_gfx_context_web_test: SKIPPING (no WEBGL_lose_context)\n");
            finish_test();
            return;
        }
        waited = 0;
        step++;
        return;
    case 4: /* until it is lost */
        draw(false);
        if (!wgf_render_is_context_lost()) return;
        lost_frames = frames;
        lost_ticks = ticks;
        expect(!wgf_gfx_priv_is_in_frame(), "no frame while lost");
        lost_target = wgf_texture_create_target(8, 8);
        lost_blank = wgf_texture_create_blank(4, 4);
        lost_load = wgf_texture_create("ctx/later.png");
        expect(wgf_resource_get_status(lost_target) == WGF_RESOURCE_STATUS_READY, "a target made while lost: READY");
        expect(wgf_resource_get_status(lost_blank) == WGF_RESOURCE_STATUS_READY, "a blank made while lost: READY");
        expect(wgf_texture_write(written, code_pixels, (int)sizeof(code_pixels)), "a write while lost: taken");
        step++;
        return;
    case 5: /* frames go on, lost, for a while; then it is given back */
        draw(true);
        expect(wgf_render_is_context_lost(), "still lost");
        if (frames - lost_frames < 30) return;
        expect(ticks > lost_ticks, "ticks go on while lost");
        expect(wgf_resource_get_status(lost_load) == WGF_RESOURCE_STATUS_PENDING,
               "a texture loading while lost waits for the context");
        restore_context();
        step++;
        return;
    case 6: /* until it is back: the target redrawn in that frame */
        if (!wgf_render_is_context_restored()) {
            draw(false);
            expect(wgf_render_is_context_lost(), "lost until it is back");
            return;
        }
        expect(!wgf_render_is_context_lost(), "not lost once back");
        expect(wgf_gfx_priv_is_in_frame(), "the frame it is back in is drawn");
        draw(true);
        waited = 0;
        step++;
        return;
    case 7: /* every load done, a few frames more, then read */
        draw(false);
        expect(!wgf_render_is_context_restored(), "restored for one frame alone");
        if (wgf_core_priv_load_get_pending_count() > 0 || ++waited < 10) return;
        expect(wgf_resource_get_status(lost_load) == WGF_RESOURCE_STATUS_READY, "the texture that waited loaded");
        read_canvas(after);
        compare();
        expect(glGetError() == GL_NO_ERROR, "no GL error");
        finish_test();
        return;
    default: return;
    }
}

int main(void)
{
    web_canvas_set_size(WIDTH, HEIGHT);
    wgf_render_set_clear_color(WGF_COLOR_DARKGRAY);
    wgf_app_run(NULL, on_tick, on_frame, NULL, NULL);
    return 0;
}
