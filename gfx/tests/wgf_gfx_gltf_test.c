#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mesh/wgf_gfx_mesh_priv.h"
#include "mesh/wgf_gfx_mesh_record_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_stage3d_priv.h"
#include "wgf_camera3d.h"
#include "wgf_actor.h"
#include "wgf_color.h"
#include "wgf_core_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_fs.h"
#include "wgf_light.h"
#include "wgf_log.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_time.h"

/* glTF files, headless: a .gltf with its buffer beside it and the same file as a .glb load
 * in the background to READY, their material and their mesh read (the file's one glTF mesh
 * is one mesh of libwgf's, shared by the two nodes showing it); a model of one is the
 * file's root, its node tree made its children (named, placed, a light where the file has
 * one), also for a model made while the mesh was pending; the same path is the same mesh;
 * a missing file, a file that isn't glTF, and a missing buffer FAILED; what isn't a glTF
 * path is refused; a file's root and its nodes keep their meshes; and drawn: each node's
 * mesh, nothing for a pending file, the placeholder for a failed one. */

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

/* Update until none of `meshes` is pending, as a frame loop would: a worker parses. */
static void settle(const wgf_mesh_t *meshes, int count)
{
    const double start = wgf_time_get_seconds();
    for (;;) {
        int i, pending = 0;
        for (i = 0; i < count; i++) pending += wgf_resource_get_status(meshes[i]) == WGF_RESOURCE_STATUS_PENDING;
        if (pending == 0 || wgf_time_get_seconds() - start > 30.0) return;
        wgf_core_priv_update();
    }
}

/* One triangle, its node "body" at x 1 with two children: "wheel", the same triangle 1 up,
 * and "lamp", a point light. The buffer is given as `buffer` (a .gltf's uri, or none for a
 * .glb's own). */
#define TRI_JSON(buffer)                                                                                               \
    "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"KHR_lights_punctual\"],"                                \
    "\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[{\"type\":\"point\",\"color\":[1,0.5,0],"                  \
    "\"intensity\":5,\"range\":10}]}},"                                                                                \
    "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"                                                                       \
    "\"nodes\":[{\"name\":\"body\",\"mesh\":0,\"translation\":[1,0,0],\"children\":[1,2]},"                            \
    "{\"name\":\"wheel\",\"mesh\":0,\"translation\":[0,1,0]},"                                                         \
    "{\"name\":\"lamp\",\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}}],"                                     \
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}],"                               \
    "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1],\"metallicFactor\":0.25}}],"             \
    "\"buffers\":[{" buffer "\"byteLength\":36}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],"                  \
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","                          \
    "\"min\":[0,0,0],\"max\":[1,1,0]}]}"

static const float tri[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};

static void put32(unsigned char *at, unsigned int value)
{
    at[0] = (unsigned char)(value & 0xFF);
    at[1] = (unsigned char)((value >> 8) & 0xFF);
    at[2] = (unsigned char)((value >> 16) & 0xFF);
    at[3] = (unsigned char)((value >> 24) & 0xFF);
}

/* The same file as a .glb: its header, its JSON chunk (padded with spaces), its BIN chunk. */
static void write_glb(const char *path)
{
    static const char json[] = TRI_JSON("");
    unsigned char glb[4096];
    const unsigned int json_size = (unsigned int)((sizeof(json) - 1 + 3) & ~3u);
    const unsigned int total = 12 + 8 + json_size + 8 + (unsigned int)sizeof(tri);
    memset(glb, ' ', sizeof(glb));
    memcpy(glb, "glTF", 4);
    put32(glb + 4, 2);
    put32(glb + 8, total);
    put32(glb + 12, json_size);
    memcpy(glb + 16, "JSON", 4);
    memcpy(glb + 20, json, sizeof(json) - 1);
    put32(glb + 20 + json_size, (unsigned int)sizeof(tri));
    memcpy(glb + 24 + json_size, "BIN\0", 4);
    memcpy(glb + 28 + json_size, tri, sizeof(tri));
    write_file(path, glb, (int)total);
}

/* A file's tree under `root`, as TRI_JSON makes it. */
static void expect_tree(wgf_actor_t root, wgf_mesh_t mesh, const char *what)
{
    const wgf_actor_t body = wgf_actor_find(root, "body");
    const wgf_actor_t wheel = wgf_actor_find(root, "body/wheel");
    const wgf_actor_t lamp = wgf_actor_find(root, "body/lamp");
    const wgf_actor_t light = wgf_actor_get_child(lamp, 0);
    char line[128];
    snprintf(line, sizeof(line), "%s: the file's nodes, its root's children", what);
    expect(wgf_actor_get_child_count(root) == 1 && body != 0 && wheel != 0 && lamp != 0, line);
    snprintf(line, sizeof(line), "%s: each placed as in the file", what);
    expect(near(wgf_actor_get_position(body).x, 1.0f) && near(wgf_actor_get_position(wheel).y, 1.0f), line);
    snprintf(line, sizeof(line), "%s: a node with triangles a model of its mesh, one both nodes share; one without a plain actor",
             what);
    expect(wgf_actor_get_kind(body) == WGF_ACTOR_KIND_MODEL && wgf_model_get_mesh(body) != 0 &&
               wgf_model_get_mesh(body) == wgf_model_get_mesh(wheel) && wgf_model_get_mesh(wheel) != mesh &&
               wgf_actor_get_kind(lamp) == WGF_ACTOR_KIND_PLAIN,
           line);
    snprintf(line, sizeof(line), "%s: the file's light under its node", what);
    expect(light != 0 && wgf_light_get_type(light) == WGF_LIGHT_TYPE_POINT && near(wgf_light_get_intensity(light), 5) &&
               near(wgf_light_get_range(light), 10) && wgf_light_get_color(light) == wgf_color_make(255, 188, 0, 255),
           line);
    snprintf(line, sizeof(line), "%s: its nodes keep the file's mesh", what);
    expect(!wgf_model_set_mesh(wheel, 0) && !wgf_model_set_mesh(root, 0), line);
}

/* One frame drawing `stage`: how many parts it kept. */
static int drawn(wgf_actor_t stage)
{
    int count;
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    count = wgf_gfx_priv_stage3d_get_item_count();
    wgf_gfx_priv_end_frame();
    return count;
}

int main(void)
{
    static const char gltf[] = TRI_JSON("\"uri\":\"tri.bin\",");
    static const char lost[] = TRI_JSON("\"uri\":\"gone.bin\",");
    static const char not_gltf[] = "{ this is not glTF";
    wgf_mesh_t mesh, glb, early, failed[3], part;
    wgf_actor_t root, early_root, stage, camera;
    wgf_vec3_t lo, hi;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the failures below warn, on purpose */
    write_file("models/tri.gltf", gltf, (int)sizeof(gltf) - 1);
    write_file("models/tri.bin", tri, (int)sizeof(tri));
    write_file("models/lost.gltf", lost, (int)sizeof(lost) - 1);
    write_file("models/broken.gltf", not_gltf, (int)sizeof(not_gltf) - 1);
    write_glb("models/tri.glb");
    wgf_platform_priv_set_size(640, 480);
    expect(wgf_gfx_priv_start(), "gfx starts");
    stage = wgf_stage3d_create();
    camera = wgf_camera3d_create();
    wgf_stage3d_set_camera(stage, camera);
    wgf_actor_set_parent(camera, stage);
    wgf_actor_set_position(camera, 0, 0, 5);

    /* a model made while its mesh loads: its tree made when the mesh is READY */
    early = wgf_mesh_create("models/tri.gltf");
    expect(early != 0 && wgf_resource_get_status(early) == WGF_RESOURCE_STATUS_PENDING, "pending at once");
    early_root = wgf_model_create(early);
    expect(early_root != 0 && wgf_actor_get_child_count(early_root) == 0, "a model of it: no tree yet");
    wgf_actor_set_parent(early_root, stage);
    expect(drawn(stage) == 0, "a pending file draws nothing");
    settle(&early, 1);
    expect(wgf_resource_get_status(early) == WGF_RESOURCE_STATUS_READY, "then READY");
    expect_tree(early_root, early, "made while pending");
    expect(drawn(stage) == 2, "then its nodes' meshes draw, the root nothing of its own");

    mesh = wgf_mesh_create("./models/tri.gltf");
    expect(mesh == early, "the same path is the same mesh");
    expect(wgf_mesh_get_material_count(mesh) == 1 &&
               near(wgf_material_get_vec4(wgf_mesh_get_material(mesh, 0), "base_color").x, 1.0f) &&
               near(wgf_material_get_float(wgf_mesh_get_material(mesh, 0), "metallic"), 0.25f),
           "its material, the file's");
    {
        /* the file's one glTF mesh, one mesh resource: its primitive and its box; the file's own
           record holds no triangles; and it is shared, referenced by the file and by each node
           showing it (early_root's body and wheel), one more a node of each further model */
        const wgf_gfx_priv_mesh_record_t *record = wgf_gfx_priv_mesh_record(mesh);
        part = record != NULL && record->part_count == 1 ? record->parts[0] : 0;
        expect(part != 0 && part != mesh && wgf_gfx_priv_mesh_get_primitive_count(part) == 1 &&
                   wgf_gfx_priv_mesh_get_bounds(part, &lo, &hi) && near(hi.x, 1.0f) && near(hi.y, 1.0f) &&
                   wgf_gfx_priv_mesh_get_primitive_count(mesh) == 0 &&
                   wgf_model_get_mesh(wgf_actor_find(early_root, "body")) == part,
               "the file's glTF mesh is one mesh of its own, the file's record holding no triangles");
        expect(part != 0 && wgf_core_priv_resource_get(part)->refs == 1 + 2,
               "referenced by the file, and once a node showing it");
    }
    root = wgf_model_create(mesh);
    expect_tree(root, mesh, "made when READY");
    expect(part != 0 && wgf_core_priv_resource_get(part)->refs == 1 + 4, "a second model's two nodes share it too");
    {
        const wgf_actor_t other = wgf_model_create(0);
        wgf_actor_set_parent(wgf_actor_create(), other);
        expect(!wgf_model_set_mesh(other, mesh), "a file's mesh goes on no model with children");
        wgf_actor_destroy(other, WGF_ACTOR_DESTROY_CHILDREN);
    }
    wgf_actor_destroy(root, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(early_root, WGF_ACTOR_DESTROY_CHILDREN);
    expect(wgf_resource_release(mesh), "the second reference let go");

    glb = wgf_mesh_create("models/tri.glb");
    settle(&glb, 1);
    expect(wgf_resource_get_status(glb) == WGF_RESOURCE_STATUS_READY && wgf_mesh_get_material_count(glb) == 1,
           "the same file as a .glb: READY");
    root = wgf_model_create(glb);
    expect_tree(root, glb, "a .glb");
    wgf_actor_destroy(root, WGF_ACTOR_DESTROY_CHILDREN);

    failed[0] = wgf_mesh_create("models/missing.gltf");
    failed[1] = wgf_mesh_create("models/broken.gltf");
    failed[2] = wgf_mesh_create("models/lost.gltf");
    settle(failed, 3);
    expect(wgf_resource_get_status(failed[0]) == WGF_RESOURCE_STATUS_FAILED, "a missing file: FAILED");
    expect(wgf_resource_get_status(failed[1]) == WGF_RESOURCE_STATUS_FAILED, "a file that isn't glTF: FAILED");
    expect(wgf_resource_get_status(failed[2]) == WGF_RESOURCE_STATUS_FAILED, "a missing buffer: FAILED");
    root = wgf_model_create(failed[0]);
    expect(root != 0 && wgf_actor_get_child_count(root) == 0, "a model of a failed file: no tree");
    wgf_actor_set_parent(root, stage);
    expect(drawn(stage) == 1, "a failed file draws the placeholder");
    wgf_actor_destroy(root, WGF_ACTOR_DESTROY_CHILDREN);
    expect(wgf_mesh_create("models/tri.obj") == 0 && wgf_mesh_create("") == 0 && wgf_mesh_create(NULL) == 0,
           "a path that isn't glTF: none (logged)");

    /* a mesh let go of while it loads */
    early = wgf_mesh_create("models/tri.gltf#again");
    expect(wgf_resource_release(early), "let go of while pending");
    {
        int i;
        for (i = 0; i < 200; i++) wgf_core_priv_update();
    }

    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    expect(wgf_resource_get_status(glb) == WGF_RESOURCE_STATUS_NONE &&
               wgf_resource_get_status(early) == WGF_RESOURCE_STATUS_NONE,
           "gfx's stop frees the meshes");
    wgf_core_priv_shutdown();

    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
