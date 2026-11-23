/* 3D collision as plain math (wgf_collide3d.h): raylib's boxes, spheres, and rays, their
 * hit a caller's array of floats as wgf_collide2d's (the normal, the depth, the point, the
 * time). */
#include "wgf_collide3d.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

enum { NX, NY, NZ, DEPTH, X, Y, Z, TIME, FIELDS };

/* A hit's fields into the caller's array, as many as fit. */
static void fill(float *hit, int hit_count, const float normal[3], float depth, const float point[3], float time)
{
    const float fields[FIELDS] = {normal[0], normal[1], normal[2], depth, point[0], point[1], point[2], time};
    int i;
    if (hit == NULL) return;
    for (i = 0; i < hit_count && i < FIELDS; i++) hit[i] = fields[i];
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

bool wgf_collide3d_point_in_box(float px, float py, float pz, float x0, float y0, float z0, float x1, float y1,
                                float z1)
{
    return px >= x0 && px <= x1 && py >= y0 && py <= y1 && pz >= z0 && pz <= z1;
}

bool wgf_collide3d_point_in_sphere(float px, float py, float pz, float cx, float cy, float cz, float r)
{
    const float dx = px - cx, dy = py - cy, dz = pz - cz;
    return dx * dx + dy * dy + dz * dz <= r * r;
}

bool wgf_collide3d_box_box(float ax0, float ay0, float az0, float ax1, float ay1, float az1, float bx0, float by0,
                           float bz0, float bx1, float by1, float bz1, float *hit, int hit_count)
{
    /* the least overlap of the three axes, out of b the way a's middle lies */
    const float over[3] = {fminf(ax1, bx1) - fmaxf(ax0, bx0), fminf(ay1, by1) - fmaxf(ay0, by0),
                           fminf(az1, bz1) - fmaxf(az0, bz0)};
    const float ac[3] = {(ax0 + ax1) * 0.5f, (ay0 + ay1) * 0.5f, (az0 + az1) * 0.5f};
    const float bc[3] = {(bx0 + bx1) * 0.5f, (by0 + by1) * 0.5f, (bz0 + bz1) * 0.5f};
    const float lo[3] = {bx0, by0, bz0}, hi[3] = {bx1, by1, bz1};
    float normal[3] = {0, 0, 0}, point[3];
    int axis = 0, k;
    if (over[0] <= 0 || over[1] <= 0 || over[2] <= 0) return false;
    if (over[1] < over[axis]) axis = 1;
    if (over[2] < over[axis]) axis = 2;
    normal[axis] = ac[axis] < bc[axis] ? -1.0f : 1.0f;
    for (k = 0; k < 3; k++) point[k] = clampf(ac[k], lo[k], hi[k]);
    fill(hit, hit_count, normal, over[axis], point, 0);
    return true;
}

bool wgf_collide3d_sphere_sphere(float ax, float ay, float az, float ar, float bx, float by, float bz, float br,
                                 float *hit, int hit_count)
{
    const float dx = ax - bx, dy = ay - by, dz = az - bz, d2 = dx * dx + dy * dy + dz * dz, reach = ar + br;
    float d, normal[3] = {0, 1, 0}, point[3];
    if (d2 >= reach * reach) return false;
    d = sqrtf(d2);
    if (d > 0) normal[0] = dx / d, normal[1] = dy / d, normal[2] = dz / d;
    point[0] = bx + normal[0] * br, point[1] = by + normal[1] * br, point[2] = bz + normal[2] * br;
    fill(hit, hit_count, normal, reach - d, point, 0);
    return true;
}

bool wgf_collide3d_sphere_box(float cx, float cy, float cz, float r, float x0, float y0, float z0, float x1, float y1,
                              float z1, float *hit, int hit_count)
{
    const float c[3] = {cx, cy, cz}, lo[3] = {x0, y0, z0}, hi[3] = {x1, y1, z1};
    float point[3], normal[3] = {0, 0, 0}, d2 = 0, d;
    int k;
    for (k = 0; k < 3; k++) {
        point[k] = clampf(c[k], lo[k], hi[k]);
        d2 += (c[k] - point[k]) * (c[k] - point[k]);
    }
    if (d2 >= r * r) return false;
    if (d2 > 0) { /* its center outside: out along the way to it */
        d = sqrtf(d2);
        for (k = 0; k < 3; k++) normal[k] = (c[k] - point[k]) / d;
        fill(hit, hit_count, normal, r - d, point, 0);
    } else { /* inside: out through the nearest face */
        int axis = 0;
        float best = FLT_MAX;
        for (k = 0; k < 3; k++) {
            const float to_lo = c[k] - lo[k], to_hi = hi[k] - c[k];
            if (to_lo < best) best = to_lo, axis = k, normal[0] = normal[1] = normal[2] = 0, normal[k] = -1;
            if (to_hi < best) best = to_hi, axis = k, normal[0] = normal[1] = normal[2] = 0, normal[k] = 1;
        }
        point[axis] = normal[axis] > 0 ? hi[axis] : lo[axis];
        fill(hit, hit_count, normal, r + best, point, 0);
    }
    return true;
}

bool wgf_collide3d_ray_sphere(float ox, float oy, float oz, float dx, float dy, float dz, float cx, float cy, float cz,
                              float r, float *hit, int hit_count)
{
    const float lx = ox - cx, ly = oy - cy, lz = oz - cz;
    const float a = dx * dx + dy * dy + dz * dz, b = 2 * (lx * dx + ly * dy + lz * dz),
                c = lx * lx + ly * ly + lz * lz - r * r;
    const float zero[3] = {0, 0, 0};
    float disc, t, point[3], normal[3];
    if (a <= 0) return false;
    if (c <= 0) { /* starting inside */
        const float origin[3] = {ox, oy, oz};
        fill(hit, hit_count, zero, 0, origin, 0);
        return true;
    }
    disc = b * b - 4 * a * c;
    if (disc < 0) return false;
    t = (-b - sqrtf(disc)) / (2 * a);
    if (t < 0) return false;
    point[0] = ox + dx * t, point[1] = oy + dy * t, point[2] = oz + dz * t;
    normal[0] = (point[0] - cx) / r, normal[1] = (point[1] - cy) / r, normal[2] = (point[2] - cz) / r;
    fill(hit, hit_count, normal, 0, point, t);
    return true;
}

bool wgf_collide3d_ray_box(float ox, float oy, float oz, float dx, float dy, float dz, float x0, float y0, float z0,
                           float x1, float y1, float z1, float *hit, int hit_count)
{
    const float o[3] = {ox, oy, oz}, d[3] = {dx, dy, dz}, lo[3] = {x0, y0, z0}, hi[3] = {x1, y1, z1};
    float near_t = -FLT_MAX, far_t = FLT_MAX, normal[3] = {0, 0, 0}, point[3];
    int k, axis = -1;
    for (k = 0; k < 3; k++) { /* the slabs: each axis's entry and exit */
        if (d[k] == 0) {
            if (o[k] < lo[k] || o[k] > hi[k]) return false;
            continue;
        }
        {
            float t0 = (lo[k] - o[k]) / d[k], t1 = (hi[k] - o[k]) / d[k];
            if (t0 > t1) {
                const float swap = t0;
                t0 = t1, t1 = swap;
            }
            if (t0 > near_t) near_t = t0, axis = k;
            if (t1 < far_t) far_t = t1;
        }
        if (near_t > far_t || far_t < 0) return false;
    }
    if (near_t < 0 || axis < 0) { /* starting inside */
        fill(hit, hit_count, normal, 0, o, 0);
        return true;
    }
    normal[axis] = d[axis] > 0 ? -1.0f : 1.0f;
    for (k = 0; k < 3; k++) point[k] = o[k] + d[k] * near_t;
    fill(hit, hit_count, normal, 0, point, near_t);
    return true;
}

bool wgf_collide3d_ray_triangle(float ox, float oy, float oz, float dx, float dy, float dz, const float *points,
                                int count, float *hit, int hit_count)
{
    /* Moller-Trumbore */
    float e1[3], e2[3], p[3], s[3], q[3], det, inv, u, v, t, normal[3], point[3], len;
    const float d[3] = {dx, dy, dz};
    int k;
    if (points == NULL || count < 9) return false;
    for (k = 0; k < 3; k++) e1[k] = points[3 + k] - points[k], e2[k] = points[6 + k] - points[k];
    p[0] = d[1] * e2[2] - d[2] * e2[1], p[1] = d[2] * e2[0] - d[0] * e2[2], p[2] = d[0] * e2[1] - d[1] * e2[0];
    det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (fabsf(det) < 1e-12f) return false; /* parallel to it */
    inv = 1.0f / det;
    s[0] = ox - points[0], s[1] = oy - points[1], s[2] = oz - points[2];
    u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0 || u > 1) return false;
    q[0] = s[1] * e1[2] - s[2] * e1[1], q[1] = s[2] * e1[0] - s[0] * e1[2], q[2] = s[0] * e1[1] - s[1] * e1[0];
    v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (v < 0 || u + v > 1) return false;
    t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    if (t < 0) return false;
    normal[0] = e1[1] * e2[2] - e1[2] * e2[1], normal[1] = e1[2] * e2[0] - e1[0] * e2[2],
    normal[2] = e1[0] * e2[1] - e1[1] * e2[0];
    len = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    if (normal[0] * d[0] + normal[1] * d[1] + normal[2] * d[2] > 0) len = -len; /* facing the ray */
    for (k = 0; k < 3; k++) normal[k] /= len;
    point[0] = ox + dx * t, point[1] = oy + dy * t, point[2] = oz + dz * t;
    fill(hit, hit_count, normal, 0, point, t);
    return true;
}
