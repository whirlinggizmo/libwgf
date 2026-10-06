#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "wgf_platform_priv.h"
#include "node/wgf_gfx_node_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_time.h"
#include "wgf_camera2d.h"
#include "wgf_canvas.h"
#include "wgf_color.h"
#include "wgf_node.h"
#include "wgf_quat.h"
#include "wgf_sprite.h"
#include "wgf_texture.h"

/* Nodes on sokol's dummy backend (a headless build): the tree, transforms, both
 * ways of destroying, what a sprite holds and lets go of, a camera's fallback, and
 * what a canvas draws. Pixels: wgf_gfx_canvas_web_test. */

static const unsigned char png_2x2[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00,
    0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x81,
    0x34, 0x18, 0x00, 0x00, 0x49, 0xc8, 0x09, 0xf7, 0xf9, 0xab, 0xb6, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* A frame at a size and DPI scale: the headless window's, as the runtime begins one. */
static void begin_frame(int width, int height, float dpi_scale)
{
    wgf_platform_priv_set_size(width, height);
    wgf_platform_priv_headless_set_dpi_scale(dpi_scale);
    wgf_gfx_priv_begin_frame();
}

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

static int near3(wgf_vec3_t v, float x, float y, float z)
{
    return near(v.x, x) && near(v.y, y) && near(v.z, z);
}

static void update_until_done(wgf_handle_t task)
{
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

/* A worker decodes it: wait on time, not on a count of updates. */
static void settle_texture(wgf_handle_t texture)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0)
        wgf_core_priv_update();
}

static void test_tree(void)
{
    const wgf_handle_t canvas = wgf_canvas_create();
    const wgf_handle_t a = wgf_node_create(), b = wgf_node_create(), c = wgf_node_create(), d = wgf_node_create();

    expect(wgf_node_get_type(canvas) == WGF_NODE_TYPE_CANVAS && wgf_node_get_type(a) == WGF_NODE_TYPE_NODE, "types");
    expect(wgf_node_get_type(0) == WGF_NODE_TYPE_NONE, "0 is not a node");
    expect(wgf_node_set_parent(a, canvas) && wgf_node_set_parent(b, canvas) && wgf_node_set_parent(c, canvas),
           "three children");
    expect(wgf_node_get_child_count(canvas) == 3 && wgf_node_get_child(canvas, 0) == a &&
               wgf_node_get_child(canvas, 2) == c,
           "in the order they were added");
    expect(wgf_node_get_child(canvas, 3) == 0 && wgf_node_get_child(canvas, -1) == 0, "no child past either end");
    expect(wgf_node_get_parent(b) == canvas && wgf_node_get_index(b) == 1, "parent and index");
    expect(wgf_node_set_index(a, 99) && wgf_node_get_index(a) == 2 && wgf_node_get_child(canvas, 0) == b,
           "a large index moves it to the front of the drawing");
    expect(wgf_node_set_index(a, -5) && wgf_node_get_index(a) == 0, "a negative one to the back");
    expect(!wgf_node_set_index(d, 0), "a detached node has no place to move");

    expect(wgf_node_set_parent(d, a) && wgf_node_get_parent(d) == a, "a grandchild");
    expect(!wgf_node_set_parent(a, d), "a node can't go under its own child");
    expect(!wgf_node_set_parent(a, a), "nor under itself");
    expect(!wgf_node_set_parent(canvas, a), "a canvas is always a root");
    expect(!wgf_node_set_parent(a, 12345), "a parent that isn't a node is refused");
    expect(wgf_node_set_parent(d, 0) && wgf_node_get_parent(d) == 0 && wgf_node_get_child_count(a) == 0, "detach");
    expect(wgf_node_set_parent(d, b) && wgf_node_get_parent(d) == b, "and attach elsewhere");

    wgf_node_destroy(canvas, WGF_NODE_DESTROY_CHILDREN);
    expect(wgf_node_get_type(canvas) == WGF_NODE_TYPE_NONE && wgf_node_get_type(a) == WGF_NODE_TYPE_NONE &&
               wgf_node_get_type(d) == WGF_NODE_TYPE_NONE,
           "destroying a canvas takes its whole tree");
    expect(!wgf_node_set_position(d, 1, 2, 3) && wgf_node_get_child_count(canvas) == 0, "and its handles go stale");
}

static void test_transforms(void)
{
    const float quarter = 1.57079632679f;
    const wgf_handle_t parent = wgf_node_create(), child = wgf_node_create();
    wgf_node_set_parent(child, parent);

    expect(near3(wgf_node_get_scale(parent), 1, 1, 1) && near3(wgf_node_get_position(parent), 0, 0, 0), "defaults");
    expect(wgf_node_set_position(parent, 100, 50, 0) && wgf_node_set_position(child, 10, 0, 0), "positions");
    expect(near3(wgf_node_get_world_position(child), 110, 50, 0), "a child's world position adds its parent's");
    wgf_node_set_rotation(parent, 0, 0, quarter);
    expect(near3(wgf_node_get_world_position(child), 100, 60, 0), "a turned parent turns its child about it");
    wgf_node_set_scale(parent, 2, 2, 1);
    expect(near3(wgf_node_get_world_position(child), 100, 70, 0), "and a scaled one scales its distance");
    expect(wgf_node_set_transform(child, 1, 2, 3, 0.1f, 0.2f, 0.3f, 4, 5, 6) &&
               near3(wgf_node_get_position(child), 1, 2, 3) && near3(wgf_node_get_rotation(child), 0.1f, 0.2f, 0.3f) &&
               near3(wgf_node_get_scale(child), 4, 5, 6),
           "set_transform sets all three");
    {
        /* kept as a rotation, read back as angles: past a half turn, other angles for
           the same rotation */
        wgf_quat_t given, read;
        wgf_node_set_rotation(child, 0.5f, 4.0f, -0.25f);
        given = wgf_quat_from_euler(wgf_vec3_make(0.5f, 4.0f, -0.25f));
        read = wgf_quat_from_euler(wgf_node_get_rotation(child));
        expect(fabsf(fabsf(wgf_quat_dot(given, read)) - 1.0f) < 1e-5f, "angles read back as the same rotation");
    }
    expect(wgf_node_is_visible(child) && wgf_node_set_visible(child, false) && !wgf_node_is_visible(child), "visible");
    expect(wgf_node_is_enabled(child) && wgf_node_set_enabled(child, false) && !wgf_node_is_enabled(child), "enabled");
    expect(!wgf_node_is_enabled(12345) && !wgf_node_set_enabled(12345, true), "a non-node isn't enabled");
    wgf_node_set_enabled(child, true);
    expect(near3(wgf_node_get_world_position(12345), 0, 0, 0) && !wgf_node_is_visible(12345), "a non-node reads 0");
    wgf_node_destroy(parent, WGF_NODE_DESTROY_CHILDREN);
}

/* World matrices are kept between reads: whatever moves, every read below must
 * see it, however deep, and however the move was made. */
static void test_cached_transforms(void)
{
    const float quarter = 1.57079632679f;
    const wgf_handle_t a = wgf_node_create(), b = wgf_node_create(), c = wgf_node_create(), other = wgf_node_create();
    wgf_node_set_parent(b, a);
    wgf_node_set_parent(c, b);
    wgf_node_set_position(c, 1, 0, 0);
    expect(near3(wgf_node_get_world_position(c), 1, 0, 0), "read once: now kept");
    wgf_node_set_position(a, 10, 0, 0);
    expect(near3(wgf_node_get_world_position(c), 11, 0, 0), "a grandparent moved after a read: the grandchild follows");
    expect(near3(wgf_node_get_world_position(c), 11, 0, 0), "and again, from what is kept");
    wgf_node_set_rotation(b, 0, 0, quarter);
    expect(near3(wgf_node_get_world_position(c), 10, 1, 0), "a parent turned");
    wgf_node_set_scale(a, 3, 3, 3);
    expect(near3(wgf_node_get_world_position(b), 10, 0, 0) && near3(wgf_node_get_world_position(c), 10, 3, 0),
           "a grandparent scaled");
    wgf_node_set_transform(a, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    expect(near3(wgf_node_get_world_position(c), 0, 1, 0), "set_transform");
    wgf_node_set_position(other, 100, 100, 0);
    expect(near3(wgf_node_get_world_position(c), 0, 1, 0), "a node elsewhere moving changes nothing here");
    wgf_node_set_parent(b, other);
    expect(near3(wgf_node_get_world_position(c), 100, 101, 0), "reparented: the subtree follows its new parent");
    wgf_node_set_position(c, 2, 0, 0);
    wgf_node_set_position(c, 3, 0, 0); /* moved twice between reads */
    expect(near3(wgf_node_get_world_position(c), 100, 103, 0), "the latest of two moves");
    wgf_node_destroy(a, WGF_NODE_DESTROY_CHILDREN);
    wgf_node_destroy(other, WGF_NODE_DESTROY_CHILDREN);
}

static void test_keep_children(void)
{
    const float quarter = 1.57079632679f;
    const wgf_handle_t root = wgf_node_create(), first = wgf_node_create(), group = wgf_node_create(),
                      last = wgf_node_create(), x = wgf_node_create(), y = wgf_node_create();
    wgf_node_set_parent(first, root);
    wgf_node_set_parent(group, root);
    wgf_node_set_parent(last, root);
    wgf_node_set_parent(x, group);
    wgf_node_set_parent(y, group);
    wgf_node_set_transform(group, 100, 50, 0, 0, 0, quarter, 2, 2, 1);
    wgf_node_set_position(x, 10, 0, 0);
    wgf_node_set_position(y, 0, 5, 0);
    {
        const wgf_vec3_t x_before = wgf_node_get_world_position(x), y_before = wgf_node_get_world_position(y);
        wgf_node_destroy(group, WGF_NODE_KEEP_CHILDREN);
        expect(wgf_node_get_type(group) == WGF_NODE_TYPE_NONE, "the node itself goes");
        expect(wgf_node_get_parent(x) == root && wgf_node_get_parent(y) == root, "its children move up to its parent");
        expect(wgf_node_get_child_count(root) == 4 && wgf_node_get_child(root, 0) == first &&
                   wgf_node_get_child(root, 1) == x && wgf_node_get_child(root, 2) == y &&
                   wgf_node_get_child(root, 3) == last,
               "in its place, in their order");
        expect(near3(wgf_node_get_world_position(x), x_before.x, x_before.y, x_before.z) &&
                   near3(wgf_node_get_world_position(y), y_before.x, y_before.y, y_before.z),
               "where they were");
        expect(near3(wgf_node_get_scale(x), 2, 2, 1) && near(wgf_node_get_rotation(x).z, quarter),
               "with the parent's turn and scale taken on");
    }
    {
        const wgf_handle_t lone = wgf_node_create(), kid = wgf_node_create();
        wgf_node_set_parent(kid, lone);
        wgf_node_set_position(lone, 7, 0, 0);
        wgf_node_destroy(lone, WGF_NODE_KEEP_CHILDREN);
        expect(wgf_node_get_parent(kid) == 0 && near3(wgf_node_get_position(kid), 7, 0, 0),
               "under a node with no parent, left with none, where they were");
        wgf_node_destroy(kid, WGF_NODE_DESTROY_CHILDREN);
    }
    wgf_node_destroy(root, WGF_NODE_DESTROY_CHILDREN);
}

static void test_sprites_and_references(void)
{
    wgf_handle_t texture, other, sprite, twin, canvas, camera;
    update_until_done(wgf_fs_write("node_test/rgbw.png", png_2x2, (int)sizeof(png_2x2)));
    texture = wgf_texture_create("node_test/rgbw.png");
    settle_texture(texture);
    expect(wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_READY, "a texture");

    sprite = wgf_sprite_create(texture);
    twin = wgf_sprite_create(texture);
    expect(wgf_node_get_type(sprite) == WGF_NODE_TYPE_SPRITE && wgf_sprite_get_texture(sprite) == texture, "sprites");
    expect(wgf_sprite_create(12345) == 0, "a sprite of something that isn't a texture is refused");
    wgf_resource_release(texture); /* ours: the sprites hold their own */
    expect(wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_READY, "the sprites keep it alive");
    wgf_node_destroy(sprite, WGF_NODE_DESTROY_CHILDREN);
    expect(wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_READY, "destroying one sprite doesn't free it");

    other = wgf_texture_create("node_test/rgbw.png"); /* the same file: the same texture, one more reference */
    expect(other == texture, "the same texture");
    expect(wgf_sprite_set_texture(twin, 0) && wgf_sprite_get_texture(twin) == 0, "a sprite let go of its texture");
    expect(wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_READY, "ours is still held");
    expect(wgf_sprite_set_texture(twin, texture), "and took it back");
    wgf_resource_release(other);
    wgf_node_destroy(twin, WGF_NODE_DESTROY_CHILDREN);
    expect(wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_NONE, "the last reference gone: the texture is freed");

    /* the sprite's own fields */
    texture = wgf_texture_create("node_test/rgbw.png");
    sprite = wgf_sprite_create(texture);
    wgf_resource_release(texture);
    for (int i = 0; i < 100000 && wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_PENDING; i++) {
        wgf_core_priv_update();
    }
    expect(wgf_sprite_get_source(sprite).z == 0 && wgf_sprite_get_size(sprite).x == 2 &&
               wgf_sprite_get_size(sprite).y == 2,
           "defaults: the whole texture, at its size, read back as drawn");
    expect(wgf_sprite_set_source(sprite, 1, 0, 1, 1) && wgf_sprite_get_size(sprite).x == 1,
           "unset: the source region's size");
    wgf_sprite_set_source(sprite, 0, 0, 0, 0);
    expect(near(wgf_sprite_get_pivot(sprite).x, 0.5f) && wgf_sprite_get_tint(sprite) == WGF_COLOR_WHITE,
           "defaults: centered, white");
    expect(wgf_sprite_set_source(sprite, 1, 0, 1, 1) && wgf_sprite_get_source(sprite).x == 1 &&
               wgf_sprite_get_source(sprite).w == 1,
           "a source region");
    expect(wgf_sprite_set_size(sprite, 32, 16) && wgf_sprite_get_size(sprite).y == 16, "a size");
    expect(wgf_sprite_set_pivot(sprite, 0, 1) && wgf_sprite_get_pivot(sprite).y == 1, "a pivot");
    expect(wgf_sprite_set_tint(sprite, WGF_COLOR_RED) && wgf_sprite_get_tint(sprite) == WGF_COLOR_RED, "a tint");
    expect(!wgf_sprite_set_tint(wgf_node_create(), WGF_COLOR_RED), "sprite calls refuse other kinds of node");

    /* a camera, and what happens when it goes */
    canvas = wgf_canvas_create();
    camera = wgf_camera2d_create();
    expect(wgf_camera2d_get_zoom(camera) == 1.0f && wgf_camera2d_set_zoom(camera, 2) && wgf_camera2d_get_zoom(camera) == 2,
           "zoom");
    expect(!wgf_camera2d_set_zoom(camera, 0) && !wgf_camera2d_set_zoom(camera, -1), "a zoom of 0 or less is refused");
    expect(wgf_canvas_set_camera(canvas, camera) && wgf_canvas_get_camera(canvas) == camera, "a canvas's camera");
    expect(!wgf_canvas_set_camera(canvas, sprite), "only a 2D camera");
    wgf_node_destroy(camera, WGF_NODE_DESTROY_CHILDREN);
    expect(wgf_canvas_get_camera(canvas) == 0, "a destroyed camera leaves the canvas with none");

    /* what a canvas draws */
    {
        const wgf_handle_t hidden = wgf_node_create(), under = wgf_sprite_create(wgf_sprite_get_texture(sprite));
        int empty, one, two, three;
        wgf_node_set_parent(sprite, canvas);
        wgf_node_set_parent(hidden, canvas);
        wgf_node_set_parent(under, hidden);
        begin_frame(64, 64, 1.0f);
        empty = sgl_num_vertices();
        wgf_canvas_draw(canvas);
        one = sgl_num_vertices();
        wgf_node_set_visible(hidden, false);
        wgf_canvas_draw(canvas);
        two = sgl_num_vertices();
        expect(two - one == one - empty, "a hidden node's children still draw (it draws nothing itself)");
        wgf_node_set_enabled(hidden, false);
        wgf_canvas_draw(canvas);
        three = sgl_num_vertices();
        wgf_gfx_priv_end_frame();
        expect(three - two == (one - empty) / 2, "a node not enabled takes what is under it with it");
        wgf_canvas_draw(canvas); /* outside a frame: nothing, and no harm */
    }
    wgf_node_destroy(canvas, WGF_NODE_DESTROY_CHILDREN);
    expect(wgf_node_get_type(sprite) == WGF_NODE_TYPE_NONE, "gone with its canvas");
    update_until_done(wgf_fs_rmdir("node_test"));
}

static void test_deep(void)
{
    /* a chain far deeper than any C stack would take recursively */
    wgf_handle_t root = wgf_node_create(), tip = root;
    int i;
    for (i = 0; i < 20000; i++) {
        const wgf_handle_t next = wgf_node_create();
        wgf_node_set_parent(next, tip);
        tip = next;
    }
    expect(wgf_node_get_type(tip) == WGF_NODE_TYPE_NODE, "a 20000 deep chain");
    wgf_node_destroy(root, WGF_NODE_DESTROY_CHILDREN);
    expect(wgf_node_get_type(tip) == WGF_NODE_TYPE_NONE, "destroyed from its root, its tip too");
}

/* Many children leaving and joining, in a mixed order: a leaving child leaves a hole,
 * closed when the children are next read whole. Order, counts, indices, and lookups
 * stay right through every kind of change, against a model kept beside the tree. */
static void test_many_children(void)
{
    enum { N = 3000 };
    static wgf_handle_t model[N * 2]; /* what the parent's children should be, in order */
    int count = 0, mismatches = 0;
    unsigned seed = 7u;
    const wgf_handle_t parent = wgf_node_create(), other = wgf_node_create();
    for (int i = 0; i < N; i++) {
        model[count] = wgf_node_create();
        wgf_node_set_parent(model[count++], parent);
    }
    for (int round = 0; round < 40; round++) {
        for (int k = 0; k < 50; k++) { /* leave: destroyed, or moved to another parent */
            seed = seed * 1664525u + 1013904223u;
            const int at = (int)((seed >> 8) % (unsigned)count);
            if (k % 2 == 0) wgf_node_destroy(model[at], WGF_NODE_DESTROY_CHILDREN);
            else wgf_node_set_parent(model[at], other);
            for (int j = at; j < count - 1; j++) model[j] = model[j + 1];
            count--;
        }
        if (wgf_node_get_child_count(parent) != count) mismatches++;
        for (int k = 0; k < 20; k++) { /* join: appended, or put in at an index */
            const wgf_handle_t n = wgf_node_create();
            seed = seed * 1664525u + 1013904223u;
            if (k % 2 == 0) {
                wgf_node_set_parent(n, parent);
                model[count++] = n;
            } else {
                const int at = (int)((seed >> 8) % (unsigned)(count + 1));
                wgf_node_set_parent(n, parent);
                wgf_node_set_index(n, at);
                for (int j = count; j > at; j--) model[j] = model[j - 1];
                model[at] = n;
                count++;
            }
        }
        if (round % 8 == 0) { /* a node kept in a moved place */
            seed = seed * 1664525u + 1013904223u;
            const int from = (int)((seed >> 8) % (unsigned)count), to = (from * 7 + 3) % count;
            const wgf_handle_t n = model[from];
            wgf_node_set_index(n, to);
            for (int j = from; j < count - 1; j++) model[j] = model[j + 1];
            for (int j = count - 1; j > to; j--) model[j] = model[j - 1];
            model[to] = n;
        }
        for (int i = 0; i < count; i += 97) { /* indices, read before the order is */
            if (wgf_node_get_index(model[i]) != i) mismatches++;
        }
        for (int i = 0; i < count; i++) { /* the order */
            if (wgf_node_get_child(parent, i) != model[i]) mismatches++;
        }
    }
    expect(mismatches == 0, "many children leaving and joining: order, counts, and indices kept");
    expect(wgf_node_get_child(parent, count) == 0 && wgf_node_get_child_count(other) > 0, "past the end: none");
    {
        /* keep-children destroy in the middle of a holey parent: its children in its place */
        const wgf_handle_t middle = model[count / 2], a = wgf_node_create(), b = wgf_node_create();
        wgf_node_set_parent(a, middle);
        wgf_node_set_parent(b, middle);
        wgf_node_destroy(model[count / 2 - 1], WGF_NODE_DESTROY_CHILDREN); /* a hole before it */
        wgf_node_destroy(middle, WGF_NODE_KEEP_CHILDREN);
        expect(wgf_node_get_child(parent, count / 2 - 1) == a && wgf_node_get_child(parent, count / 2) == b,
               "a node's children take its place among holey siblings");
    }
    wgf_node_destroy(parent, WGF_NODE_DESTROY_CHILDREN);
    wgf_node_destroy(other, WGF_NODE_DESTROY_CHILDREN);
    {
        /* a parent never read whole (never drawn): a child among 100 siblings leaving, a
           hole where it was, and another joining at the end, 10,000 times; nothing reads
           the children, so only the joining closes the holes: its array stays within
           twice its children, where without that it would hold every one ever added */
        const wgf_handle_t idle = wgf_node_create();
        wgf_handle_t kids[100];
        for (int i = 0; i < 100; i++) {
            kids[i] = wgf_node_create();
            wgf_node_set_parent(kids[i], idle);
        }
        for (int i = 0; i < 10000; i++) {
            const int k = (i * 37) % 100;
            wgf_node_destroy(kids[k], WGF_NODE_DESTROY_CHILDREN);
            kids[k] = wgf_node_create();
            wgf_node_set_parent(kids[k], idle);
        }
        expect(wgf_node_get_child_count(idle) == 100 && wgf_gfx_priv_node_of(idle)->child_capacity <= 256,
               "a parent never drawn keeps a bounded array");
        wgf_node_destroy(idle, WGF_NODE_DESTROY_CHILDREN);
    }
}

int main(void)
{
    wgf_core_priv_init();
    expect(wgf_gfx_priv_start(), "setup");
    test_tree();
    test_transforms();
    test_cached_transforms();
    test_keep_children();
    test_sprites_and_references();
    test_deep();
    test_many_children();
    {
        /* gfx shutting down with nodes still alive releases what they hold */
        const wgf_handle_t canvas = wgf_canvas_create();
        wgf_node_set_parent(wgf_node_create(), canvas);
        wgf_gfx_priv_stop();
        expect(wgf_node_get_type(canvas) == WGF_NODE_TYPE_NONE, "shutdown ends every node");
    }
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
