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
 * Each layer is drawn by the range of sokol_gl's commands recorded while it was the
 * current one (`sgl_context_draw_layer_range`, libwgf's sokol fork), not by a scan of the
 * frame's whole list a layer, which costs layers times commands (wgrender-c measured 3,000
 * switches at 5.4 ms against 0.6 ms by range).
 * A command made with no replay closes a layer and draws nothing: a render target's
 * begin or end (wgf_gfx_render_target.c), so each layer is drawn in the pass it was
 * recorded in, as libwgt's.
 * Its own file, reached only by a command's maker, so a program that draws no stage
 * links none of it: render.c only calls the drawing installed here. */

typedef struct command_t {
    void (*replay)(int index); /* NULL: a pass begun or ended */
    int index;
    int pass;        /* the pass it, and the layer before it, are drawn in */
    int scissor[4];  /* the clip it was made under, in the framebuffer's pixels */
    int viewport[4]; /* immediate mode's then (3D's is the visible area's), given back after it */
    int first;       /* the sokol_gl command the layer after it starts at */
} command_t;

static command_t *commands;
static int command_count, command_capacity;
static unsigned commands_frame; /* the frame they were made in */

/* Pass `pass`'s layers and commands; the screen's last, which ends the frame's. */
static void draw_commands(int pass)
{
    const sgl_context context = wgf_gfx_priv_render_get_context();
    int i, total;
    sgl_set_context(context);
    total = sgl_num_commands();
    for (i = 0; i <= command_count; i++) {
        /* layer i's commands: from where the command before it opened it to where the next closed it */
        const int first = i > 0 ? commands[i - 1].first : 0, end = i < command_count ? commands[i].first : total;
        if ((i < command_count ? commands[i].pass : 0) == pass) {
            sgl_context_draw_layer_range(context, i, first, end - first);
        }
        if (i < command_count && commands[i].pass == pass && commands[i].replay != NULL) {
            const command_t *c = &commands[i];
            sg_apply_scissor_rect(c->scissor[0], c->scissor[1], c->scissor[2], c->scissor[3], true);
            c->replay(c->index);
            /* the layer after it goes on as immediate mode left off */
            sg_apply_viewport(c->viewport[0], c->viewport[1], c->viewport[2], c->viewport[3], true);
            sg_apply_scissor_rect(c->scissor[0], c->scissor[1], c->scissor[2], c->scissor[3], true);
        }
    }
    if (pass == 0) command_count = 0;
}

void wgf_gfx_priv_render_draw_pass(int pass)
{
    draw_commands(pass);
}

/* Side layers: immediate mode recorded for a command to draw itself (a stage's 3D
 * shapes), numbered past every layer the frame's commands can open. */
#define SIDE_LAYER_BASE (1 << 20)
static int side_layers;
static unsigned side_frame;
static int *side_ranges; /* each side layer's first command and its end, two to a layer */
static int side_capacity;

int wgf_gfx_priv_render_begin_side_layer(const float view_proj[16])
{
    const sgl_context context = wgf_gfx_priv_render_get_context();
    if (side_frame != wgf_gfx_priv_render_get_frame()) {
        side_layers = 0;
        side_frame = wgf_gfx_priv_render_get_frame();
    }
    sgl_set_context(context);
    if (side_layers == side_capacity) {
        const int capacity = side_capacity > 0 ? side_capacity * 2 : 16;
        int *grown = (int *)realloc(side_ranges, sizeof(int) * 2 * (size_t)capacity);
        if (grown != NULL) {
            side_ranges = grown;
            side_capacity = capacity;
        }
    }
    if (side_layers < side_capacity) {
        side_ranges[side_layers * 2] = side_ranges[side_layers * 2 + 1] = sgl_num_commands();
    }
    sgl_layer(SIDE_LAYER_BASE + side_layers);
    sgl_push_pipeline();
    sgl_matrix_mode_projection();
    sgl_push_matrix();
    sgl_matrix_mode_modelview();
    sgl_push_matrix();
    wgf_gfx_priv_render_load_3d(view_proj);
    return SIDE_LAYER_BASE + side_layers++;
}

void wgf_gfx_priv_render_end_side_layer(void)
{
    sgl_set_context(wgf_gfx_priv_render_get_context());
    if (side_layers > 0 && side_layers <= side_capacity) side_ranges[side_layers * 2 - 1] = sgl_num_commands();
    sgl_matrix_mode_projection();
    sgl_pop_matrix();
    sgl_matrix_mode_modelview();
    sgl_pop_matrix();
    sgl_pop_pipeline();
    sgl_layer(commands_frame == wgf_gfx_priv_render_get_frame() ? command_count : 0);
}

void wgf_gfx_priv_render_draw_side_layer(int layer)
{
    const sgl_context context = wgf_gfx_priv_render_get_context();
    const int n = layer - SIDE_LAYER_BASE;
    if (n >= 0 && n < side_layers && n < side_capacity) {
        sgl_context_draw_layer_range(context, layer, side_ranges[n * 2], side_ranges[n * 2 + 1] - side_ranges[n * 2]);
    } else {
        sgl_context_draw_layer(context, layer); /* no room was had to keep its range: found by a scan */
    }
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
    c->pass = wgf_gfx_priv_render_get_pass();
    wgf_gfx_priv_render_get_scissor(c->scissor);
    wgf_gfx_priv_render_get_viewport(c->viewport);
    sgl_set_context(wgf_gfx_priv_render_get_context());
    c->first = sgl_num_commands();
    sgl_layer(command_count);
    wgf_gfx_priv_render_set_drawing(draw_commands);
    return true;
}
