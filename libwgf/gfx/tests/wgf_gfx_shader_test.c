#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_stage3d_priv.h"
#include "wgf_actor.h"
#include "wgf_camera3d.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_log.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_shader.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_time.h"

/* Custom shaders on sokol's dummy backend (a headless build), as libwgt's shader test: a
 * .wgfshader read (its parameters, textures, and program for this backend), and what
 * refuses one (another format version, not a .wgfshader, no program for this backend, a
 * missing file); a custom material's values kept by the shader's names, set before it
 * loads and checked once it has, and refused after by name and type; its shading, its
 * shader, and the references between them; and a stage drawing a model with one. The
 * files are written here, their sources stand-ins (the dummy backend checks the layout
 * only). Pixels: wgf_gfx_shader_window_test and _web_test. */

static int failures;

static void check(int ok, const char *what, int line)
{
    if (!ok) {
        printf("FAIL: line %d: %s\n", line, what);
        failures++;
    }
}
#define CHECK(c) check((c) ? 1 : 0, #c, __LINE__)

/* A surface shader's file as tools/pack_shader.py writes one (examples/shaders/wave.glsl's
 * shape), for `slang`, under format `version`. */
static int surface_file(char *out, size_t size, int version, const char *slang)
{
    return snprintf(out, size,
                    "wgfshader %d\n"
                    "kind surface\n"
                    "param color vec4 fs 0\n"
                    "param speed float fs 16\n"
                    "param count int fs 20\n"
                    "param amplitude float vs 0\n"
                    "texture noise_tex\n"
                    "program model %s\n"
                    "attr 0 position\nattr 1 normal\nattr 2 texcoord0\nattr 3 texcoord1\nattr 4 tangent\nattr 5 color0\n"
                    "ub 0 vertex 80 wgf_object 5\n"
                    "ub 1 fragment 1184 wgf_frame 74\n"
                    "ub 2 fragment 32 params 2\n"
                    "ub 3 vertex 16 wave_params 1\n"
                    "view 0 fragment noise_tex 2d float\n"
                    "view 11 vertex wgf_instance_tex 2d unfilterable_float\n"
                    "sampler 10 vertex wgf_data_smp nonfiltering\n"
                    "sampler 0 fragment noise_smp filtering\n"
                    "pair 0 vertex 11 10 wgf_instance_tex_wgf_data_smp\n"
                    "pair 1 fragment 0 0 noise_tex_noise_smp\n"
                    "source vs 4\nmain\n"
                    "source fs 4\nmain\n"
                    "end\n",
                    version, slang);
}

static void write_file(const char *path, const char *text, int size)
{
    const wgf_fs_task_t task = wgf_fs_write(path, (const unsigned char *)text, size);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    CHECK(wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_DONE);
    wgf_fs_task_destroy(task);
}

static void settle(wgf_handle_t resource)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(resource) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 10.0) {
        wgf_core_priv_update();
    }
}

static void test_files(void)
{
    static char text[4096];
    wgf_shader_t shader, again, failed;
    int size;

    size = surface_file(text, sizeof(text), 1, "glsl410");
    write_file("good.wgfshader", text, size);
    size = surface_file(text, sizeof(text), 2, "glsl410");
    write_file("version.wgfshader", text, size);
    size = surface_file(text, sizeof(text), 1, "glsl300es"); /* the web's alone: none for this backend */
    write_file("web_only.wgfshader", text, size);
    write_file("nonsense.wgfshader", "not a shader\n", 13);

    shader = wgf_shader_create("good.wgfshader");
    CHECK(shader != 0 && wgf_resource_get_status(shader) == WGF_RESOURCE_STATUS_PENDING);
    again = wgf_shader_create("good.wgfshader");
    CHECK(again == shader); /* shared by its path, one more reference */
    wgf_resource_release(again);
    settle(shader);
    CHECK(wgf_resource_get_status(shader) == WGF_RESOURCE_STATUS_READY);

    failed = wgf_shader_create("version.wgfshader");
    settle(failed);
    CHECK(wgf_resource_get_status(failed) == WGF_RESOURCE_STATUS_FAILED);
    wgf_resource_release(failed);
    failed = wgf_shader_create("web_only.wgfshader");
    settle(failed);
    CHECK(wgf_resource_get_status(failed) == WGF_RESOURCE_STATUS_FAILED);
    wgf_resource_release(failed);
    failed = wgf_shader_create("nonsense.wgfshader");
    settle(failed);
    CHECK(wgf_resource_get_status(failed) == WGF_RESOURCE_STATUS_FAILED);
    wgf_resource_release(failed);
    failed = wgf_shader_create("missing.wgfshader");
    settle(failed);
    CHECK(wgf_resource_get_status(failed) == WGF_RESOURCE_STATUS_FAILED);
    wgf_resource_release(failed);
    wgf_resource_release(shader);
}

static void test_material(void)
{
    wgf_shader_t shader = wgf_shader_create("good.wgfshader");
    wgf_material_t material, built_in;
    wgf_texture_t target;
    wgf_vec4_t v;

    CHECK(wgf_material_create_custom(0) == 0);
    built_in = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    CHECK(wgf_material_create_custom(built_in) == 0); /* a material isn't a shader */
    CHECK(wgf_material_get_shader(built_in) == 0);
    CHECK(wgf_material_create(WGF_MATERIAL_SHADING_CUSTOM) == 0);

    /* set while its shader loads: kept, then checked against it */
    material = wgf_material_create_custom(shader);
    CHECK(material != 0 && wgf_material_get_shader(material) == shader);
    CHECK(wgf_material_get_shading(material) == WGF_MATERIAL_SHADING_CUSTOM);
    CHECK(!wgf_material_set_shading(material, WGF_MATERIAL_SHADING_PBR));
    CHECK(wgf_material_set_vec4(material, "color", 0.5f, 0.25f, 1.0f, 1.0f));
    CHECK(wgf_material_set_float(material, "nope", 1.0f));   /* unknown until it loads */
    CHECK(wgf_material_set_vec2(material, "speed", 1.0f, 2.0f)); /* of another type: so too */
    CHECK(!wgf_material_set_float(material, "", 1.0f));
    CHECK(wgf_material_set_texture(material, "noise_tex", 0));
    CHECK(wgf_material_set_texture(material, "no_tex", 0));
    wgf_resource_release(shader); /* the material holds its own */
    settle(shader);
    CHECK(wgf_resource_get_status(shader) == WGF_RESOURCE_STATUS_READY);

    v = wgf_material_get_vec4(material, "color");
    CHECK(v.x == 0.5f && v.y == 0.25f && v.z == 1.0f && v.w == 1.0f); /* kept */
    CHECK(wgf_material_get_float(material, "nope") == 0.0f);          /* dropped, warned */
    CHECK(wgf_material_get_vec2(material, "speed").x == 0.0f);        /* dropped, warned */
    CHECK(!wgf_material_set_float(material, "nope", 1.0f));            /* refused now */
    CHECK(!wgf_material_set_vec2(material, "speed", 1.0f, 2.0f));
    CHECK(wgf_material_set_float(material, "speed", 3.0f) && wgf_material_get_float(material, "speed") == 3.0f);
    CHECK(wgf_material_set_float(material, "amplitude", 0.25f)); /* the vertex hook's */
    CHECK(wgf_material_set_int(material, "count", 7) && wgf_material_get_int(material, "count") == 7);
    CHECK(!wgf_material_set_int(material, "speed", 1));
    CHECK(wgf_material_set_color(material, "color", WGF_COLOR_WHITE));
    v = wgf_material_get_vec4(material, "color"); /* a color is read back as the vec4 it set */
    CHECK(v.x == 1.0f && v.y == 1.0f && v.z == 1.0f && v.w == 1.0f);
    CHECK(!wgf_material_set_color(material, "speed", WGF_COLOR_WHITE)); /* a color goes in a vec3 or vec4 */
    CHECK(!wgf_material_set_texture(material, "no_tex", 0)); /* refused now */
    target = wgf_texture_create_target(8, 8);
    CHECK(wgf_material_set_texture(material, "noise_tex", target));
    CHECK(wgf_material_get_texture(material, "noise_tex") == target);
    CHECK(wgf_material_set_texture_sampling(material, "noise_tex", WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_MIRROR,
                                            WGF_TEXTURE_FILTER_NEAREST));
    CHECK(wgf_material_get_texture_wrap_v(material, "noise_tex") == WGF_TEXTURE_WRAP_MIRROR);
    CHECK(!wgf_material_set_float(material, "base_color", 1.0f)); /* a built-in's names don't apply */
    wgf_resource_release(target); /* the material holds it */
    CHECK(wgf_resource_get_status(target) == WGF_RESOURCE_STATUS_READY);

    /* a stage draws a model with it: one draw of its own shader */
    {
        const wgf_actor_t stage = wgf_stage3d_create(), camera = wgf_camera3d_create();
        const wgf_mesh_t mesh = wgf_mesh_create_cube(1.0f, 1.0f, 1.0f);
        const wgf_actor_t model = wgf_model_create(mesh);
        wgf_resource_release(mesh);
        wgf_actor_set_position(camera, 0.0f, 0.0f, 5.0f);
        wgf_stage3d_set_camera(stage, camera);
        wgf_model_set_material(model, 0, material);
        wgf_actor_set_parent(model, stage);
        wgf_platform_priv_set_size(64, 64);
        wgf_gfx_priv_begin_frame();
        wgf_stage3d_draw(stage);
        wgf_gfx_priv_end_frame();
        CHECK(wgf_gfx_priv_stage3d_get_draw_calls() == 1);
        wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
        wgf_actor_destroy(camera, WGF_ACTOR_DESTROY_CHILDREN);
    }

    wgf_resource_release(material); /* its shader and texture go with it */
    CHECK(wgf_resource_get_status(shader) == WGF_RESOURCE_STATUS_NONE);
    CHECK(wgf_resource_get_status(target) == WGF_RESOURCE_STATUS_NONE);
    wgf_resource_release(built_in);
}

int main(void)
{
    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the refusals warn, on purpose */
    CHECK(wgf_gfx_priv_start());
    test_files();
    test_material();
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
