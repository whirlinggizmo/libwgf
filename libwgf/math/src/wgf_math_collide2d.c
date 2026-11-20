/* 2D collision as plain math (wgf_collide2d.h): helix-legacy's Collide2D, carried whole into C,
 * its Hit2D a caller's array of floats (the normal, the depth, the point, the time). */
#include "wgf_collide2d.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

enum { NX, NY, DEPTH, X, Y, TIME, FIELDS };

/* A hit's fields into the caller's array, as many as fit. */
static void fill(float *hit, int hit_count, float nx, float ny, float depth, float x, float y, float time)
{
    const float fields[FIELDS] = {nx, ny, depth, x, y, time};
    int i;
    if (hit == NULL) return;
    for (i = 0; i < hit_count && i < FIELDS; i++) hit[i] = fields[i];
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

bool wgf_collide2d_point_in_rect(float px, float py, float rx, float ry, float rw, float rh)
{
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

bool wgf_collide2d_point_in_circle(float px, float py, float cx, float cy, float r)
{
    const float dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy < r * r;
}

bool wgf_collide2d_point_in_triangle(float px, float py, float x0, float y0, float x1, float y1, float x2, float y2)
{
    const float a = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
    const float b = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
    const float c = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
    return (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0);
}

bool wgf_collide2d_point_in_polygon(float px, float py, const float *points, int count)
{
    const int n = count / 2;
    bool inside = false;
    int i, j;
    if (points == NULL || n < 3) return false;
    for (i = 0, j = n - 1; i < n; j = i++) {
        const float xi = points[2 * i], yi = points[2 * i + 1], xj = points[2 * j], yj = points[2 * j + 1];
        if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}

bool wgf_collide2d_rect_rect(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh, float *hit,
                             int hit_count)
{
    float left, right, up, down;
    if (!(ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by)) return false;
    /* the least move out of b: along the axis it overlaps least */
    left = ax + aw - bx, right = bx + bw - ax, up = ay + ah - by, down = by + bh - ay;
    if (fminf(left, right) < fminf(up, down)) {
        if (left < right) fill(hit, hit_count, -1, 0, left, bx, clampf(ay + ah * 0.5f, by, by + bh), 0);
        else fill(hit, hit_count, 1, 0, right, bx + bw, clampf(ay + ah * 0.5f, by, by + bh), 0);
    } else {
        if (up < down) fill(hit, hit_count, 0, -1, up, clampf(ax + aw * 0.5f, bx, bx + bw), by, 0);
        else fill(hit, hit_count, 0, 1, down, clampf(ax + aw * 0.5f, bx, bx + bw), by + bh, 0);
    }
    return true;
}

bool wgf_collide2d_circle_circle(float ax, float ay, float ar, float bx, float by, float br, float *hit, int hit_count)
{
    const float dx = ax - bx, dy = ay - by, r = ar + br, d2 = dx * dx + dy * dy;
    float d, nx, ny;
    if (d2 >= r * r) return false;
    d = sqrtf(d2);
    nx = d > 0 ? dx / d : 0.0f;
    ny = d > 0 ? dy / d : -1.0f;
    fill(hit, hit_count, nx, ny, r - d, bx + nx * br, by + ny * br, 0);
    return true;
}

bool wgf_collide2d_circle_rect(float cx, float cy, float r, float rx, float ry, float rw, float rh, float *hit,
                               int hit_count)
{
    const float nx = clampf(cx, rx, rx + rw), ny = clampf(cy, ry, ry + rh);
    const float dx = cx - nx, dy = cy - ny, d2 = dx * dx + dy * dy;
    const bool inside = cx > rx && cx < rx + rw && cy > ry && cy < ry + rh;
    if (!inside && d2 >= r * r) return false;
    if (!inside) {
        const float d = sqrtf(d2);
        if (d > 0) fill(hit, hit_count, dx / d, dy / d, r - d, nx, ny, 0);
        else fill(hit, hit_count, 0, -1, r, nx, ny, 0);
    } else { /* the nearest edge: out across it */
        const float left = cx - rx, right = rx + rw - cx, top = cy - ry, bottom = ry + rh - cy;
        const float least = fminf(fminf(left, right), fminf(top, bottom));
        if (least == left) fill(hit, hit_count, -1, 0, left + r, rx, cy, 0);
        else if (least == right) fill(hit, hit_count, 1, 0, right + r, rx + rw, cy, 0);
        else if (least == top) fill(hit, hit_count, 0, -1, top + r, cx, ry, 0);
        else fill(hit, hit_count, 0, 1, bottom + r, cx, ry + rh, 0);
    }
    return true;
}

bool wgf_collide2d_circle_segment(float cx, float cy, float r, float x0, float y0, float x1, float y1, float *hit,
                                  int hit_count)
{
    const float ex = x1 - x0, ey = y1 - y0, len2 = ex * ex + ey * ey;
    const float t = clampf(len2 == 0 ? 0.0f : ((cx - x0) * ex + (cy - y0) * ey) / len2, 0.0f, 1.0f);
    const float qx = x0 + ex * t, qy = y0 + ey * t, dx = cx - qx, dy = cy - qy, d2 = dx * dx + dy * dy;
    float d;
    if (d2 >= r * r) return false;
    d = sqrtf(d2);
    if (d > 0) {
        fill(hit, hit_count, dx / d, dy / d, r - d, qx, qy, 0);
    } else { /* on it: off to its left, as it runs */
        const float len = sqrtf(len2);
        fill(hit, hit_count, len > 0 ? ey / len : 0.0f, len > 0 ? -ex / len : -1.0f, r, qx, qy, 0);
    }
    return true;
}

bool wgf_collide2d_circle_polygon(float cx, float cy, float r, const float *points, int count, float *hit, int hit_count)
{
    const int n = count / 2;
    bool inside;
    float best = FLT_MAX, qx = 0, qy = 0, ex = 0, ey = 0, d;
    int i;
    if (points == NULL || n < 3) return false;
    inside = wgf_collide2d_point_in_polygon(cx, cy, points, count);
    for (i = 0; i < n; i++) { /* the edge point nearest the center */
        const int j = (i + 1) % n;
        const float ax = points[2 * i], ay = points[2 * i + 1], sx = points[2 * j] - ax, sy = points[2 * j + 1] - ay;
        const float len2 = sx * sx + sy * sy;
        const float t = clampf(len2 == 0 ? 0.0f : ((cx - ax) * sx + (cy - ay) * sy) / len2, 0.0f, 1.0f);
        const float px = ax + sx * t, py = ay + sy * t, d2 = (cx - px) * (cx - px) + (cy - py) * (cy - py);
        if (d2 < best) best = d2, qx = px, qy = py, ex = sx, ey = sy;
    }
    if (!inside && best >= r * r) return false;
    d = sqrtf(best);
    if (d > 0) { /* outside, away from the edge; inside, toward it and across */
        fill(hit, hit_count, (inside ? qx - cx : cx - qx) / d, (inside ? qy - cy : cy - qy) / d, inside ? r + d : r - d,
             qx, qy, 0);
    } else { /* on the edge: its normal that points out of the polygon */
        const float len = sqrtf(ex * ex + ey * ey);
        float nx = len > 0 ? ey / len : 0.0f, ny = len > 0 ? -ex / len : -1.0f;
        if (wgf_collide2d_point_in_polygon(qx + nx * 1e-4f, qy + ny * 1e-4f, points, count)) nx = -nx, ny = -ny;
        fill(hit, hit_count, nx, ny, r, qx, qy, 0);
    }
    return true;
}

/* Both polygons projected onto `points`' edges' normals: false at a gap, else the least
 * overlap so far and its way out of `others`. */
static bool axes(const float *points, int n, const float *a, int an, const float *b, int bn, float *least, float *nx,
                 float *ny)
{
    int i, k;
    for (i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        const float ex = points[2 * j] - points[2 * i], ey = points[2 * j + 1] - points[2 * i + 1];
        const float len = sqrtf(ex * ex + ey * ey);
        float ax, ay, a_min = FLT_MAX, a_max = -FLT_MAX, b_min = FLT_MAX, b_max = -FLT_MAX, overlap;
        if (len == 0) continue;
        ax = -ey / len, ay = ex / len;
        for (k = 0; k < an; k++) {
            const float p = a[2 * k] * ax + a[2 * k + 1] * ay;
            a_min = fminf(a_min, p), a_max = fmaxf(a_max, p);
        }
        for (k = 0; k < bn; k++) {
            const float p = b[2 * k] * ax + b[2 * k + 1] * ay;
            b_min = fminf(b_min, p), b_max = fmaxf(b_max, p);
        }
        if (a_max <= b_min || b_max <= a_min) return false;
        overlap = fminf(a_max - b_min, b_max - a_min);
        if (overlap < *least) {
            const float sign = (a_min + a_max) < (b_min + b_max) ? -1.0f : 1.0f; /* away from b's middle */
            *least = overlap;
            *nx = ax * sign;
            *ny = ay * sign;
        }
    }
    return true;
}

bool wgf_collide2d_polygon_polygon(const float *points, int count, const float *others, int others_count, float *hit,
                                   int hit_count)
{
    const int an = count / 2, bn = others_count / 2;
    float least = FLT_MAX, nx = 0, ny = 0;
    if (points == NULL || others == NULL || an < 2 || bn < 2) return false; /* 2: a segment */
    if (!axes(points, an, points, an, others, bn, &least, &nx, &ny)) return false;
    if (!axes(others, bn, points, an, others, bn, &least, &nx, &ny)) return false;
    fill(hit, hit_count, nx, ny, least, 0, 0, 0);
    return true;
}

bool wgf_collide2d_segment_segment(float ax0, float ay0, float ax1, float ay1, float bx0, float by0, float bx1, float by1,
                                   float *hit, int hit_count)
{
    const float rx = ax1 - ax0, ry = ay1 - ay0, sx = bx1 - bx0, sy = by1 - by0, cross = rx * sy - ry * sx;
    float qx, qy, t, u, len, nx, ny;
    if (cross == 0) return false;
    qx = bx0 - ax0, qy = by0 - ay0;
    t = (qx * sy - qy * sx) / cross;
    u = (qx * ry - qy * rx) / cross;
    if (t <= 0 || t >= 1 || u <= 0 || u >= 1) return false;
    len = sqrtf(sx * sx + sy * sy);
    nx = -sy / len, ny = sx / len; /* the crossed segment's normal, facing the way a came from */
    if (nx * rx + ny * ry > 0) nx = -nx, ny = -ny;
    fill(hit, hit_count, nx, ny, 0, ax0 + rx * t, ay0 + ry * t, t);
    return true;
}

bool wgf_collide2d_ray_rect(float ox, float oy, float dx, float dy, float rx, float ry, float rw, float rh, float *hit,
                            int hit_count)
{
    float t_min = 0, t_max = 1, nx = 0, ny = 0;
    int axis;
    for (axis = 0; axis < 2; axis++) {
        const float o = axis == 0 ? ox : oy, d = axis == 0 ? dx : dy, lo = axis == 0 ? rx : ry;
        const float hi = axis == 0 ? rx + rw : ry + rh;
        float t1, t2, n = -1;
        if (d == 0) {
            if (o < lo || o > hi) return false;
            continue;
        }
        t1 = (lo - o) / d, t2 = (hi - o) / d;
        if (t1 > t2) {
            const float t = t1;
            t1 = t2, t2 = t, n = 1;
        }
        if (t1 > t_min) {
            t_min = t1;
            nx = axis == 0 ? n : 0.0f;
            ny = axis == 1 ? n : 0.0f;
        }
        if (t2 < t_max) t_max = t2;
        if (t_min > t_max) return false;
    }
    fill(hit, hit_count, nx, ny, 0, ox + dx * t_min, oy + dy * t_min, t_min);
    return true;
}

bool wgf_collide2d_ray_circle(float ox, float oy, float dx, float dy, float cx, float cy, float r, float *hit,
                              int hit_count)
{
    const float fx = ox - cx, fy = oy - cy, a = dx * dx + dy * dy;
    float b, c, disc, t, x, y;
    if (fx * fx + fy * fy < r * r) { /* from inside */
        fill(hit, hit_count, 0, 0, 0, ox, oy, 0);
        return true;
    }
    if (a == 0) return false;
    b = 2 * (fx * dx + fy * dy), c = fx * fx + fy * fy - r * r, disc = b * b - 4 * a * c;
    if (disc < 0) return false;
    t = (-b - sqrtf(disc)) / (2 * a);
    if (t < 0 || t > 1) return false;
    x = ox + dx * t, y = oy + dy * t;
    fill(hit, hit_count, (x - cx) / r, (y - cy) / r, 0, x, y, t);
    return true;
}

bool wgf_collide2d_sweep_rect_rect(float ax, float ay, float aw, float ah, float dx, float dy, float bx, float by,
                                   float bw, float bh, float *hit, int hit_count)
{
    if (ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by) {
        fill(hit, hit_count, 0, 0, 0, ax, ay, 0);
        return true;
    }
    /* a's corner as a ray against b grown by a's size (their Minkowski sum) */
    return wgf_collide2d_ray_rect(ax, ay, dx, dy, bx - aw, by - ah, bw + aw, bh + ah, hit, hit_count);
}

bool wgf_collide2d_sweep_circle_circle(float ax, float ay, float ar, float dx, float dy, float bx, float by, float br,
                                       float *hit, int hit_count)
{
    return wgf_collide2d_ray_circle(ax, ay, dx, dy, bx, by, ar + br, hit, hit_count);
}

/* ------------------------------------------------------------ move and slide ---- */

typedef struct solids_t {
    const float *rects;
    const int *cells;
    int count, columns;
    float size;
} solids_t;

/* Whether any solid overlaps the box (strictly: touching isn't overlapping). */
static bool blocked(const solids_t *s, float x, float y, float w, float h)
{
    int i;
    if (s->rects != NULL) {
        for (i = 0; i + 3 < s->count; i += 4) {
            const float *r = &s->rects[i];
            if (x < r[0] + r[2] && x + w > r[0] && y < r[1] + r[3] && y + h > r[1]) return true;
        }
        return false;
    } else {
        const float eps = 1e-4f;
        const int rows = s->columns > 0 ? s->count / s->columns : 0;
        int c0 = (int)floorf(x / s->size), c1 = (int)floorf((x + w - eps) / s->size);
        int r0 = (int)floorf(y / s->size), r1 = (int)floorf((y + h - eps) / s->size), c, r;
        c0 = c0 < 0 ? 0 : c0, r0 = r0 < 0 ? 0 : r0;
        c1 = c1 >= s->columns ? s->columns - 1 : c1, r1 = r1 >= rows ? rows - 1 : r1;
        for (r = r0; r <= r1; r++) {
            for (c = c0; c <= c1; c++) {
                if (s->cells[r * s->columns + c] != 0) return true;
            }
        }
        return false;
    }
}

/* How far along one axis the box can go toward `delta` before a solid: the solids' sides
 * the way it moves, the nearest that its span on the other axis meets. */
static float travel(const solids_t *s, float x, float y, float w, float h, float delta, bool across, bool *met)
{
    float best = delta;
    int i;
    *met = false;
    if (delta == 0 || blocked(s, x, y, w, h)) return 0.0f; /* starting inside a solid: left where it is */
    if (s->rects != NULL) {
        for (i = 0; i + 3 < s->count; i += 4) {
            const float *r = &s->rects[i];
            if (across) {
                if (!(y < r[1] + r[3] && y + h > r[1])) continue;
                if (delta > 0 && r[0] >= x + w && r[0] - (x + w) < best) best = r[0] - (x + w), *met = true;
                if (delta < 0 && r[0] + r[2] <= x && r[0] + r[2] - x > best) best = r[0] + r[2] - x, *met = true;
            } else {
                if (!(x < r[0] + r[2] && x + w > r[0])) continue;
                if (delta > 0 && r[1] >= y + h && r[1] - (y + h) < best) best = r[1] - (y + h), *met = true;
                if (delta < 0 && r[1] + r[3] <= y && r[1] + r[3] - y > best) best = r[1] + r[3] - y, *met = true;
            }
        }
        return best;
    } else { /* step cell by cell the way it moves, the first solid line of cells stopping it */
        const float size = s->size, eps = 1e-4f;
        const float lead = across ? (delta > 0 ? x + w : x) : (delta > 0 ? y + h : y);
        const float lo = across ? y : x, hi = across ? y + h : x + w;
        const int rows = s->columns > 0 ? s->count / s->columns : 0;
        const int a0 = (int)floorf(lo / size), a1 = (int)floorf((hi - eps) / size);
        int line = delta > 0 ? (int)floorf((lead - eps) / size) + 1 : (int)floorf(lead / size) - 1;
        const int last = delta > 0 ? (int)floorf((lead + delta - eps) / size) : (int)floorf((lead + delta) / size);
        for (; delta > 0 ? line <= last : line >= last; line += delta > 0 ? 1 : -1) {
            int k;
            for (k = a0; k <= a1; k++) {
                const int c = across ? line : k, r = across ? k : line;
                if (c < 0 || r < 0 || c >= s->columns || r >= rows || s->cells[r * s->columns + c] == 0) continue;
                *met = true;
                return delta > 0 ? line * size - lead : (line + 1) * size - lead;
            }
        }
        return delta;
    }
}

static bool move(const solids_t *s, float x, float y, float w, float h, float dx, float dy, float *out, int out_count)
{
    float result[6] = {0};
    bool met;
    if (out == NULL || out_count <= 0) return false;
    x += travel(s, x, y, w, h, dx, true, &met);
    if (met) result[dx > 0 ? 3 : 2] = 1;
    y += travel(s, x, y, w, h, dy, false, &met);
    if (met) result[dy > 0 ? 5 : 4] = 1;
    result[0] = x, result[1] = y;
    {
        int i;
        for (i = 0; i < out_count && i < 6; i++) out[i] = result[i];
    }
    return true;
}

bool wgf_collide2d_move_rects(float x, float y, float w, float h, float dx, float dy, const float *rects, int count,
                              float *out, int out_count)
{
    const solids_t s = {rects != NULL ? rects : (const float *)"", NULL, rects != NULL ? count : 0, 0, 0};
    return move(&s, x, y, w, h, dx, dy, out, out_count);
}

bool wgf_collide2d_move_cells(float x, float y, float w, float h, float dx, float dy, float size, const int *cells,
                              int count, int columns, float *out, int out_count)
{
    static const int none[1] = {0};
    const bool grid = cells != NULL && columns > 0 && size > 0;
    const solids_t s = {NULL, grid ? cells : none, grid ? count : 0, grid ? columns : 1, grid ? size : 1.0f};
    return move(&s, x, y, w, h, dx, dy, out, out_count);
}
