#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "emitter/wgf_gfx_emitter_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_resource.h"

/* An emitter's scene line (BUILDING.md, "Scene files": emitter2d, emitter3d): its keys
 * applied and its line written, on the emitter core's own record, reached by the scene
 * through the hooks this file sets as it is linked (wgf_gfx_actor_priv.h), so a program
 * loading scenes links no particles unless it makes an emitter. The rules are the public
 * calls' (wgf_emitter2d.h): a value they would refuse changes nothing. */

static wgf_gfx_priv_emitter_t *emitter_of(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL ||
        (actor_ptr->type != WGF_ACTOR_KIND_EMITTER2D && actor_ptr->type != WGF_ACTOR_KIND_EMITTER3D)) {
        return NULL;
    }
    return actor_ptr->as.emitter;
}

static float at(const double *n, int count, int i)
{
    return i < count ? (float)n[i] : 0.0f;
}

static void apply(wgf_actor_t actor, const char *key, const double *n, int count, const wgf_color_t *colors,
                  int color_count, bool truth, const char *text)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(actor);
    const int d = e != NULL && e->two_d ? 2 : 3; /* a vector's numbers */
    int i;
    if (e == NULL) return;
    if (strcmp(key, "texture") == 0) {
        const wgf_texture_t texture = text[0] != '\0' ? wgf_texture_create(text) : 0;
        wgf_gfx_priv_emitter_set_texture(e, texture);
        if (texture != 0) wgf_resource_release(texture); /* the emitter holds its own */
    } else if (strcmp(key, "source") == 0) {
        const bool whole = !(n[2] > 0.0) || !(n[3] > 0.0);
        for (i = 0; i < 4; i++) e->source[i] = whole ? 0.0f : (float)n[i];
    } else if (strcmp(key, "frames") == 0) {
        wgf_gfx_priv_emitter_set_frames(e, (int)n[0], (int)n[1], (int)n[2], (float)n[3]);
    } else if (strcmp(key, "rate") == 0) {
        if (n[0] >= 0.0) e->rate = (float)n[0];
    } else if (strcmp(key, "emitting") == 0) {
        e->emitting = truth;
    } else if (strcmp(key, "max") == 0) {
        wgf_gfx_priv_emitter_set_max(e, (int)n[0]);
    } else if (strcmp(key, "life") == 0) {
        wgf_gfx_priv_emitter_set_life(e, (float)n[0], (float)n[1]);
    } else if (strcmp(key, "spawn_box") == 0) {
        if (n[0] >= 0.0 && n[1] >= 0.0 && at(n, count, 2) >= 0.0f) {
            e->box = wgf_vec3_make((float)n[0], (float)n[1], d == 3 ? at(n, count, 2) : 0.0f);
            e->sphere = 0.0f;
        }
    } else if (strcmp(key, "spawn_circle") == 0 || strcmp(key, "spawn_sphere") == 0) {
        if (n[0] >= 0.0) {
            e->sphere = (float)n[0];
            e->box = wgf_vec3_make(0, 0, 0);
        }
    } else if (strcmp(key, "velocity") == 0) {
        e->velocity = wgf_vec3_make((float)n[0], (float)n[1], d == 3 ? (float)n[2] : 0.0f);
        e->spread = fmaxf(at(n, count, d), 0.0f);
        e->speed_variance = fminf(fmaxf(at(n, count, d + 1), 0.0f), 1.0f);
    } else if (strcmp(key, "gravity") == 0) {
        e->gravity = wgf_vec3_make((float)n[0], (float)n[1], d == 3 ? (float)n[2] : 0.0f);
    } else if (strcmp(key, "drag") == 0) {
        if (n[0] >= 0.0) e->drag = (float)n[0];
    } else if (strcmp(key, "inherit") == 0) {
        e->inherit = (float)n[0];
    } else if (strcmp(key, "stretch") == 0) {
        if (n[0] >= 0.0) e->stretch = (float)n[0];
    } else if (strcmp(key, "size") == 0) {
        wgf_gfx_priv_emitter_set_size(e, (float)n[0], (float)n[1], at(n, count, 2));
    } else if (strcmp(key, "color") == 0) {
        wgf_gfx_priv_emitter_set_color(e, colors[0], colors[1]);
    } else if (strcmp(key, "palette") == 0) {
        e->palette_count = 0;
        for (i = 0; i < color_count; i++) wgf_gfx_priv_emitter_add_palette_color(e, colors[i]);
    } else if (strcmp(key, "spin") == 0) {
        e->spin_min = (float)n[0];
        e->spin_max = (float)n[1];
    } else if (strcmp(key, "alpha") == 0) {
        wgf_gfx_priv_emitter_set_alpha_mode(e, (wgf_alpha_mode_t)(int)n[0], e->alpha_cutoff);
    } else if (strcmp(key, "cutoff") == 0) {
        wgf_gfx_priv_emitter_set_alpha_mode(e, e->alpha_mode, (float)n[0]);
    } else if (strcmp(key, "seed") == 0) {
        wgf_gfx_priv_emitter_set_seed(e, (uint32_t)(int)n[0]);
    } else if (strcmp(key, "ending") == 0) {
        e->ending = truth;
    } else if (strcmp(key, "lingering") == 0) {
        e->lingering = truth;
    }
}

static void placed(wgf_actor_t actor, float prewarm, int burst)
{
    wgf_gfx_priv_emitter_t *e = emitter_of(actor) != NULL ? wgf_gfx_priv_emitter_place(actor) : NULL;
    if (e == NULL) return;
    if (prewarm > 0.0f) wgf_gfx_priv_emitter_prewarm(e, prewarm);
    if (burst > 0) wgf_gfx_priv_emitter_burst(e, burst);
}

/* A line's text, built in turn into one buffer. */
static struct {
    char text[2048];
    size_t length;
} out;

static void put(const char *format, ...)
{
    va_list args;
    int wrote;
    if (out.length >= sizeof(out.text)) return;
    va_start(args, format);
    wrote = vsnprintf(out.text + out.length, sizeof(out.text) - out.length, format, args);
    va_end(args);
    if (wrote > 0) out.length += (size_t)wrote;
}

/* The line as a scene writes it: its curves by their first and last keys (more keys are
 * code's), and its palette. */
static const char *line(wgf_actor_t actor)
{
    static const char *const alpha[] = {"opaque", "mask", "blend", "add"};
    const wgf_gfx_priv_emitter_t *e = emitter_of(actor);
    int i;
    out.length = 0;
    out.text[0] = '\0';
    if (e == NULL) return out.text;
    put(e->two_d ? "emitter2d" : "emitter3d");
    if (e->texture != 0) put(" texture=\"%s\"", wgf_resource_get_path(e->texture));
    if (e->source[2] > 0.0f) put(" source=%.9g,%.9g,%.9g,%.9g", e->source[0], e->source[1], e->source[2], e->source[3]);
    put(" frames=%d,%d,%d,%.9g rate=%.9g emitting=%s max=%d life=%.9g,%.9g", e->columns, e->rows, e->frames,
        e->frames_per_second, e->rate, e->emitting ? "true" : "false", e->max, e->life_min, e->life_max);
    if (e->two_d) {
        put(" spawn_box=%.9g,%.9g spawn_circle=%.9g velocity=%.9g,%.9g,%.9g,%.9g gravity=%.9g,%.9g", e->box.x, e->box.y,
            e->sphere, e->velocity.x, e->velocity.y, e->spread, e->speed_variance, e->gravity.x, e->gravity.y);
    } else {
        put(" spawn_box=%.9g,%.9g,%.9g spawn_sphere=%.9g velocity=%.9g,%.9g,%.9g,%.9g,%.9g gravity=%.9g,%.9g,%.9g",
            e->box.x, e->box.y, e->box.z, e->sphere, e->velocity.x, e->velocity.y, e->velocity.z, e->spread,
            e->speed_variance, e->gravity.x, e->gravity.y, e->gravity.z);
    }
    put(" drag=%.9g inherit=%.9g stretch=%.9g", e->drag, e->inherit, e->stretch);
    if (e->size_keys > 0) put(" size=%.9g,%.9g,%.9g", e->size_values[0], e->size_values[e->size_keys - 1], e->size_variance);
    if (e->color_keys > 0) {
        put(" color=#%08X,#%08X", (unsigned)e->color_values[0], (unsigned)e->color_values[e->color_keys - 1]);
    }
    for (i = 0; i < e->palette_count; i++) put(i == 0 ? " palette=#%08X" : ",#%08X", (unsigned)e->palette[i]);
    put(" spin=%.9g,%.9g alpha=%s cutoff=%.9g seed=%d ending=%s lingering=%s", e->spin_min, e->spin_max,
        alpha[e->alpha_mode], e->alpha_cutoff, (int)e->seed, e->ending ? "true" : "false",
        e->lingering ? "true" : "false");
    return out.text;
}

const wgf_gfx_priv_emitter_scene_t wgf_gfx_priv_emitter_scene_hooks = {wgf_gfx_priv_emitter_create, apply, placed,
                                                                        line};
