/* The cubemap tests' surface shader (wgf_gfx_cubemap_window_test.c): its cubemap `sky`
 * sampled along `dir`, whatever the surface, times its texture2D `tint_tex`, unlit, so each
 * face's color shows where it is drawn. Packed by tools/gen_shaders.py. */
@fs fs
@include_block wgf_surface
layout(binding=2) uniform params {
    vec3 dir; /* the direction sampled, world space */
};
layout(binding=0) uniform textureCube sky;
layout(binding=0) uniform sampler sky_smp;
layout(binding=1) uniform texture2D tint_tex;
layout(binding=1) uniform sampler tint_smp;

void main() {
    vec3 c = texture(samplerCube(sky, sky_smp), dir).rgb * texture(sampler2D(tint_tex, tint_smp), wgf_uv0).rgb;
    wgf_output(wgf_srgb_to_linear(c), 1.0);
}
@end
