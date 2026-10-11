/* The shadow maps' depth pass (libwgf/gfx/src/stage/wgf_gfx_shadow.c): the casters as a light
 * sees them. libwgt's wgt_gfx_depth.glsl (wgrender's), trimmed as the model shader is:
 * where each caster stands is its record in the frame's instance texture
 * (wgf_gfx_instance.glsl), the one the shading pass reads, so a run of casters that agree
 * on mesh and material is one draw here too; no skinning (milestone 3's). Written once in sokol-shdc's annotated
 * GLSL; tools/gen_shaders.py makes wgf_gfx_depth.glsl.h, committed.
 *
 * Only depth is written: the pass has no color attachment, so the fragment shader has
 * nothing to say unless the material is alpha-tested, where a cut-out texel must not
 * cast (a leaf casts its shape, not its quad).
 *
 * vs_depth_params  light_view_proj, and instance_base (x: the draw's first record)
 * fs_depth_params  cutoff.x: the alpha cutoff (0: no alpha test)
 */

@include wgf_gfx_instance.glsl

@vs vs_depth
@include_block instance_data
layout(binding=0) uniform vs_depth_params {
    mat4 light_view_proj;
    vec4 instance_base;
};
in vec3 position;
in vec2 texcoord0;
out vec2 v_uv0;
void main() {
    int record = int(instance_base.x) + gl_InstanceIndex;
    gl_Position = light_view_proj * (instance_model(record) * vec4(position, 1.0));
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
