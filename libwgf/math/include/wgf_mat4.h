#ifndef WGF_MAT4_H
#define WGF_MAT4_H

#include <math.h>
#include "wgf_trig.h"
#include "wgf_api.h"
#include "wgf_quat.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 4 by 4 matrix: 16 floats, column by column (m[column * 4 + row]), with no
 * padding, as OpenGL and shaders take them. A point is a column vector on the right:
 * mul(a, b) applied to a point applies b first, then a. Space is right-handed; a
 * camera looks down its -z, with +y up. */
typedef struct wgf_mat4_t {
    float m[16];
} wgf_mat4_t;

#ifndef __cplusplus
_Static_assert(sizeof(wgf_mat4_t) == 16 * sizeof(float), "wgf_mat4_t is 16 floats");
#endif

static inline wgf_mat4_t wgf_mat4_identity(void)
{
    wgf_mat4_t r;
    int i;
    for (i = 0; i < 16; i++) r.m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    return r;
}

/* a times b: b's transform, then a's. */
static inline wgf_mat4_t wgf_mat4_mul(wgf_mat4_t a, wgf_mat4_t b)
{
    wgf_mat4_t r;
    int c, row, k;
    for (c = 0; c < 4; c++) {
        for (row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (k = 0; k < 4; k++) sum += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = sum;
        }
    }
    return r;
}

static inline wgf_mat4_t wgf_mat4_transpose(wgf_mat4_t a)
{
    wgf_mat4_t r;
    int c, row;
    for (c = 0; c < 4; c++) {
        for (row = 0; row < 4; row++) r.m[c * 4 + row] = a.m[row * 4 + c];
    }
    return r;
}

/* The cofactors of a, transposed: its inverse times its determinant. */
static inline wgf_mat4_t wgf_mat4_adjugate(wgf_mat4_t m)
{
    const float *a = m.m;
    wgf_mat4_t r;
    float *inv = r.m;
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
             a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
             a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
             a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
              a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
             a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
             a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
             a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
              a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
             a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
              a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
              a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
             a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    return r;
}

static inline float wgf_mat4_determinant(wgf_mat4_t a)
{
    const wgf_mat4_t adj = wgf_mat4_adjugate(a);
    return a.m[0] * adj.m[0] + a.m[1] * adj.m[4] + a.m[2] * adj.m[8] + a.m[3] * adj.m[12];
}

/* The inverse; the identity when there is none (a determinant within 1e-12 of 0,
 * such as a scale of 0), as wgrender's. */
static inline wgf_mat4_t wgf_mat4_invert(wgf_mat4_t a)
{
    wgf_mat4_t r = wgf_mat4_adjugate(a);
    const float det = a.m[0] * r.m[0] + a.m[1] * r.m[4] + a.m[2] * r.m[8] + a.m[3] * r.m[12];
    int i;
    if (det > -1e-12f && det < 1e-12f) return wgf_mat4_identity();
    for (i = 0; i < 16; i++) r.m[i] *= 1.0f / det;
    return r;
}

/* Scale, then rotate, then move: the transform of an actor at `position`, turned by
 * `rotation`, scaled by `scale`. */
static inline wgf_mat4_t wgf_mat4_from_trs(wgf_vec3_t position, wgf_quat_t rotation,
                                                          wgf_vec3_t scale)
{
    const wgf_quat_t q = rotation;
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    wgf_mat4_t r = wgf_mat4_identity();
    r.m[0] = (1.0f - 2.0f * (yy + zz)) * scale.x;
    r.m[1] = (2.0f * (xy + wz)) * scale.x;
    r.m[2] = (2.0f * (xz - wy)) * scale.x;
    r.m[4] = (2.0f * (xy - wz)) * scale.y;
    r.m[5] = (1.0f - 2.0f * (xx + zz)) * scale.y;
    r.m[6] = (2.0f * (yz + wx)) * scale.y;
    r.m[8] = (2.0f * (xz + wy)) * scale.z;
    r.m[9] = (2.0f * (yz - wx)) * scale.z;
    r.m[10] = (1.0f - 2.0f * (xx + yy)) * scale.z;
    r.m[12] = position.x;
    r.m[13] = position.y;
    r.m[14] = position.z;
    return r;
}

/* The pieces from_trs makes a matrix of, back out of one: exact unless the matrix
 * shears (an uneven scale over a turn), when the nearest rotation is given. */
static inline wgf_vec3_t wgf_mat4_get_translation(wgf_mat4_t m)
{
    return wgf_vec3_make(m.m[12], m.m[13], m.m[14]);
}

static inline wgf_vec3_t wgf_mat4_get_scale(wgf_mat4_t m)
{
    return wgf_vec3_make(wgf_vec3_length(wgf_vec3_make(m.m[0], m.m[1], m.m[2])),
                             wgf_vec3_length(wgf_vec3_make(m.m[4], m.m[5], m.m[6])),
                             wgf_vec3_length(wgf_vec3_make(m.m[8], m.m[9], m.m[10])));
}

static inline wgf_quat_t wgf_mat4_get_rotation(wgf_mat4_t m)
{
    const wgf_vec3_t scale = wgf_mat4_get_scale(m);
    float r[9], trace;
    int c;
    for (c = 0; c < 3; c++) { /* the rotation alone: each column at length 1 */
        const float s = c == 0 ? scale.x : c == 1 ? scale.y : scale.z;
        r[c * 3 + 0] = s > 0.0f ? m.m[c * 4 + 0] / s : (c == 0 ? 1.0f : 0.0f);
        r[c * 3 + 1] = s > 0.0f ? m.m[c * 4 + 1] / s : (c == 1 ? 1.0f : 0.0f);
        r[c * 3 + 2] = s > 0.0f ? m.m[c * 4 + 2] / s : (c == 2 ? 1.0f : 0.0f);
    }
    trace = r[0] + r[4] + r[8];
    if (trace > 0.0f) {
        const float s = sqrtf(trace + 1.0f) * 2.0f;
        return wgf_quat_normalize(
            wgf_quat_make((r[5] - r[7]) / s, (r[6] - r[2]) / s, (r[1] - r[3]) / s, 0.25f * s));
    }
    if (r[0] > r[4] && r[0] > r[8]) {
        const float s = sqrtf(1.0f + r[0] - r[4] - r[8]) * 2.0f;
        return wgf_quat_normalize(
            wgf_quat_make(0.25f * s, (r[3] + r[1]) / s, (r[6] + r[2]) / s, (r[5] - r[7]) / s));
    }
    if (r[4] > r[8]) {
        const float s = sqrtf(1.0f + r[4] - r[0] - r[8]) * 2.0f;
        return wgf_quat_normalize(
            wgf_quat_make((r[3] + r[1]) / s, 0.25f * s, (r[7] + r[5]) / s, (r[6] - r[2]) / s));
    }
    {
        const float s = sqrtf(1.0f + r[8] - r[0] - r[4]) * 2.0f;
        return wgf_quat_normalize(
            wgf_quat_make((r[6] + r[2]) / s, (r[7] + r[5]) / s, 0.25f * s, (r[1] - r[3]) / s));
    }
}

/* A point moved by the whole transform, divided by w where w isn't 0, so a
 * projection maps it into clip space as wgrender's does; a direction only turned and
 * scaled. */
static inline wgf_vec3_t wgf_mat4_transform_point(wgf_mat4_t m, wgf_vec3_t p)
{
    float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    const float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
    if (w > 1e-8f || w < -1e-8f) {
        x /= w;
        y /= w;
        z /= w;
    }
    return wgf_vec3_make(x, y, z);
}

static inline wgf_vec3_t wgf_mat4_transform_direction(wgf_mat4_t m, wgf_vec3_t d)
{
    return wgf_vec3_make(m.m[0] * d.x + m.m[4] * d.y + m.m[8] * d.z, m.m[1] * d.x + m.m[5] * d.y + m.m[9] * d.z,
                             m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z);
}

/* A perspective projection: `fov_y` the vertical field of view in radians, `aspect`
 * width over height, seeing from `near` to `far` in front of the camera (both more
 * than 0), into OpenGL's clip space (z -1 at near, 1 at far). */
static inline wgf_mat4_t wgf_mat4_perspective(float fov_y, float aspect, float near_z, float far_z)
{
    const float f = 1.0f / wgf_trig_tan(fov_y * 0.5f);
    wgf_mat4_t r;
    int i;
    for (i = 0; i < 16; i++) r.m[i] = 0.0f;
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (far_z + near_z) / (near_z - far_z);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * far_z * near_z / (near_z - far_z);
    return r;
}

/* An orthographic projection of the box from left to right, bottom to top, and near
 * to far in front of the camera, into OpenGL's clip space. */
static inline wgf_mat4_t wgf_mat4_orthographic(float left, float right, float bottom, float top,
                                                              float near_z, float far_z)
{
    wgf_mat4_t r = wgf_mat4_identity();
    r.m[0] = 2.0f / (right - left);
    r.m[5] = 2.0f / (top - bottom);
    r.m[10] = -2.0f / (far_z - near_z);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(far_z + near_z) / (far_z - near_z);
    return r;
}

/* A view matrix: the world as seen from `eye` looking at `target`, with `up` as near
 * to up as can be. Where eye and target are the same, or up is along the line
 * between them, the identity. */
static inline wgf_mat4_t wgf_mat4_look_at(wgf_vec3_t eye, wgf_vec3_t target,
                                                         wgf_vec3_t up)
{
    const wgf_vec3_t back = wgf_vec3_normalize(wgf_vec3_sub(eye, target)); /* the camera's +z */
    const wgf_vec3_t right = wgf_vec3_normalize(wgf_vec3_cross(up, back));
    const wgf_vec3_t true_up = wgf_vec3_cross(back, right);
    wgf_mat4_t r = wgf_mat4_identity();
    if (wgf_vec3_dot(back, back) == 0.0f || wgf_vec3_dot(right, right) == 0.0f) return r;
    r.m[0] = right.x;
    r.m[4] = right.y;
    r.m[8] = right.z;
    r.m[1] = true_up.x;
    r.m[5] = true_up.y;
    r.m[9] = true_up.z;
    r.m[2] = back.x;
    r.m[6] = back.y;
    r.m[10] = back.z;
    r.m[12] = -wgf_vec3_dot(right, eye);
    r.m[13] = -wgf_vec3_dot(true_up, eye);
    r.m[14] = -wgf_vec3_dot(back, eye);
    return r;
}

#ifdef __cplusplus
}
#endif

#endif
