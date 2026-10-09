#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_mat4.h"
#include "wgf_trig.h"
#include "wgf_quat.h"
#include "wgf_vec2.h"
#include "wgf_vec3.h"
#include "wgf_vec4.h"

/* The layout every binding relies on, and the operations' results. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-5f;
}

static int near3(wgf_vec3_t v, float x, float y, float z)
{
    return near(v.x, x) && near(v.y, y) && near(v.z, z);
}

int main(void)
{
    const float pi = 3.14159265f;
    wgf_quat_t quarter = wgf_quat_from_axis_angle(wgf_vec3_make(0, 0, 1), pi / 2);
    wgf_quat_t half;

    /* layout */
    expect(offsetof(wgf_vec2_t, y) == sizeof(float), "vec2: y follows x");
    expect(offsetof(wgf_vec3_t, z) == 2 * sizeof(float), "vec3: z third");
    expect(offsetof(wgf_vec4_t, w) == 3 * sizeof(float), "vec4: w fourth");
    expect(offsetof(wgf_quat_t, w) == 3 * sizeof(float), "quat: w fourth");

    /* vectors */
    expect(near3(wgf_vec3_add(wgf_vec3_make(1, 2, 3), wgf_vec3_make(4, 5, 6)), 5, 7, 9), "add");
    expect(near3(wgf_vec3_sub(wgf_vec3_make(4, 5, 6), wgf_vec3_make(1, 2, 3)), 3, 3, 3), "sub");
    expect(near3(wgf_vec3_scale(wgf_vec3_make(1, 2, 3), 2), 2, 4, 6), "scale");
    expect(near(wgf_vec3_dot(wgf_vec3_make(1, 2, 3), wgf_vec3_make(4, 5, 6)), 32), "dot");
    expect(near(wgf_vec2_length(wgf_vec2_make(3, 4)), 5), "length");
    expect(near(wgf_vec4_length(wgf_vec4_normalize(wgf_vec4_make(1, 2, 3, 4))), 1), "normalize to length 1");
    expect(near3(wgf_vec3_normalize(wgf_vec3_make(0, 0, 0)), 0, 0, 0), "a zero vector stays zero");
    expect(near3(wgf_vec3_lerp(wgf_vec3_make(0, 0, 0), wgf_vec3_make(10, 20, 30), 0.25f), 2.5f, 5, 7.5f), "lerp");
    expect(near3(wgf_vec3_cross(wgf_vec3_make(1, 0, 0), wgf_vec3_make(0, 1, 0)), 0, 0, 1), "x cross y is z");

    /* quaternions */
    expect(near3(wgf_quat_rotate(wgf_quat_identity(), wgf_vec3_make(1, 2, 3)), 1, 2, 3), "identity rotates nothing");
    expect(near3(wgf_quat_rotate(quarter, wgf_vec3_make(1, 0, 0)), 0, 1, 0), "a quarter turn about z takes x to y");
    expect(near3(wgf_quat_rotate(wgf_quat_mul(quarter, quarter), wgf_vec3_make(1, 0, 0)), -1, 0, 0),
           "two quarter turns make a half");
    expect(near3(wgf_quat_rotate(wgf_quat_conjugate(quarter), wgf_vec3_make(0, 1, 0)), 1, 0, 0),
           "the conjugate turns back");
    /* halfway through a quarter turn: an eighth. (Not a half turn: at exactly 180 degrees
       both ways round are as short, and which one comes back hangs on the sign of a
       rounding error, which an optimized build rounds the other way.) */
    half = wgf_quat_slerp(wgf_quat_identity(), quarter, 0.5f);
    expect(near3(wgf_quat_rotate(half, wgf_vec3_make(1, 0, 0)), 0.70710678f, 0.70710678f, 0),
           "slerp halfway through a quarter turn");
    expect(near(wgf_quat_dot(wgf_quat_normalize(wgf_quat_make(0, 0, 0, 0)), wgf_quat_identity()), 1),
           "a zero quaternion normalizes to the identity");
    expect(near(wgf_quat_from_axis_angle(wgf_vec3_make(0, 0, 0), 1).w, 1), "a zero axis gives the identity");

    /* three angles: x, then y, then z, about the fixed axes */
    expect(near3(wgf_quat_rotate(wgf_quat_from_euler(wgf_vec3_make(0, 0, pi / 2)), wgf_vec3_make(1, 0, 0)), 0, 1, 0),
           "euler z: a quarter turn takes x to y");
    expect(near3(wgf_quat_rotate(wgf_quat_from_euler(wgf_vec3_make(pi / 2, 0, pi / 2)), wgf_vec3_make(0, 1, 0)), 0, 0, 1),
           "euler x first, then z: y goes to z, and z stays");
    {
        const wgf_vec3_t angles = wgf_vec3_make(0.3f, -0.7f, 2.1f);
        const wgf_vec3_t back = wgf_quat_to_euler(wgf_quat_from_euler(angles));
        expect(near3(back, 0.3f, -0.7f, 2.1f), "to_euler undoes from_euler");
        expect(near3(wgf_quat_to_euler(wgf_quat_identity()), 0, 0, 0), "the identity is no angles");
    }

    /* 4 by 4 matrices */
    {
        const wgf_quat_t turn = wgf_quat_from_euler(wgf_vec3_make(0.4f, -1.1f, 0.7f));
        const wgf_mat4_t m = wgf_mat4_from_trs(wgf_vec3_make(1, 2, 3), turn, wgf_vec3_make(2, 3, 4));
        const wgf_mat4_t inv = wgf_mat4_invert(m);
        const wgf_mat4_t product = wgf_mat4_mul(m, inv);
        const wgf_quat_t back = wgf_mat4_get_rotation(m);
        const wgf_mat4_t one = wgf_mat4_identity();
        int i, identity = 1, fallback = 1;
        for (i = 0; i < 16; i++) {
            if (fabsf(product.m[i] - one.m[i]) >= 1e-4f) identity = 0;
        }
        expect(identity, "a matrix times its inverse is the identity");
        expect(near(wgf_mat4_determinant(m), 24), "the determinant of a turn and a scale is the scales' product");
        expect(near3(wgf_mat4_get_translation(m), 1, 2, 3) && near3(wgf_mat4_get_scale(m), 2, 3, 4),
               "translation and scale back out of from_trs");
        expect(fabsf(fabsf(wgf_quat_dot(back, turn)) - 1) < 1e-5f, "and the rotation (the same turn, either sign)");
        expect(near3(wgf_mat4_transform_point(m, wgf_vec3_make(0, 0, 0)), 1, 2, 3), "a point at the origin moves");
        expect(near3(wgf_mat4_transform_direction(m, wgf_vec3_make(0, 0, 0)), 0, 0, 0), "a direction doesn't");
        {
            const wgf_mat4_t t = wgf_mat4_transpose(m);
            expect(near(t.m[1], m.m[4]) && near(t.m[12], m.m[3]), "transpose");
        }
        for (i = 0; i < 16; i++) {
            const wgf_mat4_t flat = wgf_mat4_from_trs(wgf_vec3_make(0, 0, 0), wgf_quat_identity(), wgf_vec3_make(1, 0, 1));
            const wgf_mat4_t none = wgf_mat4_invert(flat);
            const wgf_mat4_t identity_matrix = wgf_mat4_identity();
            if (none.m[i] != identity_matrix.m[i]) fallback = 0;
        }
        expect(fallback, "a matrix with no inverse inverts to the identity");
        {
            const wgf_mat4_t p = wgf_mat4_perspective(1.0f, 1.0f, 1.0f, 10.0f);
            expect(near(wgf_mat4_transform_point(p, wgf_vec3_make(0, 0, -1)).z, -1) &&
                       near(wgf_mat4_transform_point(p, wgf_vec3_make(0, 0, -10)).z, 1),
                   "a projection divides by w: near and far land on -1 and 1");
        }
        expect(near3(wgf_mat4_transform_point(wgf_mat4_mul(wgf_mat4_from_trs(wgf_vec3_make(5, 0, 0), wgf_quat_identity(), wgf_vec3_make(1, 1, 1)),
                                                           wgf_mat4_from_trs(wgf_vec3_make(0, 0, 0), wgf_quat_identity(), wgf_vec3_make(2, 2, 2))),
                                              wgf_vec3_make(1, 0, 0)), 7, 0, 0),
               "mul(a, b) applies b first: scaled, then moved");
    }
    {
        /* perspective: the near plane's middle at z -1, the far one's at 1 */
        const wgf_mat4_t p = wgf_mat4_perspective(pi / 2, 2.0f, 1.0f, 10.0f);
        float clip[4];
        int row;
        for (row = 0; row < 4; row++) clip[row] = p.m[8 + row] * -1.0f + p.m[12 + row];
        expect(near(clip[2] / clip[3], -1), "a point on the near plane is at depth -1");
        for (row = 0; row < 4; row++) clip[row] = p.m[8 + row] * -10.0f + p.m[12 + row];
        expect(near(clip[2] / clip[3], 1), "and one on the far plane at 1");
        for (row = 0; row < 4; row++) clip[row] = p.m[row] * 2.0f + p.m[8 + row] * -1.0f + p.m[12 + row];
        expect(near(clip[0] / clip[3], 1), "a 90 degree view, twice as wide: x of 2 at depth 1 is the right edge");
    }
    {
        const wgf_mat4_t o = wgf_mat4_orthographic(-2, 2, -1, 1, 1, 9);
        expect(near3(wgf_mat4_transform_point(o, wgf_vec3_make(2, 1, -9)), 1, 1, 1), "orthographic: the far corner");
        expect(near3(wgf_mat4_transform_point(o, wgf_vec3_make(-2, -1, -1)), -1, -1, -1), "and the near one");
    }
    {
        /* a camera at (0, 0, 5) looking at the origin: the origin is 5 in front, down -z */
        const wgf_mat4_t view = wgf_mat4_look_at(wgf_vec3_make(0, 0, 5), wgf_vec3_make(0, 0, 0), wgf_vec3_make(0, 1, 0));
        const wgf_mat4_t side = wgf_mat4_look_at(wgf_vec3_make(5, 0, 0), wgf_vec3_make(0, 0, 0), wgf_vec3_make(0, 1, 0));
        expect(near3(wgf_mat4_transform_point(view, wgf_vec3_make(0, 0, 0)), 0, 0, -5), "look_at: the target ahead");
        expect(near3(wgf_mat4_transform_point(side, wgf_vec3_make(0, 0, 0)), 0, 0, -5) &&
                   near3(wgf_mat4_transform_point(side, wgf_vec3_make(0, 1, 0)), 0, 1, -5),
               "from the side too, up kept up");
        const wgf_mat4_t same = wgf_mat4_look_at(wgf_vec3_make(1, 1, 1), wgf_vec3_make(1, 1, 1), wgf_vec3_make(0, 1, 0));
        expect(near(same.m[0], 1), "the eye on the target: the identity");
    }
    {
        /* look_rotation turns -z to forward and keeps y up */
        const wgf_quat_t q = wgf_quat_look_rotation(wgf_vec3_make(1, 0, 0), wgf_vec3_make(0, 1, 0));
        const wgf_quat_t down = wgf_quat_look_rotation(wgf_vec3_make(0, -1, -1), wgf_vec3_make(0, 1, 0));
        expect(near3(wgf_quat_rotate(q, wgf_vec3_make(0, 0, -1)), 1, 0, 0), "look_rotation: -z to forward");
        expect(near3(wgf_quat_rotate(q, wgf_vec3_make(0, 1, 0)), 0, 1, 0), "with y still up");
        expect(near3(wgf_quat_rotate(down, wgf_vec3_make(0, 0, -1)), 0, -0.70710678f, -0.70710678f), "looking down and ahead");
        expect(near(wgf_quat_look_rotation(wgf_vec3_make(0, 1, 0), wgf_vec3_make(0, 1, 0)).w, 1),
               "forward along up: the identity");
    }

    {
        /* the trigonometry the rotations use gives the same bits on every target: a hash of its
           results over a sweep of angles, held to one number natively, under Wine and MSVC, and in
           a browser (glibc's sinf and Emscripten's differ on these); and each within an ulp of the
           C library's */
        uint32_t hash = 2166136261u;
        int worst = 0, i, k;
        for (i = -200000; i <= 200000; i += 7) {
            const float a = (float)i * 1e-4f; /* -20..20 */
            const float mine[5] = {wgf_trig_sin(a), wgf_trig_cos(a), wgf_trig_atan2(a, 0.7f), wgf_trig_asin(a / 20.0f),
                                   wgf_trig_acos(a / 20.0f)};
            const float theirs[5] = {sinf(a), cosf(a), atan2f(a, 0.7f), asinf(a / 20.0f), acosf(a / 20.0f)};
            for (k = 0; k < 5; k++) {
                int32_t x, y, apart;
                uint32_t u;
                memcpy(&u, &mine[k], sizeof(u));
                hash = (hash ^ u) * 16777619u;
                memcpy(&x, &mine[k], sizeof(x));
                memcpy(&y, &theirs[k], sizeof(y));
                apart = (x < 0) != (y < 0) ? (mine[k] == theirs[k] ? 0 : 1000) : (x > y ? x - y : y - x);
                if (apart > worst) worst = apart;
            }
        }
        if (getenv("WGF_TEST_SHOW")) printf("trig hash %08x, worst %d ulp\n", (unsigned)hash, worst);
        expect(hash == 0xFDD87A04u, "the rotations' trigonometry: the same bits on every target");
        expect(worst <= 1, "within an ulp of the C library's");
        {
            const double half_turn = 3.14159265358979323846;
            expect(wgf_trig_sin(0) == 0 && wgf_trig_cos(0) == 1 && wgf_trig_atan2(0, 0) == 0 &&
                       wgf_trig_asin(1) == (float)(half_turn / 2) && wgf_trig_acos(-1) == (float)half_turn &&
                       isnan(wgf_trig_asin(2)) && isnan(wgf_trig_sin(INFINITY)),
                   "its edges");
        }
    }

    return failures == 0 ? 0 : 1;
}
