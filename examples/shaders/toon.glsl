/* Toon shading (examples/c/gfx-shaders): the stage's lights in a few flat bands, and a rim
 * of light around the silhouette. libwgt's examples/shaders/toon.glsl. Packed by
 * tools/pack_shader.py (tools/gen_shaders.py). */
@fs fs
@include_block wgf_surface
layout(binding=2) uniform params {
    vec4 color;  /* linear rgba, times the texture */
    float bands; /* light levels */
    float rim;   /* rim light strength */
};
layout(binding=0) uniform texture2D base_tex;
layout(binding=0) uniform sampler base_smp;

void main() {
    vec4 albedo = texture(sampler2D(base_tex, base_smp), wgf_uv0);
    albedo.rgb = wgf_srgb_to_linear(albedo.rgb);
    albedo *= color * wgf_color;
    vec3 n = normalize(wgf_normal) * (gl_FrontFacing ? 1.0 : -1.0);
    vec3 light = wgf_ambient();
    for (int i = 0; i < 8; i++) {
        if (i >= wgf_light_count()) {
            break;
        }
        vec3 to_light;
        vec3 radiance = wgf_light(i, wgf_world_pos, to_light);
        float lambert = max(dot(n, to_light), 0.0) * wgf_shadow(i, wgf_world_pos, n);
        light += radiance * (floor(lambert * bands + 0.5) / max(bands, 1.0)) / 3.14159265;
    }
    vec3 v = normalize(wgf_camera_position() - wgf_world_pos);
    float edge = pow(1.0 - max(dot(n, v), 0.0), 3.0) * rim;
    wgf_output(albedo.rgb * light + vec3(edge), albedo.a);
}
@end
