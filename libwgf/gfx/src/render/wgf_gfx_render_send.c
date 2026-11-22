#include "render/wgf_gfx_render_priv.h"

#include <stdlib.h>
#include <string.h>

/* Writes that outlast their frame (wgf_gfx_render_priv.h, wgf_gfx_priv_render_send_buffer):
 * a blank texture's pixels, a changed mesh's vertices. sokol takes one through a staging
 * buffer, which it keeps two of on the GPU, so each is kept only while it is in use. */

#define IDLE_FRAMES 30 /* frames nothing was sent through one before it goes */

typedef struct staging_t {
    uint32_t to;       /* the buffer's or image's id it sends to */
    bool image;        /* which: their ids can be the same number */
    sg_buffer buffer;  /* the staging buffer */
    unsigned sent;     /* the frame of its last send (wgf_gfx_priv_render_get_last_frame() + 1) */
} staging_t;

static staging_t *stagings;
static int staging_count, staging_capacity;

/* The frame's end: the ones idle since IDLE_FRAMES go. gfx's stop: all of them, and the list. */
static void frame(bool stop)
{
    const unsigned last = wgf_gfx_priv_render_get_last_frame();
    int i;
    for (i = staging_count - 1; i >= 0; i--) {
        if (!stop && last - stagings[i].sent < IDLE_FRAMES) continue;
        sg_destroy_buffer(stagings[i].buffer);
        stagings[i] = stagings[--staging_count];
    }
    if (stop) {
        free(stagings);
        stagings = NULL;
        staging_count = staging_capacity = 0;
    }
}

/* `data` written to the staging buffer of `to`, made the first time; NULL in a pass, when
 * it was sent to already this frame, or when the buffer wasn't made. */
static const staging_t *stage(uint32_t to, bool image, const void *data, size_t size)
{
    const unsigned now = wgf_gfx_priv_render_get_last_frame() + 1; /* the frames ended: one send between two */
    sg_write_buffer_desc write;
    staging_t *s = NULL;
    int i;
    if (sg_query_pass_state() != SG_PASSSTATE_NONE) return NULL;
    for (i = 0; i < staging_count && s == NULL; i++) {
        if (stagings[i].to == to && stagings[i].image == image) s = &stagings[i];
    }
    if (s != NULL && s->sent == now) return NULL;
    if (s == NULL) {
        sg_buffer_desc desc;
        if (staging_count == staging_capacity) {
            const int capacity = staging_capacity > 0 ? staging_capacity * 2 : 8;
            staging_t *grown = (staging_t *)realloc(stagings, (size_t)capacity * sizeof(staging_t));
            if (grown == NULL) return NULL;
            stagings = grown;
            staging_capacity = capacity;
        }
        memset(&desc, 0, sizeof(desc));
        desc.usage.staging_buffer = true;
        desc.usage.write_transient = true;
        desc.usage.copy_src = true;
        desc.size = size;
        desc.label = "wgf-staging";
        s = &stagings[staging_count];
        s->to = to;
        s->image = image;
        s->buffer = sg_make_buffer(&desc);
        if (sg_query_buffer_state(s->buffer) != SG_RESOURCESTATE_VALID) {
            sg_destroy_buffer(s->buffer);
            return NULL;
        }
        staging_count++;
        wgf_gfx_priv_render_set_staging(frame);
    }
    memset(&write, 0, sizeof(write));
    write.src.data.ptr = data;
    write.src.data.size = size;
    write.dst.buffer = s->buffer;
    sg_write_buffer_transient(&write);
    s->sent = now;
    return s;
}

int wgf_gfx_priv_render_get_staging_count(void)
{
    return staging_count;
}

bool wgf_gfx_priv_render_send_buffer(sg_buffer buffer, const void *data, size_t size)
{
    const staging_t *s = stage(buffer.id, false, data, size);
    sg_copy_buffer_to_buffer_desc copy;
    if (s == NULL) return false;
    memset(&copy, 0, sizeof(copy));
    copy.src.buffer = s->buffer;
    copy.dst.buffer = buffer;
    copy.size = size;
    sg_copy_buffer_to_buffer(&copy);
    return true;
}

bool wgf_gfx_priv_render_send_image(sg_image image, const void *data, size_t size)
{
    const staging_t *s = stage(image.id, true, data, size);
    sg_copy_buffer_to_image_desc copy;
    if (s == NULL) return false;
    memset(&copy, 0, sizeof(copy));
    copy.src.buffer = s->buffer;
    copy.dst.image = image;
    sg_copy_buffer_to_image(&copy);
    return true;
}
