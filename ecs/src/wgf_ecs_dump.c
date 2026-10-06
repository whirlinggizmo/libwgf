#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_behavior.h"
#include "wgf_bounds.h"
#include "wgf_collider.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_ecs.h"
#include "wgf_ecs_priv.h"
#include "wgf_emitter2d.h"
#include "wgf_lifetime.h"
#include "wgf_motion.h"
#include "wgf_shape2d.h"
#include "wgf_sprite.h"
#include "wgf_text.h"
#include "wgf_voice.h"

/* The world written as a scene (wgf_ecs_dump): every live entity, oldest first, every
 * setting it has, in the format wgf_scene.h reads, numbers to 9 significant digits so
 * a float reads back as itself. */

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

static void dump_shape(out_t *out, wgf_node_t node)
{
    const wgf_vec2_t pivot = wgf_shape2d_get_pivot(node);
    put(out, "    shape2d");
    switch (wgf_shape2d_get_kind(node)) {
        case WGF_SHAPE2D_KIND_RECTANGLE: {
            const wgf_vec2_t size = wgf_shape2d_get_size(node);
            put(out, " rectangle=%.9g,%.9g", size.x, size.y);
            break;
        }
        case WGF_SHAPE2D_KIND_CIRCLE: put(out, " circle=%.9g", wgf_shape2d_get_radius(node)); break;
        case WGF_SHAPE2D_KIND_LINE: {
            const wgf_vec2_t a = wgf_shape2d_get_line_start(node), b = wgf_shape2d_get_line_end(node);
            put(out, " line=%.9g,%.9g,%.9g,%.9g", a.x, a.y, b.x, b.y);
            break;
        }
        case WGF_SHAPE2D_KIND_POLYGON: {
            float points[2048];
            const int n = wgf_shape2d_get_points(node, points, 2048);
            int i;
            put(out, " polygon=");
            for (i = 0; i < n; i++) put(out, i > 0 ? ",%.9g" : "%.9g", points[i]);
            break;
        }
        default: break;
    }
    put(out, " outline=%.9g", wgf_shape2d_get_outline(node));
    put_color(out, "color", wgf_shape2d_get_color(node));
    put(out, " pivot=%.9g,%.9g\n", pivot.x, pivot.y);
}

static void dump_sprite(out_t *out, wgf_node_t node)
{
    const wgf_vec4_t source = wgf_sprite_get_source(node);
    const wgf_vec2_t pivot = wgf_sprite_get_pivot(node);
    put(out, "    sprite");
    if (wgf_sprite_get_texture(node) != 0) put_text(out, "texture", path_of(wgf_sprite_get_texture(node)));
    put(out, " source=%.9g,%.9g,%.9g,%.9g", source.x, source.y, source.z, source.w);
    {
        const wgf_vec2_t size = wgf_sprite_get_size(node);
        put(out, " size=%.9g,%.9g", size.x, size.y);
    }
    put(out, " pivot=%.9g,%.9g", pivot.x, pivot.y);
    put_color(out, "tint", wgf_sprite_get_tint(node));
    put(out, "\n");
}

static void dump_text(out_t *out, wgf_node_t node)
{
    static const char *const across[] = {"left", "center", "right"}, *const down[] = {"top", "middle", "bottom"};
    put(out, "    text");
    put_text(out, "string", wgf_text_get_string(node));
    if (wgf_text_get_font(node) != 0) put_text(out, "font", path_of(wgf_text_get_font(node)));
    put(out, " size=%.9g", wgf_text_get_font_size(node));
    put_color(out, "color", wgf_text_get_color(node));
    put(out, " wrap=%.9g align=%s,%s\n", wgf_text_get_wrap_width(node), across[wgf_text_get_halign(node)],
        down[wgf_text_get_valign(node)]);
}

static void dump_emitter(out_t *out, wgf_node_t node)
{
    const wgf_vec2_t gravity = wgf_emitter2d_get_gravity(node);
    put(out, "    emitter2d rate=%.9g emitting=%s capacity=%d life=%.9g,%.9g direction=%.9g spread=%.9g",
        wgf_emitter2d_get_rate(node), wgf_emitter2d_is_emitting(node) ? "true" : "false",
        wgf_emitter2d_get_capacity(node), wgf_emitter2d_get_life_min(node), wgf_emitter2d_get_life_max(node),
        wgf_emitter2d_get_direction(node), wgf_emitter2d_get_spread(node));
    put(out, " speed=%.9g,%.9g radius=%.9g gravity=%.9g,%.9g drag=%.9g size=%.9g,%.9g",
        wgf_emitter2d_get_speed_min(node), wgf_emitter2d_get_speed_max(node), wgf_emitter2d_get_radius(node),
        gravity.x, gravity.y, wgf_emitter2d_get_drag(node), wgf_emitter2d_get_size_start(node),
        wgf_emitter2d_get_size_end(node));
    put(out, " color=#%08X,#%08X stretch=%.9g\n", (unsigned)wgf_emitter2d_get_color_start(node),
        (unsigned)wgf_emitter2d_get_color_end(node), wgf_emitter2d_get_stretch(node));
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

static void dump_entity(out_t *out, wgf_entity_t e)
{
    const wgf_vec3_t p = wgf_entity_get_position(e), r = wgf_entity_get_rotation(e), s = wgf_entity_get_scale(e);
    const char *name = wgf_entity_get_name(e);
    if (name[0] != '\0') {
        char quoted[2 * WGF_ECS_PRIV_NAME_MAX + 4];
        wgf_ecs_priv_quote(name, quoted, sizeof(quoted));
        put(out, "entity %s\n", quoted);
    } else {
        put(out, "entity\n");
    }
    put(out, "    transform position=%.9g,%.9g,%.9g rotation=%.9g,%.9g,%.9g scale=%.9g,%.9g,%.9g\n", p.x, p.y, p.z, r.x,
        r.y, r.z, s.x, s.y, s.z);
    if (wgf_entity_has_component(e, WGF_COMPONENT_MOTION)) {
        const wgf_vec3_t v = wgf_motion_get_velocity(e), w = wgf_motion_get_spin(e);
        put(out, "    motion velocity=%.9g,%.9g,%.9g spin=%.9g,%.9g,%.9g damping=%.9g max_speed=%.9g\n", v.x, v.y, v.z,
            w.x, w.y, w.z, wgf_motion_get_damping(e), wgf_motion_get_max_speed(e));
    }
    if (wgf_entity_has_component(e, WGF_COMPONENT_BOUNDS)) {
        static const char *const modes[] = {"wrap", "clamp", "destroy"};
        const wgf_vec4_t rect = wgf_bounds_get_rect(e);
        if (wgf_bounds_is_visible(e)) {
            put(out, "    bounds visible=true mode=%s margin=%.9g\n", modes[wgf_bounds_get_mode(e)],
                wgf_bounds_get_margin(e));
        } else {
            put(out, "    bounds rect=%.9g,%.9g,%.9g,%.9g mode=%s margin=%.9g\n", rect.x, rect.y, rect.z, rect.w,
                modes[wgf_bounds_get_mode(e)], wgf_bounds_get_margin(e));
        }
    }
    if (wgf_entity_has_component(e, WGF_COMPONENT_LIFETIME)) {
        put(out, "    lifetime seconds=%.9g\n", wgf_lifetime_get_seconds(e));
    }
    if (wgf_entity_has_component(e, WGF_COMPONENT_COLLIDER)) {
        put(out, "    collider radius=%.9g layer=%d mask=%d%s\n", wgf_collider_get_radius(e), wgf_collider_get_layer(e),
            wgf_collider_get_mask(e), wgf_collider_is_enabled(e) ? "" : " enabled=false");
    }
    if (wgf_entity_has_component(e, WGF_COMPONENT_BEHAVIOR)) {
        int i;
        put(out, "    behavior");
        put_text(out, "name", wgf_behavior_get_name(e));
        for (i = 0; i < wgf_behavior_get_param_count(e); i++) {
            const char *key = wgf_behavior_get_param_key(e, i);
            put_text(out, key, wgf_behavior_get_param(e, key));
        }
        put(out, "\n");
    }
    if (wgf_entity_has_component(e, WGF_COMPONENT_SHAPE2D)) {
        dump_shape(out, wgf_entity_get_component_node(e, WGF_COMPONENT_SHAPE2D));
    }
    if (wgf_entity_has_component(e, WGF_COMPONENT_SPRITE)) {
        dump_sprite(out, wgf_entity_get_component_node(e, WGF_COMPONENT_SPRITE));
    }
    if (wgf_entity_has_component(e, WGF_COMPONENT_TEXT)) {
        dump_text(out, wgf_entity_get_component_node(e, WGF_COMPONENT_TEXT));
    }
    if (wgf_entity_has_component(e, WGF_COMPONENT_EMITTER2D)) {
        dump_emitter(out, wgf_entity_get_component_node(e, WGF_COMPONENT_EMITTER2D));
    }
    if (wgf_entity_has_component(e, WGF_COMPONENT_VOICE)) dump_voice(out, wgf_entity_get_voice(e));
    put(out, "end\n");
}

const char *wgf_ecs_dump(void)
{
    out_t out = {NULL, 0, 0, false};
    wgf_entity_t *all;
    int count = 0, i;
    if (wgf_ecs_priv_world() == NULL) return "";
    out.capacity = 4096;
    out.text = (char *)malloc(out.capacity);
    if (out.text == NULL) return "";
    out.text[0] = '\0';
    put(&out, "wgf-scene 1\n");
    all = wgf_ecs_priv_entities(&count);
    for (i = 0; i < count; i++) dump_entity(&out, all[i]);
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
