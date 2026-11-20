#include <math.h>
#include <stdio.h>
#include <string.h>

#include "draw/wgf_gfx_draw3d_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_actor.h"
#include "wgf_camera3d.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_draw.h"
#include "wgf_fs.h"
#include "wgf_log.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_time.h"

/* Immediate mode's model draws, headless (what is recorded, on sokol's dummy backend): a
 * generated cube's wires (6 vertices a triangle) and points (one a vertex), placed by its
 * actor; a glTF file of two joints ("hip", and "arm" a unit above it) and a triangle the
 * arm's joint weights, played: its wires and points posed on the CPU where its animation
 * has the arm at two times (as wgf_gfx_skin_test's joint matrices say), a hidden node not
 * drawn, and its skeleton (the hip a cross, with nothing above it; the arm a line from the
 * hip), from its root or its node; and refusals: outside 3D, a handle that isn't a model,
 * a destroyed one, a file not yet loaded. Pixels: wgf_gfx_draw3d_window_test. */

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
    return fabsf(a - b) < 1e-4f;
}

static void write_file(const char *path, const void *data, int size)
{
    const wgf_fs_task_t task = wgf_fs_write(path, data, size);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

static void settle(wgf_mesh_t mesh)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(mesh) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
}

static void put32(unsigned char *at, unsigned int value)
{
    at[0] = (unsigned char)(value & 0xFF);
    at[1] = (unsigned char)((value >> 8) & 0xFF);
    at[2] = (unsigned char)((value >> 16) & 0xFF);
    at[3] = (unsigned char)((value >> 24) & 0xFF);
}

#define S2 0.70710678f

/* The file's buffer (wgf_gfx_skin_test's): the triangle, its joints and weights, the inverse
 * binds, and "raise"'s keys -- the arm 1 to 3 up over 2 s, the hip a step to a quarter turn
 * about z at 1 s. */
static int make_bin(unsigned char *bin)
{
    static const float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};                  /* 0 */
    static const unsigned char joints[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};     /* 36 */
    static const float weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};            /* 48 */
    static const float bind[32] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,   /* 96: the hip's */
                                   1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1}; /* the arm's, 1 up */
    static const float raise_times[2] = {0, 2};                                      /* 224 */
    static const float raise_places[6] = {0, 1, 0, 0, 3, 0};                         /* 232 */
    static const float step_times[2] = {0, 1};                                       /* 256 */
    static const float step_turns[8] = {0, 0, 0, 1, 0, 0, S2, S2};                   /* 264 */
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

/* What a model's pose gave: its parts, vertices, and triangles' indices, its first two
 * vertices' places, and its lowest x. */
typedef struct posed_t {
    int parts, vertices, indices;
    float first[6];
    float min_x;
} posed_t;

static void collect(const float *positions, int vertex_count, const uint32_t *indices, int index_count, void *user)
{
    posed_t *posed = (posed_t *)user;
    int v;
    (void)indices;
    if (posed->vertices == 0 && vertex_count >= 2) memcpy(posed->first, positions, sizeof(posed->first));
    for (v = 0; v < vertex_count; v++) {
        if (posed->vertices == 0 && v == 0) posed->min_x = positions[0];
        if (positions[3 * v] < posed->min_x) posed->min_x = positions[3 * v];
    }
    posed->parts++;
    posed->vertices += vertex_count;
    posed->indices += index_count;
}

static posed_t pose(wgf_actor_t model, bool *ok)
{
    posed_t posed;
    memset(&posed, 0, sizeof(posed));
    *ok = wgf_gfx_priv_draw_model_pose(model, collect, &posed);
    return posed;
}

int main(void)
{
    wgf_mesh_t cube_mesh, file_mesh, late_mesh;
    wgf_actor_t camera, cube, root, body, late, gone;
    int vertex_count = 0, index_count = 0, before;
    bool ok;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the refusals below warn, on purpose */
    write_glb("draw-model-test/arm.glb");
    wgf_platform_priv_set_size(640, 480);
    expect(wgf_gfx_priv_start(), "gfx starts");
    camera = wgf_camera3d_create();
    wgf_actor_set_position(camera, 0, 0, 10);

    /* a generated cube, placed by its actor */
    cube_mesh = wgf_mesh_create_cube(1, 1, 1);
    cube = wgf_model_create(cube_mesh);
    wgf_actor_set_position(cube, 5, 0, 0);
    expect(wgf_gfx_priv_mesh_get_vertices(cube_mesh, &vertex_count) != NULL &&
               wgf_gfx_priv_mesh_get_indices(cube_mesh, &index_count) != NULL && index_count == 36,
           "the cube's 12 triangles");
    {
        const posed_t posed = pose(cube, &ok);
        expect(ok && posed.parts == 1 && posed.vertices == vertex_count && posed.indices == index_count,
               "its pose: one part, every vertex and index");
        expect(near(posed.min_x, 4.5f), "placed by its actor: its left face at x 4.5");
    }

    /* refused outside 3D */
    expect(!wgf_draw_model_wires(cube, WGF_COLOR_RED) && !wgf_draw_model_points(cube, WGF_COLOR_RED) &&
               !wgf_draw_model_skeleton(cube, WGF_COLOR_RED),
           "outside a frame: refused");
    wgf_gfx_priv_begin_frame();
    before = sgl_num_vertices();
    expect(!wgf_draw_model_wires(cube, WGF_COLOR_RED) && sgl_num_vertices() == before, "in 2D: refused, nothing drawn");
    expect(wgf_draw_begin_3d(camera), "3D begun");

    before = sgl_num_vertices();
    expect(wgf_draw_model_wires(cube, WGF_COLOR_RED) && sgl_num_vertices() - before == 6 * 12,
           "the cube's wires: three edges a triangle");
    before = sgl_num_vertices();
    expect(wgf_draw_model_points(cube, WGF_COLOR_RED) && sgl_num_vertices() - before == vertex_count,
           "its points: one a vertex");
    before = sgl_num_vertices();
    expect(wgf_draw_model_skeleton(cube, WGF_COLOR_RED) && sgl_num_vertices() == before,
           "no skin: no skeleton, and not a refusal");

    /* refusals: not a model, none, a destroyed one, a file not yet loaded */
    gone = wgf_model_create(cube_mesh);
    wgf_actor_destroy(gone, WGF_ACTOR_DESTROY_CHILDREN);
    before = sgl_num_vertices();
    expect(!wgf_draw_model_wires(camera, WGF_COLOR_RED) && !wgf_draw_model_points(0, WGF_COLOR_RED) &&
               !wgf_draw_model_skeleton(camera, WGF_COLOR_RED) && !wgf_draw_model_wires(gone, WGF_COLOR_RED) &&
               sgl_num_vertices() == before,
           "a camera, 0, and a destroyed model: refused, nothing drawn");
    late_mesh = wgf_mesh_create("draw-model-test/late.glb"); /* never written: PENDING, then FAILED */
    late = wgf_model_create(late_mesh);
    expect(!wgf_draw_model_wires(late, WGF_COLOR_RED) && !wgf_draw_model_skeleton(late, WGF_COLOR_RED) &&
               sgl_num_vertices() == before,
           "a file not loaded: refused");
    wgf_draw_end_3d();
    wgf_gfx_priv_end_frame();

    /* a skinned file, played, posed on the CPU */
    file_mesh = wgf_mesh_create("draw-model-test/arm.glb");
    root = wgf_model_create(file_mesh);
    settle(file_mesh);
    expect(wgf_resource_get_status(file_mesh) == WGF_RESOURCE_STATUS_READY, "the file READY");
    body = wgf_actor_find(root, "body");
    wgf_actor_set_position(root, 10, 0, 0);
    expect(wgf_model_play_animation(root, 0, 0.0f) && wgf_model_set_animation_time(root, 0.5f), "played at 0.5 s");
    {
        const posed_t posed = pose(root, &ok);
        expect(ok && posed.parts == 1 && posed.vertices == 3 && posed.indices == 3,
               "the root's pose: its node's triangle");
        expect(near(posed.first[0], 10.0f) && near(posed.first[1], 0.5f) && near(posed.first[2], 0.0f) &&
                   near(posed.first[3], 11.0f) && near(posed.first[4], 0.5f),
               "at 0.5 s the arm half a unit above its bind: the triangle with it, placed by the root");
    }
    wgf_model_set_animation_time(root, 1.0f);
    {
        const posed_t posed = pose(root, &ok);
        expect(ok && near(posed.first[0], 9.0f) && near(posed.first[1], 0.0f) && near(posed.first[3], 9.0f) &&
                   near(posed.first[4], 1.0f),
               "at 1 s the hip's quarter turn about z: the triangle turned with the arm");
    }
    {
        const posed_t posed = pose(body, &ok);
        expect(ok && posed.vertices == 3 && near(posed.first[0], 9.0f), "its node posed the same, given alone");
    }

    wgf_gfx_priv_begin_frame();
    expect(wgf_draw_begin_3d(camera), "3D begun again");
    before = sgl_num_vertices();
    expect(wgf_draw_model_wires(root, WGF_COLOR_GREEN) && sgl_num_vertices() - before == 6, "its wires: one triangle");
    before = sgl_num_vertices();
    expect(wgf_draw_model_points(root, WGF_COLOR_GREEN) && sgl_num_vertices() - before == 3, "its points: three");
    before = sgl_num_vertices();
    expect(wgf_draw_model_skeleton(root, WGF_COLOR_YELLOW) && sgl_num_vertices() - before == 6 + 2,
           "its skeleton: the hip a cross (nothing above it), the arm a line from the hip");
    before = sgl_num_vertices();
    expect(wgf_draw_model_skeleton(body, WGF_COLOR_YELLOW) && sgl_num_vertices() - before == 6 + 2,
           "given its node, the node's skin");
    wgf_actor_set_visible(body, false);
    before = sgl_num_vertices();
    expect(wgf_draw_model_wires(root, WGF_COLOR_GREEN) && sgl_num_vertices() == before, "a hidden node: not drawn");
    wgf_actor_set_visible(body, true);
    wgf_draw_end_3d();
    expect(!wgf_draw_model_wires(root, WGF_COLOR_GREEN), "after end_3d: refused");
    wgf_gfx_priv_end_frame();

    wgf_actor_destroy(root, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(late, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(cube, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(camera, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_resource_release(file_mesh);
    wgf_resource_release(late_mesh);
    wgf_resource_release(cube_mesh);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_gfx_draw_model_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
