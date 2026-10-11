/* The frame's per-placement records (wgf_gfx_model.glsl's shading pass and
 * wgf_gfx_depth.glsl's shadow pass read them): libwgt's wgt_gfx_instance.glsl (wgrender's).
 * One record a part of a model, eight texels, 128 records a row of an RGBA32F texture the
 * stage writes before any pass (libwgf/gfx/src/stage/wgf_gfx_stage3d.c); a draw says where
 * its first record is and the hardware counts from there, so any number of parts that
 * agree on everything else go up as one draw. Included, not a program of its own
 * (tools/gen_shaders.py skips a file with none). */

@block instance_data
layout(binding=9) uniform texture2D instance_tex;
layout(binding=9) uniform sampler instance_smp;
@image_sample_type instance_tex unfilterable_float
@sampler_type instance_smp nonfiltering

vec4 instance_texel(int record, int texel) {
    return texelFetch(sampler2D(instance_tex, instance_smp), ivec2((record % 128) * 8 + texel, record / 128), 0);
}
/* rows 0..2 of the model matrix; its last row is always (0, 0, 0, 1) */
mat4 instance_model(int r) {
    vec4 r0 = instance_texel(r, 0), r1 = instance_texel(r, 1), r2 = instance_texel(r, 2);
    return mat4(vec4(r0.x, r1.x, r2.x, 0.0),
                vec4(r0.y, r1.y, r2.y, 0.0),
                vec4(r0.z, r1.z, r2.z, 0.0),
                vec4(r0.w, r1.w, r2.w, 1.0));
}
/* rows 3..5: its inverse transpose, for normals */
mat3 instance_normal(int r) {
    vec4 n0 = instance_texel(r, 3), n1 = instance_texel(r, 4), n2 = instance_texel(r, 5);
    return mat3(vec3(n0.x, n1.x, n2.x), vec3(n0.y, n1.y, n2.y), vec3(n0.z, n1.z, n2.z));
}
vec4 instance_tint(int r) { return instance_texel(r, 6); } /* linear, alpha as it is */
/* y: the tangents' handedness, -1 where the placement mirrors them */
vec4 instance_extra(int r) { return instance_texel(r, 7); }
@end
