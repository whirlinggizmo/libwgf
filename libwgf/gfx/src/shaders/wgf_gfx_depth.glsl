/* The shadow maps' depth pass (libwgf/gfx/src/stage/wgf_gfx_shadow.c): the casters as a light
 * sees them. libwgt's wgt_gfx_depth.glsl (wgrender's), trimmed as the model shader is:
 * one caster a draw, its matrix a uniform (libwgt reads the frame's instance records,
 * step 11's), and no skinning (milestone 3's). Written once in sokol-shdc's annotated
 * GLSL; tools/gen_shaders.py makes wgf_gfx_depth.glsl.h, committed.
 *
 * Only depth is written: the pass has no color attachment, so the fragment shader has
 * nothing to say unless the material is alpha-tested, where a cut-out texel must not
 * cast (a leaf casts its shape, not its quad).
 *
 * vs_depth_params  mvp: the light's view-projection times the caster's world matrix
 * fs_depth_params  cutoff.x: the alpha cutoff (0: no alpha test)
 */

@vs vs_depth
layout(binding=0) uniform vs_depth_params {
    mat4 mvp;
};
in vec3 position;
in vec2 texcoord0;
out vec2 v_uv0;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    v_uv0 = texcoord0;
}
@end

@fs fs_depth
layout(binding=1) uniform fs_depth_params {
    vec4 cutoff;
};
layout(binding=0) uniform texture2D base_color_tex;
layout(binding=0) uniform sampler base_color_smp;
in vec2 v_uv0;
void main() {
    if (cutoff.x > 0.0 && texture(sampler2D(base_color_tex, base_color_smp), v_uv0).a < cutoff.x) {
        discard;
    }
}
@end

@program depth vs_depth fs_depth
