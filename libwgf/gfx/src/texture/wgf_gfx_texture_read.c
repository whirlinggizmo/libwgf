#include "wgf_texture.h"

#include <stdlib.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "texture/wgf_gfx_png_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_render.h"

/* Render targets read back, new to libwgf: wgrender-c left it open among its targets'
 * follow-ups ("reading pixels back / screenshots"), and libwgt's tests read the screen
 * with GL of their own. Each read is a task; the part's update, at the start of the frame
 * after it was asked, reads every one whose frame has been drawn and committed (marked at
 * the frame's end, so one asked in a tick, before the update, waits a frame), outside any
 * pass, through platform's GL (wgf_platform_priv_read_image).
 * A target is stored top row first (wgf_gfx_render.c's flipped passes), which is GL's
 * first row read, so the rows need no flipping. A part of its own, installed by the first
 * read, so a program that reads none links none of it.
 * The screen is read too (wgf_render_read_screen): at the end of the frame, after its last
 * pass and before it is shown, the only time the window's framebuffer holds it; its rows,
 * GL's bottom first, are turned over. */

typedef struct read_t {
    wgf_texture_read_status_t status;
    wgf_texture_t target; /* a reference held while PENDING */
    bool drawn;           /* the frame it was asked in has been drawn: read at the next update */
    bool screen;          /* the screen's: read as the frame ends */
    int width, height;
    unsigned char *data; /* DONE: width * height * 4 bytes */
} read_t;

static bool pool_ready;
static wgf_core_priv_handle_pool_t read_pool;
static read_t *reads;

static read_t *read_of_at(wgf_texture_read_t read, const char *caller)
{
    uint16_t index;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve_at(&read_pool, read, &index, caller)) return NULL;
    return &reads[index];
}
#define read_of(...) read_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

/* One read: DONE with its pixels, or FAILED; its target let go of either way. */
static void read_target(read_t *read_ptr)
{
    static bool warned;
    sg_image image;
    int width, height;
    unsigned char *data = NULL;
    if (wgf_gfx_priv_texture_get_target_image(read_ptr->target, &image, &width, &height)) {
        data = malloc((size_t)width * (size_t)height * 4u);
        if (data != NULL && !wgf_platform_priv_read_image(image, width, height, data)) {
            free(data);
            data = NULL;
            if (!warned) wgf_log_warn("wgf_texture_read: there is no GPU to read here (a headless build)");
            warned = true;
        }
    }
    wgf_resource_release(read_ptr->target);
    read_ptr->target = 0;
    read_ptr->status = data != NULL ? WGF_TEXTURE_READ_STATUS_DONE : WGF_TEXTURE_READ_STATUS_FAILED;
    if (data == NULL) return;
    read_ptr->data = data;
    read_ptr->width = width;
    read_ptr->height = height;
}

/* The screen's read: DONE with the frame as drawn, or FAILED. */
static void read_screen(read_t *read_ptr)
{
    static bool warned;
    const int width = wgf_platform_priv_get_framebuffer_width(), height = wgf_platform_priv_get_framebuffer_height();
    const size_t row = (size_t)width * 4u;
    unsigned char *data = width > 0 && height > 0 ? malloc(row * (size_t)height) : NULL;
    unsigned char *swap = data != NULL ? malloc(row) : NULL;
    int y;
    if (swap == NULL || !wgf_platform_priv_read_screen(width, height, data)) {
        if (swap != NULL && !warned) wgf_log_warn("wgf_render_read_screen: there is no GPU to read here (a headless build)");
        if (swap != NULL) warned = true;
        free(data);
        free(swap);
        read_ptr->status = WGF_TEXTURE_READ_STATUS_FAILED;
        return;
    }
    for (y = 0; y < height / 2; y++) { /* the top row first */
        unsigned char *top = data + (size_t)y * row, *bottom = data + (size_t)(height - 1 - y) * row;
        memcpy(swap, top, row);
        memcpy(top, bottom, row);
        memcpy(bottom, swap, row);
    }
    free(swap);
    read_ptr->status = WGF_TEXTURE_READ_STATUS_DONE;
    read_ptr->data = data;
    read_ptr->width = width;
    read_ptr->height = height;
}

static void update(float dt)
{
    uint16_t i;
    (void)dt;
    for (i = 1; i < read_pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&read_pool, i) != 0 &&
            reads[i].status == WGF_TEXTURE_READ_STATUS_PENDING && reads[i].drawn && !reads[i].screen) {
            read_target(&reads[i]);
        }
    }
}

static void end_frame(void)
{
    uint16_t i;
    for (i = 1; i < read_pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&read_pool, i) == 0) continue;
        if (reads[i].screen && reads[i].status == WGF_TEXTURE_READ_STATUS_PENDING) read_screen(&reads[i]);
        reads[i].drawn = true;
    }
}

static void free_read(read_t *read_ptr)
{
    if (read_ptr->target != 0) wgf_resource_release(read_ptr->target);
    free(read_ptr->data);
}

static void stop(void)
{
    uint16_t i;
    for (i = 1; i < read_pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&read_pool, i) != 0) free_read(&reads[i]);
    }
    wgf_core_priv_handle_pool_destroy(&read_pool);
    reads = NULL;
    pool_ready = false;
}

static wgf_core_priv_part_t part = {.name = "texture reads",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_TEXTURE_READS,
                                    .update = update,
                                    .end_frame = end_frame,
                                    .stop = stop};

/* ------------------------------------------------------------ public API ---- */

/* A new read, PENDING; 0 (logged, as `caller`) out of room. */
static wgf_texture_read_t new_read(const char *caller)
{
    wgf_texture_read_t read;
    read_t *read_ptr;
    if (!pool_ready) {
        pool_ready = wgf_core_priv_handle_pool_init(&read_pool, WGF_CORE_PRIV_HANDLE_KIND_TEXTURE_READ,
                                                    (void **)&reads, sizeof(read_t), 4, 256);
        if (!pool_ready) return 0;
        wgf_core_priv_part_install(&part);
    }
    read = wgf_core_priv_handle_pool_alloc(&read_pool);
    read_ptr = read_of(read);
    if (read_ptr == NULL) {
        wgf_log_warn("%s: no room for another read (destroy those read)", caller);
        return 0;
    }
    memset(read_ptr, 0, sizeof(*read_ptr));
    read_ptr->status = WGF_TEXTURE_READ_STATUS_PENDING;
    return read;
}

wgf_texture_read_t wgf_texture_read(wgf_texture_t target)
{
    sg_image image;
    int width, height;
    wgf_texture_read_t read;
    if (!wgf_gfx_priv_texture_get_target_image(target, &image, &width, &height)) {
        wgf_log_warn("wgf_texture_read: not a render target (wgf_texture_create_target makes one)");
        return 0;
    }
    read = new_read("wgf_texture_read");
    if (read == 0) return 0;
    read_of(read)->target = target;
    wgf_gfx_priv_texture_retain(target);
    return read;
}

wgf_texture_read_t wgf_render_read_screen(void)
{
    const wgf_texture_read_t read = new_read("wgf_render_read_screen");
    if (read != 0) read_of(read)->screen = true;
    return read;
}

wgf_fs_task_t wgf_texture_read_save(wgf_texture_read_t read, const char *path)
{
    const read_t *read_ptr = read_of(read);
    unsigned char *png;
    int size = 0;
    wgf_fs_task_t task;
    if (read_ptr == NULL || read_ptr->data == NULL || path == NULL) return 0;
    png = wgf_gfx_priv_png_encode(read_ptr->data, read_ptr->width, read_ptr->height, &size);
    if (png == NULL) {
        wgf_log_warn("wgf_texture_read_save: no memory to encode %s", path);
        return 0;
    }
    task = wgf_fs_write(path, png, size); /* copied before it returns */
    free(png);
    return task;
}

wgf_texture_read_status_t wgf_texture_read_get_status(wgf_texture_read_t read)
{
    const read_t *read_ptr = read_of(read);
    return read_ptr != NULL ? read_ptr->status : WGF_TEXTURE_READ_STATUS_NONE;
}

int wgf_texture_read_get_width(wgf_texture_read_t read)
{
    const read_t *read_ptr = read_of(read);
    return read_ptr != NULL ? read_ptr->width : 0;
}

int wgf_texture_read_get_height(wgf_texture_read_t read)
{
    const read_t *read_ptr = read_of(read);
    return read_ptr != NULL ? read_ptr->height : 0;
}

const unsigned char *wgf_texture_read_get_data(wgf_texture_read_t read)
{
    const read_t *read_ptr = read_of(read);
    return read_ptr != NULL ? read_ptr->data : NULL;
}

int wgf_texture_read_get_size(wgf_texture_read_t read)
{
    const read_t *read_ptr = read_of(read);
    return read_ptr != NULL && read_ptr->data != NULL ? read_ptr->width * read_ptr->height * 4 : 0;
}

bool wgf_texture_read_destroy(wgf_texture_read_t read)
{
    read_t *read_ptr = read_of(read);
    if (read_ptr == NULL) return false;
    free_read(read_ptr);
    return wgf_core_priv_handle_pool_free(&read_pool, read);
}
