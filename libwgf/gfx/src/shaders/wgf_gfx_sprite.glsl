/* Sprites (libwgf/gfx/src/sprite/wgf_gfx_sprite_batch.c): libwgt's instanced quads, from
 * wgt_gfx_sprite.glsl (wgrender's, its HISTORY's "a sprite renderer, and particle
 * emitters"), whole but for the lit programs (milestone 2.5's). Written once in sokol-shdc's
 * annotated GLSL; tools/gen_shaders.py makes wgf_gfx_sprite.glsl.h, committed.
 *
 * One quad (6 corners) drawn once per sprite. Each sprite carries where it is and how
 * big, its source rectangle, its tint, and how it faces:
 *   facing 0  camera-facing (spherical): the batch's camera right and up
 *   facing 1  upright, turning about Y (cylindrical): the batch's right, world up
 *   facing 2+ its own axes (a 2D sprite's, flat on the ground, or free)
 * The billboard axes come from the camera, the same for every sprite in a batch, so
 * they're uniforms; the CPU works them out as picking will. Colors are sRGB values, like
 * immediate mode's: texture x tint.
 *
 * Alpha: > 0 a mask cutoff (texels below it are discarded, the rest are opaque),
 * < 0 opaque, 0 as is (blended or added by the pipeline).
 *
 * Two ways to get a sprite's data:
 *   quad         per-instance vertex attributes (backends with base-instance draws)
 *   quad_pulled  fetched from a float texture by index: first + gl_InstanceID, 6 texels
 *                a sprite, 256 sprites a row (WebGL2, which can't draw from a base
 *                instance: this way a batch doesn't rebind its instances; wgrender-c
 *                measured 16,000 blended sprites from 4 textures 20.5 ms to 11.0 on a
 *                phone)
 */

@block quad_common
layout(binding=0) uniform sprite_params {
    mat4 view_proj;
    vec4 camera_right; /* xyz: facing 0's right */
    vec4 camera_up;    /* xyz: facing 0's up */
    vec4 upright;      /* xyz: facing 1's right (its up is world Y) */
};
out vec2 uv;
out vec4 color;
out float alpha_mode;

/* corner: x -0.5 left .. 0.5 right, y -0.5 bottom .. 0.5 top. pos: xyz where the pivot
   goes, w facing. size: xy world size (scale included), zw pivot (0..1, y down).
   source: u0, v0 (top-left), u1, v1. right, up: facing 2+ axes; up.w alpha. */
void place(vec2 corner, vec4 pos, vec4 size, vec4 source, vec3 right_axis, vec4 up_axis, vec4 tint) {
    vec3 right = right_axis;
    vec3 up = up_axis.xyz;
    if (pos.w < 0.5) {
        right = camera_right.xyz;
        up = camera_up.xyz;
    } else if (pos.w < 1.5) {
        right = upright.xyz;
        up = vec3(0.0, 1.0, 0.0);
    }
    /* the pivot sits on the position: the quad reaches (1 - pivot) of its size right of
       it and pivot.y of it up (the pivot runs down the texture) */
    vec2 local = vec2((corner.x + 0.5 - size.z) * size.x, (corner.y - 0.5 + size.w) * size.y);
    vec3 world = pos.xyz + right * local.x + up * local.y;
    gl_Position = view_proj * vec4(world, 1.0);
    uv = vec2(mix(source.x, source.z, corner.x + 0.5), mix(source.y, source.w, 0.5 - corner.y));
    color = tint;
    alpha_mode = up_axis.w;
}
@end

@vs vs_quad
@include_block quad_common
in vec2 corner;
in vec4 inst_pos;
in vec4 inst_size;
in vec4 inst_uv;
in vec3 inst_right;
in vec4 inst_up;
in vec4 inst_color;

void main() {
    place(corner, inst_pos, inst_size, inst_uv, inst_right, inst_up, inst_color);
}
@end

@vs vs_quad_pulled
@include_block quad_common
layout(binding=1) uniform sprite_batch {
    vec4 batch; /* x: the batch's first sprite */
};
layout(binding=1) uniform texture2D sprite_data;
layout(binding=1) uniform sampler sprite_data_smp;
@image_sample_type sprite_data unfilterable_float
@sampler_type sprite_data_smp nonfiltering
in vec2 corner;

vec4 texel(int sprite, int k) {
    return texelFetch(sampler2D(sprite_data, sprite_data_smp), ivec2((sprite % 256) * 6 + k, sprite / 256), 0);
}

void main() {
    int sprite = int(batch.x) + gl_InstanceIndex;
    place(corner, texel(sprite, 0), texel(sprite, 1), texel(sprite, 2), texel(sprite, 3).xyz, texel(sprite, 4),
          texel(sprite, 5));
}
@end

@fs fs_sprite
layout(binding=0) uniform texture2D sprite_tex;
layout(binding=0) uniform sampler sprite_smp;
in vec2 uv;
in vec4 color;
in float alpha_mode;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(sprite_tex, sprite_smp), uv) * color;
    if (alpha_mode > 0.0 && frag_color.a < alpha_mode) {
        discard;
    }
    if (alpha_mode != 0.0) {
        frag_color.a = 1.0;
    }
}
@end

@program sprite vs_quad fs_sprite
@program sprite_pulled vs_quad_pulled fs_sprite
