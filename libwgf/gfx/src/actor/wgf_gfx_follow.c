/* Cameras that follow (wgf_follow.h): helix-legacy's CameraFollow2D, CameraFollow3D, and
 * Orbit3D, a table of up to 64 cameras each with its target, stepped once a frame by a part
 * (installed by the first follow) after the ticks and before the frame callback, so a
 * simulated target is followed where it is drawn. */
#include "wgf_follow.h"

#include <math.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_camera2d.h"
#include "wgf_handle.h"
#include "wgf_core_part_priv.h"
#include "wgf_presentation.h"
#include "wgf_trig.h"

#define MAX_FOLLOWS 64
#define RECOVERY 1.5f    /* trauma lost a second */
#define FREQUENCY 25.0f  /* the shake's changes a second */
#define THROW 0.04f      /* how far full trauma throws the view: a share of what it shows (2D) or its distance (3D) */
#define TURN 6.0f        /* CHASE: how quickly the heading swings round, a rate a second */

typedef struct follow_t {
    wgf_actor_t camera, target;
    float smoothing, dead[2], bounds[4], offset[3], orbit[3];
    int mode;
    float point[3];  /* followed to, before the shake */
    float heading;   /* CHASE: the heading now */
    float trauma, time;
    bool placed;     /* the point has been put on the target once */
} follow_t;

static struct {
    bool installed;
    follow_t follows[MAX_FOLLOWS];
} fl;

static bool is_camera(wgf_actor_t actor)
{
    const wgf_actor_kind_t kind = wgf_actor_get_kind(actor);
    return kind == WGF_ACTOR_KIND_CAMERA2D || kind == WGF_ACTOR_KIND_CAMERA3D;
}

static follow_t *follow_of(wgf_actor_t camera)
{
    int i;
    if (camera == 0) return NULL;
    for (i = 0; i < MAX_FOLLOWS; i++) {
        if (fl.follows[i].camera == camera) return &fl.follows[i];
    }
    return NULL;
}

/* A rate's catch-up over dt: the share of the way covered. */
static float damp(float rate, float dt)
{
    return rate <= 0.0f ? 1.0f : 1.0f - expf(-rate * dt);
}

/* One axis of the point following `at`: free in a dead zone `dead` wide, then caught up. */
static float chase(float point, float at, float dead, float share)
{
    const float half = dead * 0.5f;
    float want = point;
    if (at > point + half) want = at - half;
    else if (at < point - half) want = at + half;
    return point + (want - point) * share;
}

/* Smooth noise in -1..1 from two sines, a seed apart for each axis. */
static float noise(float t, float seed)
{
    return 0.6f * wgf_trig_sin(t * 6.2831853f + seed) + 0.4f * wgf_trig_sin(t * 2.3f * 6.2831853f + seed * 1.7f);
}

/* The camera put at `world` (its tree's root's space) under whatever its parent is. */
static void place(wgf_actor_t camera, float x, float y, float z)
{
    const wgf_actor_t parent = wgf_actor_get_parent(camera);
    wgf_vec3_t at = wgf_vec3_make(x, y, z);
    if (parent != 0 && wgf_actor_get_kind(parent) != WGF_ACTOR_KIND_STAGE2D &&
        wgf_actor_get_kind(parent) != WGF_ACTOR_KIND_STAGE3D) {
        at = wgf_mat4_transform_point(wgf_mat4_invert(wgf_gfx_priv_actor_get_world_matrix(parent)), at);
    }
    wgf_actor_set_position(camera, at.x, at.y, at.z);
}

static void step_2d(follow_t *f, wgf_vec3_t at, float dt, bool snap)
{
    const float share = snap ? 1.0f : damp(f->smoothing, dt);
    const wgf_vec4_t visible = wgf_presentation_get_visible();
    float zoom = wgf_camera2d_get_zoom(f->camera), half_w, half_h, x, y, amount;
    zoom = zoom > 0.0f ? zoom : 1.0f;
    half_w = visible.z / zoom * 0.5f;
    half_h = visible.w / zoom * 0.5f;
    f->point[0] = snap ? at.x : chase(f->point[0], at.x, f->dead[0], share);
    f->point[1] = snap ? at.y : chase(f->point[1], at.y, f->dead[1], share);
    if (f->bounds[2] > f->bounds[0] && f->bounds[3] > f->bounds[1]) { /* the view kept inside */
        const float l = f->bounds[0] + half_w, r = f->bounds[2] - half_w,
                    t = f->bounds[1] + half_h, b = f->bounds[3] - half_h;
        f->point[0] = l > r ? (f->bounds[0] + f->bounds[2]) * 0.5f
                      : f->point[0] < l ? l
                      : f->point[0] > r ? r
                                        : f->point[0];
        f->point[1] = t > b ? (f->bounds[1] + f->bounds[3]) * 0.5f
                      : f->point[1] < t ? t
                      : f->point[1] > b ? b
                                        : f->point[1];
    }
    amount = f->trauma * f->trauma * THROW * half_h * 2.0f;
    x = f->point[0] + amount * noise(f->time * FREQUENCY * 0.1f, 0.0f);
    y = f->point[1] + amount * noise(f->time * FREQUENCY * 0.1f, 3.1f);
    place(f->camera, x, y, 0.0f);
}

static void step_3d(follow_t *f, wgf_vec3_t at, float dt, bool snap)
{
    const float share = snap ? 1.0f : damp(f->smoothing, dt);
    float ox, oy, oz, x, y, z, amount, distance;
    f->point[0] = snap ? at.x : chase(f->point[0], at.x, f->dead[0], share);
    f->point[1] = snap ? at.y : chase(f->point[1], at.y, f->dead[1], share);
    f->point[2] = snap ? at.z : chase(f->point[2], at.z, f->dead[0], share);
    if (f->mode == WGF_FOLLOW_MODE_ORBIT) {
        const wgf_vec2_t around = wgf_trig_cos_sin(f->orbit[0]), up = wgf_trig_cos_sin(f->orbit[1]);
        ox = f->orbit[2] * up.x * around.y;
        oy = f->orbit[2] * up.y;
        oz = f->orbit[2] * up.x * around.x;
    } else if (f->mode == WGF_FOLLOW_MODE_CHASE) {
        const wgf_vec3_t forward = wgf_actor_get_drawn_direction(f->target, 0.0f, 0.0f, -1.0f);
        float heading = f->heading, diff;
        if (forward.x * forward.x + forward.z * forward.z > 1e-8f) heading = wgf_trig_atan2(-forward.x, -forward.z);
        diff = heading - f->heading;
        while (diff > 3.14159265f) diff -= 6.2831853f;
        while (diff < -3.14159265f) diff += 6.2831853f;
        f->heading += diff * (snap ? 1.0f : damp(TURN, dt)); /* the shorter way round */
        {
            const wgf_vec2_t turn = wgf_trig_cos_sin(f->heading);
            ox = f->offset[0] * turn.x + f->offset[2] * turn.y;
            oy = f->offset[1];
            oz = -f->offset[0] * turn.y + f->offset[2] * turn.x;
        }
    } else {
        ox = f->offset[0], oy = f->offset[1], oz = f->offset[2];
    }
    distance = sqrtf(ox * ox + oy * oy + oz * oz);
    amount = f->trauma * f->trauma * THROW * distance;
    x = f->point[0] + amount * noise(f->time * FREQUENCY * 0.1f, 0.0f);
    y = f->point[1] + amount * noise(f->time * FREQUENCY * 0.1f, 3.1f);
    z = f->point[2] + amount * noise(f->time * FREQUENCY * 0.1f, 5.3f);
    place(f->camera, x + ox, y + oy, z + oz);
    wgf_actor_look_at(f->camera, x, y, z, 0.0f, 1.0f, 0.0f);
}

static void step(follow_t *f, float dt, bool snap)
{
    wgf_vec3_t at;
    if (!wgf_handle_is_alive(f->camera)) {
        memset(f, 0, sizeof(*f));
        return;
    }
    if (!wgf_handle_is_alive(f->target)) return; /* its target gone: it stays where it is */
    at = wgf_actor_get_drawn_position(f->target);
    if (!f->placed) snap = true, f->placed = true;
    f->time += dt;
    f->trauma = f->trauma - RECOVERY * dt > 0.0f ? f->trauma - RECOVERY * dt : 0.0f;
    if (wgf_actor_get_kind(f->camera) == WGF_ACTOR_KIND_CAMERA2D) step_2d(f, at, dt, snap);
    else step_3d(f, at, dt, snap);
}

static void update(float dt)
{
    int i;
    for (i = 0; i < MAX_FOLLOWS; i++) {
        if (fl.follows[i].camera != 0 && fl.follows[i].target != 0) step(&fl.follows[i], dt, false);
    }
}

static void stop(void)
{
    memset(&fl, 0, sizeof(fl));
}

static wgf_core_priv_part_t part = {.name = "follow",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_FOLLOW,
                                    .update = update,
                                    .stop = stop};

bool wgf_follow_set_target(wgf_actor_t camera, wgf_actor_t target)
{
    follow_t *f;
    int i;
    if (!is_camera(camera) || (target != 0 && !wgf_handle_is_alive(target))) return false;
    f = follow_of(camera);
    if (f == NULL) {
        if (target == 0) return true;
        for (i = 0; i < MAX_FOLLOWS && f == NULL; i++) {
            if (fl.follows[i].camera == 0 || !wgf_handle_is_alive(fl.follows[i].camera)) f = &fl.follows[i];
        }
        if (f == NULL) return false;
        memset(f, 0, sizeof(*f));
        f->camera = camera;
        f->smoothing = 8.0f;
        f->offset[1] = 3.0f, f->offset[2] = 6.0f;
        f->orbit[1] = 0.4f, f->orbit[2] = 6.0f;
        if (!fl.installed) {
            wgf_core_priv_part_install(&part);
            fl.installed = true;
        }
    }
    if (target != f->target) f->placed = false;
    f->target = target;
    return true;
}

wgf_actor_t wgf_follow_get_target(wgf_actor_t camera)
{
    const follow_t *f = follow_of(camera);
    return f != NULL && wgf_handle_is_alive(camera) ? f->target : 0;
}

/* The follow of a live camera with a target set once; NULL otherwise. */
static follow_t *following(wgf_actor_t camera)
{
    follow_t *f = follow_of(camera);
    return f != NULL && wgf_handle_is_alive(camera) ? f : NULL;
}

bool wgf_follow_set_smoothing(wgf_actor_t camera, float rate)
{
    follow_t *f = following(camera);
    if (f == NULL || !isfinite(rate)) return false;
    f->smoothing = rate;
    return true;
}

float wgf_follow_get_smoothing(wgf_actor_t camera)
{
    const follow_t *f = following(camera);
    return f != NULL ? f->smoothing : 0.0f;
}

bool wgf_follow_set_dead_zone(wgf_actor_t camera, float width, float height)
{
    follow_t *f = following(camera);
    if (f == NULL || !(width >= 0.0f) || !(height >= 0.0f) || !isfinite(width) || !isfinite(height)) return false;
    f->dead[0] = width, f->dead[1] = height;
    return true;
}

wgf_vec2_t wgf_follow_get_dead_zone(wgf_actor_t camera)
{
    const follow_t *f = following(camera);
    return f != NULL ? wgf_vec2_make(f->dead[0], f->dead[1]) : wgf_vec2_make(0.0f, 0.0f);
}

bool wgf_follow_set_bounds(wgf_actor_t camera, float left, float top, float right, float bottom)
{
    follow_t *f = following(camera);
    if (f == NULL || !isfinite(left) || !isfinite(top) || !isfinite(right) || !isfinite(bottom)) return false;
    f->bounds[0] = left, f->bounds[1] = top, f->bounds[2] = right, f->bounds[3] = bottom;
    return true;
}

wgf_vec4_t wgf_follow_get_bounds(wgf_actor_t camera)
{
    const follow_t *f = following(camera);
    return f != NULL ? wgf_vec4_make(f->bounds[0], f->bounds[1], f->bounds[2], f->bounds[3])
                     : wgf_vec4_make(0.0f, 0.0f, 0.0f, 0.0f);
}

bool wgf_follow_set_offset(wgf_actor_t camera, float x, float y, float z)
{
    follow_t *f = following(camera);
    if (f == NULL || !isfinite(x) || !isfinite(y) || !isfinite(z)) return false;
    f->offset[0] = x, f->offset[1] = y, f->offset[2] = z;
    return true;
}

wgf_vec3_t wgf_follow_get_offset(wgf_actor_t camera)
{
    const follow_t *f = following(camera);
    return f != NULL ? wgf_vec3_make(f->offset[0], f->offset[1], f->offset[2]) : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_follow_set_mode(wgf_actor_t camera, wgf_follow_mode_t mode)
{
    follow_t *f = following(camera);
    if (f == NULL || (int)mode < WGF_FOLLOW_MODE_CHASE || mode > WGF_FOLLOW_MODE_ORBIT) return false;
    f->mode = (int)mode;
    return true;
}

wgf_follow_mode_t wgf_follow_get_mode(wgf_actor_t camera)
{
    const follow_t *f = following(camera);
    return f != NULL ? (wgf_follow_mode_t)f->mode : WGF_FOLLOW_MODE_CHASE;
}

bool wgf_follow_set_orbit(wgf_actor_t camera, float yaw, float pitch, float distance)
{
    follow_t *f = following(camera);
    const float most = 1.5533430f; /* 89 degrees */
    if (f == NULL || !isfinite(yaw) || !isfinite(pitch) || !(distance > 0.0f) || !isfinite(distance)) return false;
    f->orbit[0] = yaw;
    f->orbit[1] = pitch < -most ? -most : pitch > most ? most : pitch;
    f->orbit[2] = distance;
    return true;
}

wgf_vec3_t wgf_follow_get_orbit(wgf_actor_t camera)
{
    const follow_t *f = following(camera);
    return f != NULL ? wgf_vec3_make(f->orbit[0], f->orbit[1], f->orbit[2]) : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_follow_shake(wgf_actor_t camera, float amount)
{
    follow_t *f = following(camera);
    if (f == NULL || !isfinite(amount) || amount < 0.0f) return false;
    f->trauma = f->trauma + amount > 1.0f ? 1.0f : f->trauma + amount;
    return true;
}

float wgf_follow_get_trauma(wgf_actor_t camera)
{
    const follow_t *f = following(camera);
    return f != NULL ? f->trauma : 0.0f;
}

bool wgf_follow_snap(wgf_actor_t camera)
{
    follow_t *f = following(camera);
    if (f == NULL || f->target == 0) return false;
    step(f, 0.0f, true);
    return true;
}
