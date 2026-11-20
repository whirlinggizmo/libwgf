#include <math.h>
#include <stdio.h>

#include "material/wgf_gfx_material_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "mesh/wgf_gfx_mesh_record_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_actor.h"
#include "wgf_resource.h"
#include "wgf_texture.h"

/* Generated meshes and materials, headless: each shape's geometry (closed, its normals
 * unit length and facing out, its triangles wound counterclockwise as seen from where
 * they face, its tangents perpendicular to its normals), shared by its parameters, and
 * its material; and the materials' parameters by name, their kinds, refusals, and
 * defaults. */

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

/* Every vertex's normal unit length and its tangent perpendicular to it, and every
 * triangle facing the way its corners' normals point. */
static void expect_shape(wgf_mesh_t mesh, const char *what)
{
    int vertex_count, index_count, i, bad = 0;
    const float *v = wgf_gfx_priv_mesh_get_vertices(mesh, &vertex_count);
    const uint32_t *ix = wgf_gfx_priv_mesh_get_indices(mesh, &index_count);
    if (v == NULL || ix == NULL || vertex_count == 0 || index_count % 3 != 0) {
        printf("FAIL: %s: no geometry\n", what);
        failures++;
        return;
    }
    for (i = 0; i < vertex_count; i++) {
        const float *n = &v[i * WGF_GFX_PRIV_MESH_VERTEX_FLOATS + 3], *t = &v[i * WGF_GFX_PRIV_MESH_VERTEX_FLOATS + 10];
        if (!near(n[0] * n[0] + n[1] * n[1] + n[2] * n[2], 1.0f) ||
            fabsf(n[0] * t[0] + n[1] * t[1] + n[2] * t[2]) > 1e-3f) {
            bad++;
        }
    }
    for (i = 0; i < index_count; i += 3) {
        const float *a = &v[ix[i] * WGF_GFX_PRIV_MESH_VERTEX_FLOATS];
        const float *b = &v[ix[i + 1] * WGF_GFX_PRIV_MESH_VERTEX_FLOATS];
        const float *c = &v[ix[i + 2] * WGF_GFX_PRIV_MESH_VERTEX_FLOATS];
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float cross[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                e1[0] * e2[1] - e1[1] * e2[0]};
        if (cross[0] * (a[3] + b[3] + c[3]) + cross[1] * (a[4] + b[4] + c[4]) + cross[2] * (a[5] + b[5] + c[5]) <=
            0.0f) {
            bad++;
        }
    }
    if (bad > 0) {
        printf("FAIL: %s: %d bad vertices or triangles\n", what, bad);
        failures++;
    }
}

int main(void)
{
    wgf_mesh_t cube, sphere, plane;
    wgf_material_t material, own;
    wgf_vec3_t lo, hi;
    int count;

    /* meshes, before gfx has started: made at once, uploaded when first drawn */
    cube = wgf_mesh_create_cube(2.0f, 1.0f, 4.0f);
    expect(cube != 0 && wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_READY, "a cube, ready at once");
    expect(wgf_gfx_priv_mesh_get_bounds(cube, &lo, &hi) && near(lo.x, -1.0f) && near(hi.y, 0.5f) && near(hi.z, 2.0f),
           "its box: centered on the origin");
    wgf_gfx_priv_mesh_get_vertices(cube, &count);
    expect(count == 24, "a cube's 24 corners: each face its own");
    expect_shape(cube, "cube");
    expect(wgf_mesh_create_cube(2.0f, 1.0f, 4.0f) == cube, "the same cube again: the same mesh");
    expect(wgf_resource_release(cube), "one reference let go");
    expect(wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_READY, "the other kept it");
    expect(wgf_mesh_create_cube(2.0f, 1.0f, 4.5f) != cube, "another size: another mesh");
    expect(wgf_mesh_create_cube(0.0f, 1.0f, 1.0f) == 0, "a size of 0: none (logged)");

    sphere = wgf_mesh_create_sphere(1.0f, 1, 1000);
    expect(sphere != 0 && sphere == wgf_mesh_create_sphere(1.0f, 2, 512), "counts clamped: rings 2, segments 512");
    expect_shape(sphere, "sphere");
    plane = wgf_mesh_create_plane(10.0f, 4.0f, 3);
    expect_shape(plane, "plane");
    expect_shape(wgf_mesh_create_cylinder(1.0f, 2.0f, 16), "cylinder");
    expect_shape(wgf_mesh_create_cone(1.0f, 2.0f, 16), "cone");
    expect_shape(wgf_mesh_create_capsule(0.5f, 3.0f, 8, 16), "capsule");
    expect_shape(wgf_mesh_create_torus(2.0f, 0.5f, 24, 12), "torus");

    /* a mesh of the program's own triangles: a strip of two quads, its normals made smooth */
    {
        const float strip[] = {0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 2, 0.5f, 0, 2, 0.5f, 1};
        const float uvs[] = {0, 0, 0, 1, 0.5f, 0, 0.5f, 1, 1, 0, 1, 1};
        const int indices[] = {0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5};
        const int past[] = {0, 1, 9};
        const wgf_mesh_t road = wgf_mesh_create_triangles(strip, 18, NULL, 0, uvs, 12, indices, 12);
        const float *v;
        expect(road != 0 && wgf_resource_get_status(road) == WGF_RESOURCE_STATUS_READY &&
                   wgf_mesh_create_triangles(strip, 18, NULL, 0, uvs, 12, indices, 12) != road,
               "the program's own triangles: ready at once, and its own, never shared");
        expect_shape(road, "triangles");
        v = wgf_gfx_priv_mesh_get_vertices(road, &count);
        expect(count == 6 && near(v[3 + 1], 1.0f) && v[2 * WGF_GFX_PRIV_MESH_VERTEX_FLOATS + 4] < 0.99f &&
                   near(v[1 * WGF_GFX_PRIV_MESH_VERTEX_FLOATS + 7], 1.0f),
               "its corners as given, a smooth normal where the strip bends, its texture coordinates");
        expect(wgf_gfx_priv_mesh_get_bounds(road, &lo, &hi) && near(hi.x, 2.0f) && near(hi.y, 0.5f),
               "its box from its corners");
        expect(wgf_mesh_create_triangles(strip, 9, NULL, 0, NULL, 0, NULL, 0) != 0, "three corners in threes: one triangle");
        expect(wgf_mesh_create_triangles(strip, 18, NULL, 0, NULL, 0, past, 3) == 0 &&
                   wgf_mesh_create_triangles(strip, 17, NULL, 0, NULL, 0, NULL, 0) == 0 &&
                   wgf_mesh_create_triangles(strip, 18, NULL, 0, uvs, 10, indices, 12) == 0,
               "an index past the corners, or counts that don't fit: none (logged)");
    }

    /* a heightmap: three corners a row along x, two rows along z, a ridge up the middle */
    {
        const float heights[6] = {0, 1, 0, 0, 1, 0};
        const wgf_mesh_t land = wgf_mesh_create_heightmap(heights, 6, 3, 2.0f, 2.0f, 4.0f);
        float positions[18], normals[18], uvs[12], colors[24];
        int index_count;
        expect(land != 0 && wgf_resource_get_status(land) == WGF_RESOURCE_STATUS_READY &&
                   wgf_mesh_get_vertex_count(land) == 6 && wgf_gfx_priv_mesh_get_indices(land, &index_count) != NULL &&
                   index_count == 12,
               "a heightmap: a corner a height, two triangles a cell");
        expect_shape(land, "heightmap");
        expect(wgf_mesh_get_positions(land, positions, 18) == 18 && near(positions[0], -1) && near(positions[2], -2) &&
                   near(positions[3], 0) && near(positions[4], 2) && near(positions[5], -2) &&
                   near(positions[15], 1) && near(positions[16], 0) && near(positions[17], 2),
               "its corners: centered in x and z, each height times the height");
        expect(wgf_mesh_get_normals(land, normals, 18) == 18 && fabsf(normals[0] + 0.8944f) < 1e-3f &&
                   fabsf(normals[1] - 0.4472f) < 1e-3f && near(normals[2], 0),
               "a corner's normal, smooth: off the slope beside it");
        expect(wgf_mesh_get_uvs(land, uvs, 12) == 12 && near(uvs[0], 0) && near(uvs[2], 0.5f) && near(uvs[10], 1) &&
                   near(uvs[11], 1),
               "its texture coordinates span it once");
        expect(wgf_mesh_get_colors(land, colors, 24) == 24 && near(colors[0], 1) && near(colors[23], 1),
               "white, until colored");
        expect(wgf_gfx_priv_mesh_get_bounds(land, &lo, &hi) && near(lo.x, -1) && near(lo.y, 0) && near(hi.y, 2) &&
                   near(hi.z, 2),
               "its box");
        expect(wgf_mesh_get_positions(land, positions, 5) == 3 && wgf_mesh_get_positions(land, NULL, 18) == 0 &&
                   wgf_mesh_get_positions(cube, positions, 18) == 18,
               "read: whole corners that fit, none into nothing, a generated shape's too");
        expect(wgf_mesh_create_heightmap(heights, 6, 1, 2, 2, 2) == 0 &&
                   wgf_mesh_create_heightmap(heights, 5, 3, 2, 2, 2) == 0 &&
                   wgf_mesh_create_heightmap(heights, 3, 3, 2, 2, 2) == 0 &&
                   wgf_mesh_create_heightmap(heights, 6, 3, 0, 2, 2) == 0 &&
                   wgf_mesh_create_heightmap(NULL, 6, 3, 2, 2, 2) == 0,
               "one column, part of a row, one row, no width, no heights: none (logged)");

        /* changed in place: flattened and lifted, its normals as they were until made again */
        {
            int i;
            for (i = 0; i < 6; i++) positions[i * 3 + 1] = 3.0f;
            positions[0] = -5.0f;
        }
        expect(wgf_mesh_set_positions(land, positions, 18), "its positions changed");
        expect(wgf_gfx_priv_mesh_get_bounds(land, &lo, &hi) && near(lo.x, -5) && near(lo.y, 3) && near(hi.y, 3) &&
                   near(wgf_mesh_get_bounds_min(land).x, -5),
               "its box made again");
        wgf_mesh_get_positions(land, positions, 18);
        wgf_mesh_get_normals(land, normals, 18);
        expect(near(positions[1], 3) && near(positions[0], -5) && fabsf(normals[0] + 0.8944f) < 1e-3f,
               "read back as set, its normals as they were");
        expect(wgf_mesh_set_normals(land, NULL, 0) && wgf_mesh_get_normals(land, normals, 18) == 18 &&
                   near(normals[0], 0) && near(normals[1], 1) && near(normals[16], 1),
               "normals made smooth again: flat, up");
        {
            const float given[18] = {1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0};
            expect(wgf_mesh_set_normals(land, given, 18) && wgf_mesh_get_normals(land, normals, 18) == 18 &&
                       near(normals[0], 1) && near(normals[1], 0),
                   "normals given");
        }
        expect(wgf_mesh_set_uvs(land, NULL, 0) && wgf_mesh_get_uvs(land, uvs, 12) == 12 && near(uvs[10], 0),
               "texture coordinates: 0 for none");
        {
            float paint[24];
            const float *v;
            int i;
            for (i = 0; i < 6; i++) {
                paint[i * 4] = 1.0f;
                paint[i * 4 + 1] = 0.5f;
                paint[i * 4 + 2] = 2.0f;
                paint[i * 4 + 3] = -1.0f;
            }
            expect(wgf_mesh_set_colors(land, paint, 24) && wgf_mesh_get_colors(land, colors, 24) == 24 &&
                       near(colors[0], 1) && fabsf(colors[1] - 0.5f) < 1e-3f && near(colors[2], 1) &&
                       near(colors[3], 0),
                   "colors: read back as given, clamped to 0..1");
            v = wgf_gfx_priv_mesh_get_vertices(land, &count);
            expect(count == 6 && near(v[14], 1) && fabsf(v[15] - 0.2140f) < 1e-3f && near(v[16], 1) && near(v[17], 0),
                   "stored linear, in each corner");
            expect(wgf_mesh_set_colors(land, NULL, 0) && wgf_mesh_get_colors(land, colors, 24) == 24 &&
                       near(colors[1], 1) && near(colors[3], 1),
                   "colors: white for none");
            expect(!wgf_mesh_set_positions(land, positions, 15) && !wgf_mesh_set_positions(land, NULL, 0) &&
                       !wgf_mesh_set_positions(land, NULL, 18) && !wgf_mesh_set_normals(land, normals, 3) &&
                       !wgf_mesh_set_uvs(land, uvs, 18) && !wgf_mesh_set_colors(land, paint, 18),
                   "a count that isn't the corners': refused");
            expect(!wgf_mesh_set_positions(cube, positions, 18) && !wgf_mesh_set_colors(cube, NULL, 0) &&
                       !wgf_mesh_set_positions(12345, positions, 18) && wgf_mesh_get_vertex_count(12345) == 0 &&
                       wgf_mesh_get_colors(12345, colors, 24) == 0,
                   "a generated shape, shared, isn't changed in place; nor what isn't a mesh");
        }
        {
            const float strip[] = {0, 0, 0, 0, 0, 1, 1, 0, 0};
            const float moved[] = {0, 1, 0, 0, 1, 1, 1, 1, 0};
            const wgf_mesh_t made = wgf_mesh_create_triangles(strip, 9, NULL, 0, NULL, 0, NULL, 0);
            expect(wgf_mesh_set_positions(made, moved, 9) && near(wgf_mesh_get_bounds_min(made).y, 1),
                   "the program's own triangles, changed in place");
        }
    }

    /* a generated mesh's one material */
    expect(wgf_mesh_get_material_count(cube) == 1 && wgf_mesh_get_material(cube, 1) == 0, "one material slot");
    material = wgf_mesh_get_material(cube, 0);
    expect(wgf_material_get_shading(material) == WGF_MATERIAL_SHADING_PBR &&
               near(wgf_material_get_float(material, "metallic"), 0.0f) &&
               near(wgf_material_get_float(material, "roughness"), 0.5f) &&
               near(wgf_material_get_vec4(material, "base_color").x, 1.0f),
           "white, not metallic, roughness 0.5");

    /* materials */
    own = wgf_material_create(WGF_MATERIAL_SHADING_UNLIT);
    expect(own != 0 && wgf_material_get_shading(own) == WGF_MATERIAL_SHADING_UNLIT, "an unlit material");
    expect(wgf_material_create((wgf_material_shading_t)7) == 0, "no such shading: none (logged)");
    expect(near(wgf_material_get_float(own, "metallic"), 1.0f) &&
               near(wgf_material_get_float(own, "roughness"), 1.0f) &&
               wgf_material_get_alpha_mode(own) == WGF_ALPHA_MODE_OPAQUE &&
               near(wgf_material_get_alpha_cutoff(own), 0.5f) && !wgf_material_is_double_sided(own) &&
               near(wgf_material_get_vec2(own, "normal_texture_scale").y, 1.0f),
           "glTF's defaults");
    expect(wgf_material_set_color(own, "base_color", wgf_color_make(255, 128, 0, 51)) &&
               near(wgf_material_get_vec4(own, "base_color").x, 1.0f) &&
               fabsf(wgf_material_get_vec4(own, "base_color").y - 0.2158f) < 1e-3f &&
               near(wgf_material_get_vec4(own, "base_color").w, 0.2f),
           "a color: sRGB to linear, its alpha as it is");
    expect(wgf_material_set_vec3(own, "emissive", 2.0f, 0.5f, 0.0f) &&
               near(wgf_material_get_vec3(own, "emissive").x, 2.0f),
           "emissive past 1");
    expect(!wgf_material_set_float(own, "emissive", 1.0f) && !wgf_material_set_float(own, "shininess", 1.0f) &&
               wgf_material_get_float(own, "shininess") == 0.0f,
           "the wrong kind, or no such name: refused (logged), and 0");
    expect(wgf_material_set_int(own, "normal_texture_texcoord", 1) &&
               !wgf_material_set_int(own, "normal_texture_texcoord", 2) &&
               wgf_material_get_int(own, "normal_texture_texcoord") == 1,
           "a texture coordinate set: 0 or 1");
    expect(wgf_material_set_alpha_mode(own, WGF_ALPHA_MODE_MASK, -1.0f) && wgf_material_get_alpha_cutoff(own) == 0.0f &&
               !wgf_material_set_alpha_mode(own, (wgf_alpha_mode_t)3, 0.5f),
           "alpha mode: the cutoff clamped, a mode that isn't one refused");
    expect(!wgf_material_set_texture(own, "normal_texture", cube) &&
               wgf_material_get_texture(own, "normal_texture") == 0,
           "a texture parameter takes only a texture");
    expect(wgf_material_set_texture_sampling(own, "base_color_texture", WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_MIRROR,
                                             WGF_TEXTURE_FILTER_NEAREST) &&
               wgf_material_get_texture_wrap_v(own, "base_color_texture") == WGF_TEXTURE_WRAP_MIRROR &&
               !wgf_material_set_texture_sampling(own, "base_color_texture", (wgf_texture_wrap_t)9,
                                                  WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_FILTER_LINEAR),
           "sampling, and a wrap that isn't one refused");
    {
        float m[6];
        wgf_material_set_vec2(own, "base_color_texture_scale", 2.0f, 3.0f);
        wgf_material_set_vec2(own, "base_color_texture_offset", 0.25f, 0.5f);
        wgf_gfx_priv_material_uv_matrix(&wgf_gfx_priv_material_get(own)->textures[0], m);
        expect(near(m[0], 2.0f) && near(m[4], 3.0f) && near(m[2], 0.25f) && near(m[5], 0.5f), "the texture transform");
    }
    expect(wgf_material_get_float(cube, "metallic") == 0.0f && !wgf_material_set_float(cube, "metallic", 1.0f),
           "a mesh isn't a material");
    expect(wgf_resource_release(own), "a material let go of");

    /* with gfx: a primitive's buffers, uploaded when asked for, and every mesh freed at its stop */
    expect(wgf_gfx_priv_start(), "gfx starts");
    {
        wgf_gfx_priv_mesh_primitive_t primitive;
        expect(wgf_gfx_priv_mesh_get_primitive_count(cube) == 1 &&
                   wgf_gfx_priv_mesh_get_primitive(cube, 0, &primitive) &&
                   primitive.index_count == 36 && primitive.vertices.id != SG_INVALID_ID,
               "the primitive uploaded when asked for");
        expect(!wgf_gfx_priv_mesh_get_primitive(cube, 1, &primitive), "no second primitive");
    }
    { /* a mesh changed in place after it was sent: its vertices sent again, in a buffer that changes */
        const float heights[4] = {0, 0, 0, 0}, lifted[12] = {0, 1, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1};
        const wgf_mesh_t land = wgf_mesh_create_heightmap(heights, 4, 2, 1, 1, 1);
        const wgf_gfx_priv_mesh_record_t *record = wgf_gfx_priv_mesh_record(land);
        wgf_gfx_priv_mesh_primitive_t primitive;
        uint32_t first;
        expect(wgf_gfx_priv_mesh_get_primitive(land, 0, &primitive) && record != NULL &&
                   !record->primitives[0].dynamic,
               "sent as made: a buffer that doesn't change");
        first = primitive.vertices.id;
        expect(wgf_mesh_set_positions(land, lifted, 12) && record->primitives[0].changed &&
                   record->primitives[0].tangents_stale,
               "changed: to be sent again");
        expect(wgf_gfx_priv_mesh_get_primitive(land, 0, &primitive) && primitive.vertices.id != first &&
                   sg_query_buffer_state(primitive.vertices) == SG_RESOURCESTATE_VALID &&
                   record->primitives[0].dynamic && !record->primitives[0].changed &&
                   !record->primitives[0].tangents_stale,
               "sent again, in a buffer that changes, its tangents made again");
        first = primitive.vertices.id;
        wgf_gfx_priv_begin_frame();
        wgf_gfx_priv_end_frame();
        expect(wgf_mesh_set_colors(land, NULL, 0) && wgf_gfx_priv_mesh_get_primitive(land, 0, &primitive) &&
                   primitive.vertices.id == first && !record->primitives[0].changed,
               "a frame on: written into the same buffer");
        expect(wgf_mesh_set_colors(land, NULL, 0) && wgf_gfx_priv_mesh_get_primitive(land, 0, &primitive) &&
                   record->primitives[0].changed,
               "a second change in the frame: kept for the next");
        wgf_gfx_priv_begin_frame();
        wgf_gfx_priv_end_frame();
        expect(wgf_gfx_priv_mesh_get_primitive(land, 0, &primitive) && !record->primitives[0].changed,
               "and written then");
    }
    { /* bounds: the mesh's own, and a tree of models' placed */
        const wgf_actor_t root = wgf_actor_create(), model = wgf_model_create(cube);
        wgf_vec3_t low = wgf_mesh_get_bounds_min(cube), high = wgf_mesh_get_bounds_max(cube);
        expect(fabsf(low.x + 1) < 1e-4f && fabsf(high.y - 0.5f) < 1e-4f && fabsf(high.z - 2) < 1e-4f, "a mesh's box, its own space");
        wgf_actor_set_parent(model, root);
        wgf_actor_set_position(root, 10, 0, 0);
        wgf_actor_set_scale(model, 2, 2, 2);
        low = wgf_model_get_bounds_min(root), high = wgf_model_get_bounds_max(root);
        expect(fabsf(low.x - 8) < 1e-4f && fabsf(high.x - 12) < 1e-4f && fabsf(high.y - 1) < 1e-4f, "a tree's models placed and scaled");
        expect(wgf_model_get_bounds_max(wgf_actor_create()).x == 0 && wgf_mesh_get_bounds_max(12345).x == 0, "none: 0");
        wgf_actor_destroy(root, WGF_ACTOR_DESTROY_CHILDREN);
    }
    wgf_gfx_priv_stop();
    expect(wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_NONE &&
               wgf_resource_get_status(material) == WGF_RESOURCE_STATUS_NONE,
           "gfx's stop frees the meshes and their materials");

    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
