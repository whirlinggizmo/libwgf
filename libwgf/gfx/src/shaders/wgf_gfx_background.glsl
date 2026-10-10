/* An environment behind a stage's models (libwgf/gfx/src/stage/wgf_gfx_environment.c): a
 * full-screen triangle at the far plane, each pixel the environment in its view direction,
 * a mip of its background cube for blur, tone mapped as the stage is. libwgt's background
 * program (wgrender's), in a file of its own so a program with no environment links none
 * of it. Written once in sokol-shdc's annotated GLSL; tools/gen_shaders.py makes
 * wgf_gfx_background.glsl.h, committed.
 *
 * bg_params  inv_view_proj: the camera's view-projection inverted (GL's -1..1 depth);
 *            bg_env: x intensity, y the mip (blur), z/w the cosine and sine of the
 *            rotation about +y; bg_tonemap: as the model shader's u_tonemap
 */
@include wgf_gfx_display.glsl

@vs vs_background
in vec2 bg_position;
out vec2 v_ndc;
void main() {
    gl_Position = vec4(bg_position, 1.0, 1.0);
    v_ndc = bg_position;
}
@end

@fs fs_background
@include_block display
layout(binding=0) uniform bg_params {
    mat4 inv_view_proj;
    vec4 bg_env;
    vec4 bg_tonemap;
};
layout(binding=0) uniform textureCube bg_tex;
layout(binding=0) uniform sampler bg_smp;
in vec2 v_ndc;
out vec4 frag_color;
void main() {
    vec4 near_point = inv_view_proj * vec4(v_ndc, -1.0, 1.0);
    vec4 far_point = inv_view_proj * vec4(v_ndc, 1.0, 1.0);
    vec3 dir = normalize(far_point.xyz / far_point.w - near_point.xyz / near_point.w);
    vec3 radiance = textureLod(samplerCube(bg_tex, bg_smp), env_dir(dir, bg_env), bg_env.y).rgb * bg_env.x;
    frag_color = vec4(to_display(radiance, bg_tonemap), 1.0);
}
@end

@program background vs_background fs_background
