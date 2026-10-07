#include "wgf_light.h"

#include <math.h>
#include <stddef.h>

#include "material/wgf_gfx_material_priv.h"
#include "stage/wgf_gfx_stage_priv.h"

/* Lights: nodes that light their stage, and what a stage's draw makes of them. The
 * selection, attenuation, and cone are libwgt's (wgrender's wgr_light). */

#define PI 3.14159265358979323846f

static wgf_gfx_priv_node_t *light_of(wgf_node_t light)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(light);
    return node_ptr != NULL && node_ptr->type == WGF_NODE_TYPE_LIGHT ? node_ptr : NULL;
}

wgf_node_t wgf_light_create(wgf_light_type_t type)
{
    wgf_node_t light;
    wgf_gfx_priv_node_t *node_ptr;
    if ((int)type < WGF_LIGHT_TYPE_DIRECTIONAL || type > WGF_LIGHT_TYPE_SPOT) return 0;
    light = wgf_gfx_priv_node_create(WGF_NODE_TYPE_LIGHT);
    node_ptr = wgf_gfx_priv_node_of(light);
    if (node_ptr == NULL) return 0;
    node_ptr->as.light.type = (int)type;
    node_ptr->as.light.color = 0xFFFFFFFFu;
    node_ptr->as.light.intensity = 1.0f;
    node_ptr->as.light.inner_angle = PI / 6.0f; /* wgrender's default cone */
    node_ptr->as.light.outer_angle = PI / 4.0f;
    return light;
}

wgf_light_type_t wgf_light_get_type(wgf_node_t light)
{
    const wgf_gfx_priv_node_t *node_ptr = light_of(light);
    return node_ptr != NULL ? (wgf_light_type_t)node_ptr->as.light.type : WGF_LIGHT_TYPE_DIRECTIONAL;
}

bool wgf_light_set_color(wgf_node_t light, wgf_color_t color)
{
    wgf_gfx_priv_node_t *node_ptr = light_of(light);
    if (node_ptr == NULL) return false;
    node_ptr->as.light.color = color;
    return true;
}

wgf_color_t wgf_light_get_color(wgf_node_t light)
{
    const wgf_gfx_priv_node_t *node_ptr = light_of(light);
    return node_ptr != NULL ? node_ptr->as.light.color : 0;
}

bool wgf_light_set_intensity(wgf_node_t light, float intensity)
{
    wgf_gfx_priv_node_t *node_ptr = light_of(light);
    if (node_ptr == NULL) return false;
    node_ptr->as.light.intensity = intensity > 0.0f ? intensity : 0.0f;
    return true;
}

float wgf_light_get_intensity(wgf_node_t light)
{
    const wgf_gfx_priv_node_t *node_ptr = light_of(light);
    return node_ptr != NULL ? node_ptr->as.light.intensity : 0.0f;
}

bool wgf_light_set_range(wgf_node_t light, float range)
{
    wgf_gfx_priv_node_t *node_ptr = light_of(light);
    if (node_ptr == NULL) return false;
    node_ptr->as.light.range = range > 0.0f ? range : 0.0f;
    return true;
}

float wgf_light_get_range(wgf_node_t light)
{
    const wgf_gfx_priv_node_t *node_ptr = light_of(light);
    return node_ptr != NULL ? node_ptr->as.light.range : 0.0f;
}

bool wgf_light_set_spot_cone(wgf_node_t light, float inner, float outer)
{
    wgf_gfx_priv_node_t *node_ptr = light_of(light);
    if (node_ptr == NULL) return false;
    outer = outer < 0.0f ? 0.0f : (outer > PI * 0.5f ? PI * 0.5f : outer);
    inner = inner < 0.0f ? 0.0f : (inner > outer ? outer : inner);
    node_ptr->as.light.inner_angle = inner;
    node_ptr->as.light.outer_angle = outer;
    return true;
}

float wgf_light_get_spot_inner_angle(wgf_node_t light)
{
    const wgf_gfx_priv_node_t *node_ptr = light_of(light);
    return node_ptr != NULL ? node_ptr->as.light.inner_angle : 0.0f;
}

float wgf_light_get_spot_outer_angle(wgf_node_t light)
{
    const wgf_gfx_priv_node_t *node_ptr = light_of(light);
    return node_ptr != NULL ? node_ptr->as.light.outer_angle : 0.0f;
}

/* -------------------------------------------------------------- shading ---- */

void wgf_gfx_priv_light_resolve(const wgf_gfx_priv_node_t *light_ptr, wgf_mat4_t world,
                                wgf_gfx_priv_stage_light_t *out)
{
    const wgf_color_t color = light_ptr->as.light.color;
    const float intensity = light_ptr->as.light.intensity;
    out->type = light_ptr->as.light.type;
    out->radiance = wgf_vec3_make(wgf_gfx_priv_srgb_to_linear((float)wgf_color_get_red(color) / 255.0f) * intensity,
                                  wgf_gfx_priv_srgb_to_linear((float)wgf_color_get_green(color) / 255.0f) * intensity,
                                  wgf_gfx_priv_srgb_to_linear((float)wgf_color_get_blue(color) / 255.0f) * intensity);
    out->position = wgf_mat4_get_translation(world);
    out->direction = wgf_vec3_normalize(wgf_mat4_transform_direction(world, wgf_vec3_make(0.0f, 0.0f, -1.0f)));
    out->range = light_ptr->as.light.range;
    out->cos_inner = cosf(light_ptr->as.light.inner_angle);
    out->cos_outer = cosf(light_ptr->as.light.outer_angle);
}

static float attenuation(float distance, float range)
{
    const float inverse_square = 1.0f / (distance * distance > 0.01f ? distance * distance : 0.01f);
    float ratio, window;
    if (range <= 0.0f) return inverse_square;
    /* KHR_lights_punctual: a smooth window to nothing at the range */
    ratio = distance / range;
    window = 1.0f - ratio * ratio * ratio * ratio;
    window = window < 0.0f ? 0.0f : (window > 1.0f ? 1.0f : window);
    return window * window * inverse_square;
}

static float spot_factor(float cos_angle, float cos_inner, float cos_outer)
{
    float t;
    if (cos_inner - cos_outer <= 1e-6f) return cos_angle >= cos_outer ? 1.0f : 0.0f;
    t = (cos_angle - cos_outer) / (cos_inner - cos_outer);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t); /* smoothstep */
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* What `light` is estimated to give a model in the box [bmin, bmax]; 0: it can't reach it. */
static float score_light(const wgf_gfx_priv_stage_light_t *light, wgf_vec3_t bmin, wgf_vec3_t bmax)
{
    float score = 0.2126f * light->radiance.x + 0.7152f * light->radiance.y + 0.0722f * light->radiance.z;
    wgf_vec3_t nearest, center, to_center;
    float distance, center_distance;
    if (score <= 0.0f || light->type == WGF_LIGHT_TYPE_DIRECTIONAL) return score;
    nearest = wgf_vec3_make(clampf(light->position.x, bmin.x, bmax.x), clampf(light->position.y, bmin.y, bmax.y),
                            clampf(light->position.z, bmin.z, bmax.z));
    distance = wgf_vec3_length(wgf_vec3_sub(nearest, light->position));
    if (light->range > 0.0f && distance >= light->range) return 0.0f;
    score *= attenuation(distance, light->range);
    if (light->type == WGF_LIGHT_TYPE_SPOT && distance > 0.0f) {
        center = wgf_vec3_scale(wgf_vec3_add(bmin, bmax), 0.5f);
        to_center = wgf_vec3_sub(center, light->position);
        center_distance = wgf_vec3_length(to_center);
        if (center_distance > 1e-6f) {
            score *= spot_factor(wgf_vec3_dot(to_center, light->direction) / center_distance, light->cos_inner,
                                 light->cos_outer);
        }
    }
    return score;
}

int wgf_gfx_priv_light_select(const wgf_gfx_priv_stage_light_t *lights, int count, wgf_vec3_t world_min,
                              wgf_vec3_t world_max, int *out_indices, int max_out)
{
    float scores[WGF_GFX_PRIV_MAX_STAGE_LIGHTS];
    int chosen = 0, i, j;
    if (max_out > WGF_GFX_PRIV_MAX_STAGE_LIGHTS) max_out = WGF_GFX_PRIV_MAX_STAGE_LIGHTS;
    for (i = 0; i < count && i < WGF_GFX_PRIV_MAX_STAGE_LIGHTS; i++) {
        const float score = score_light(&lights[i], world_min, world_max);
        int at = chosen;
        if (score <= 0.0f) continue;
        /* into a list highest first; equal scores keep the lights' order */
        while (at > 0 && scores[at - 1] < score) at--;
        if (at >= max_out) continue;
        if (chosen < max_out) chosen++;
        for (j = chosen - 1; j > at; j--) {
            scores[j] = scores[j - 1];
            out_indices[j] = out_indices[j - 1];
        }
        scores[at] = score;
        out_indices[at] = i;
    }
    return chosen;
}
