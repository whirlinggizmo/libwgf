#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_behavior.h"
#include "wgf_bounds.h"
#include "wgf_collider.h"
#include "wgf_component.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_world.h"
#include "wgf_ecs_priv.h"
#include "wgf_emitter2d.h"
#include "wgf_lifetime.h"
#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_motion.h"
#include "wgf_shape2d.h"
#include "wgf_sprite.h"
#include "wgf_text.h"
#include "wgf_voice.h"

/* The simulated actors written as a scene (wgf_world_dump): each top one, oldest first, and
 * everything under it, every setting each has, in the format wgf_scene.h reads, numbers to
 * 9 significant digits so a float reads back as itself. */

#define PATH_BYTES 256

typedef struct out_t {
    char *text;
    size_t length, capacity;
    bool failed;
} out_t;

static char *dumped; /* the last dump, the caller's to read until the next */

static void put(out_t *out, const char *format, ...)
{
    va_list args;
    int n;
    if (out->failed) return;
    for (;;) {
        va_start(args, format);
        n = vsnprintf(out->text + out->length, out->capacity - out->length, format, args);
        va_end(args);
        if (n < 0) {
            out->failed = true;
            return;
        }
        if ((size_t)n < out->capacity - out->length) {
            out->length += (size_t)n;
            return;
        }
        {
            const size_t capacity = out->capacity * 2 + (size_t)n + 1;
            char *grown = (char *)realloc(out->text, capacity);
            if (grown == NULL) {
                out->failed = true;
                return;
            }
            out->text = grown;
            out->capacity = capacity;
        }
    }
}

void wgf_ecs_priv_quote(const char *text, char *out, size_t out_size)
{
    size_t n = 0;
    if (out_size < 3) {
        if (out_size > 0) out[0] = '\0';
        return;
    }
    out[n++] = '"';
    for (; *text != '\0' && n + 3 < out_size; text++) {
        if (*text == '"' || *text == '\\') out[n++] = '\\';
        out[n++] = *text;
    }
    out[n++] = '"';
    out[n] = '\0';
}

/* A resource's path as it was created from ("" for none), which a scene names it by. */
static const char *path_of(wgf_handle_t resource)
{
    const wgf_core_priv_resource_t *record = resource != 0 ? wgf_core_priv_resource_get(resource) : NULL;
    return record != NULL ? record->path : "";
}

static void put_text(out_t *out, const char *key, const char *text)
{
    char quoted[2 * WGF_ECS_PRIV_VALUE_MAX * 16 + 4];
    wgf_ecs_priv_quote(text, quoted, sizeof(quoted));
    put(out, " %s=%s", key, quoted);
}

static void put_color(out_t *out, const char *key, wgf_color_t color)
{
    put(out, " %s=#%08X", key, (unsigned)color);
}

static void dump_shape(out_t *out, wgf_actor_t actor)
{
    const wgf_vec2_t pivot = wgf_shape2d_get_pivot(actor);
    put(out, "    shape2d");
    switch (wgf_shape2d_get_kind(actor)) {
        case WGF_SHAPE2D_KIND_RECTANGLE: {
            const wgf_vec2_t size = wgf_shape2d_get_size(actor);
            put(out, " rectangle=%.9g,%.9g", size.x, size.y);
            break;
        }
        case WGF_SHAPE2D_KIND_CIRCLE: put(out, " circle=%.9g", wgf_shape2d_get_radius(actor)); break;
        case WGF_SHAPE2D_KIND_LINE: {
            const wgf_vec2_t a = wgf_shape2d_get_line_start(actor), b = wgf_shape2d_get_line_end(actor);
            put(out, " line=%.9g,%.9g,%.9g,%.9g", a.x, a.y, b.x, b.y);
            break;
        }
        case WGF_SHAPE2D_KIND_POLYGON: {
            float points[2048];
            const int n = wgf_shape2d_get_points(actor, points, 2048);
            int i;
            put(out, " polygon=");
            for (i = 0; i < n; i++) put(out, i > 0 ? ",%.9g" : "%.9g", points[i]);
            break;
        }
        default: break;
    }
    put(out, " outline=%.9g", wgf_shape2d_get_outline(actor));
    put_color(out, "color", wgf_shape2d_get_color(actor));
    put(out, " pivot=%.9g,%.9g\n", pivot.x, pivot.y);
}

static void dump_sprite(out_t *out, wgf_actor_t actor)
{
    const wgf_vec4_t source = wgf_sprite_get_source(actor);
    const wgf_vec2_t pivot = wgf_sprite_get_pivot(actor);
    put(out, "    sprite");
    if (wgf_sprite_get_texture(actor) != 0) put_text(out, "texture", path_of(wgf_sprite_get_texture(actor)));
    put(out, " source=%.9g,%.9g,%.9g,%.9g", source.x, source.y, source.z, source.w);
    {
        const wgf_vec2_t size = wgf_sprite_get_size(actor);
        put(out, " size=%.9g,%.9g", size.x, size.y);
    }
    put(out, " pivot=%.9g,%.9g", pivot.x, pivot.y);
    put_color(out, "tint", wgf_sprite_get_tint(actor));
    put(out, "\n");
}

static void dump_text(out_t *out, wgf_actor_t actor)
{
    static const char *const across[] = {"left", "center", "right"}, *const down[] = {"top", "middle", "bottom"};
    put(out, "    text");
    put_text(out, "string", wgf_text_get_string(actor));
    if (wgf_text_get_font(actor) != 0) put_text(out, "font", path_of(wgf_text_get_font(actor)));
    put(out, " size=%.9g", wgf_text_get_font_size(actor));
    put_color(out, "color", wgf_text_get_color(actor));
    put(out, " wrap=%.9g align=%s,%s\n", wgf_text_get_wrap_width(actor), across[wgf_text_get_halign(actor)],
        down[wgf_text_get_valign(actor)]);
}

static void dump_emitter(out_t *out, wgf_actor_t actor)
{
    const wgf_vec2_t gravity = wgf_emitter2d_get_gravity(actor);
    put(out, "    emitter2d rate=%.9g emitting=%s capacity=%d life=%.9g,%.9g direction=%.9g spread=%.9g",
        wgf_emitter2d_get_rate(actor), wgf_emitter2d_is_emitting(actor) ? "true" : "false",
        wgf_emitter2d_get_capacity(actor), wgf_emitter2d_get_life_min(actor), wgf_emitter2d_get_life_max(actor),
        wgf_emitter2d_get_direction(actor), wgf_emitter2d_get_spread(actor));
    put(out, " speed=%.9g,%.9g radius=%.9g gravity=%.9g,%.9g drag=%.9g size=%.9g,%.9g",
        wgf_emitter2d_get_speed_min(actor), wgf_emitter2d_get_speed_max(actor), wgf_emitter2d_get_radius(actor),
        gravity.x, gravity.y, wgf_emitter2d_get_drag(actor), wgf_emitter2d_get_size_start(actor),
        wgf_emitter2d_get_size_end(actor));
    put(out, " color=#%08X,#%08X stretch=%.9g\n", (unsigned)wgf_emitter2d_get_color_start(actor),
        (unsigned)wgf_emitter2d_get_color_end(actor), wgf_emitter2d_get_stretch(actor));
}

/* A model: its mesh as the generated shape it is, with its create call's parameters
 * (a mesh of no generated shape is left out), and its tint. */
static void dump_model(out_t *out, wgf_actor_t actor)
{
    const wgf_gfx_priv_model_hooks_t *hooks = wgf_gfx_priv_get_model_hooks(); /* set: there is a model */
    float params[4];
    int count, i;
    const char *shape = hooks->describe(actor, params, &count);
    put(out, "    model");
    if (shape != NULL) {
        put(out, " %s=", shape);
        for (i = 0; i < count; i++) put(out, i == 0 ? "%.9g" : ",%.9g", params[i]);
    }
    put_color(out, "tint", hooks->get_tint(actor));
    put(out, "\n");
}

static void dump_voice(out_t *out, wgf_voice_t voice)
{
    const wgf_sound_t sound = wgf_voice_get_sound(voice);
    put(out, "    voice");
    if (sound != 0) {
        put_text(out, "sound", path_of(sound));
        put(out, " streamed=%s",
            WGF_CORE_PRIV_HANDLE_KIND(sound) == WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND_STREAMED ? "true" : "false");
    }
    put(out, " volume=%.9g pitch=%.9g pan=%.9g loop=%s play=%s\n", wgf_voice_get_volume(voice),
        wgf_voice_get_pitch(voice), wgf_voice_get_pan(voice), wgf_voice_is_loop(voice) ? "true" : "false",
        wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING ? "true" : "false");
}

static void dump_actor(out_t *out, wgf_actor_t e, const char *path);

/* `e`'s lines, a level in from its block's: its kind's, then its transform, components,
 * and behaviors; then the actors under it, each a block a level further in. */
static void dump_lines(out_t *out, wgf_actor_t e, const char *pad)
{
    const wgf_vec3_t p = wgf_actor_get_position(e), r = wgf_actor_get_rotation(e), s = wgf_actor_get_scale(e);
    int i, b;
    switch (wgf_actor_get_kind(e)) { /* each writes its own line, from its own 4 spaces */
        case WGF_ACTOR_KIND_SHAPE2D: put(out, "%s", pad); dump_shape(out, e); break;
        case WGF_ACTOR_KIND_SPRITE: put(out, "%s", pad); dump_sprite(out, e); break;
        case WGF_ACTOR_KIND_TEXT: put(out, "%s", pad); dump_text(out, e); break;
        case WGF_ACTOR_KIND_EMITTER2D: put(out, "%s", pad); dump_emitter(out, e); break;
        case WGF_ACTOR_KIND_MODEL:
            if (wgf_gfx_priv_get_model_hooks() != NULL) {
                put(out, "%s", pad);
                dump_model(out, e);
            }
            break;
        default: break; /* a plain actor, or a kind scenes don't make: its transform and the rest alone */
    }
    put(out, "%s    transform position=%.9g,%.9g,%.9g rotation=%.9g,%.9g,%.9g scale=%.9g,%.9g,%.9g\n", pad, p.x, p.y,
        p.z, r.x, r.y, r.z, s.x, s.y, s.z);
    if (wgf_actor_has_component(e, WGF_COMPONENT_MOTION)) {
        const wgf_vec3_t v = wgf_motion_get_velocity(e), w = wgf_motion_get_spin(e);
        put(out, "%s    motion velocity=%.9g,%.9g,%.9g spin=%.9g,%.9g,%.9g damping=%.9g max_speed=%.9g\n", pad, v.x,
            v.y, v.z, w.x, w.y, w.z, wgf_motion_get_damping(e), wgf_motion_get_max_speed(e));
    }
    if (wgf_actor_has_component(e, WGF_COMPONENT_BOUNDS)) {
        static const char *const modes[] = {"wrap", "clamp", "destroy"};
        const wgf_vec4_t rect = wgf_bounds_get_rect(e);
        if (wgf_bounds_is_visible(e)) {
            put(out, "%s    bounds visible=true mode=%s margin=%.9g\n", pad, modes[wgf_bounds_get_mode(e)],
                wgf_bounds_get_margin(e));
        } else {
            put(out, "%s    bounds rect=%.9g,%.9g,%.9g,%.9g mode=%s margin=%.9g\n", pad, rect.x, rect.y, rect.z,
                rect.w, modes[wgf_bounds_get_mode(e)], wgf_bounds_get_margin(e));
        }
    }
    if (wgf_actor_has_component(e, WGF_COMPONENT_LIFETIME)) {
        put(out, "%s    lifetime seconds=%.9g\n", pad, wgf_lifetime_get_seconds(e));
    }
    if (wgf_actor_has_component(e, WGF_COMPONENT_COLLIDER)) {
        put(out, "%s    collider radius=%.9g layer=%d mask=%d%s\n", pad, wgf_collider_get_radius(e),
            wgf_collider_get_layer(e), wgf_collider_get_mask(e), wgf_collider_is_enabled(e) ? "" : " enabled=false");
    }
    if (wgf_actor_has_component(e, WGF_COMPONENT_VOICE)) {
        put(out, "%s", pad);
        dump_voice(out, wgf_actor_get_voice(e));
    }
    for (b = 0; b < wgf_actor_get_behavior_count(e); b++) {
        const int id = wgf_actor_get_behavior(e, b);
        put(out, "%s    behavior", pad);
        put_text(out, "name", wgf_behavior_get_name(e, id));
        for (i = 0; i < wgf_behavior_get_param_count(e, id); i++) {
            const char *key = wgf_behavior_get_param_key(e, id, i);
            put_text(out, key, wgf_behavior_get_param(e, id, key));
        }
        put(out, "\n");
    }
}

/* `e`'s block at `path` ("" for an unnamed top actor with nothing under it), then each
 * actor under it, a block of its own naming `e` as its parent by path: an unnamed one is
 * given the name `_<index>` among its siblings, so its block has a path. */
static void dump_actor(out_t *out, wgf_actor_t e, const char *path)
{
    int i;
    if (path[0] != '\0') {
        char quoted[2 * PATH_BYTES + 4];
        wgf_ecs_priv_quote(path, quoted, sizeof(quoted));
        put(out, "actor %s\n", quoted);
    } else {
        put(out, "actor\n");
    }
    dump_lines(out, e, "");
    put(out, "end\n");
    for (i = 0; i < wgf_actor_get_child_count(e); i++) {
        const wgf_actor_t child = wgf_actor_get_child(e, i);
        char below[PATH_BYTES];
        if (wgf_actor_get_name(child)[0] == '\0') {
            snprintf(below, sizeof(below), "_%d", i);
            wgf_actor_set_name(child, below);
        }
        snprintf(below, sizeof(below), "%s/%s", path, wgf_actor_get_name(child));
        dump_actor(out, child, below);
    }
}

/* Whether an actor above `actor` has components or behaviors: it is written with that one. */
static bool under_another(wgf_actor_t actor)
{
    wgf_actor_t up = wgf_actor_get_parent(actor);
    while (up != 0) {
        if (wgf_ecs_priv_record_of(up) != NULL) return true;
        up = wgf_actor_get_parent(up);
    }
    return false;
}

const char *wgf_world_dump(void)
{
    out_t out = {NULL, 0, 0, false};
    wgf_actor_t *all;
    int count = 0, i;
    if (!wgf_ecs_priv_started()) return "";
    out.capacity = 4096;
    out.text = (char *)malloc(out.capacity);
    if (out.text == NULL) return "";
    out.text[0] = '\0';
    put(&out, "wgf-scene 2\n");
    all = wgf_ecs_priv_actors(&count);
    for (i = 0; i < count; i++) {
        char path[PATH_BYTES];
        if (under_another(all[i])) continue;
        if (wgf_actor_get_name(all[i])[0] == '\0' && wgf_actor_get_child_count(all[i]) > 0) {
            snprintf(path, sizeof(path), "_%d", i); /* its parts need a path to name it by */
            wgf_actor_set_name(all[i], path);
        }
        snprintf(path, sizeof(path), "%s", wgf_actor_get_name(all[i]));
        dump_actor(&out, all[i], path);
    }
    free(all);
    if (out.failed) {
        free(out.text);
        return "";
    }
    free(dumped);
    dumped = out.text;
    return dumped;
}

void wgf_ecs_priv_dump_shutdown(void)
{
    free(dumped);
    dumped = NULL;
}
