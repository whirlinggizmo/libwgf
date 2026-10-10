#include "wgf_render.h"

#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "util/sokol_gl.h"
#include "wgf_core_part_priv.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"

/* gfx's start and stop, and the frame, as libwgt's: sokol_gfx set up on the window's
 * device, and each frame one pass into the window, both as the platform gives them
 * (sokol_glue). A frame is recorded first and drawn at its end, so what its parts must
 * send to the GPU first (text's glyph atlas) goes up before the pass opens, which it
 * can't inside one. Immediate mode (sokol_gl) records the frame, and a stage's draw is a
 * command between its layers (wgf_gfx_render_commands.c), drawn in the same pass;
 * libwgt's render targets and effects are milestone 2, step 12's. */

static bool setup;
static bool in_frame;
static unsigned frame_number;
static unsigned last_frame;
static wgf_color_t clear_color = 0x000000FFu; /* black */
static wgf_color_t bar_color = 0x000000FFu;   /* black: the presentation's bars (wgf_presentation.h) */
static int frame_width;
static int frame_height;
static float frame_dpi_scale = 1.0f;
static wgf_platform_priv_presentation_t present; /* the frame's: logical to framebuffer pixels */
static void (*fill_bars)(void); /* wgf_presentation_set's (wgf_gfx_presentation.c); NULL before a mode is set */
static void (*drawing)(void);   /* the frame's commands' (wgf_gfx_render_commands.c); NULL before any */
static int viewport_3d[4];      /* immediate mode's 3D viewport (set_3d), in frame viewport_3d_frame */
static unsigned viewport_3d_frame;

/* Immediate mode drawing records into a sokol_gl context of gfx's own, drawn inside the
 * frame's pass. sokol_gl's default context can't be resized, so it stays tiny and
 * unused, and gfx's is replaced with a larger one when a frame runs out of room. */
#define START_VERTICES 65536
#define START_COMMANDS 16384
#define MAX_VERTICES (1 << 22)
#define MAX_COMMANDS (1 << 20)

static sgl_context draw_context;
static sgl_pipeline draw_pipeline_2d;
static sgl_pipeline draw_pipeline_3d; /* made by the first 3D draw: depth tested and written */
static int vertex_capacity;
static int command_capacity;
static bool at_most_logged;

/* The clip stack: each push intersects with the clip it's pushed inside, so a scroll
 * list in a panel stays inside the panel. Clips are logical pixels with a top-left
 * origin; the scissor they become is in the frame's pixels. */
#define MAX_CLIPS 32
typedef struct clip_rect_t {
    float x, y, width, height;
} clip_rect_t;
static clip_rect_t clips[MAX_CLIPS];
static int clip_depth;    /* entries in use */
static int clip_overflow; /* pushes past MAX_CLIPS, so their pops match up */
static bool clip_warned;

/* sokol_gfx's and sokol_gl's log lines, into core's log. */
static void sokol_log(const char *tag, uint32_t level, uint32_t item, const char *message, uint32_t line,
                      const char *file, void *user)
{
    static const wgf_log_level_t levels[] = {WGF_LOG_LEVEL_FATAL, WGF_LOG_LEVEL_ERROR, WGF_LOG_LEVEL_WARN,
                                             WGF_LOG_LEVEL_INFO};
    const wgf_log_level_t wgf_level = level < 4 ? levels[level] : WGF_LOG_LEVEL_INFO;
    (void)user;
    if (wgf_level < wgf_log_get_level()) return;
    wgf_log_format(wgf_level, file, (int)line, "%s: %s (item %u)", tag, message != NULL ? message : "",
                   (unsigned)item);
}

static const char *backend_name(sg_backend backend)
{
    switch (backend) {
        case SG_BACKEND_GLCORE: return "OpenGL core";
        case SG_BACKEND_GLES3: return "WebGL2 / GLES3";
        case SG_BACKEND_D3D11: return "Direct3D 11";
        case SG_BACKEND_METAL_MACOS:
        case SG_BACKEND_METAL_IOS:
        case SG_BACKEND_METAL_SIMULATOR: return "Metal";
        case SG_BACKEND_WGPU: return "WebGPU";
        case SG_BACKEND_DUMMY: return "dummy (no GPU)";
        default: return "unknown";
    }
}

bool wgf_gfx_priv_start(void)
{
    sg_desc desc;
    sgl_desc_t gl_desc;
    sgl_context_desc_t context_desc;
    sg_pipeline_desc pipeline_desc;
    int samples;
    if (setup) return true;
    memset(&desc, 0, sizeof(desc));
    desc.environment = wgf_platform_priv_get_environment();
    samples = desc.environment.defaults.sample_count > 1 ? desc.environment.defaults.sample_count : 1;
    desc.logger.func = sokol_log;
    desc.buffer_pool_size = WGF_GFX_PRIV_BUFFER_POOL_SIZE;
    desc.image_pool_size = WGF_GFX_PRIV_IMAGE_POOL_SIZE;
    desc.view_pool_size = WGF_GFX_PRIV_VIEW_POOL_SIZE;
    desc.pipeline_pool_size = WGF_GFX_PRIV_PIPELINE_POOL_SIZE;
    sg_setup(&desc);
    if (!sg_isvalid()) {
        wgf_log_error("wgf_gfx: couldn't start on the window's device");
        return false;
    }
    wgf_log_info("wgf_gfx: %s, %d sample%s a pixel", backend_name(sg_query_backend()), samples,
                 samples == 1 ? "" : "s");

    memset(&gl_desc, 0, sizeof(gl_desc));
    gl_desc.max_vertices = 64;
    gl_desc.max_commands = 16;
    gl_desc.logger.func = sokol_log;
    sgl_setup(&gl_desc);
    vertex_capacity = START_VERTICES;
    command_capacity = START_COMMANDS;
    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.max_vertices = vertex_capacity;
    context_desc.max_commands = command_capacity;
    draw_context = sgl_make_context(&context_desc);
    sgl_set_context(draw_context);
    /* 2D: alpha blended, no depth; alpha written too (sokol_gl writes RGB alone unless
       told), as libwgt's */
    memset(&pipeline_desc, 0, sizeof(pipeline_desc));
    pipeline_desc.colors[0].write_mask = SG_COLORMASK_RGBA;
    pipeline_desc.colors[0].blend.enabled = true;
    pipeline_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pipeline_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pipeline_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pipeline_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    draw_pipeline_2d = sgl_make_pipeline(&pipeline_desc);
    at_most_logged = false;
    setup = true;
    wgf_gfx_priv_texture_setup();
    return true;
}

static int doubled(int value, int most)
{
    return value > most / 2 ? most : value * 2;
}

/* A frame ran out of room: the draws past it were dropped; make room for the next. */
static void grow_draw_context(sgl_error_t error)
{
    const bool vertices_full = error.vertices_full && vertex_capacity < MAX_VERTICES;
    const bool commands_full = (error.commands_full || error.uniforms_full) && command_capacity < MAX_COMMANDS;
    sgl_context_desc_t context_desc;
    sgl_context context;

    if (!error.vertices_full && !error.commands_full && !error.uniforms_full) return;
    if (!vertices_full && !commands_full) {
        if (!at_most_logged) {
            wgf_log_warn("wgf_gfx: a frame needed more than %d vertices or %d draw commands, the most there can be; "
                         "draws past them were dropped",
                         MAX_VERTICES, MAX_COMMANDS);
            at_most_logged = true;
        }
        return;
    }
    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.max_vertices = vertices_full ? doubled(vertex_capacity, MAX_VERTICES) : vertex_capacity;
    context_desc.max_commands = commands_full ? doubled(command_capacity, MAX_COMMANDS) : command_capacity;
    context = sgl_make_context(&context_desc);
    if (context.id == SG_INVALID_ID) {
        wgf_log_error("wgf_gfx: couldn't grow immediate mode drawing to %d vertices, %d commands",
                      context_desc.max_vertices, context_desc.max_commands);
        return;
    }
    wgf_log_warn("wgf_gfx: a frame ran out of room (%d vertices, %d commands) and lost the draws past it; "
                 "growing to %d vertices, %d commands",
                 vertex_capacity, command_capacity, context_desc.max_vertices, context_desc.max_commands);
    sgl_destroy_context(draw_context);
    draw_context = context;
    vertex_capacity = context_desc.max_vertices;
    command_capacity = context_desc.max_commands;
    sgl_set_context(draw_context);
}

void wgf_gfx_priv_stop(void)
{
    if (!setup) return;
    if (in_frame) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_actor_shutdown(); /* before what actors hold references to */
    wgf_core_priv_part_stop(WGF_CORE_PRIV_PART_LAYER_GFX);
    wgf_gfx_priv_texture_shutdown();
    wgf_gfx_priv_draw_shutdown();
    sgl_destroy_pipeline(draw_pipeline_2d);
    sgl_destroy_context(draw_context);
    sgl_shutdown(); /* and the 3D pipeline with it */
    draw_pipeline_3d.id = SG_INVALID_ID;
    drawing = NULL;
    sg_shutdown();
    setup = false;
    frame_width = 0;
    frame_height = 0;
    frame_dpi_scale = 1.0f;
}

bool wgf_gfx_priv_render_is_running(void)
{
    return setup;
}

/* ---- clipping -------------------------------------------------------------- */

static void warn_clips(const char *what)
{
    if (!clip_warned) wgf_log_warn("wgf_gfx: %s", what);
    clip_warned = true;
}

/* What the frame shows, in logical pixels: the presentation's visible area (the whole
 * frame under NONE). */
static clip_rect_t frame_rect(void)
{
    clip_rect_t rect;
    rect.x = present.visible_x;
    rect.y = present.visible_y;
    rect.width = present.visible_width;
    rect.height = present.visible_height;
    return rect;
}

static clip_rect_t current_clip(void)
{
    return clip_depth > 0 ? clips[clip_depth - 1] : frame_rect();
}

void wgf_gfx_priv_render_get_scissor(int rect[4])
{
    const clip_rect_t clip = current_clip();
    const float x0 = present.offset_x + clip.x * present.scale_x, y0 = present.offset_y + clip.y * present.scale_y;
    const float x1 = present.offset_x + (clip.x + clip.width) * present.scale_x;
    const float y1 = present.offset_y + (clip.y + clip.height) * present.scale_y;
    rect[0] = (int)(x0 + 0.5f);
    rect[1] = (int)(y0 + 0.5f);
    rect[2] = (int)(x1 + 0.5f) - rect[0];
    rect[3] = (int)(y1 + 0.5f) - rect[1];
}

/* Immediate mode drawn from now on is clipped: a scissor, in the frame's pixels,
 * recorded in the layer. */
static void apply_clip(void)
{
    int rect[4];
    wgf_gfx_priv_render_get_scissor(rect);
    sgl_set_context(draw_context);
    sgl_scissor_rect(rect[0], rect[1], rect[2], rect[3], true);
}

void wgf_render_push_clip(float x, float y, float width, float height)
{
    const clip_rect_t parent = current_clip();
    const float x0 = x > parent.x ? x : parent.x;
    const float y0 = y > parent.y ? y : parent.y;
    const float x1 = x + (width > 0.0f ? width : 0.0f), px1 = parent.x + parent.width;
    const float y1 = y + (height > 0.0f ? height : 0.0f), py1 = parent.y + parent.height;
    const float right = x1 < px1 ? x1 : px1, bottom = y1 < py1 ? y1 : py1;
    clip_rect_t *clip;

    if (!in_frame) {
        warn_clips("wgf_render_push_clip outside a frame does nothing");
        return;
    }
    if (clip_depth >= MAX_CLIPS) {
        clip_overflow++;
        warn_clips("clip stack full (32 deep); extra pushes don't clip");
        return;
    }
    clip = &clips[clip_depth++];
    clip->x = x0;
    clip->y = y0;
    clip->width = right > x0 ? right - x0 : 0.0f;
    clip->height = bottom > y0 ? bottom - y0 : 0.0f;
    apply_clip();
}

void wgf_render_pop_clip(void)
{
    if (!in_frame) {
        warn_clips("wgf_render_pop_clip outside a frame does nothing");
        return;
    }
    if (clip_overflow > 0) {
        clip_overflow--;
        return;
    }
    if (clip_depth <= 0) {
        warn_clips("wgf_render_pop_clip without a matching push");
        return;
    }
    clip_depth--;
    apply_clip();
}

bool wgf_gfx_priv_render_get_clip(float *x, float *y, float *width, float *height)
{
    const clip_rect_t clip = current_clip();
    *x = clip.x;
    *y = clip.y;
    *width = clip.width;
    *height = clip.height;
    return clip_depth > 0;
}

/* ---- the frame ------------------------------------------------------------- */

void wgf_gfx_priv_begin_frame(void)
{
    if (!setup || in_frame) return;
    frame_width = wgf_platform_priv_get_framebuffer_width();
    frame_height = wgf_platform_priv_get_framebuffer_height();
    frame_dpi_scale = wgf_platform_priv_get_dpi_scale();
    wgf_platform_priv_get_presentation(&present);
    clip_depth = clip_overflow = 0;
    sgl_set_context(draw_context);
    sgl_defaults();
    in_frame = true; /* before the 2D projection, which reads the frame's size */
    wgf_gfx_priv_render_set_2d();
    if (present.bars && fill_bars != NULL) fill_bars();
    frame_number++;
}

void wgf_gfx_priv_end_frame(void)
{
    const wgf_core_priv_part_t *part;
    sgl_error_t error;
    sg_pass pass;
    if (!in_frame) return;
    if (clip_depth > 0 || clip_overflow > 0) warn_clips("clips pushed this frame weren't all popped");
    clip_depth = clip_overflow = 0;
    for (part = wgf_core_priv_part_list(); part != NULL; part = part->next) { /* text's glyphs, to the GPU */
        if (part->flush != NULL) part->flush();
    }
    memset(&pass, 0, sizeof(pass));
    pass.swapchain = wgf_platform_priv_get_swapchain();
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    {
        const wgf_color_t cleared = present.bars ? bar_color : clear_color;
        pass.action.colors[0].clear_value.r = (float)wgf_color_get_red(cleared) / 255.0f;
        pass.action.colors[0].clear_value.g = (float)wgf_color_get_green(cleared) / 255.0f;
        pass.action.colors[0].clear_value.b = (float)wgf_color_get_blue(cleared) / 255.0f;
        pass.action.colors[0].clear_value.a = (float)wgf_color_get_alpha(cleared) / 255.0f;
    }
    sg_begin_pass(&pass);
    if (drawing != NULL) drawing();
    else sgl_context_draw(draw_context);
    error = sgl_context_error(draw_context);
    sg_end_pass();
    sg_commit();
    in_frame = false;
    last_frame = frame_number;
    grow_draw_context(error);
    for (part = wgf_core_priv_part_list(); part != NULL; part = part->next) {
        if (part->end_frame != NULL) part->end_frame();
    }
}

void wgf_gfx_priv_render_set_2d(void)
{
    /* the framebuffer's edges in logical coordinates: a logical point lands on the
       pixel the presentation puts it at (offset + logical * scale) */
    const float left = -present.offset_x / present.scale_x, top = -present.offset_y / present.scale_y;
    const float right = ((float)frame_width - present.offset_x) / present.scale_x;
    const float bottom = ((float)frame_height - present.offset_y) / present.scale_y;
    sgl_set_context(draw_context);
    sgl_load_pipeline(draw_pipeline_2d);
    sgl_matrix_mode_projection();
    sgl_load_identity();
    sgl_ortho(left, right, bottom, top, -1.0f, 1.0f);
    sgl_matrix_mode_modelview();
    sgl_load_identity();
}

void wgf_gfx_priv_render_load_3d(const float view_proj[16])
{
    sgl_set_context(draw_context);
    if (draw_pipeline_3d.id == SG_INVALID_ID) {
        sg_pipeline_desc desc;
        memset(&desc, 0, sizeof(desc));
        desc.colors[0].write_mask = SG_COLORMASK_RGBA;
        desc.colors[0].blend.enabled = true;
        desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        desc.depth.write_enabled = true;
        draw_pipeline_3d = sgl_make_pipeline(&desc);
    }
    sgl_load_pipeline(draw_pipeline_3d);
    sgl_matrix_mode_projection();
    sgl_load_matrix(view_proj);
    sgl_matrix_mode_modelview();
    sgl_load_identity();
}

void wgf_gfx_priv_render_set_3d(const float view_proj[16])
{
    const wgf_platform_priv_presentation_t *p = &present;
    /* what the camera sees fills the visible area, in the framebuffer's pixels */
    viewport_3d[0] = (int)(p->offset_x + p->visible_x * p->scale_x + 0.5f);
    viewport_3d[1] = (int)(p->offset_y + p->visible_y * p->scale_y + 0.5f);
    viewport_3d[2] = (int)(p->visible_width * p->scale_x + 0.5f);
    viewport_3d[3] = (int)(p->visible_height * p->scale_y + 0.5f);
    viewport_3d_frame = frame_number;
    sgl_set_context(draw_context);
    sgl_viewport(viewport_3d[0], viewport_3d[1], viewport_3d[2], viewport_3d[3], true);
    wgf_gfx_priv_render_load_3d(view_proj);
}

void wgf_gfx_priv_render_end_3d(void)
{
    sgl_set_context(draw_context);
    sgl_viewport(0, 0, frame_width, frame_height, true);
    viewport_3d_frame = 0;
    wgf_gfx_priv_render_set_2d();
}

sgl_context wgf_gfx_priv_render_get_context(void)
{
    return draw_context;
}

void wgf_gfx_priv_render_set_drawing(void (*draw)(void))
{
    drawing = draw;
}


void wgf_gfx_priv_render_get_viewport(int rect[4])
{
    if (viewport_3d_frame == frame_number && frame_number != 0) {
        memcpy(rect, viewport_3d, sizeof(viewport_3d));
        return;
    }
    rect[0] = rect[1] = 0;
    rect[2] = frame_width;
    rect[3] = frame_height;
}

unsigned wgf_gfx_priv_render_get_frame(void)
{
    return frame_number;
}

unsigned wgf_gfx_priv_render_get_last_frame(void)
{
    return last_frame;
}

bool wgf_gfx_priv_is_in_frame(void)
{
    return in_frame;
}

int wgf_gfx_priv_render_get_vertex_capacity(void)
{
    return vertex_capacity;
}

int wgf_gfx_priv_render_get_command_capacity(void)
{
    return command_capacity;
}

void wgf_render_set_clear_color(wgf_color_t color)
{
    clear_color = color;
}

wgf_color_t wgf_render_get_clear_color(void)
{
    return clear_color;
}

void wgf_presentation_set_bar_color(wgf_color_t color)
{
    bar_color = color;
}

wgf_color_t wgf_presentation_get_bar_color(void)
{
    return bar_color;
}

/* The bars: the frame is cleared to their color, so the visible area is filled with
 * the clear color first, and drawing after is clipped to it. Called only through the
 * hook wgf_presentation_set installs (from its own file: a hook set here would be
 * inlined into the frame's start, linking this into every program). */
void wgf_gfx_priv_render_fill_visible(void)
{
    const clip_rect_t frame = frame_rect();
    sgl_begin_quads();
    sgl_c4b((uint8_t)wgf_color_get_red(clear_color), (uint8_t)wgf_color_get_green(clear_color),
            (uint8_t)wgf_color_get_blue(clear_color), (uint8_t)wgf_color_get_alpha(clear_color));
    sgl_v2f(frame.x, frame.y);
    sgl_v2f(frame.x + frame.width, frame.y);
    sgl_v2f(frame.x + frame.width, frame.y + frame.height);
    sgl_v2f(frame.x, frame.y + frame.height);
    sgl_end();
    apply_clip();
}

void wgf_gfx_priv_render_set_bars(void (*fill)(void))
{
    fill_bars = fill;
}

void wgf_gfx_priv_render_get_visible(float *x, float *y, float *width, float *height)
{
    const clip_rect_t frame = frame_rect();
    *x = frame.x;
    *y = frame.y;
    *width = frame.width;
    *height = frame.height;
}

int wgf_render_get_width(void)
{
    return frame_width;
}

int wgf_render_get_height(void)
{
    return frame_height;
}

float wgf_render_get_dpi_scale(void)
{
    return frame_dpi_scale;
}
