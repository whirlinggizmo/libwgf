#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_log.h"
#include "wgf_resource.h"
#include "wgf_sheet.h"
#include "wgf_sprite.h"
#include "wgf_time.h"

/* Sprite sheets and 2D animation, headless: a 2 by 2 image cut into four 1 by 1 frames once
 * READY, clips forward, backward, and ping-pong, looping and once (COMPLETE, holding its last
 * frame), paused and sped up, the sprite's region following, and what is refused. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static const unsigned char png_2x2[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00,
    0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x81,
    0x34, 0x18, 0x00, 0x00, 0x49, 0xc8, 0x09, 0xf7, 0xf9, 0xab, 0xb6, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

/* TexturePacker's hash: a trimmed frame (its 1x1 texels 1 in from the left of a 2x2 picture)
 * and a run named by number, out of order. */
static const char packer[] = "{\"frames\": {"
                             "\"run_02.png\": {\"frame\": {\"x\": 1, \"y\": 0, \"w\": 1, \"h\": 1}},"
                             "\"run_01.png\": {\"frame\": {\"x\": 0, \"y\": 0, \"w\": 1, \"h\": 1},"
                             " \"spriteSourceSize\": {\"x\": 1, \"y\": 0, \"w\": 1, \"h\": 1},"
                             " \"sourceSize\": {\"w\": 2, \"h\": 2}},"
                             "\"idle.png\": {\"frame\": {\"x\": 0, \"y\": 1, \"w\": 1, \"h\": 1}}},"
                             " \"meta\": {\"image\": \"four.png\"}}";

/* Aseprite's array: durations, and tags (a ping-pong, a reverse). */
static const char aseprite[] = "{\"frames\": ["
                               "{\"filename\": \"a 0\", \"frame\": {\"x\": 0, \"y\": 0, \"w\": 1, \"h\": 1},"
                               " \"duration\": 100},"
                               "{\"filename\": \"a 1\", \"frame\": {\"x\": 1, \"y\": 0, \"w\": 1, \"h\": 1},"
                               " \"duration\": 300},"
                               "{\"filename\": \"a 2\", \"frame\": {\"x\": 0, \"y\": 1, \"w\": 1, \"h\": 1},"
                               " \"duration\": 100}],"
                               " \"meta\": {\"image\": \"four.png\", \"frameTags\": ["
                               "{\"name\": \"walk_left\", \"from\": 0, \"to\": 2, \"direction\": \"forward\"},"
                               "{\"name\": \"walk\", \"from\": 1, \"to\": 2, \"direction\": \"pingpong\"},"
                               "{\"name\": \"back\", \"from\": 0, \"to\": 2, \"direction\": \"reverse\"}]}}";

static void frames(int n, float dt)
{
    while (n-- > 0) wgf_core_priv_part_update(dt);
}

int main(void)
{
    wgf_texture_t texture;
    wgf_sheet_t sheet;
    wgf_actor_t sprite;
    wgf_fs_task_t write;
    const int all[4] = {0, 1, 2, 3}, three[3] = {0, 1, 2};
    double start;
    int i;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(wgf_gfx_priv_start(), "setup");
    write = wgf_fs_write("sheet_test/four.png", png_2x2, (int)sizeof(png_2x2));
    for (i = 0; i < 1000 && wgf_fs_task_get_status(write) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(write);
    texture = wgf_texture_create("sheet_test/four.png");
    sheet = wgf_sheet_create_grid(texture, 1, 1, 0, 0, 0);
    expect(sheet != 0 && wgf_sheet_get_texture(sheet) == texture, "a sheet over a texture");
    start = wgf_time_get_seconds();
    while (wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
    expect(wgf_sheet_get_frame_count(sheet) == 4 && wgf_sheet_get_frame(sheet, 3).x == 1.0f &&
               wgf_sheet_get_frame(sheet, 3).y == 1.0f && wgf_sheet_get_frame(sheet, 4).z == 0.0f,
           "cut into four frames once READY, left to right then down");

    expect(wgf_sheet_add_clip(sheet, "walk", all, 4, 0.1f, WGF_SHEET_DIRECTION_FORWARD) &&
               wgf_sheet_add_clip(sheet, "back", all, 4, 0.1f, WGF_SHEET_DIRECTION_BACKWARD) &&
               wgf_sheet_add_clip(sheet, "bob", three, 3, 0.1f, WGF_SHEET_DIRECTION_PING_PONG) &&
               wgf_sheet_get_clip_count(sheet) == 3,
           "three clips");
    expect(!wgf_sheet_add_clip(sheet, "", all, 4, 0.1f, WGF_SHEET_DIRECTION_FORWARD) &&
               !wgf_sheet_add_clip(sheet, "x", all, 0, 0.1f, WGF_SHEET_DIRECTION_FORWARD) &&
               !wgf_sheet_add_clip(sheet, "x", all, 4, -1.0f, WGF_SHEET_DIRECTION_FORWARD) &&
               !wgf_sheet_add_clip(sheet, "x", all, 4, 0.1f, (wgf_sheet_direction_t)5),
           "no name, no frames, time below 0, a direction that isn't one: refused");

    sprite = wgf_sprite_create(0);
    expect(wgf_sprite_set_sheet(sprite, sheet) && wgf_sprite_get_sheet(sprite) == sheet &&
               wgf_sprite_get_texture(sprite) == texture,
           "a sprite on the sheet: its texture");
    expect(wgf_sprite_play_clip(sprite, "walk", true) && wgf_sprite_get_clip_state(sprite) == WGF_PLAY_STATE_PLAYING &&
               strcmp(wgf_sprite_get_clip(sprite), "walk") == 0 && wgf_sprite_get_clip_frame(sprite) == 0,
           "playing from its first frame");
    frames(3, 0.05f);
    expect(wgf_sprite_get_clip_frame(sprite) == 1 && wgf_sprite_get_source(sprite).x == 1.0f,
           "a tenth of a second on: the second frame, its region");
    frames(6, 0.05f);
    expect(wgf_sprite_get_clip_frame(sprite) == 0 && wgf_sprite_get_clip_state(sprite) == WGF_PLAY_STATE_PLAYING,
           "looping round to its first");
    {
        const wgf_actor_t level = wgf_actor_create();
        const int frame = wgf_sprite_get_clip_frame(sprite);
        wgf_actor_set_parent(sprite, level);
        wgf_actor_set_paused(level, true);
        frames(4, 0.05f);
        expect(wgf_sprite_get_clip_frame(sprite) == frame &&
                   wgf_sprite_get_clip_state(sprite) == WGF_PLAY_STATE_PLAYING,
               "under a paused actor: its clip held, still playing");
        wgf_actor_set_parent(sprite, 0);
        wgf_actor_destroy(level, WGF_ACTOR_DESTROY_CHILDREN);
    }
    expect(wgf_sprite_pause_clip(sprite) && wgf_sprite_get_clip_state(sprite) == WGF_PLAY_STATE_PAUSED, "paused");
    frames(10, 0.05f);
    expect(wgf_sprite_get_clip_frame(sprite) == 0 && wgf_sprite_resume_clip(sprite), "held, and on again");
    expect(wgf_sprite_play_clip(sprite, "back", false) && wgf_sprite_get_clip_frame(sprite) == 3,
           "backward, from its last");
    frames(10, 0.05f);
    expect(wgf_sprite_get_clip_state(sprite) == WGF_PLAY_STATE_COMPLETE && wgf_sprite_get_clip_frame(sprite) == 0,
           "once: COMPLETE, holding its end");
    expect(wgf_sprite_play_clip(sprite, "bob", true) && wgf_sprite_set_clip_speed(sprite, 2.0f) &&
               wgf_sprite_get_clip_speed(sprite) == 2.0f,
           "ping-pong at twice the speed");
    frames(2, 0.05f);
    expect(wgf_sprite_get_clip_frame(sprite) == 2, "forward twice as fast");
    frames(1, 0.05f);
    expect(wgf_sprite_get_clip_frame(sprite) == 1, "and back");
    expect(wgf_sprite_stop_clip(sprite) && wgf_sprite_get_clip_state(sprite) == WGF_PLAY_STATE_STOPPED &&
               strcmp(wgf_sprite_get_clip(sprite), "") == 0 && wgf_sprite_get_clip_frame(sprite) == -1,
           "stopped");
    expect(!wgf_sprite_play_clip(sprite, "missing", true) && !wgf_sprite_set_sheet(sheet, sheet) &&
               !wgf_sprite_set_sheet(sprite, 12345),
           "a clip it hasn't, a sprite or a sheet that isn't one: refused");
    wgf_sprite_play_clip(sprite, "walk", true);
    wgf_sheet_destroy(sheet);
    frames(1, 0.05f);
    expect(!wgf_handle_is_alive(sheet) && wgf_sprite_get_clip_state(sprite) == WGF_PLAY_STATE_STOPPED &&
               wgf_sprite_get_sheet(sprite) == 0,
           "its sheet destroyed: stopped");

    wgf_actor_destroy(sprite, WGF_ACTOR_DESTROY_CHILDREN);
    { /* atlases: read from their files */
        wgf_sheet_t hash, array;
        int events[4];
        write = wgf_fs_write("sheet_test/packer.json", (const unsigned char *)packer, (int)sizeof(packer) - 1);
        for (i = 0; i < 1000 && wgf_fs_task_get_status(write) == WGF_FS_TASK_STATUS_PENDING; i++)
            wgf_core_priv_update();
        wgf_fs_task_destroy(write);
        write = wgf_fs_write("sheet_test/ase.json", (const unsigned char *)aseprite, (int)sizeof(aseprite) - 1);
        for (i = 0; i < 1000 && wgf_fs_task_get_status(write) == WGF_FS_TASK_STATUS_PENDING; i++)
            wgf_core_priv_update();
        wgf_fs_task_destroy(write);
        hash = wgf_sheet_create("sheet_test/packer.json");
        array = wgf_sheet_create("sheet_test/ase.json");
        expect(hash != 0 && wgf_resource_get_status(hash) == WGF_RESOURCE_STATUS_PENDING, "an atlas loading");
        start = wgf_time_get_seconds();
        while ((wgf_resource_get_status(hash) == WGF_RESOURCE_STATUS_PENDING ||
                wgf_resource_get_status(array) == WGF_RESOURCE_STATUS_PENDING ||
                wgf_resource_get_status(wgf_sheet_get_texture(hash)) == WGF_RESOURCE_STATUS_PENDING) &&
               wgf_time_get_seconds() - start < 30.0) {
            wgf_core_priv_update();
        }
        expect(wgf_resource_get_status(hash) == WGF_RESOURCE_STATUS_READY && wgf_sheet_get_frame_count(hash) == 3 &&
                   strcmp(wgf_sheet_get_frame_name(hash, 0), "run_02.png") == 0 &&
                   wgf_sheet_find_frame(hash, "idle.png") == 2 && wgf_sheet_get_frame(hash, 2).y == 1.0f,
               "TexturePacker's hash: its frames in its order, by name");
        expect(wgf_resource_get_status(wgf_sheet_get_texture(hash)) == WGF_RESOURCE_STATUS_READY &&
                   wgf_sheet_get_texture(hash) == texture,
               "its image, beside it, the texture made from that path");
        expect(wgf_sheet_find_clip(hash, "run") == 0 && wgf_sheet_get_clip_count(hash) == 1,
               "a clip of its numbered run");
        sprite = wgf_sprite_create(0);
        wgf_sprite_set_sheet(sprite, hash);
        expect(wgf_sprite_play_clip(sprite, "run", true) && wgf_sprite_get_source(sprite).x == 0.0f &&
                   wgf_sprite_get_size(sprite).x == 2.0f,
               "run_01 first, by its number; trimmed, the sprite the whole picture's size");
        frames(3, 0.05f);
        expect(wgf_sprite_get_source(sprite).x == 1.0f && wgf_sprite_get_size(sprite).x == 1.0f,
               "run_02 after 0.1 s (no duration: 0.1), not trimmed");
        wgf_sprite_set_sheet(sprite, array);
        expect(wgf_sheet_get_clip_count(array) == 4 && strcmp(wgf_sheet_get_clip_name(array, 1), "walk") == 0 &&
                   wgf_sheet_find_clip(array, "a") == 3,
               "Aseprite's tags, its clips; then its numbered run (\"a 0\" to \"a 2\")");
        expect(wgf_sheet_set_clip_event(array, "walk", 1, 7) && wgf_sheet_get_clip_event(array, "walk", 1) == 7 &&
                   !wgf_sheet_set_clip_event(array, "walk", 5, 7) && !wgf_sheet_set_clip_event(array, "nope", 0, 7),
               "an event on a clip's second frame; outside it, or a clip it hasn't: refused");
        expect(wgf_sprite_play_clip(sprite, "walk", true) && wgf_sprite_get_source(sprite).x == 1.0f,
               "walk: frame a 1");
        frames(2, 0.1f);
        expect(wgf_sprite_get_source(sprite).x == 1.0f, "a 1 shows for its 0.3 s");
        frames(1, 0.1f);
        expect(wgf_sprite_get_source(sprite).y == 1.0f && wgf_sprite_take_clip_events(sprite, events, 4) == 1 &&
                   events[0] == 7 && wgf_sprite_take_clip_events(sprite, events, 4) == 0,
               "a 2 after it: its event raised, taken once");
        expect(wgf_sprite_set_clip_facing(sprite, "left") && strcmp(wgf_sprite_get_clip(sprite), "walk_left") == 0 &&
                   strcmp(wgf_sprite_get_clip_facing(sprite), "left") == 0 && wgf_sprite_get_clip_frame(sprite) == 1,
               "facing left: walk_left, on from the same place");
        expect(wgf_sprite_set_clip_facing(sprite, "up") && strcmp(wgf_sprite_get_clip(sprite), "walk") == 0,
               "facing up, with no walk_up: walk");
        expect(wgf_sprite_play_clip(sprite, "back", false) && wgf_sprite_get_source(sprite).y == 1.0f,
               "a reverse tag: from its last");
        wgf_actor_destroy(sprite, WGF_ACTOR_DESTROY_CHILDREN);
        wgf_resource_release(hash);
        wgf_resource_release(array);
    }
    write = wgf_fs_write("sheet_test/broken.json", (const unsigned char *)"{\"frames\": 3}", 13);
    for (i = 0; i < 1000 && wgf_fs_task_get_status(write) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(write);
    {
        const wgf_sheet_t broken = wgf_sheet_create("sheet_test/broken.json");
        start = wgf_time_get_seconds();
        while (wgf_resource_get_status(broken) == WGF_RESOURCE_STATUS_PENDING &&
               wgf_time_get_seconds() - start < 30.0) {
            wgf_core_priv_update();
        }
        expect(wgf_resource_get_status(broken) == WGF_RESOURCE_STATUS_FAILED, "not an atlas: FAILED");
        wgf_resource_release(broken);
    }

    wgf_resource_release(texture);
    write = wgf_fs_rmdir("sheet_test");
    for (i = 0; i < 1000 && wgf_fs_task_get_status(write) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(write);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
