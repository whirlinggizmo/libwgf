#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "material/wgf_gfx_shader_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "texture/wgf_gfx_cubemap_priv.h"
#include "texture/wgf_gfx_png_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_priv.h"
#include "wgf_asset.h"
#include "wgf_cubemap.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_log.h"
#include "wgf_material.h"
#include "wgf_resource.h"
#include "wgf_shader.h"
#include "wgf_texture.h"
#include "wgf_time.h"

/* Cubemaps on sokol's dummy backend (a headless build). Their CPU half: each layout told
 * from its shape (and a square, or a shape that isn't the layout given, from none), each
 * face's place in the crosses, and the faces cut from a generated 4 by 3 cross and a 3 by 4
 * one, texel by texel. Loading: a cross and a row, PENDING then READY at their face size
 * with the layout found; the same path the same cubemap; six face files READY as FACES; and
 * FAILED for a square image, a layout its shape isn't, faces of two sizes, a face that
 * isn't square, and a missing face. Loaded again in place: one image at another size, then
 * another layout; six files, any of them saved; and a broken file, or faces of two sizes,
 * keeping what it had. Bound to a custom material: refused for a texture2D's
 * name, a texture's handle, and a built-in material, kept for a textureCube's; set while its
 * shader loads, a cubemap on a texture2D's name dropped once it is READY. Pixels:
 * wgf_gfx_cubemap_window_test. */

static int failures;

static void check(int ok, const char *what, int line)
{
    if (!ok) {
        printf("FAIL: line %d: %s\n", line, what);
        failures++;
    }
}
#define CHECK(c) check((c) ? 1 : 0, #c, __LINE__)

/* A texel of face `face` at (x, y) of the faces generated here: each channel says which. */
static void texel(int face, int x, int y, unsigned char out[4])
{
    out[0] = (unsigned char)(face * 40 + 10);
    out[1] = (unsigned char)x;
    out[2] = (unsigned char)y;
    out[3] = 255;
}

/* An image of `layout` with faces of `size` (texel's), the rest grey: its RGBA, malloc'd. */
static unsigned char *make_image(wgf_cubemap_layout_t layout, int size, int *width, int *height)
{
    const int columns = layout == WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL       ? 6
                        : layout == WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE ? 4
                        : layout == WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR ? 3
                                                                           : 1;
    const int rows = layout == WGF_CUBEMAP_LAYOUT_LINE_VERTICAL         ? 6
                     : layout == WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE ? 3
                     : layout == WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR ? 4
                                                                        : 1;
    unsigned char *rgba;
    int face, x, y, column = 0, row = 0;
    *width = columns * size;
    *height = rows * size;
    rgba = (unsigned char *)malloc((size_t)*width * (size_t)*height * 4);
    if (rgba == NULL) return NULL;
    memset(rgba, 128, (size_t)*width * (size_t)*height * 4);
    for (face = 0; face < 6; face++) {
        if (!wgf_gfx_priv_cubemap_face_cell(layout, face, &column, &row)) continue;
        for (y = 0; y < size; y++) {
            for (x = 0; x < size; x++) {
                texel(face, x, y, &rgba[(((size_t)row * size + y) * (size_t)*width + (size_t)column * size + x) * 4]);
            }
        }
    }
    return rgba;
}

static void test_layouts(void)
{
    int size = -1, column = -1, row = -1;
    CHECK(wgf_gfx_priv_cubemap_layout_of(96, 16, WGF_CUBEMAP_LAYOUT_AUTO, &size) ==
              WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL &&
          size == 16);
    CHECK(wgf_gfx_priv_cubemap_layout_of(16, 96, WGF_CUBEMAP_LAYOUT_AUTO, &size) == WGF_CUBEMAP_LAYOUT_LINE_VERTICAL &&
          size == 16);
    CHECK(wgf_gfx_priv_cubemap_layout_of(64, 48, WGF_CUBEMAP_LAYOUT_AUTO, &size) ==
              WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE &&
          size == 16);
    CHECK(wgf_gfx_priv_cubemap_layout_of(48, 64, WGF_CUBEMAP_LAYOUT_AUTO, &size) ==
              WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR &&
          size == 16);
    CHECK(wgf_gfx_priv_cubemap_layout_of(64, 48, WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE, &size) ==
              WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE &&
          size == 16); /* given, and its shape */
    CHECK(wgf_gfx_priv_cubemap_layout_of(32, 32, WGF_CUBEMAP_LAYOUT_AUTO, &size) == WGF_CUBEMAP_LAYOUT_AUTO &&
          size == 0); /* a square is none */
    CHECK(wgf_gfx_priv_cubemap_layout_of(64, 48, WGF_CUBEMAP_LAYOUT_LINE_VERTICAL, &size) == WGF_CUBEMAP_LAYOUT_AUTO);
    CHECK(wgf_gfx_priv_cubemap_layout_of(66, 48, WGF_CUBEMAP_LAYOUT_AUTO, &size) == WGF_CUBEMAP_LAYOUT_AUTO);
    CHECK(wgf_gfx_priv_cubemap_layout_of(0, 0, WGF_CUBEMAP_LAYOUT_AUTO, &size) == WGF_CUBEMAP_LAYOUT_AUTO);
    CHECK(wgf_gfx_priv_cubemap_layout_of(96, 16, WGF_CUBEMAP_LAYOUT_FACES, &size) == WGF_CUBEMAP_LAYOUT_AUTO);
    /* raylib's places: +X, -X, +Y, -Y, +Z, -Z */
    CHECK(wgf_gfx_priv_cubemap_face_cell(WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE, 0, &column, &row) && column == 2 &&
          row == 1);
    CHECK(wgf_gfx_priv_cubemap_face_cell(WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE, 2, &column, &row) && column == 1 &&
          row == 0);
    CHECK(wgf_gfx_priv_cubemap_face_cell(WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE, 5, &column, &row) && column == 3 &&
          row == 1);
    CHECK(wgf_gfx_priv_cubemap_face_cell(WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR, 1, &column, &row) && column == 1 &&
          row == 3);
    CHECK(wgf_gfx_priv_cubemap_face_cell(WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR, 4, &column, &row) && column == 0 &&
          row == 1);
    CHECK(wgf_gfx_priv_cubemap_face_cell(WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL, 3, &column, &row) && column == 3 &&
          row == 0);
    CHECK(!wgf_gfx_priv_cubemap_face_cell(WGF_CUBEMAP_LAYOUT_AUTO, 0, &column, &row) &&
          !wgf_gfx_priv_cubemap_face_cell(WGF_CUBEMAP_LAYOUT_LINE_VERTICAL, 6, &column, &row));
}

/* The faces cut from a generated image of `layout`: each texel where it was. */
static void test_extract(wgf_cubemap_layout_t layout)
{
    const int size = 5;
    int width, height, found_size = 0, face, x, y, wrong = 0;
    unsigned char *image = make_image(layout, size, &width, &height);
    unsigned char faces[6 * 5 * 5 * 4], want[4];
    CHECK(image != NULL);
    if (image == NULL) return;
    CHECK(wgf_gfx_priv_cubemap_layout_of(width, height, WGF_CUBEMAP_LAYOUT_AUTO, &found_size) == layout &&
          found_size == size);
    memset(faces, 0, sizeof(faces));
    wgf_gfx_priv_cubemap_extract(image, width, layout, size, faces);
    for (face = 0; face < 6; face++) {
        for (y = 0; y < size; y++) {
            for (x = 0; x < size; x++) {
                texel(face, x, y, want);
                wrong += memcmp(&faces[(((size_t)face * size + y) * size + x) * 4], want, 4) != 0;
            }
        }
    }
    CHECK(wrong == 0);
    free(image);
}

static void write_bytes(const char *path, const unsigned char *data, int size)
{
    const wgf_fs_task_t task = wgf_fs_write(path, data, size);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    CHECK(wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_DONE);
    wgf_fs_task_destroy(task);
}

/* A PNG at `path` of `width` by `height` RGBA. */
static void write_png(const char *path, const unsigned char *rgba, int width, int height)
{
    int size = 0;
    unsigned char *png = wgf_gfx_priv_png_encode(rgba, width, height, &size);
    CHECK(png != NULL);
    if (png != NULL) write_bytes(path, png, size);
    free(png);
}

/* A PNG at `path` of a layout's generated image. */
static void write_layout(const char *path, wgf_cubemap_layout_t layout, int size)
{
    int width, height;
    unsigned char *rgba = make_image(layout, size, &width, &height);
    if (rgba != NULL) write_png(path, rgba, width, height);
    free(rgba);
}

/* A PNG at `path`, `width` by `height`, of one color. */
static void write_plain(const char *path, int width, int height, unsigned char red)
{
    unsigned char *rgba = (unsigned char *)malloc((size_t)width * (size_t)height * 4);
    int i;
    if (rgba == NULL) return;
    for (i = 0; i < width * height; i++) {
        rgba[i * 4] = red;
        rgba[i * 4 + 1] = rgba[i * 4 + 2] = 0;
        rgba[i * 4 + 3] = 255;
    }
    write_png(path, rgba, width, height);
    free(rgba);
}

static void settle(wgf_handle_t resource)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(resource) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
}

/* Update until nothing is loading: a cubemap of six files loading again is six requests. */
static void settle_all(void)
{
    const double start = wgf_time_get_seconds();
    while (wgf_core_priv_load_get_pending_count() > 0 && wgf_time_get_seconds() - start < 30.0) wgf_core_priv_update();
}

/* Loaded again in place: one image (another size, then another layout, then a broken file),
 * and six files (one saved, then faces of two sizes, then a broken one). */
static void test_reload(void)
{
    wgf_cubemap_t one, six;
    write_layout("cubes/again.png", WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE, 8);
    write_plain("cubes/a.png", 8, 8, 10);
    write_plain("cubes/b.png", 8, 8, 20);
    one = wgf_cubemap_create("cubes/again.png", WGF_CUBEMAP_LAYOUT_AUTO);
    six = wgf_cubemap_create_faces("cubes/a.png", "cubes/b.png", "cubes/a.png", "cubes/b.png", "cubes/a.png",
                                   "cubes/b.png");
    settle_all();
    CHECK(wgf_cubemap_get_size(one) == 8 && wgf_cubemap_get_size(six) == 8);

    write_layout("cubes/again.png", WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE, 16);
    CHECK(wgf_asset_can_reload("cubes/again.png") && wgf_asset_reload("cubes/again.png") == 1);
    settle_all();
    CHECK(wgf_resource_get_status(one) == WGF_RESOURCE_STATUS_READY && wgf_cubemap_get_size(one) == 16);
    write_layout("cubes/again.png", WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL, 4); /* its layout read again */
    CHECK(wgf_asset_reload("cubes/again.png") == 1);
    settle_all();
    CHECK(wgf_cubemap_get_size(one) == 4 && wgf_cubemap_get_layout(one) == WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL);
    write_bytes("cubes/again.png", (const unsigned char *)"not an image", 12);
    CHECK(wgf_asset_reload("cubes/again.png") == 1);
    settle_all();
    CHECK(wgf_resource_get_status(one) == WGF_RESOURCE_STATUS_READY && wgf_cubemap_get_size(one) == 4); /* kept */

    write_plain("cubes/a.png", 16, 16, 30);
    write_plain("cubes/b.png", 16, 16, 40);
    CHECK(wgf_asset_can_reload("cubes/b.png") && wgf_asset_reload("cubes/b.png") == 1); /* any of the six */
    settle_all();
    CHECK(wgf_resource_get_status(six) == WGF_RESOURCE_STATUS_READY && wgf_cubemap_get_size(six) == 16);
    write_plain("cubes/b.png", 8, 8, 50); /* its faces two sizes */
    CHECK(wgf_asset_reload("cubes/b.png") == 1);
    settle_all();
    CHECK(wgf_resource_get_status(six) == WGF_RESOURCE_STATUS_READY && wgf_cubemap_get_size(six) == 16); /* kept */
    write_bytes("cubes/b.png", (const unsigned char *)"not an image", 12);
    CHECK(wgf_asset_reload("cubes/a.png") == 1);
    settle_all();
    CHECK(wgf_resource_get_status(six) == WGF_RESOURCE_STATUS_READY && wgf_cubemap_get_size(six) == 16); /* kept */
    write_plain("cubes/b.png", 4, 4, 60);
    write_plain("cubes/a.png", 4, 4, 70);
    CHECK(wgf_asset_reload("cubes/a.png") == 1);
    settle_all();
    CHECK(wgf_cubemap_get_size(six) == 4); /* and loads again after them */
    wgf_resource_release(one);
    wgf_resource_release(six);
}

static void test_loading(void)
{
    wgf_cubemap_t cross, row, again, cube;

    write_layout("cubes/cross.png", WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE, 8);
    write_layout("cubes/row.png", WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL, 4);
    write_plain("cubes/square.png", 16, 16, 200);
    write_plain("cubes/face8.png", 8, 8, 10);
    write_plain("cubes/face8b.png", 8, 8, 20);
    write_plain("cubes/face4.png", 4, 4, 30);
    write_plain("cubes/wide.png", 8, 4, 40);

    cross = wgf_cubemap_create("cubes/cross.png", WGF_CUBEMAP_LAYOUT_AUTO);
    CHECK(cross != 0 && wgf_resource_get_status(cross) == WGF_RESOURCE_STATUS_PENDING);
    CHECK(wgf_cubemap_get_size(cross) == 0 && wgf_cubemap_get_layout(cross) == WGF_CUBEMAP_LAYOUT_AUTO);
    again = wgf_cubemap_create("cubes/cross.png", WGF_CUBEMAP_LAYOUT_AUTO);
    CHECK(again == cross); /* shared by its path, one more reference */
    wgf_resource_release(again);
    settle(cross);
    CHECK(wgf_resource_get_status(cross) == WGF_RESOURCE_STATUS_READY);
    CHECK(wgf_cubemap_get_size(cross) == 8);
    CHECK(wgf_cubemap_get_layout(cross) == WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE);
    CHECK(strcmp(wgf_resource_get_path(cross), "cubes/cross.png") == 0);
    CHECK(strcmp(wgf_handle_get_kind_name(cross), "gfx.cubemap") == 0);

    row = wgf_cubemap_create("cubes/row.png", WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL);
    CHECK(wgf_cubemap_get_layout(row) == WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL); /* given: said at once */
    settle(row);
    CHECK(wgf_resource_get_status(row) == WGF_RESOURCE_STATUS_READY && wgf_cubemap_get_size(row) == 4);
    wgf_resource_release(row);

    CHECK(wgf_cubemap_create("cubes/cross.png", WGF_CUBEMAP_LAYOUT_FACES) == 0);
    CHECK(wgf_cubemap_create("cubes/cross.png", (wgf_cubemap_layout_t)9) == 0);

    cube = wgf_cubemap_create("cubes/square.png", WGF_CUBEMAP_LAYOUT_AUTO);
    settle(cube);
    CHECK(wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_FAILED && wgf_cubemap_get_size(cube) == 0);
    wgf_resource_release(cube);
    cube = wgf_cubemap_create("cubes/row.png", WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR); /* not its shape */
    settle(cube);
    CHECK(wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_FAILED);
    wgf_resource_release(cube);
    cube = wgf_cubemap_create("cubes/missing.png", WGF_CUBEMAP_LAYOUT_AUTO);
    settle(cube);
    CHECK(wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_FAILED);
    wgf_resource_release(cube);

    /* six files */
    cube = wgf_cubemap_create_faces("cubes/face8.png", "cubes/face8b.png", "cubes/face8.png", "cubes/face8b.png",
                                    "cubes/face8.png", "cubes/face8b.png");
    CHECK(cube != 0 && wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_PENDING);
    CHECK(wgf_cubemap_get_layout(cube) == WGF_CUBEMAP_LAYOUT_FACES);
    settle(cube);
    CHECK(wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_READY && wgf_cubemap_get_size(cube) == 8);
    CHECK(strcmp(wgf_resource_get_path(cube), "cubes/face8.png") == 0);
    again = wgf_cubemap_create_faces("cubes/face8.png", "cubes/face8b.png", "cubes/face8.png", "cubes/face8b.png",
                                     "cubes/face8.png", "cubes/face8b.png");
    CHECK(again != cube); /* never shared */
    wgf_resource_release(again);
    wgf_resource_release(cube);
    cube = wgf_cubemap_create_faces("cubes/face8.png", "cubes/face8.png", "cubes/face4.png", "cubes/face8.png",
                                    "cubes/face8.png", "cubes/face8.png");
    settle(cube);
    CHECK(wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_FAILED); /* faces of two sizes */
    wgf_resource_release(cube);
    cube = wgf_cubemap_create_faces("cubes/face8.png", "cubes/face8.png", "cubes/face8.png", "cubes/wide.png",
                                    "cubes/face8.png", "cubes/face8.png");
    settle(cube);
    CHECK(wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_FAILED); /* a face that isn't square */
    wgf_resource_release(cube);
    cube = wgf_cubemap_create_faces("cubes/face8.png", "cubes/face8.png", "cubes/face8.png", "cubes/face8.png",
                                    "cubes/face8.png", "cubes/nothing.png");
    settle(cube);
    CHECK(wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_FAILED); /* a face missing */
    wgf_resource_release(cube);
    CHECK(wgf_cubemap_create_faces("a.png", "a.png", NULL, "a.png", "a.png", "a.png") == 0);
    CHECK(wgf_cubemap_get_size(12345) == 0 && wgf_cubemap_get_layout(0) == WGF_CUBEMAP_LAYOUT_AUTO);

    wgf_resource_release(cross);
}

/* A surface shader's file as tools/pack_shader.py writes one (tests/shaders/cube.glsl's
 * shape: a textureCube `sky` and a texture2D `tint_tex`), its sources stand-ins. */
static int surface_file(char *out, size_t size)
{
    static const char *const program_lines = "attr 0 position\nattr 1 normal\nattr 2 texcoord0\nattr 3 texcoord1\n"
                                             "attr 4 tangent\nattr 5 color0\n"
                                             "ub 0 vertex 80 wgf_object 5\n"
                                             "ub 1 fragment 1184 wgf_frame 74\n"
                                             "ub 2 fragment 16 params 1\n"
                                             "view 0 fragment sky cube float\n"
                                             "view 1 fragment tint_tex 2d float\n"
                                             "view 11 vertex wgf_instance_tex 2d unfilterable_float\n"
                                             "sampler 10 vertex wgf_data_smp nonfiltering\n"
                                             "sampler 0 fragment sky_smp filtering\n"
                                             "sampler 1 fragment tint_smp filtering\n"
                                             "pair 0 vertex 11 10 wgf_instance_tex_wgf_data_smp\n"
                                             "pair 1 fragment 0 0 sky_sky_smp\n"
                                             "pair 2 fragment 1 1 tint_tex_tint_smp\n"
                                             "source vs 4\nmain\n"
                                             "source fs 4\nmain\n";
    return snprintf(out, size,
                    "wgfshader 2\nkind surface\nparam dir vec3 fs 0\ntexture sky\ntexture tint_tex\n"
                    "program model glsl410\n%s"
                    "program model_skinned glsl410\n%s"
                    "end\n",
                    program_lines, program_lines);
}

static void test_binding(void)
{
    static char text[4096];
    const int size = surface_file(text, sizeof(text));
    wgf_cubemap_t cube;
    wgf_texture_t texture = wgf_texture_create_blank(2, 2);
    wgf_shader_t shader;
    wgf_material_t material, early, plain;

    write_bytes("cubes/cube.wgfshader", (const unsigned char *)text, size);
    cube = wgf_cubemap_create("cubes/cross.png", WGF_CUBEMAP_LAYOUT_AUTO);
    shader = wgf_shader_create("cubes/cube.wgfshader");
    early = wgf_material_create_custom(shader);
    CHECK(wgf_material_set_cubemap(early, "sky", cube)); /* while it loads: kept */
    CHECK(wgf_material_set_cubemap(early, "tint_tex", cube)); /* kept for now, dropped once it is READY */
    CHECK(wgf_material_get_cubemap(early, "sky") == cube && wgf_material_get_texture(early, "sky") == 0);
    settle(shader);
    settle(cube);
    CHECK(wgf_resource_get_status(shader) == WGF_RESOURCE_STATUS_READY);
    {
        const wgf_gfx_priv_shader_t *made = wgf_gfx_priv_shader_get(shader);
        CHECK(made != NULL && made->texture_count == 2 && made->texture_cube[0] && !made->texture_cube[1]);
    }
    material = wgf_material_create_custom(shader);
    CHECK(wgf_material_set_cubemap(material, "sky", cube) && wgf_material_get_cubemap(material, "sky") == cube);
    CHECK(!wgf_material_set_cubemap(material, "tint_tex", cube)); /* a texture2D's name */
    CHECK(!wgf_material_set_texture(material, "sky", texture));   /* and the other way */
    CHECK(!wgf_material_set_cubemap(material, "sky", texture));   /* a texture's handle */
    CHECK(!wgf_material_set_cubemap(material, "nothing", cube));
    CHECK(wgf_material_set_texture(material, "tint_tex", texture) &&
          wgf_material_get_cubemap(material, "tint_tex") == 0);
    CHECK(wgf_material_set_texture_sampling(material, "sky", WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP,
                                            WGF_TEXTURE_FILTER_NEAREST) &&
          wgf_material_get_texture_filter(material, "sky") == WGF_TEXTURE_FILTER_NEAREST);
    CHECK(wgf_material_set_cubemap(material, "sky", 0) && wgf_material_get_cubemap(material, "sky") == 0);
    CHECK(wgf_material_set_cubemap(material, "sky", cube));

    /* set while it loaded: the texture2D's dropped as it is bound, the textureCube's kept */
    CHECK(wgf_material_get_cubemap(early, "sky") == cube);
    CHECK(wgf_material_get_cubemap(early, "tint_tex") == 0);

    plain = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    CHECK(!wgf_material_set_cubemap(plain, "base_color_texture", cube)); /* a built-in material has none */
    CHECK(!wgf_material_set_cubemap(0, "sky", cube));

    /* the material holds its reference: the cubemap outlives the caller's */
    wgf_resource_release(cube);
    CHECK(wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_READY);
    wgf_resource_release(material);
    wgf_resource_release(early);
    CHECK(wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_NONE); /* the last reference gone */
    wgf_resource_release(plain);
    wgf_resource_release(shader);
    wgf_resource_release(texture);
}

int main(void)
{
    test_layouts();
    test_extract(WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE);
    test_extract(WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR);
    test_extract(WGF_CUBEMAP_LAYOUT_LINE_VERTICAL);
    test_extract(WGF_CUBEMAP_LAYOUT_LINE_HORIZONTAL);

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the failures below warn, on purpose */
    CHECK(wgf_gfx_priv_start());
    test_loading();
    test_reload();
    test_binding();
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_gfx_cubemap_test: ok\n");
    return failures == 0 ? 0 : 1;
}
