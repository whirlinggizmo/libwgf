/* A sprite batch's quads (libwgf/gfx/src/sprite/wgf_gfx_sprite_batch2d.c): many quads of one
 * texture on a 2D stage, one instanced draw, each quad a 32-byte record the program writes
 * (wgf_sprite_batch.h). Written once in sokol-shdc's annotated GLSL; tools/gen_shaders.py
 * makes wgf_gfx_quads.glsl.h, committed.
 *
 * A record's attributes: where its pivot is and its size, its turn, its region of the
 * texture in texels, and its tint. A batch binds its records at an offset, so it needs no
 * base instance (WebGL2 has none); a size or region of 0 is the region's or the texture's.
 * Colors are sRGB values, like immediate mode's: texture x tint. */

@vs vs_quads
layout(binding=0) uniform quads_params {
    mat4 view_proj;    /* the batch's units into clip space */
    vec4 texture_size; /* xy: the texture's pixels; zw: the pivot (0..1, y down) */
    vec4 mode;         /* x: > 0 a mask cutoff, < 0 opaque, 0 as is; y: 1 shows the whole texture */
};
in vec2 corner;          /* 0, 0 top-left .. 1, 1 bottom-right */
in vec4 inst_place;      /* x, y where the pivot goes; width, height */
in float inst_rotation;  /* radians */
in ivec4 inst_region;    /* x, y, width, height in texels */
in vec4 inst_color;
out vec2 uv;
out vec4 color;
out float alpha_mode;

void main() {
    vec4 region = vec4(inst_region);
    if (mode.y > 0.5 || region.z <= 0.0 || region.w <= 0.0) {
        region = vec4(0.0, 0.0, texture_size.xy);
    }
    vec2 size = inst_place.zw;
    if (size.x <= 0.0 || size.y <= 0.0) {
        size = region.zw;
    }
    vec2 local = (corner - texture_size.zw) * size;
    float c = cos(inst_rotation);
    float s = sin(inst_rotation);
    vec2 placed = inst_place.xy + vec2(local.x * c - local.y * s, local.x * s + local.y * c);
    gl_Position = view_proj * vec4(placed, 0.0, 1.0);
    uv = (region.xy + corner * region.zw) / texture_size.xy;
    color = inst_color;
    alpha_mode = mode.x;
}
@end

@fs fs_quads
layout(binding=0) uniform texture2D quads_tex;
layout(binding=0) uniform sampler quads_smp;
in vec2 uv;
in vec4 color;
in float alpha_mode;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(quads_tex, quads_smp), uv) * color;
    if (alpha_mode > 0.0 && frag_color.a < alpha_mode) {
        discard;
    }
    if (alpha_mode != 0.0) {
        frag_color.a = 1.0;
    }
}
@end

@program quads vs_quads fs_quads
