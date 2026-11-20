#include <math.h>
#include <stdio.h>

#include "wgf_collide2d.h"

/* 2D collision as plain math: each overlap's push out (its normal and depth), points in
 * shapes, segments crossing, rays and sweeps meeting, and a box moved and slid against
 * rectangles and against a grid of cells: stopped at a wall's side, landing on a floor,
 * sliding along it, and what it met. */

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
    float hit[6], out[6];
    const float square[8] = {0, 0, 10, 0, 10, 10, 0, 10}, shifted[8] = {8, 2, 18, 2, 18, 12, 8, 12};
    const float floor_and_wall[8] = {0, 100, 200, 10, 150, 0, 10, 100}; /* a floor at y 100; a wall at x 150 */
    /* a 10 by 6 grid of 16-pixel cells: the bottom row solid, and a pillar at column 6 */
    int cells[60] = {0};
    int i;
    for (i = 50; i < 60; i++) cells[i] = 1;
    cells[46] = cells[36] = 1;

    expect(wgf_collide2d_rect_rect(0, 0, 10, 10, 8, 2, 10, 10, hit, 6) && near(hit[0], -1) && near(hit[2], 2),
           "rectangles: out to the left by 2, the least way");
    expect(!wgf_collide2d_rect_rect(0, 0, 10, 10, 10, 0, 10, 10, NULL, 0), "rectangles only touching don't overlap");
    expect(wgf_collide2d_circle_circle(0, 0, 5, 8, 0, 5, hit, 6) && near(hit[0], -1) && near(hit[2], 2) && near(hit[3], 3),
           "circles: out to the left by 2, at b's nearest point");
    expect(wgf_collide2d_circle_rect(5, -2, 3, 0, 0, 10, 10, hit, 6) && near(hit[1], -1) && near(hit[2], 1),
           "a circle on a rectangle's top: up by 1");
    expect(wgf_collide2d_circle_rect(5, 1, 2, 0, 0, 10, 10, hit, 6) && near(hit[1], -1) && near(hit[2], 3),
           "a circle's center inside: out across the nearest edge");
    expect(wgf_collide2d_circle_segment(5, 1, 2, 0, 0, 10, 0, hit, 6) && near(hit[1], 1) && near(hit[2], 1),
           "a circle on a segment");
    expect(wgf_collide2d_circle_polygon(5, 12, 3, square, 8, hit, 6) && near(hit[1], 1) && near(hit[2], 1) &&
               !wgf_collide2d_circle_polygon(5, 14, 3, square, 8, NULL, 0),
           "a circle below a polygon, and clear of it");
    expect(wgf_collide2d_polygon_polygon(square, 8, shifted, 8, hit, 6) && near(hit[0], -1) && near(hit[2], 2),
           "convex polygons: out by the least overlap");
    expect(!wgf_collide2d_polygon_polygon(square, 8, shifted, 5, NULL, 0), "a polygon of too few points");

    expect(wgf_collide2d_point_in_rect(0, 0, 0, 0, 10, 10) && !wgf_collide2d_point_in_rect(10, 5, 0, 0, 10, 10),
           "a point on a rectangle's left edge is in it, on its right isn't");
    expect(wgf_collide2d_point_in_circle(1, 1, 0, 0, 2) && !wgf_collide2d_point_in_circle(2, 2, 0, 0, 2), "in a circle");
    expect(wgf_collide2d_point_in_triangle(2, 1, 0, 0, 10, 0, 0, 10) && !wgf_collide2d_point_in_triangle(8, 8, 0, 0, 10, 0, 0, 10),
           "in a triangle");
    expect(wgf_collide2d_point_in_polygon(5, 5, square, 8) && !wgf_collide2d_point_in_polygon(15, 5, square, 8),
           "in a polygon");

    expect(wgf_collide2d_segment_segment(0, 5, 10, 5, 4, 0, 4, 10, hit, 6) && near(hit[5], 0.4f) && near(hit[3], 4) &&
               near(hit[0], -1),
           "segments crossing: where, and the crossed one's normal facing the first's start");
    expect(wgf_collide2d_ray_rect(-10, 5, 20, 0, 0, 0, 10, 10, hit, 6) && near(hit[5], 0.5f) && near(hit[0], -1) &&
               near(hit[3], 0),
           "a ray meeting a rectangle's left face halfway");
    expect(!wgf_collide2d_ray_rect(-10, 5, 5, 0, 0, 0, 10, 10, NULL, 0), "and one falling short");
    expect(wgf_collide2d_ray_circle(-10, 0, 20, 0, 0, 0, 2, hit, 6) && near(hit[5], 0.4f) && near(hit[0], -1),
           "a ray meeting a circle");
    expect(wgf_collide2d_sweep_rect_rect(-20, 0, 10, 10, 20, 0, 0, 0, 10, 10, hit, 6) && near(hit[5], 0.5f) &&
               near(hit[3], -10),
           "a rectangle swept into another: where it stops");
    expect(wgf_collide2d_sweep_circle_circle(-10, 0, 1, 20, 0, 0, 0, 1, hit, 6) && near(hit[5], 0.4f),
           "a circle swept into another");

    expect(wgf_collide2d_move_rects(10, 80, 16, 16, 0, 10, floor_and_wall, 8, out, 6) && near(out[1], 84) && out[5] == 1 &&
               out[2] == 0,
           "falling onto the floor: standing on it");
    expect(wgf_collide2d_move_rects(120, 84, 16, 16, 30, 5, floor_and_wall, 8, out, 6) && near(out[0], 134) && out[3] == 1 &&
               near(out[1], 84) && out[5] == 1,
           "walking into the wall: stopped at it, still on the floor");
    expect(wgf_collide2d_move_rects(10, 60, 16, 16, 5, 5, floor_and_wall, 8, out, 6) && near(out[0], 15) && near(out[1], 65) &&
               out[2] + out[3] + out[4] + out[5] == 0,
           "in the open: moved all the way");
    expect(wgf_collide2d_move_rects(140, 90, 16, 16, 5, 0, floor_and_wall, 8, out, 6) && near(out[0], 140),
           "starting inside a solid: left there");
    expect(!wgf_collide2d_move_rects(0, 0, 1, 1, 1, 1, floor_and_wall, 8, NULL, 0), "no out: refused");

    expect(wgf_collide2d_move_cells(20, 50, 16, 16, 0, 40, 16, cells, 60, 10, out, 6) && near(out[1], 64) && out[5] == 1,
           "cells: falling onto the bottom row");
    expect(wgf_collide2d_move_cells(60, 64, 16, 16, 40, 0, 16, cells, 60, 10, out, 6) && near(out[0], 80) && out[3] == 1,
           "walking into the pillar: stopped at its left side");
    expect(wgf_collide2d_move_cells(120, 64, 16, 16, -40, 0, 16, cells, 60, 10, out, 6) && near(out[0], 112) && out[2] == 1,
           "and from the right");
    expect(wgf_collide2d_move_cells(150, 10, 16, 16, 30, 0, 16, cells, 60, 10, out, 6) && near(out[0], 180) && out[3] == 0,
           "off the grid's edge: open");
    return failures == 0 ? 0 : 1;
}
