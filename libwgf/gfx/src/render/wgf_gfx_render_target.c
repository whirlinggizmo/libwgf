#include "wgf_render.h"

#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_log.h"

/* Drawing into render targets, libwgt's (wgrender's wgr_render_begin_texture): each
 * target begun is a pass of the frame's own, recorded in the same immediate mode as the
 * screen and between the same commands, a command with no replay closing the layer at
 * its begin and its end (wgf_gfx_render_commands.c). At the frame's end each target's
 * pass is drawn, in the order begun, before the screen's: cleared to the target's color,
 * or, a target drawn into earlier this frame, keeping it. Its own file, set as the
 * frame's hooks by the first target begun, so a program that draws into none links none
 * of it. */

#define MAX_TARGET_PASSES 16

typedef struct pass_t {
    wgf_texture_t target;
    int width, height;
} pass_t;

static pass_t passes[MAX_TARGET_PASSES + 1]; /* numbered from 1: 0 is the screen */
static int pass_count;                       /* this frame's */
static unsigned passes_frame;
static bool passes_logged;


/* How pass `index` starts: cleared to its target's color, or keeping what an earlier
 * pass into the same target drew. */
static sg_pass_action action_of(int index)
{
    const wgf_color_t color = wgf_gfx_priv_texture_get_clear_color(passes[index].target);
    sg_pass_action action;
    bool drawn_before = false;
    int p;
    for (p = 1; p < index; p++) drawn_before = drawn_before || passes[p].target == passes[index].target;
    memset(&action, 0, sizeof(action));
    action.colors[0].load_action = drawn_before ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR;
    action.colors[0].clear_value.r = (float)wgf_color_get_red(color) / 255.0f;
    action.colors[0].clear_value.g = (float)wgf_color_get_green(color) / 255.0f;
    action.colors[0].clear_value.b = (float)wgf_color_get_blue(color) / 255.0f;
    action.colors[0].clear_value.a = (float)wgf_color_get_alpha(color) / 255.0f;
    if (drawn_before) action.depth.load_action = SG_LOADACTION_LOAD;
    return action;
}

/* The frame's end: a target left open is ended; then each target's pass, before the
 * screen's. */
static void at_frame_end(bool draw)
{
    int p;
    if (!draw) {
        if (wgf_gfx_priv_render_get_pass() == 0) return;
        wgf_log_warn("wgf_gfx: a frame ended inside wgf_render_begin_target; ending the target");
        wgf_render_end_target();
        return;
    }
    if (passes_frame != wgf_gfx_priv_render_get_frame()) return; /* none this frame */
    for (p = 1; p <= pass_count; p++) {
        sg_pass pass;
        int width, height;
        memset(&pass, 0, sizeof(pass));
        if (!wgf_gfx_priv_texture_get_target(passes[p].target, &pass.attachments, &width, &height)) {
            continue; /* released since */
        }
        pass.action = action_of(p);
        pass.label = "wgf-render-target";
        wgf_gfx_priv_render_set_drawing_into(passes[p].target);
        sg_begin_pass(&pass);
        wgf_gfx_priv_render_draw_pass(p);
        sg_end_pass();
    }
    wgf_gfx_priv_render_set_drawing_into(0);
    pass_count = 0;
}

bool wgf_render_begin_target(wgf_texture_t target)
{
    sg_attachments attachments;
    int width, height;
    if (!wgf_gfx_priv_is_in_frame()) {
        wgf_log_warn("wgf_render_begin_target: not in a frame (draw in a frame callback)");
        return false;
    }
    if (wgf_gfx_priv_render_get_pass() != 0) {
        wgf_log_warn("wgf_render_begin_target: already drawing into a target (wgf_render_end_target first)");
        return false;
    }
    if (!wgf_gfx_priv_texture_get_target(target, &attachments, &width, &height)) {
        wgf_log_warn("wgf_render_begin_target: not a render target (wgf_texture_create_target makes one)");
        return false;
    }
    if (passes_frame != wgf_gfx_priv_render_get_frame()) {
        passes_frame = wgf_gfx_priv_render_get_frame();
        pass_count = 0;
    }
    if (pass_count >= MAX_TARGET_PASSES) {
        if (!passes_logged) {
            wgf_log_warn("wgf_render_begin_target: more than %d target passes in a frame", MAX_TARGET_PASSES);
        }
        passes_logged = true;
        return false;
    }
    if (!wgf_gfx_priv_render_add_command(NULL, 0)) return false; /* what the screen drew so far is its own */
    wgf_gfx_priv_render_set_targets(at_frame_end);
    pass_count++;
    passes[pass_count].target = target;
    passes[pass_count].width = width;
    passes[pass_count].height = height;
    wgf_gfx_priv_render_enter_pass(pass_count, target, width, height);
    return true;
}

void wgf_render_end_target(void)
{
    if (!wgf_gfx_priv_is_in_frame() || wgf_gfx_priv_render_get_pass() == 0) {
        wgf_log_warn("wgf_render_end_target: not drawing into a target");
        return;
    }
    wgf_gfx_priv_render_add_command(NULL, 0); /* what the target drew is its own */
    wgf_gfx_priv_render_enter_pass(0, 0, 0, 0);
}

bool wgf_render_set_target_clear_color(wgf_texture_t target, wgf_color_t color)
{
    return wgf_gfx_priv_texture_set_clear_color(target, color);
}

wgf_color_t wgf_render_get_target_clear_color(wgf_texture_t target)
{
    return wgf_gfx_priv_texture_get_clear_color(target);
}
