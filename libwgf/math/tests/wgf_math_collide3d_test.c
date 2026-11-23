#include <math.h>
#include <stdio.h>

#include "wgf_collide3d.h"

/* 3D collision as plain math: points, box and sphere overlaps with their normals and depths,
 * and rays against a sphere, a box, and a triangle, missing, meeting, and starting inside. */

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
    return fabsf(a - b) < 1e-4f;
}

int main(void)
{
    float hit[8];
    const float triangle[9] = {0, 0, 5, 4, 0, 5, 0, 4, 5};
    expect(wgf_collide3d_point_in_box(1, 1, 1, 0, 0, 0, 2, 2, 2) &&
               !wgf_collide3d_point_in_box(3, 1, 1, 0, 0, 0, 2, 2, 2),
           "a point in a box, and out");
    expect(wgf_collide3d_point_in_sphere(0, 1, 0, 0, 0, 0, 1) && !wgf_collide3d_point_in_sphere(0, 1.1f, 0, 0, 0, 0, 1),
           "a point in a sphere, and out");
    expect(wgf_collide3d_box_box(0, 0, 0, 2, 2, 2, 1.5f, 0, 0, 3.5f, 2, 2, hit, 8) && near(hit[0], -1) &&
               near(hit[3], 0.5f),
           "boxes: out along the least overlap, toward a's side");
    expect(!wgf_collide3d_box_box(0, 0, 0, 1, 1, 1, 1, 0, 0, 2, 1, 1, NULL, 0), "boxes touching: apart");
    expect(wgf_collide3d_sphere_sphere(0, 0, 0, 1, 0, 0, 1.5f, 1, hit, 8) && near(hit[2], -1) && near(hit[3], 0.5f) &&
               near(hit[6], 0.5f),
           "spheres: out of the second, as deep as they overlap, at its surface");
    expect(wgf_collide3d_sphere_box(0, 3, 0, 1.5f, -1, -1, -1, 1, 2, 1, hit, 8) && near(hit[1], 1) &&
               near(hit[3], 0.5f) && near(hit[5], 2),
           "a sphere over a box: out of its top");
    expect(wgf_collide3d_sphere_box(0, 1.8f, 0, 0.5f, -1, -1, -1, 1, 2, 1, hit, 8) && near(hit[1], 1) &&
               near(hit[3], 0.7f),
           "its center inside: out through the nearest face");
    expect(wgf_collide3d_ray_sphere(-5, 0, 0, 2, 0, 0, 0, 0, 0, 1, hit, 8) && near(hit[7], 2) && near(hit[4], -1) &&
               near(hit[0], -1),
           "a ray meeting a sphere: two of its direction's lengths along, its normal facing back");
    expect(!wgf_collide3d_ray_sphere(-5, 2, 0, 1, 0, 0, 0, 0, 0, 1, NULL, 0) &&
               !wgf_collide3d_ray_sphere(5, 0, 0, 1, 0, 0, 0, 0, 0, 1, NULL, 0),
           "beside it, or pointed away: none");
    expect(wgf_collide3d_ray_sphere(0, 0, 0, 1, 0, 0, 0, 0, 0, 1, hit, 8) && near(hit[7], 0),
           "starting inside: at once");
    expect(wgf_collide3d_ray_box(0, 5, 0, 0, -1, 0, -1, -1, -1, 1, 1, 1, hit, 8) && near(hit[7], 4) &&
               near(hit[1], 1) && near(hit[5], 1),
           "a ray down onto a box's top");
    expect(!wgf_collide3d_ray_box(3, 5, 0, 0, -1, 0, -1, -1, -1, 1, 1, 1, NULL, 0), "beside it: none");
    expect(wgf_collide3d_ray_triangle(1, 1, 0, 0, 0, 1, triangle, 9, hit, 8) && near(hit[7], 5) && near(hit[2], -1) &&
               near(hit[6], 5),
           "a ray through a triangle: its normal facing the ray");
    expect(!wgf_collide3d_ray_triangle(3, 3, 0, 0, 0, 1, triangle, 9, NULL, 0) &&
               !wgf_collide3d_ray_triangle(1, 1, 0, 1, 0, 0, triangle, 9, NULL, 0) &&
               !wgf_collide3d_ray_triangle(1, 1, 0, 0, 0, 1, triangle, 6, NULL, 0),
           "past its edge, parallel to it, too few points: none");
    if (failures == 0) printf("wgf_math_collide3d_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
