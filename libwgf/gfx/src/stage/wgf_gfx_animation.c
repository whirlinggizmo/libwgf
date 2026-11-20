#include "wgf_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "mesh/wgf_gfx_mesh_record_priv.h"
#include "stage/wgf_gfx_model_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_log.h"
#include "wgf_quat.h"
#include "wgf_resource.h"

/* A glTF file's animations, played on its root (wgf_model.h): libwgt's player
 * (wgt_gfx_animation.c), its clip played as a voice plays a sound, advanced by a part once a
 * frame and written into the tree's own actors, so a joint is an actor that moves (and what
 * hangs under it moves with it), and the skin's joints place the skinned parts when they are
 * drawn (wgf_gfx_stage3d.c). Redesigned from libwgt's: a clip is chosen as it is played, a
 * crossfade blends the clip faded from (still moving, or the pose it held) into the new one
 * over seconds, every animated node starts each pose from the file's rest so what one clip
 * moves and the next doesn't goes back, and a clip moved on is posed at once in the part's
 * update rather than when it is needed (libwgt's lazy pose and its clip boxes: the robots
 * benchmark draws every robot it moves). Linked by the first animation call alone, which
 * installs the part. */

typedef struct trs_t {
    wgf_vec3_t position;
    wgf_quat_t rotation;
    wgf_vec3_t scale;
} trs_t;

struct wgf_gfx_priv_animation_t {
    int animation; /* playing (or stopped at, paused in): into the file's; -1 none */
    int state;     /* wgf_play_state_t */
    float time;    /* seconds into it */
    float speed;
    bool loop;
    bool fresh; /* played, not moved since: its start the speed says, once the file is there */
    /* a crossfade: from `from` at `from_time` (moving on), or from the pose `held` */
    bool fading, from_held;
    int from;
    float from_time, fade, faded;
    trs_t *pose, *held, *scratch; /* a node at a time; the pose last written */
    int node_count;
    bool listed, posed;
    /* a layer over it: a clip on a bone and what is under it (`mask`, a node at a time), its
       weight moving to its target over `layer_fade` seconds; a clip played once fades out as it
       ends */
    int layer;    /* -1 none */
    int layer_state;
    float layer_time, layer_weight, layer_target, layer_fade;
    bool layer_loop;
    char layer_bone[64]; /* its mask's bone, by name: made again on another file's tree */
    unsigned char *mask;
    trs_t *layered;
    /* clips from another file (wgf_model_set_animations): its channels applied to this file's
       nodes of the same names (`map`, a source node at a time; -1 none), and the nodes they move */
    wgf_mesh_t source;
    int *map, map_count;
    unsigned char *moved;
};
typedef struct wgf_gfx_priv_animation_t animation_t;

/* The models with a player PLAYING, advanced each update; one leaves when it stops playing
 * or is destroyed. */
static struct {
    wgf_actor_t *models;
    int count, capacity;
} playing;

static bool installed;

/* ------------------------------------------------------------- the model ---- */

/* `model`'s record when it is a file's root; NULL for anything else. */
static wgf_gfx_priv_actor_t *root_of_at(wgf_actor_t model, const char *caller)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(model, caller);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_MODEL && actor_ptr->as.model.file_root ? actor_ptr
                                                                                                         : NULL;
}
#define root_of(...) root_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

/* The file's record, READY; NULL while it loads, or FAILED. */
static const wgf_gfx_priv_mesh_record_t *file_of(const wgf_gfx_priv_actor_t *root_ptr)
{
    return wgf_resource_get_status(root_ptr->as.model.mesh) == WGF_RESOURCE_STATUS_READY
               ? wgf_gfx_priv_mesh_record(root_ptr->as.model.mesh)
               : NULL;
}

static const wgf_gfx_priv_mesh_animation_t *clip_of(const wgf_gfx_priv_mesh_record_t *record, int animation)
{
    return record != NULL && animation >= 0 && animation < record->animation_count ? &record->animations[animation]
                                                                                    : NULL;
}

/* Where its clips come from: another file's (READY; NULL while it loads), or its own. */
static const wgf_gfx_priv_mesh_record_t *clips_of(const wgf_gfx_priv_actor_t *root_ptr)
{
    const animation_t *anim = root_ptr->as.model.animation;
    if (anim != NULL && anim->source != 0) {
        return wgf_resource_get_status(anim->source) == WGF_RESOURCE_STATUS_READY ? wgf_gfx_priv_mesh_record(anim->source)
                                                                                  : NULL;
    }
    return file_of(root_ptr);
}

/* Its own file's nodes the clips move (made once both files are READY, from another's); NULL
 * for none. */
static const unsigned char *moved_of(animation_t *anim, const wgf_gfx_priv_mesh_record_t *record,
                                     const wgf_gfx_priv_mesh_record_t *clips)
{
    int s, n, c, a;
    if (anim == NULL || anim->source == 0) return record->animated;
    if (clips == NULL) return NULL;
    if (anim->map != NULL && anim->map_count == clips->node_count) return anim->moved;
    free(anim->map);
    free(anim->moved);
    anim->map = (int *)malloc(sizeof(int) * (size_t)(clips->node_count + 1));
    anim->moved = (unsigned char *)calloc((size_t)record->node_count + 1, 1);
    if (anim->map == NULL || anim->moved == NULL) return NULL;
    anim->map_count = clips->node_count;
    for (s = 0; s < clips->node_count; s++) { /* each of its nodes to ours of its name */
        anim->map[s] = -1;
        for (n = 0; n < record->node_count && clips->nodes[s].name != NULL; n++) {
            if (record->nodes[n].name != NULL && strcmp(record->nodes[n].name, clips->nodes[s].name) == 0) {
                anim->map[s] = n;
                break;
            }
        }
    }
    for (a = 0; a < clips->animation_count; a++) {
        for (c = 0; c < clips->animations[a].channel_count; c++) {
            const int node = clips->animations[a].channels[c].node;
            if (node >= 0 && node < clips->node_count && anim->map[node] >= 0) anim->moved[anim->map[node]] = 1;
        }
    }
    return anim->moved;
}

static void free_animation(animation_t *anim)
{
    if (anim == NULL) return;
    free(anim->pose);
    free(anim->held);
    free(anim->scratch);
    free(anim->mask);
    free(anim->layered);
    free(anim->map);
    free(anim->moved);
    if (anim->source != 0) wgf_resource_release(anim->source);
    free(anim);
}

/* ------------------------------------------------------------ sampling ---- */

/* Channel `c` at `t` seconds into `out` (3 floats, or a rotation's 4): before its first key
 * the first, after its last the last; between, linear (a rotation the shorter way, nlerped), the key
 * before (STEP), or glTF's cubic Hermite spline through its tangents. */
static void sample(const wgf_gfx_priv_mesh_channel_t *c, float t, float out[4])
{
    const int comp = c->path == WGF_GFX_PRIV_MESH_PATH_ROTATION ? 4 : 3;
    const bool cubic = c->interpolation == WGF_GFX_PRIV_MESH_LERP_CUBIC;
    const int stride = cubic ? comp * 3 : comp, at = cubic ? comp : 0; /* a key's value within its stride */
    int lo = 0, hi = c->key_count - 1, k;
    float u, dt;
    if (c->key_count == 1 || t <= c->times[0]) {
        memcpy(out, &c->values[at], sizeof(float) * (size_t)comp);
    } else if (t >= c->times[hi]) {
        memcpy(out, &c->values[(size_t)hi * (size_t)stride + (size_t)at], sizeof(float) * (size_t)comp);
    } else {
        while (hi - lo > 1) { /* the keys around t: times[lo] <= t < times[hi] */
            const int mid = (lo + hi) / 2;
            if (c->times[mid] <= t) lo = mid;
            else hi = mid;
        }
        dt = c->times[hi] - c->times[lo];
        u = dt > 0.0f ? (t - c->times[lo]) / dt : 0.0f;
        {
            const float *a = &c->values[(size_t)lo * (size_t)stride + (size_t)at];
            const float *b = &c->values[(size_t)hi * (size_t)stride + (size_t)at];
            if (c->interpolation == WGF_GFX_PRIV_MESH_LERP_STEP) {
                memcpy(out, a, sizeof(float) * (size_t)comp);
            } else if (cubic) {
                const float *out_tangent = a + comp, *in_tangent = b - comp;
                const float u2 = u * u, u3 = u2 * u;
                const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f, h10 = u3 - 2.0f * u2 + u;
                const float h01 = -2.0f * u3 + 3.0f * u2, h11 = u3 - u2;
                for (k = 0; k < comp; k++) {
                    out[k] = h00 * a[k] + h10 * dt * out_tangent[k] + h01 * b[k] + h11 * dt * in_tangent[k];
                }
            } else if (comp == 4) {
                /* nlerp: between a clip's keys (a few hundredths of a second apart) it is slerp to the
                   eye, without its trigonometry (a fifth of posing 200 robots) */
                const wgf_quat_t q = wgf_quat_nlerp(wgf_quat_make(a[0], a[1], a[2], a[3]),
                                                    wgf_quat_make(b[0], b[1], b[2], b[3]), u);
                out[0] = q.x;
                out[1] = q.y;
                out[2] = q.z;
                out[3] = q.w;
            } else {
                for (k = 0; k < comp; k++) out[k] = a[k] + (b[k] - a[k]) * u;
            }
        }
    }
    if (comp == 4) { /* a spline's, and a file's own keys, kept unit */
        const wgf_quat_t q = wgf_quat_normalize(wgf_quat_make(out[0], out[1], out[2], out[3]));
        out[0] = q.x;
        out[1] = q.y;
        out[2] = q.z;
        out[3] = q.w;
    }
}

/* Animation `clip` at `t` into `out`: every node an animation of the file moves at its rest,
 * as the file has it, then this one's channels over it. */
static void evaluate(const wgf_gfx_priv_mesh_record_t *record, const unsigned char *moved, const int *map,
                     const wgf_gfx_priv_mesh_animation_t *clip, float t, trs_t *out)
{
    int n, c;
    for (n = 0; n < record->node_count; n++) {
        if (moved == NULL || !moved[n]) continue;
        out[n].position = record->nodes[n].position;
        out[n].rotation = record->nodes[n].rotation;
        out[n].scale = record->nodes[n].scale;
    }
    for (c = 0; clip != NULL && c < clip->channel_count; c++) {
        const wgf_gfx_priv_mesh_channel_t *channel = &clip->channels[c];
        const int node = map != NULL ? (channel->node >= 0 ? map[channel->node] : -1) : channel->node;
        float v[4];
        if (node < 0 || node >= record->node_count) continue;
        sample(channel, t, v);
        if (channel->path == WGF_GFX_PRIV_MESH_PATH_TRANSLATION) {
            out[node].position = wgf_vec3_make(v[0], v[1], v[2]);
        } else if (channel->path == WGF_GFX_PRIV_MESH_PATH_ROTATION) {
            out[node].rotation = wgf_quat_make(v[0], v[1], v[2], v[3]);
        } else {
            out[node].scale = wgf_vec3_make(v[0], v[1], v[2]);
        }
    }
}

/* ----------------------------------------------------------- the player ---- */

static float duration_of(const wgf_gfx_priv_mesh_record_t *record, int animation)
{
    const wgf_gfx_priv_mesh_animation_t *clip = clip_of(record, animation);
    return clip != NULL ? clip->duration : 0.0f;
}

/* Where play starts: 0, or with a negative speed its end. */
static float start_of(const animation_t *anim, const wgf_gfx_priv_mesh_record_t *record)
{
    return anim->speed < 0.0f ? duration_of(record, anim->animation) : 0.0f;
}

/* `t` wrapped into [0, duration] (looping) or held there (not). */
static float wrap(float t, float duration, bool loop)
{
    if (duration <= 0.0f) return 0.0f;
    if (loop) {
        t = fmodf(t, duration);
        return t < 0.0f ? t + duration : t;
    }
    return t < 0.0f ? 0.0f : (t > duration ? duration : t);
}

/* The player's buffers for `count` nodes: false out of memory. A crossfade from a pose held
 * for another tree is let go of. */
static bool fit(animation_t *anim, int count)
{
    trs_t *pose, *held, *scratch;
    if (anim->node_count == count && anim->pose != NULL) return true;
    pose = (trs_t *)calloc((size_t)count + 1, sizeof(trs_t));
    held = (trs_t *)calloc((size_t)count + 1, sizeof(trs_t));
    scratch = (trs_t *)calloc((size_t)count + 1, sizeof(trs_t));
    if (pose == NULL || held == NULL || scratch == NULL) {
        free(pose);
        free(held);
        free(scratch);
        wgf_log_error("wgf_model: out of memory posing a file's tree");
        return false;
    }
    free(anim->pose);
    free(anim->held);
    free(anim->scratch);
    anim->pose = pose;
    anim->held = held;
    anim->scratch = scratch;
    anim->node_count = count;
    anim->posed = false;
    anim->fading = false;
    return true;
}

/* The root's tree posed where its player is, written into its actors: the clip at its time,
 * blended from what it fades from by the fade's weight. Nothing while the file loads, or
 * without a clip. */
static void pose(wgf_actor_t model)
{
    wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    animation_t *anim = root_ptr != NULL ? root_ptr->as.model.animation : NULL;
    const wgf_gfx_priv_model_tree_t *tree = root_ptr != NULL ? root_ptr->as.model.tree : NULL;
    const wgf_gfx_priv_mesh_record_t *record = root_ptr != NULL ? file_of(root_ptr) : NULL;
    const wgf_gfx_priv_mesh_record_t *clips = root_ptr != NULL ? clips_of(root_ptr) : NULL;
    const wgf_gfx_priv_mesh_animation_t *clip;
    const unsigned char *moved;
    const int *map;
    int n;
    if (anim == NULL || tree == NULL || record == NULL || clips == NULL) return;
    moved = moved_of(anim, record, clips);
    map = anim->source != 0 ? anim->map : NULL;
    clip = clip_of(clips, anim->animation);
    if (moved == NULL || clip == NULL || !fit(anim, record->node_count)) return;
    if (anim->fresh) {
        anim->time = start_of(anim, clips);
        anim->fresh = false;
    }
    evaluate(record, moved, map, clip, anim->time, anim->pose);
    if (anim->fading) {
        const float w = anim->fade > 0.0f ? anim->faded / anim->fade : 1.0f;
        const trs_t *from = anim->held;
        if (!anim->from_held) {
            evaluate(record, moved, map, clip_of(clips, anim->from), anim->from_time, anim->scratch);
            from = anim->scratch;
        }
        for (n = 0; n < record->node_count; n++) {
            trs_t *to = &anim->pose[n];
            if (!moved[n]) continue;
            to->position = wgf_vec3_lerp(from[n].position, to->position, w);
            to->scale = wgf_vec3_lerp(from[n].scale, to->scale, w);
            to->rotation = wgf_quat_nlerp(from[n].rotation, to->rotation, w);
        }
    }
    if (anim->layer >= 0 && anim->layer_weight > 0.0f && anim->mask != NULL && anim->layered != NULL) {
        const float w = anim->layer_weight;
        evaluate(record, moved, map, clip_of(clips, anim->layer), anim->layer_time, anim->layered);
        for (n = 0; n < record->node_count; n++) {
            trs_t *to = &anim->pose[n];
            if (!moved[n] || !anim->mask[n]) continue;
            to->position = wgf_vec3_lerp(to->position, anim->layered[n].position, w);
            to->scale = wgf_vec3_lerp(to->scale, anim->layered[n].scale, w);
            to->rotation = wgf_quat_nlerp(to->rotation, anim->layered[n].rotation, w);
        }
    }
    anim->posed = true;
    for (n = 0; n < record->node_count && n < tree->node_count; n++) {
        wgf_gfx_priv_actor_t *node_ptr;
        if (!moved[n] || (node_ptr = wgf_gfx_priv_actor_of_silently(tree->nodes[n])) == NULL) {
            continue; /* not moved, or destroyed since its tree was made */
        }
        node_ptr->position = anim->pose[n].position;
        node_ptr->rotation = anim->pose[n].rotation;
        node_ptr->scale = anim->pose[n].scale;
        wgf_gfx_priv_actor_transform_changed(tree->nodes[n]);
    }
}

/* `model` moved on by `dt` seconds times its speed: wrapped when looping, COMPLETE at the end
 * it runs toward when not; its crossfade moved on by `dt` (and what it fades from by its
 * speed), ending when it is through. A model whose file FAILED is COMPLETE, as a voice whose
 * sound failed; one whose file loads is held at its start. */
static void advance(wgf_actor_t model, wgf_gfx_priv_actor_t *root_ptr, float dt)
{
    animation_t *anim = root_ptr->as.model.animation;
    const wgf_gfx_priv_mesh_record_t *record = file_of(root_ptr) != NULL ? clips_of(root_ptr) : NULL;
    float duration;
    if (record == NULL) {
        if (wgf_resource_get_status(root_ptr->as.model.mesh) == WGF_RESOURCE_STATUS_FAILED) {
            anim->state = WGF_PLAY_STATE_COMPLETE;
        }
        return;
    }
    if (clip_of(record, anim->animation) == NULL) {
        anim->state = WGF_PLAY_STATE_STOPPED;
        return;
    }
    duration = duration_of(record, anim->animation);
    if (anim->fresh) anim->time = start_of(anim, record);
    anim->fresh = false;
    anim->time += dt * anim->speed;
    if (!anim->loop && anim->speed > 0.0f && anim->time >= duration) {
        anim->time = duration;
        anim->state = WGF_PLAY_STATE_COMPLETE;
    } else if (!anim->loop && anim->speed < 0.0f && anim->time <= 0.0f) {
        anim->time = 0.0f;
        anim->state = WGF_PLAY_STATE_COMPLETE;
    }
    anim->time = wrap(anim->time, duration, anim->loop);
    if (anim->fading) {
        anim->faded += dt;
        if (!anim->from_held) {
            anim->from_time = wrap(anim->from_time + dt * anim->speed, duration_of(record, anim->from), anim->loop);
        }
        if (anim->faded >= anim->fade) anim->fading = false;
    }
    if (anim->layer >= 0) { /* the layer: on in its time, its weight toward its target */
        const float layer_duration = duration_of(record, anim->layer), step = anim->layer_fade > 0.0f ? dt / anim->layer_fade : 1.0f;
        anim->layer_time += dt * (anim->speed > 0.0f ? anim->speed : -anim->speed);
        if (!anim->layer_loop && anim->layer_time >= layer_duration) {
            anim->layer_time = layer_duration;
            anim->layer_state = WGF_PLAY_STATE_COMPLETE;
            anim->layer_target = 0.0f; /* played once: it fades out as it ends */
        }
        anim->layer_time = wrap(anim->layer_time, layer_duration, anim->layer_loop);
        if (anim->layer_weight < anim->layer_target) {
            anim->layer_weight = anim->layer_weight + step > anim->layer_target ? anim->layer_target : anim->layer_weight + step;
        } else if (anim->layer_weight > anim->layer_target) {
            anim->layer_weight = anim->layer_weight - step < anim->layer_target ? anim->layer_target : anim->layer_weight - step;
        }
        if (anim->layer_weight <= 0.0f && anim->layer_target <= 0.0f) {
            anim->layer = -1; /* faded out: gone */
            if (anim->layer_state == WGF_PLAY_STATE_PLAYING) anim->layer_state = WGF_PLAY_STATE_STOPPED;
        }
    }
    pose(model);
}

static void update(float dt)
{
    int i, kept = 0;
    for (i = 0; i < playing.count; i++) {
        const wgf_actor_t model = playing.models[i];
        wgf_gfx_priv_actor_t *root_ptr = wgf_gfx_priv_actor_of_silently(model);
        animation_t *anim;
        if (root_ptr == NULL || root_ptr->type != WGF_ACTOR_KIND_MODEL || root_ptr->as.model.animation == NULL) {
            continue; /* destroyed */
        }
        anim = root_ptr->as.model.animation;
        if (anim->state == WGF_PLAY_STATE_PLAYING && !wgf_gfx_priv_actor_is_frozen(model)) advance(model, root_ptr, dt);
        root_ptr = wgf_gfx_priv_actor_of_silently(model); /* posing makes nothing, but read it again all the same */
        anim = root_ptr->as.model.animation;
        if (anim->state == WGF_PLAY_STATE_PLAYING) {
            playing.models[kept++] = model;
        } else {
            anim->listed = false;
        }
    }
    playing.count = kept;
}

static void stop(void)
{
    free(playing.models);
    memset(&playing, 0, sizeof(playing));
    installed = false;
}

static wgf_core_priv_part_t part = {.name = "animation",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_ANIMATION,
                                    .update = update,
                                    .stop = stop};

/* A file's root's tree made (or made again, its file loaded again), or its file FAILED: a
 * clip chosen while it loaded that the file hasn't is dropped, warned; else the tree posed
 * where the player is. */
static void built(wgf_actor_t model)
{
    wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    animation_t *anim = root_ptr != NULL ? root_ptr->as.model.animation : NULL;
    const wgf_gfx_priv_mesh_record_t *record;
    if (anim == NULL) return;
    record = file_of(root_ptr) != NULL ? clips_of(root_ptr) : NULL;
    if (record == NULL) {
        if (anim->state == WGF_PLAY_STATE_PLAYING && file_of(root_ptr) == NULL) anim->state = WGF_PLAY_STATE_COMPLETE;
        return;
    }
    if (anim->animation >= record->animation_count) {
        wgf_log_warn("wgf_model: %s has %d animations, not animation %d, played while it loaded",
                     wgf_resource_get_path(root_ptr->as.model.mesh), record->animation_count, anim->animation);
        anim->animation = -1;
        anim->state = WGF_PLAY_STATE_STOPPED;
        anim->time = 0.0f;
        anim->fresh = anim->fading = false;
        return;
    }
    if (!anim->fresh) anim->time = wrap(anim->time, duration_of(record, anim->animation), anim->loop);
    anim->fading = false; /* nothing was shown to fade from */
    pose(model);
}

/* The model's player, made (at rest: no clip, a speed of 1, looping) and the part installed
 * with the first; NULL for a model that isn't a file's root, or out of memory. */
static animation_t *player_of(wgf_actor_t model)
{
    wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    animation_t *anim;
    if (root_ptr == NULL) return NULL;
    if (root_ptr->as.model.animation != NULL) return root_ptr->as.model.animation;
    if (!installed) {
        wgf_core_priv_part_install(&part);
        wgf_gfx_priv_add_model_built_hook(built);
        wgf_gfx_priv_model_set_animation_free(free_animation);
        installed = true;
    }
    anim = (animation_t *)calloc(1, sizeof(animation_t));
    if (anim == NULL) {
        wgf_log_error("wgf_model: out of memory for an animation player");
        return NULL;
    }
    anim->animation = anim->from = anim->layer = -1;
    anim->layer_state = WGF_PLAY_STATE_STOPPED;
    anim->speed = 1.0f;
    anim->loop = true;
    root_ptr->as.model.animation = anim;
    return anim;
}

/* `model` on the list of those advanced. */
static void listen(wgf_actor_t model, animation_t *anim)
{
    if (anim->listed) return;
    if (playing.count == playing.capacity) {
        const int capacity = playing.capacity > 0 ? playing.capacity * 2 : 16;
        wgf_actor_t *models = (wgf_actor_t *)realloc(playing.models, (size_t)capacity * sizeof(wgf_actor_t));
        if (models == NULL) {
            wgf_log_error("wgf_model: out of memory for the models playing");
            return;
        }
        playing.models = models;
        playing.capacity = capacity;
    }
    playing.models[playing.count++] = model;
    anim->listed = true;
}

/* The player of `model` when it has one; NULL otherwise. */
static animation_t *existing(wgf_actor_t model)
{
    const wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    return root_ptr != NULL ? root_ptr->as.model.animation : NULL;
}

/* ------------------------------------------------------- the public API ---- */

int wgf_model_get_animation_count(wgf_actor_t model)
{
    const wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    const wgf_gfx_priv_mesh_record_t *record = root_ptr != NULL ? clips_of(root_ptr) : NULL;
    return record != NULL ? record->animation_count : 0;
}

const char *wgf_model_get_animation_name(wgf_actor_t model, int animation)
{
    const wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    const wgf_gfx_priv_mesh_animation_t *clip = root_ptr != NULL ? clip_of(clips_of(root_ptr), animation) : NULL;
    return clip != NULL ? clip->name : "";
}

int wgf_model_find_animation(wgf_actor_t model, const char *name)
{
    const wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    const wgf_gfx_priv_mesh_record_t *record = root_ptr != NULL ? clips_of(root_ptr) : NULL;
    int i;
    if (record == NULL || name == NULL || name[0] == '\0') return -1;
    for (i = 0; i < record->animation_count; i++) {
        if (strcmp(record->animations[i].name, name) == 0) return i;
    }
    return -1;
}

float wgf_model_get_animation_duration(wgf_actor_t model, int animation)
{
    const wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    return root_ptr != NULL ? duration_of(clips_of(root_ptr), animation) : 0.0f;
}

bool wgf_model_play_animation(wgf_actor_t model, int animation, float fade)
{
    const wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    const wgf_gfx_priv_mesh_record_t *record = root_ptr != NULL ? clips_of(root_ptr) : NULL;
    animation_t *anim;
    if (root_ptr == NULL || animation < 0 || isnan(fade) ||
        wgf_resource_get_status(root_ptr->as.model.mesh) == WGF_RESOURCE_STATUS_FAILED ||
        (record != NULL && animation >= record->animation_count)) {
        return false;
    }
    anim = player_of(model);
    if (anim == NULL) return false;
    if (fade > 0.0f && record != NULL && anim->posed && anim->pose != NULL) { /* from what shows now */
        if (anim->state == WGF_PLAY_STATE_PLAYING && !anim->fading && clip_of(record, anim->animation) != NULL) {
            anim->from = anim->animation; /* still moving as it fades */
            anim->from_time = anim->time;
            anim->from_held = false;
        } else { /* a pose held: mid-crossfade, or not moving */
            memcpy(anim->held, anim->pose, sizeof(trs_t) * (size_t)anim->node_count);
            anim->from_held = true;
        }
        anim->fading = true;
        anim->fade = fade;
        anim->faded = 0.0f;
    } else {
        anim->fading = false;
    }
    anim->animation = animation;
    anim->state = WGF_PLAY_STATE_PLAYING;
    anim->time = 0.0f;
    anim->fresh = true;
    pose(model);
    listen(model, anim);
    return true;
}

int wgf_model_get_animation(wgf_actor_t model)
{
    const animation_t *anim = existing(model);
    return anim != NULL ? anim->animation : -1;
}

float wgf_model_get_animation_weight(wgf_actor_t model)
{
    const animation_t *anim = existing(model);
    if (anim == NULL || anim->animation < 0) return 0.0f;
    if (!anim->fading || anim->fade <= 0.0f) return 1.0f;
    return anim->faded >= anim->fade ? 1.0f : anim->faded / anim->fade;
}

bool wgf_model_pause_animation(wgf_actor_t model)
{
    animation_t *anim = existing(model);
    if (anim == NULL || anim->state != WGF_PLAY_STATE_PLAYING) return false;
    anim->state = WGF_PLAY_STATE_PAUSED;
    return true;
}

bool wgf_model_resume_animation(wgf_actor_t model)
{
    animation_t *anim = existing(model);
    if (anim == NULL || anim->state != WGF_PLAY_STATE_PAUSED) return false;
    anim->state = WGF_PLAY_STATE_PLAYING;
    listen(model, anim);
    return true;
}

bool wgf_model_stop_animation(wgf_actor_t model)
{
    animation_t *anim = existing(model);
    if (root_of(model) == NULL) return false;
    if (anim == NULL) return true; /* never played: stopped already */
    anim->state = WGF_PLAY_STATE_STOPPED;
    anim->time = 0.0f;
    anim->fresh = anim->fading = false;
    pose(model);
    return true;
}

wgf_play_state_t wgf_model_get_animation_state(wgf_actor_t model)
{
    const animation_t *anim = existing(model);
    return anim != NULL ? (wgf_play_state_t)anim->state : WGF_PLAY_STATE_STOPPED;
}

bool wgf_model_set_animation_speed(wgf_actor_t model, float speed)
{
    animation_t *anim;
    if (root_of(model) == NULL || !isfinite(speed) || (anim = player_of(model)) == NULL) return false;
    anim->speed = speed;
    if (anim->fresh) pose(model); /* not moved yet: from the end the speed says */
    return true;
}

float wgf_model_get_animation_speed(wgf_actor_t model)
{
    const animation_t *anim = existing(model);
    return anim != NULL ? anim->speed : (root_of(model) != NULL ? 1.0f : 0.0f);
}

bool wgf_model_set_animation_loop(wgf_actor_t model, bool loop)
{
    animation_t *anim = player_of(model);
    if (anim == NULL) return false;
    anim->loop = loop;
    return true;
}

bool wgf_model_is_animation_loop(wgf_actor_t model)
{
    const animation_t *anim = existing(model);
    return anim != NULL ? anim->loop : root_of(model) != NULL;
}

bool wgf_model_set_animation_time(wgf_actor_t model, float seconds)
{
    const wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    const wgf_gfx_priv_mesh_record_t *record = root_ptr != NULL ? clips_of(root_ptr) : NULL;
    animation_t *anim;
    if (root_ptr == NULL || !isfinite(seconds) || (anim = player_of(model)) == NULL) return false;
    anim->fresh = false;
    anim->time = record != NULL ? wrap(seconds, duration_of(record, anim->animation), anim->loop) : seconds;
    pose(model);
    return true;
}

float wgf_model_get_animation_time(wgf_actor_t model)
{
    const wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    const animation_t *anim = existing(model);
    if (root_ptr == NULL || anim == NULL) return 0.0f;
    return anim->fresh ? start_of(anim, clips_of(root_ptr)) : anim->time;
}

/* ------------------------------------------------------------ the layer ---- */

/* The nodes at and under the file's node named `bone`, into a mask a node at a time; false
 * when it has none of that name. */
static bool mask_of(const wgf_gfx_priv_mesh_record_t *record, const char *bone, unsigned char *mask)
{
    int n, top = -1;
    for (n = 0; n < record->node_count && top < 0; n++) {
        if (record->nodes[n].name != NULL && strcmp(record->nodes[n].name, bone) == 0) top = n;
    }
    if (top < 0) return false;
    for (n = 0; n < record->node_count; n++) {
        int at = n, depth = 0;
        mask[n] = 0;
        while (at >= 0 && depth++ < record->node_count) {
            if (at == top) {
                mask[n] = 1;
                break;
            }
            at = record->nodes[at].parent;
        }
    }
    return true;
}

bool wgf_model_play_layer_animation(wgf_actor_t model, int animation, const char *bone, float fade, bool loop)
{
    animation_t *anim = existing(model);
    wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    const wgf_gfx_priv_mesh_record_t *record = root_ptr != NULL ? file_of(root_ptr) : NULL;
    const wgf_gfx_priv_mesh_record_t *clips = root_ptr != NULL ? clips_of(root_ptr) : NULL;
    unsigned char *mask;
    trs_t *layered;
    if (anim == NULL || record == NULL || bone == NULL || clip_of(clips, animation) == NULL || !isfinite(fade)) {
        return false;
    }
    mask = (unsigned char *)calloc((size_t)record->node_count + 1, 1);
    layered = (trs_t *)calloc((size_t)record->node_count + 1, sizeof(trs_t));
    if (mask == NULL || layered == NULL || !mask_of(record, bone, mask)) {
        free(mask);
        free(layered);
        return false;
    }
    free(anim->mask);
    free(anim->layered);
    anim->mask = mask;
    anim->layered = layered;
    if (anim->layer < 0) anim->layer_weight = fade > 0.0f ? 0.0f : 1.0f;
    anim->layer = animation;
    anim->layer_time = 0.0f;
    anim->layer_loop = loop;
    snprintf(anim->layer_bone, sizeof(anim->layer_bone), "%s", bone);
    anim->layer_fade = fade > 0.0f ? fade : 0.0f;
    anim->layer_target = 1.0f;
    anim->layer_state = WGF_PLAY_STATE_PLAYING;
    return true;
}

bool wgf_model_stop_layer_animation(wgf_actor_t model, float fade)
{
    animation_t *anim = existing(model);
    if (anim == NULL || !isfinite(fade)) return false;
    if (anim->layer < 0) return true;
    anim->layer_fade = fade > 0.0f ? fade : 0.0f;
    anim->layer_target = 0.0f;
    if (fade <= 0.0f) {
        anim->layer_weight = 0.0f;
        anim->layer = -1;
    }
    anim->layer_state = WGF_PLAY_STATE_STOPPED;
    return true;
}

int wgf_model_get_layer_animation(wgf_actor_t model)
{
    const animation_t *anim = existing(model);
    return anim != NULL ? anim->layer : -1;
}

wgf_play_state_t wgf_model_get_layer_state(wgf_actor_t model)
{
    const animation_t *anim = existing(model);
    return anim != NULL ? (wgf_play_state_t)anim->layer_state : WGF_PLAY_STATE_STOPPED;
}

float wgf_model_get_layer_weight(wgf_actor_t model)
{
    const animation_t *anim = existing(model);
    return anim != NULL && anim->layer >= 0 ? anim->layer_weight : 0.0f;
}

/* ------------------------------------------------------ clips from another file ---- */

bool wgf_model_set_animations(wgf_actor_t model, wgf_mesh_t clips)
{
    animation_t *anim;
    if (root_of(model) == NULL || (clips != 0 && wgf_gfx_priv_mesh_record(clips) == NULL)) return false;
    anim = player_of(model);
    if (anim == NULL) return false;
    if (clips == anim->source) return true;
    if (clips != 0) wgf_gfx_priv_mesh_retain(clips);
    if (anim->source != 0) wgf_resource_release(anim->source);
    anim->source = clips;
    free(anim->map);
    free(anim->moved);
    anim->map = NULL;
    anim->moved = NULL;
    anim->map_count = 0;
    anim->animation = -1; /* another file's clips: what played is gone */
    anim->layer = -1;
    anim->state = WGF_PLAY_STATE_STOPPED;
    anim->fading = false;
    return true;
}

wgf_mesh_t wgf_model_get_animations(wgf_actor_t model)
{
    const animation_t *anim = existing(model);
    return anim != NULL ? anim->source : 0;
}

/* ------------------------------------------------------- sync, sockets ---- */

bool wgf_model_sync_animation(wgf_actor_t model, wgf_actor_t from)
{
    const animation_t *src = existing(from);
    wgf_gfx_priv_actor_t *root_ptr = root_of(model);
    const wgf_gfx_priv_mesh_record_t *clips = root_ptr != NULL ? clips_of(root_ptr) : NULL;
    animation_t *anim;
    if (src == NULL || root_ptr == NULL || model == from || src->animation < 0 || clips == NULL ||
        clip_of(clips, src->animation) == NULL) {
        return false;
    }
    anim = player_of(model);
    if (anim == NULL) return false;
    anim->animation = src->animation;
    anim->state = src->state;
    anim->time = src->fresh ? start_of(src, clips) : src->time;
    anim->speed = src->speed;
    anim->loop = src->loop;
    anim->fresh = false;
    anim->fading = false; /* a crossfade's held pose is the other's tree's: the clip as it is now */
    if (src->layer >= 0 && clip_of(clips, src->layer) != NULL &&
        wgf_model_play_layer_animation(model, src->layer, src->layer_bone, src->layer_fade, src->layer_loop)) {
        anim = existing(model);
        anim->layer_state = src->layer_state;
        anim->layer_time = src->layer_time;
        anim->layer_weight = src->layer_weight;
        anim->layer_target = src->layer_target;
    } else {
        anim->layer = -1;
        anim->layer_state = WGF_PLAY_STATE_STOPPED;
    }
    if (anim->state == WGF_PLAY_STATE_PLAYING) listen(model, anim);
    pose(model);
    return true;
}

/* The first actor named `name` under `actor`, depth first. */
static wgf_actor_t find_named(wgf_actor_t actor, const char *name)
{
    const int n = wgf_actor_get_child_count(actor);
    int i;
    for (i = 0; i < n; i++) {
        const wgf_actor_t child = wgf_actor_get_child(actor, i);
        wgf_actor_t found;
        if (strcmp(wgf_actor_get_name(child), name) == 0) return child;
        found = find_named(child, name);
        if (found != 0) return found;
    }
    return 0;
}

wgf_actor_t wgf_model_find_node(wgf_actor_t model, const char *name)
{
    if (root_of(model) == NULL || name == NULL || name[0] == '\0') return 0;
    return find_named(model, name);
}
