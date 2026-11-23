#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mesh/wgf_gfx_mesh_priv.h"
#include "mesh/wgf_gfx_mesh_record_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_log.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_time.h"

/* Meshes merged (wgf_mesh_create_merged), headless. Two pieces of one character: a.glb, the
 * skeleton (a hip, an arm a unit above it, a red triangle the arm weights), and b.glb, bound in
 * another pose (its arm 2 up) with a joint a.glb hasn't (a hand under the arm, weighting its
 * blue triangle) and a hat under its arm that isn't skinned, drawn with a blue material that
 * masks. Merged onto a.glb's skeleton: one mesh of a file's tree (the skeleton's nodes, and a
 * node showing the merged part), the two bodies one primitive (their materials alike but for
 * color, the colors in the vertices, a white copy drawing them) and the hat another (its
 * material masks, and kept, alone in its group); the hand's triangle bound to the arm, moved from b's bind to a's; the hat
 * following the arm where b placed it; the merged skin's reach measured again; a model of it
 * the file's root. Merged with no skeleton: a plain mesh, every piece at rest. Waiting while a
 * piece loads; FAILED when one fails; and the refusals. */

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

/* Updates, the parts' too (what makes a merge), until `mesh` isn't PENDING. */
static void settle(wgf_mesh_t mesh)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(mesh) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
        wgf_core_priv_part_update(0.0f);
    }
}

static void put32(unsigned char *at, unsigned int value)
{
    at[0] = (unsigned char)(value & 0xFF);
    at[1] = (unsigned char)((value >> 8) & 0xFF);
    at[2] = (unsigned char)((value >> 16) & 0xFF);
    at[3] = (unsigned char)((value >> 24) & 0xFF);
}

/* `text` and `bin` as a .glb file at `path`. */
static void write_glb(const char *path, const char *text, const unsigned char *bin, int bin_size)
{
    static unsigned char glb[8192];
    const size_t text_size = strlen(text);
    const unsigned int json_size = (unsigned int)((text_size + 3) & ~3u);
    const unsigned int total = 12 + 8 + json_size + 8 + (unsigned int)bin_size;
    memset(glb, ' ', sizeof(glb));
    memcpy(glb, "glTF", 4);
    put32(glb + 4, 2);
    put32(glb + 8, total);
    put32(glb + 12, json_size);
    memcpy(glb + 16, "JSON", 4);
    memcpy(glb + 20, text, text_size);
    put32(glb + 20 + json_size, (unsigned int)bin_size);
    memcpy(glb + 24 + json_size, "BIN\0", 4);
    memcpy(glb + 28 + json_size, bin, (size_t)bin_size);
    write_file(path, glb, (int)total);
}

#define TRIANGLE_AT(y) {0, (y), 0, 1, (y), 0, 0, (y) + 1, 0}
#define LIFT(y) 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, (y), 0, 1

/* a.glb: the skeleton, its arm bound a unit up, its triangle at the hip the arm weights */
static const char a_json[] =
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,2]}],"
    "\"nodes\":[{\"name\":\"hip\",\"children\":[1]},{\"name\":\"arm\",\"translation\":[0,1,0]},"
    "{\"name\":\"body\",\"mesh\":0,\"skin\":0}],"
    "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":3}],"
    "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1]}}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2},\"material\":0}]}],"
    "\"buffers\":[{\"byteLength\":224}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":224}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,1,0],"
    "\"max\":[1,2,0]},"
    "{\"bufferView\":0,\"byteOffset\":36,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
    "{\"bufferView\":0,\"byteOffset\":48,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
    "{\"bufferView\":0,\"byteOffset\":96,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"}]}";

static int a_bin(unsigned char *bin)
{
    static const float positions[9] = TRIANGLE_AT(1);
    static const unsigned char joints[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    static const float weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    static const float bind[32] = {LIFT(0), LIFT(-1)};
    memset(bin, 0, 224);
    memcpy(bin + 0, positions, sizeof(positions));
    memcpy(bin + 36, joints, sizeof(joints));
    memcpy(bin + 48, weights, sizeof(weights));
    memcpy(bin + 96, bind, sizeof(bind));
    return 224;
}

/* b.glb: its arm 2 up, a hand a unit above that weighting a triangle at 3 up, and a hat half a
 * unit above the arm, not skinned, its material masking */
static const char b_json[] =
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,3]}],"
    "\"nodes\":[{\"name\":\"hip\",\"children\":[1]},{\"name\":\"arm\",\"translation\":[0,2,0],\"children\":[2,4]},"
    "{\"name\":\"hand\",\"translation\":[0,1,0]},{\"name\":\"glove\",\"mesh\":0,\"skin\":0},"
    "{\"name\":\"hat\",\"translation\":[0,0.5,0],\"mesh\":1}],"
    "\"skins\":[{\"joints\":[0,1,2],\"inverseBindMatrices\":3}],"
    "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,0,1,1]}},"
    "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,0,1,1]},\"alphaMode\":\"MASK\"}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2},\"material\":0}]},"
    "{\"primitives\":[{\"attributes\":{\"POSITION\":4},\"material\":1}]}],"
    "\"buffers\":[{\"byteLength\":324}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":324}],"
    "\"accessors\":["
    "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,3,0],"
    "\"max\":[1,4,0]},"
    "{\"bufferView\":0,\"byteOffset\":36,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
    "{\"bufferView\":0,\"byteOffset\":48,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
    "{\"bufferView\":0,\"byteOffset\":96,\"componentType\":5126,\"count\":3,\"type\":\"MAT4\"},"
    "{\"bufferView\":0,\"byteOffset\":288,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],"
    "\"max\":[1,1,0]}]}";

static int b_bin(unsigned char *bin)
{
    static const float positions[9] = TRIANGLE_AT(3);
    static const unsigned char joints[12] = {2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0};
    static const float weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    static const float bind[48] = {LIFT(0), LIFT(-2), LIFT(-3)};
    static const float hat[9] = TRIANGLE_AT(0);
    memset(bin, 0, 324);
    memcpy(bin + 0, positions, sizeof(positions));
    memcpy(bin + 36, joints, sizeof(joints));
    memcpy(bin + 48, weights, sizeof(weights));
    memcpy(bin + 96, bind, sizeof(bind));
    memcpy(bin + 288, hat, sizeof(hat));
    return 324;
}

/* Primitive `p` of `mesh`: whether its vertex `v` is at (x, y), and its color, joint, and weight
 * (a joint of -1: not skinned). */
static int vertex_is(wgf_mesh_t mesh, int p, int v, float x, float y, const float color[4], int joint)
{
    const wgf_gfx_priv_mesh_record_t *record = wgf_gfx_priv_mesh_record(mesh);
    const wgf_gfx_priv_mesh_record_primitive_t *prim;
    const float *at;
    if (record == NULL || p >= record->primitive_count) return 0;
    prim = &record->primitives[p];
    if (v >= prim->vertex_count) return 0;
    at = &prim->vertices[(size_t)v * WGF_GFX_PRIV_MESH_VERTEX_FLOATS];
    if (!near(at[0], x) || !near(at[1], y) || !near(at[2], 0.0f)) return 0;
    if (!near(at[14], color[0]) || !near(at[15], color[1]) || !near(at[16], color[2]) || !near(at[17], color[3])) return 0;
    if (joint < 0) return prim->skin == NULL;
    return prim->skin != NULL && near(prim->skin[(size_t)v * WGF_GFX_PRIV_MESH_SKIN_FLOATS], (float)joint) &&
           near(prim->skin[(size_t)v * WGF_GFX_PRIV_MESH_SKIN_FLOATS + 4], 1.0f);
}

int main(void)
{
    static const float red[4] = {1, 0, 0, 1}, blue[4] = {0, 0, 1, 1}, white[4] = {1, 1, 1, 1};
    unsigned char bin[512];
    wgf_mesh_t a, b, merged, plain, failed, pieces[3];
    wgf_actor_t stage, root;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the refusals below are logged, on purpose */
    write_glb("models/a.glb", a_json, bin, a_bin(bin));
    write_glb("models/b.glb", b_json, bin, b_bin(bin));
    wgf_platform_priv_set_size(640, 480);
    expect(wgf_gfx_priv_start(), "gfx starts");
    stage = wgf_stage3d_create();

    /* merged while its pieces load: PENDING until they are in */
    a = wgf_mesh_create("models/a.glb");
    b = wgf_mesh_create("models/b.glb");
    pieces[0] = a;
    pieces[1] = b;
    merged = wgf_mesh_create_merged(pieces, 2, a);
    expect(merged != 0 && wgf_resource_get_status(merged) == WGF_RESOURCE_STATUS_PENDING,
           "PENDING while its pieces load");
    root = wgf_model_create(merged);
    wgf_actor_set_parent(root, stage);
    settle(a);
    settle(b);
    settle(merged);
    expect(wgf_resource_get_status(merged) == WGF_RESOURCE_STATUS_READY, "READY once they are");
    {
        const wgf_gfx_priv_mesh_record_t *record = wgf_gfx_priv_mesh_record(merged);
        wgf_mesh_t part = 0;
        expect(record != NULL && record->from_file && record->node_count == 4 && record->skin_count == 1 &&
                   record->part_count == 1 && strcmp(record->nodes[0].name, "hip") == 0 &&
                   strcmp(record->nodes[1].name, "arm") == 0 && record->nodes[2].mesh == -1 &&
                   strcmp(record->nodes[3].name, "merged") == 0 && record->nodes[3].skin == 0,
               "a file's: the skeleton's nodes, its own showing nothing, and a node showing the merged part");
        if (record != NULL && record->part_count == 1) part = record->parts[0];
        expect(wgf_gfx_priv_mesh_get_primitive_count(part) == 2,
               "two primitives: the bodies' materials alike but for color, the hat's masking");
        expect(vertex_is(part, 0, 0, 0, 1, red, 1) && vertex_is(part, 0, 2, 0, 2, red, 1),
               "a's triangle where it was, red, the arm's");
        expect(vertex_is(part, 0, 3, 0, 2, blue, 1) && vertex_is(part, 0, 4, 1, 2, blue, 1) &&
                   vertex_is(part, 0, 5, 0, 3, blue, 1),
               "b's hand's triangle bound to the arm, the hand a's hasn't: a unit above a's arm, as it was above b's");
        expect(vertex_is(part, 1, 0, 0, 1.5f, white, 1) && vertex_is(part, 1, 2, 0, 2.5f, white, 1),
               "the hat following the arm, half a unit above it, as b placed it; alone in its group, its color left "
               "in its material");
        {
            const wgf_material_t white = wgf_mesh_get_material(part, 0);
            const wgf_vec4_t color = wgf_material_get_vec4(white, "base_color");
            expect(white != 0 && near(color.x, 1) && near(color.y, 1) && near(color.z, 1) && near(color.w, 1) &&
                       wgf_material_get_alpha_mode(white) == WGF_ALPHA_MODE_OPAQUE &&
                       wgf_material_get_alpha_mode(wgf_mesh_get_material(part, 1)) == WGF_ALPHA_MODE_MASK,
                   "the bodies drawn by a white copy, the hat by its own masking material");
        }
        expect(record != NULL && record->skins[0].joint_count == 2 && near(record->skins[0].radii[1], 2.0f),
               "the merged skin a's, the arm's reach measured over every vertex it weights (b's farthest, 2)");
    }
    expect(wgf_actor_find(root, "hip/arm") != 0 && wgf_actor_find(root, "merged") != 0,
           "a model of it the file's root: the skeleton's joints actors, and the merged part");
    expect(wgf_model_find_node(root, "arm") != 0, "a socket found by name");

    /* with no skeleton: a plain mesh, every piece at rest */
    plain = wgf_mesh_create_merged(pieces, 2, 0);
    settle(plain);
    {
        const wgf_gfx_priv_mesh_record_t *record = wgf_gfx_priv_mesh_record(plain);
        expect(record != NULL && !record->from_file && record->primitive_count == 2 && record->nodes == NULL,
               "with no skeleton a plain mesh of two primitives");
        expect(vertex_is(plain, 0, 0, 0, 1, red, -1) && vertex_is(plain, 0, 3, 0, 3, blue, -1) &&
                   vertex_is(plain, 1, 0, 0, 2.5f, white, -1),
               "each piece where its file put it, skinned ones at their bind pose, none skinned");
    }

    /* a piece that fails, and the refusals */
    pieces[2] = wgf_mesh_create("models/missing.glb");
    failed = wgf_mesh_create_merged(pieces, 3, a);
    settle(failed);
    expect(wgf_resource_get_status(failed) == WGF_RESOURCE_STATUS_FAILED, "FAILED when a piece fails");
    expect(wgf_mesh_create_merged(pieces, 0, 0) == 0 && wgf_mesh_create_merged(NULL, 2, 0) == 0 &&
               wgf_mesh_create_merged(pieces, 65, 0) == 0,
           "refused: no meshes, or more than 64");
    {
        const wgf_mesh_t cube = wgf_mesh_create_cube(1, 1, 1);
        const wgf_mesh_t odd[1] = {(wgf_mesh_t)stage};
        expect(wgf_mesh_create_merged(odd, 1, 0) == 0, "refused: a piece that isn't a mesh");
        expect(wgf_mesh_create_merged(pieces, 2, cube) == 0, "refused: a skeleton that isn't a glTF file's");
        wgf_resource_release(cube);
    }

    wgf_resource_release(failed);
    wgf_resource_release(plain);
    wgf_actor_destroy(root, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_resource_release(merged);
    wgf_resource_release(pieces[2]);
    wgf_resource_release(b);
    wgf_resource_release(a);
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_gfx_merge_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
