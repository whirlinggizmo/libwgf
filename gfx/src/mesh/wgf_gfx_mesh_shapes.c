#include "mesh/wgf_gfx_mesh_shapes_priv.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Generated mesh geometry (wgf_gfx_mesh_shapes_priv.h). Every shape is built from
 * vertices and triangles through one builder, which also makes each triangle face the
 * way its vertices' normals point (so no shape has to get its winding right by hand)
 * and drops degenerate ones (a sphere's poles). libwgt's (wgrender's
 * wgr_mesh_shapes). */

#define PI 3.14159265358979f

typedef struct {
    wgf_gfx_priv_mesh_shape_t *shape;
    int vertex_capacity, index_capacity;
    bool failed;
} builder_t;

static int clamp_int(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

static int vertex(builder_t *b, float px, float py, float pz, float nx, float ny, float nz, float u, float v)
{
    wgf_gfx_priv_mesh_shape_t *s = b->shape;
    if (b->failed) return 0;
    if (s->vertex_count == b->vertex_capacity) {
        const int capacity = b->vertex_capacity > 0 ? b->vertex_capacity * 2 : 64;
        float *p = realloc(s->positions, sizeof(float) * 3 * (size_t)capacity);
        if (p != NULL) s->positions = p;
        float *n = realloc(s->normals, sizeof(float) * 3 * (size_t)capacity);
        if (n != NULL) s->normals = n;
        float *t = realloc(s->uvs, sizeof(float) * 2 * (size_t)capacity);
        if (t != NULL) s->uvs = t;
        if (p == NULL || n == NULL || t == NULL) {
            b->failed = true;
            return 0;
        }
        b->vertex_capacity = capacity;
    }
    const float len = sqrtf(nx * nx + ny * ny + nz * nz);
    const float inv = len > 0.0f ? 1.0f / len : 0.0f;
    float *p = &s->positions[s->vertex_count * 3], *n = &s->normals[s->vertex_count * 3];
    p[0] = px, p[1] = py, p[2] = pz;
    n[0] = nx * inv, n[1] = ny * inv, n[2] = nz * inv;
    s->uvs[s->vertex_count * 2] = u;
    s->uvs[s->vertex_count * 2 + 1] = v;
    return s->vertex_count++;
}

/* A triangle, turned to face along its vertices' normals; skipped if it has no area. */
static void triangle(builder_t *b, int i0, int i1, int i2)
{
    wgf_gfx_priv_mesh_shape_t *s = b->shape;
    if (b->failed) return;
    const float *p0 = &s->positions[i0 * 3], *p1 = &s->positions[i1 * 3], *p2 = &s->positions[i2 * 3];
    const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    const float c[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    const float area2 = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
    float facing = 0.0f;
    if (area2 < 1e-14f) return;
    for (int k = 0; k < 3; k++) {
        facing += c[k] * (s->normals[i0 * 3 + k] + s->normals[i1 * 3 + k] + s->normals[i2 * 3 + k]);
    }
    if (facing < 0.0f) {
        const int swap = i1;
        i1 = i2;
        i2 = swap;
    }
    if (s->index_count + 3 > b->index_capacity) {
        const int capacity = b->index_capacity > 0 ? b->index_capacity * 2 : 192;
        uint32_t *indices = realloc(s->indices, sizeof(uint32_t) * (size_t)capacity);
        if (indices == NULL) {
            b->failed = true;
            return;
        }
        s->indices = indices;
        b->index_capacity = capacity;
    }
    s->indices[s->index_count++] = (uint32_t)i0;
    s->indices[s->index_count++] = (uint32_t)i1;
    s->indices[s->index_count++] = (uint32_t)i2;
}

static void quad(builder_t *b, int i0, int i1, int i2, int i3)
{
    triangle(b, i0, i1, i2);
    triangle(b, i0, i2, i3);
}

/* Rows of `columns` + 1 vertices (the seam repeated, for its texture coordinates),
 * starting at vertex `first`: each row joined to the next by quads. */
static void join_rows(builder_t *b, int first, int rows, int columns)
{
    for (int r = 0; r + 1 < rows; r++) {
        for (int c = 0; c < columns; c++) {
            const int a = first + r * (columns + 1) + c, d = a + columns + 1;
            quad(b, a, a + 1, d + 1, d);
        }
    }
}

/* A flat disc of `segments` at height y facing `up` (+1) or down (-1): texture
 * coordinates map the disc onto the unit square, as seen from its front. */
static void disc(builder_t *b, float radius, float y, float up, int segments)
{
    const int center = vertex(b, 0.0f, y, 0.0f, 0.0f, up, 0.0f, 0.5f, 0.5f);
    for (int c = 0; c <= segments; c++) {
        const float a = 2.0f * PI * (float)c / (float)segments, x = cosf(a), z = -sinf(a);
        vertex(b, radius * x, y, radius * z, 0.0f, up, 0.0f, 0.5f + 0.5f * x, 0.5f + 0.5f * z * up);
    }
    for (int c = 0; c < segments; c++) triangle(b, center, center + 1 + c, center + 2 + c);
}

static bool finish(builder_t *b)
{
    if (b->failed || b->shape->index_count == 0) {
        wgf_gfx_priv_mesh_shape_free(b->shape);
        return false;
    }
    return true;
}

static void begin(builder_t *b, wgf_gfx_priv_mesh_shape_t *out)
{
    memset(out, 0, sizeof(*out));
    *b = (builder_t){.shape = out};
}

void wgf_gfx_priv_mesh_shape_free(wgf_gfx_priv_mesh_shape_t *shape)
{
    free(shape->positions);
    free(shape->normals);
    free(shape->uvs);
    free(shape->indices);
    memset(shape, 0, sizeof(*shape));
}

bool wgf_gfx_priv_mesh_shape_plane(float width, float length, int subdivisions, wgf_gfx_priv_mesh_shape_t *out)
{
    builder_t b;
    begin(&b, out);
    if (!(width > 0.0f) || !(length > 0.0f)) return false;
    const int n = clamp_int(subdivisions, 0, WGF_GFX_PRIV_MESH_MAX_SUBDIVISIONS) + 1; /* cells each way */
    for (int j = 0; j <= n; j++) {
        for (int i = 0; i <= n; i++) {
            const float u = (float)i / (float)n, v = (float)j / (float)n;
            vertex(&b, (u - 0.5f) * width, 0.0f, (v - 0.5f) * length, 0.0f, 1.0f, 0.0f, u, v);
        }
    }
    join_rows(&b, 0, n + 1, n);
    return finish(&b);
}

bool wgf_gfx_priv_mesh_shape_cube(float width, float height, float length, wgf_gfx_priv_mesh_shape_t *out)
{
    /* per face: its normal, then the directions texture u and v run along it */
    static const float faces[6][3][3] = {
        {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},    {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, 0, 1}, {1, 0, 0}, {0, -1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}},
    };
    const float half[3] = {width * 0.5f, height * 0.5f, length * 0.5f};
    builder_t b;
    begin(&b, out);
    if (!(width > 0.0f) || !(height > 0.0f) || !(length > 0.0f)) return false;
    for (int f = 0; f < 6; f++) {
        const float *n = faces[f][0], *u = faces[f][1], *v = faces[f][2];
        const int first = b.shape->vertex_count;
        for (int corner = 0; corner < 4; corner++) {
            const float s = (corner == 1 || corner == 2) ? 1.0f : -1.0f, t = corner >= 2 ? 1.0f : -1.0f;
            float p[3];
            for (int k = 0; k < 3; k++) p[k] = (n[k] + u[k] * s + v[k] * t) * half[k];
            vertex(&b, p[0], p[1], p[2], n[0], n[1], n[2], (s + 1.0f) * 0.5f, (t + 1.0f) * 0.5f);
        }
        quad(&b, first, first + 1, first + 2, first + 3);
    }
    return finish(&b);
}

bool wgf_gfx_priv_mesh_shape_sphere(float radius, int rings, int segments, wgf_gfx_priv_mesh_shape_t *out)
{
    builder_t b;
    begin(&b, out);
    if (!(radius > 0.0f)) return false;
    rings = clamp_int(rings, WGF_GFX_PRIV_MESH_MIN_RINGS, WGF_GFX_PRIV_MESH_MAX_RINGS);
    segments = clamp_int(segments, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    for (int r = 0; r <= rings; r++) {
        const float polar = PI * (float)r / (float)rings, sp = sinf(polar), cp = cosf(polar);
        for (int c = 0; c <= segments; c++) {
            const float a = 2.0f * PI * (float)c / (float)segments;
            const float x = sp * cosf(a), y = cp, z = -sp * sinf(a);
            vertex(&b, radius * x, radius * y, radius * z, x, y, z, (float)c / (float)segments,
                   (float)r / (float)rings);
        }
    }
    join_rows(&b, 0, rings + 1, segments);
    return finish(&b);
}

bool wgf_gfx_priv_mesh_shape_cylinder(float radius, float height, int segments, wgf_gfx_priv_mesh_shape_t *out)
{
    builder_t b;
    begin(&b, out);
    if (!(radius > 0.0f) || !(height > 0.0f)) return false;
    segments = clamp_int(segments, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    for (int r = 0; r < 2; r++) { /* the side: top row, then bottom */
        for (int c = 0; c <= segments; c++) {
            const float a = 2.0f * PI * (float)c / (float)segments, x = cosf(a), z = -sinf(a);
            vertex(&b, radius * x, r == 0 ? height * 0.5f : -height * 0.5f, radius * z, x, 0.0f, z,
                   (float)c / (float)segments, (float)r);
        }
    }
    join_rows(&b, 0, 2, segments);
    disc(&b, radius, height * 0.5f, 1.0f, segments);
    disc(&b, radius, -height * 0.5f, -1.0f, segments);
    return finish(&b);
}

bool wgf_gfx_priv_mesh_shape_cone(float radius, float height, int segments, wgf_gfx_priv_mesh_shape_t *out)
{
    builder_t b;
    begin(&b, out);
    if (!(radius > 0.0f) || !(height > 0.0f)) return false;
    segments = clamp_int(segments, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    /* the side: per segment, a tip vertex (its normal halfway round) and base vertices */
    for (int c = 0; c < segments; c++) {
        const float a0 = 2.0f * PI * (float)c / (float)segments, a1 = 2.0f * PI * (float)(c + 1) / (float)segments;
        const float am = 0.5f * (a0 + a1);
        const int tip = vertex(&b, 0.0f, height * 0.5f, 0.0f, cosf(am) * height, radius, -sinf(am) * height,
                               ((float)c + 0.5f) / (float)segments, 0.0f);
        const int base0 = vertex(&b, radius * cosf(a0), -height * 0.5f, -radius * sinf(a0), cosf(a0) * height,
                                 radius, -sinf(a0) * height, (float)c / (float)segments, 1.0f);
        const int base1 = vertex(&b, radius * cosf(a1), -height * 0.5f, -radius * sinf(a1), cosf(a1) * height,
                                 radius, -sinf(a1) * height, (float)(c + 1) / (float)segments, 1.0f);
        triangle(&b, tip, base0, base1);
    }
    disc(&b, radius, -height * 0.5f, -1.0f, segments);
    return finish(&b);
}

bool wgf_gfx_priv_mesh_shape_capsule(float radius, float height, int rings, int segments,
                                     wgf_gfx_priv_mesh_shape_t *out)
{
    builder_t b;
    begin(&b, out);
    if (!(radius > 0.0f) || !(height > 0.0f)) return false;
    const int half_rings = clamp_int(rings, WGF_GFX_PRIV_MESH_MIN_RINGS, WGF_GFX_PRIV_MESH_MAX_RINGS) / 2 > 0
                               ? clamp_int(rings, WGF_GFX_PRIV_MESH_MIN_RINGS, WGF_GFX_PRIV_MESH_MAX_RINGS) / 2
                               : 1;
    segments = clamp_int(segments, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    const float straight = height > 2.0f * radius ? height * 0.5f - radius : 0.0f; /* half the middle */
    const float total = PI * radius + 2.0f * straight;                             /* pole to pole */
    /* the top half-sphere's rows, then the bottom's: the two equators join as the side */
    for (int h = 0; h < 2; h++) {
        for (int r = 0; r <= half_rings; r++) {
            const float polar = 0.5f * PI * ((float)h + (float)r / (float)half_rings);
            const float sp = sinf(polar), cp = cosf(polar), offset = h == 0 ? straight : -straight;
            const float along = radius * polar + (h == 1 ? 2.0f * straight : 0.0f); /* for v */
            for (int c = 0; c <= segments; c++) {
                const float a = 2.0f * PI * (float)c / (float)segments;
                const float x = sp * cosf(a), z = -sp * sinf(a);
                vertex(&b, radius * x, radius * cp + offset, radius * z, x, cp, z, (float)c / (float)segments,
                       along / total);
            }
        }
    }
    join_rows(&b, 0, 2 * (half_rings + 1), segments);
    return finish(&b);
}

bool wgf_gfx_priv_mesh_shape_torus(float radius, float thickness, int rings, int segments,
                                   wgf_gfx_priv_mesh_shape_t *out)
{
    builder_t b;
    begin(&b, out);
    if (!(radius > 0.0f) || !(thickness > 0.0f)) return false;
    /* rings around the ring, segments around the tube */
    rings = clamp_int(rings, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    segments = clamp_int(segments, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    for (int i = 0; i <= rings; i++) {
        const float a = 2.0f * PI * (float)i / (float)rings, ca = cosf(a), sa = -sinf(a);
        for (int j = 0; j <= segments; j++) {
            const float t = 2.0f * PI * (float)j / (float)segments, ct = cosf(t), st = sinf(t);
            const float nx = ca * ct, ny = st, nz = sa * ct;
            vertex(&b, radius * ca + thickness * nx, thickness * ny, radius * sa + thickness * nz, nx, ny, nz,
                   (float)i / (float)rings, (float)j / (float)segments);
        }
    }
    join_rows(&b, 0, rings + 1, segments);
    return finish(&b);
}
