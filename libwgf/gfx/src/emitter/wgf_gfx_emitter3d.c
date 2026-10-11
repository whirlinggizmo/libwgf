#include "wgf_emitter3d.h"

#include <math.h>
#include <stddef.h>

#include "emitter/wgf_gfx_emitter_priv.h"
#include "wgf_core_handle_priv.h"

/* 3D emitters: the public calls over the emitter core (wgf_gfx_emitter.c), libwgt's
 * wgt_gfx_emitter3d.c, its third axis its own. Each is false, 0, or nothing for a
 * handle that isn't a 3D emitter. */

#define EMITTER(actor) wgf_gfx_priv_emitter_of_at((actor), false, WGF_CORE_PRIV_CALLER)

wgf_actor_t wgf_emitter3d_create(void)
{
    return wgf_gfx_priv_emitter_create(false);
}

bool wgf_emitter3d_set_texture(wgf_actor_t emitter, wgf_texture_t texture)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL && wgf_gfx_priv_emitter_set_texture(e, texture);
}

wgf_texture_t wgf_emitter3d_get_texture(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->texture : 0;
}

bool wgf_emitter3d_set_source(wgf_actor_t emitter, float x, float y, float width, float height)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    if (!(width > 0.0f) || !(height > 0.0f)) x = y = width = height = 0.0f; /* the whole texture */
    e->source[0] = x;
    e->source[1] = y;
    e->source[2] = width;
    e->source[3] = height;
    return true;
}

wgf_vec4_t wgf_emitter3d_get_source(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return wgf_vec4_make(0, 0, 0, 0);
    return wgf_vec4_make(e->source[0], e->source[1], e->source[2], e->source[3]);
}

bool wgf_emitter3d_set_frames(wgf_actor_t emitter, int columns, int rows, int count, float per_second)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL && wgf_gfx_priv_emitter_set_frames(e, columns, rows, count, per_second);
}

int wgf_emitter3d_get_frame_columns(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->columns : 0;
}

int wgf_emitter3d_get_frame_rows(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->rows : 0;
}

int wgf_emitter3d_get_frame_count(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->frames : 0;
}

float wgf_emitter3d_get_frame_rate(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->frames_per_second : 0.0f;
}

bool wgf_emitter3d_skip_trail(wgf_actor_t emitter)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    e->skip_trail = true;
    return true;
}

bool wgf_emitter3d_set_rate(wgf_actor_t emitter, float per_second)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL || !(per_second >= 0.0f)) return false;
    e->rate = per_second;
    return true;
}

float wgf_emitter3d_get_rate(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->rate : 0.0f;
}

bool wgf_emitter3d_burst(wgf_actor_t emitter, int count)
{
    return EMITTER(emitter) != NULL && wgf_gfx_priv_emitter_burst(wgf_gfx_priv_emitter_place(emitter), count);
}

bool wgf_emitter3d_set_emitting(wgf_actor_t emitter, bool emitting)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    e->emitting = emitting;
    return true;
}

bool wgf_emitter3d_is_emitting(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->emitting : false;
}

bool wgf_emitter3d_set_max(wgf_actor_t emitter, int count)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL && wgf_gfx_priv_emitter_set_max(e, count);
}

int wgf_emitter3d_get_max(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->max : 0;
}

bool wgf_emitter3d_set_life(wgf_actor_t emitter, float min_seconds, float max_seconds)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL && wgf_gfx_priv_emitter_set_life(e, min_seconds, max_seconds);
}

float wgf_emitter3d_get_life_min(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->life_min : 0.0f;
}

float wgf_emitter3d_get_life_max(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->life_max : 0.0f;
}

bool wgf_emitter3d_prewarm(wgf_actor_t emitter, float seconds)
{
    return EMITTER(emitter) != NULL && wgf_gfx_priv_emitter_prewarm(wgf_gfx_priv_emitter_place(emitter), seconds);
}

bool wgf_emitter3d_set_spawn_box(wgf_actor_t emitter, float half_x, float half_y, float half_z)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL || !(half_x >= 0.0f) || !(half_y >= 0.0f) || !(half_z >= 0.0f)) return false;
    e->box = wgf_vec3_make(half_x, half_y, half_z);
    e->sphere = 0.0f;
    return true;
}

wgf_vec3_t wgf_emitter3d_get_spawn_box(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->box : wgf_vec3_make(0, 0, 0);
}

bool wgf_emitter3d_set_spawn_sphere(wgf_actor_t emitter, float radius)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL || !(radius >= 0.0f)) return false;
    e->sphere = radius;
    e->box = wgf_vec3_make(0, 0, 0);
    return true;
}

float wgf_emitter3d_get_spawn_sphere(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->sphere : 0.0f;
}

bool wgf_emitter3d_set_velocity(wgf_actor_t emitter, float x, float y, float z, float spread, float speed_variance)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    e->velocity = wgf_vec3_make(x, y, z);
    e->spread = fmaxf(spread, 0.0f);
    e->speed_variance = fminf(fmaxf(speed_variance, 0.0f), 1.0f);
    return true;
}

wgf_vec3_t wgf_emitter3d_get_velocity(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->velocity : wgf_vec3_make(0, 0, 0);
}

float wgf_emitter3d_get_spread(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->spread : 0.0f;
}

float wgf_emitter3d_get_speed_variance(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->speed_variance : 0.0f;
}

bool wgf_emitter3d_set_gravity(wgf_actor_t emitter, float x, float y, float z)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    e->gravity = wgf_vec3_make(x, y, z);
    return true;
}

wgf_vec3_t wgf_emitter3d_get_gravity(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->gravity : wgf_vec3_make(0, 0, 0);
}

bool wgf_emitter3d_set_drag(wgf_actor_t emitter, float per_second)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL || !(per_second >= 0.0f)) return false;
    e->drag = per_second;
    return true;
}

float wgf_emitter3d_get_drag(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->drag : 0.0f;
}

bool wgf_emitter3d_set_inherit_velocity(wgf_actor_t emitter, float fraction)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    e->inherit = fraction;
    return true;
}

float wgf_emitter3d_get_inherit_velocity(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->inherit : 0.0f;
}

bool wgf_emitter3d_set_stretch(wgf_actor_t emitter, float seconds)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL || !(seconds >= 0.0f)) return false;
    e->stretch = seconds;
    return true;
}

float wgf_emitter3d_get_stretch(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->stretch : 0.0f;
}

bool wgf_emitter3d_set_size(wgf_actor_t emitter, float start, float end, float variance)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL && wgf_gfx_priv_emitter_set_size(e, start, end, variance);
}

float wgf_emitter3d_get_size_variance(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->size_variance : 0.0f;
}

bool wgf_emitter3d_add_size_key(wgf_actor_t emitter, float t, float size)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL && wgf_gfx_priv_emitter_add_size_key(e, t, size);
}

bool wgf_emitter3d_clear_size_keys(wgf_actor_t emitter)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    e->size_keys = 0;
    return true;
}

int wgf_emitter3d_get_size_key_count(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->size_keys : 0;
}

float wgf_emitter3d_get_size_key_time(wgf_actor_t emitter, int index)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? (index >= 0 && index < e->size_keys ? e->size_times[index] : 0.0f) : 0.0f;
}

float wgf_emitter3d_get_size_key_value(wgf_actor_t emitter, int index)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? (index >= 0 && index < e->size_keys ? e->size_values[index] : 0.0f) : 0.0f;
}

bool wgf_emitter3d_set_color(wgf_actor_t emitter, wgf_color_t start, wgf_color_t end)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    wgf_gfx_priv_emitter_set_color(e, start, end);
    return true;
}

bool wgf_emitter3d_add_color_key(wgf_actor_t emitter, float t, wgf_color_t color)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL && wgf_gfx_priv_emitter_add_color_key(e, t, color);
}

bool wgf_emitter3d_clear_color_keys(wgf_actor_t emitter)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    e->color_keys = 0;
    return true;
}

int wgf_emitter3d_get_color_key_count(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->color_keys : 0;
}

float wgf_emitter3d_get_color_key_time(wgf_actor_t emitter, int index)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? (index >= 0 && index < e->color_keys ? e->color_times[index] : 0.0f) : 0.0f;
}

wgf_color_t wgf_emitter3d_get_color_key_value(wgf_actor_t emitter, int index)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? (index >= 0 && index < e->color_keys ? e->color_values[index] : 0) : 0;
}

bool wgf_emitter3d_add_palette_color(wgf_actor_t emitter, wgf_color_t color)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL && wgf_gfx_priv_emitter_add_palette_color(e, color);
}

bool wgf_emitter3d_clear_palette(wgf_actor_t emitter)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    e->palette_count = 0;
    return true;
}

int wgf_emitter3d_get_palette_count(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->palette_count : 0;
}

wgf_color_t wgf_emitter3d_get_palette_color(wgf_actor_t emitter, int index)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? (index >= 0 && index < e->palette_count ? e->palette[index] : 0) : 0;
}

bool wgf_emitter3d_set_spin(wgf_actor_t emitter, float min, float max)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    e->spin_min = min;
    e->spin_max = max;
    return true;
}

float wgf_emitter3d_get_spin_min(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->spin_min : 0.0f;
}

float wgf_emitter3d_get_spin_max(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->spin_max : 0.0f;
}

bool wgf_emitter3d_set_alpha_mode(wgf_actor_t emitter, wgf_alpha_mode_t mode, float cutoff)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL && wgf_gfx_priv_emitter_set_alpha_mode(e, mode, cutoff);
}

wgf_alpha_mode_t wgf_emitter3d_get_alpha_mode(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->alpha_mode : WGF_ALPHA_MODE_OPAQUE;
}

float wgf_emitter3d_get_alpha_cutoff(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->alpha_cutoff : 0.0f;
}

bool wgf_emitter3d_set_seed(wgf_actor_t emitter, int seed)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    wgf_gfx_priv_emitter_set_seed(e, (uint32_t)seed);
    return true;
}

int wgf_emitter3d_get_seed(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? (int)e->seed : 0;
}

bool wgf_emitter3d_set_ending(wgf_actor_t emitter, bool ending)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    e->ending = ending;
    return true;
}

bool wgf_emitter3d_is_ending(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->ending : false;
}

bool wgf_emitter3d_set_lingering(wgf_actor_t emitter, bool lingering)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    e->lingering = lingering;
    return true;
}

bool wgf_emitter3d_is_lingering(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? e->lingering : false;
}

int wgf_emitter3d_get_count(wgf_actor_t emitter)
{
    const wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    return e != NULL ? wgf_gfx_priv_emitter_count_alive(e) : 0;
}

bool wgf_emitter3d_clear(wgf_actor_t emitter)
{
    wgf_gfx_priv_emitter_t *e = EMITTER(emitter);
    if (e == NULL) return false;
    wgf_gfx_priv_emitter_clear(e);
    return true;
}
