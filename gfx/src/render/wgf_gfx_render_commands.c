#include <stdlib.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "util/sokol_gl.h"
#include "wgf_log.h"

/* The frame's commands: draws of sokol_gfx's own (a stage's models) between the
 * frame's immediate mode, as libwgt's (wgrender's render commands). Immediate mode
 * records into a sokol_gl layer; a command closes the layer before it and opens the
 * next, so at the frame's end layer 0 is drawn, then command 0, then layer 1, and so on,
 * in the order they were made. A frame with no commands draws its one layer as before.
 * Its own file, reached only by a command's maker, so a program that draws no stage
 * links none of it: render.c only calls the drawing installed here. */

typedef struct command_t {
    void (*replay)(int index);
    int index;
    int scissor[4];  /* the clip it was made under, in the framebuffer's pixels */
    int viewport[4]; /* immediate mode's then (3D's is the visible area's), given back after it */
} command_t;

static command_t *commands;
static int command_count, command_capacity;
static unsigned commands_frame; /* the frame they were made in */

static void draw_commands(void)
{
    const sgl_context context = wgf_gfx_priv_render_get_context();
    int i;
    for (i = 0; i <= command_count; i++) {
        sgl_context_draw_layer(context, i);
        if (i < command_count) {
            const command_t *c = &commands[i];
            sg_apply_scissor_rect(c->scissor[0], c->scissor[1], c->scissor[2], c->scissor[3], true);
            c->replay(c->index);
            /* the layer after it goes on as immediate mode left off */
            sg_apply_viewport(c->viewport[0], c->viewport[1], c->viewport[2], c->viewport[3], true);
            sg_apply_scissor_rect(c->scissor[0], c->scissor[1], c->scissor[2], c->scissor[3], true);
        }
    }
    command_count = 0;
}

bool wgf_gfx_priv_render_add_command(void (*replay)(int index), int index)
{
    command_t *c;
    if (!wgf_gfx_priv_is_in_frame()) return false;
    if (commands_frame != wgf_gfx_priv_render_get_frame()) { /* a frame's first: the last frame's are drawn */
        command_count = 0;
        commands_frame = wgf_gfx_priv_render_get_frame();
    }
    if (command_count == command_capacity) {
        const int capacity = command_capacity > 0 ? command_capacity * 2 : 16;
        command_t *grown = (command_t *)realloc(commands, sizeof(command_t) * (size_t)capacity);
        if (grown == NULL) {
            wgf_log_error("wgf_gfx: out of memory recording the frame's draws");
            return false;
        }
        commands = grown;
        command_capacity = capacity;
    }
    c = &commands[command_count++];
    c->replay = replay;
    c->index = index;
    wgf_gfx_priv_render_get_scissor(c->scissor);
    wgf_gfx_priv_render_get_viewport(c->viewport);
    sgl_set_context(wgf_gfx_priv_render_get_context());
    sgl_layer(command_count);
    wgf_gfx_priv_render_set_drawing(draw_commands);
    return true;
}
