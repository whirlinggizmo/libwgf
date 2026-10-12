/* Dissolve (examples/c/gfx-shaders): the surface burns away and back where a noise texture
 * is below a moving threshold, with a glowing edge. libwgt's
 * examples/shaders/dissolve.glsl. Packed by tools/pack_shader.py (tools/gen_shaders.py). */
@fs fs
@include_block wgf_surface
layout(binding=2) uniform params {
    vec4 color;      /* linear rgba */
    vec3 edge_color; /* linear rgb, may exceed 1 */
    float speed;     /* cycles a second */
};
layout(binding=0) uniform texture2D noise_tex;
layout(binding=0) uniform sampler noise_smp;

void main() {
    float noise = texture(sampler2D(noise_tex, noise_smp), wgf_uv0 * 2.0).r;
    float threshold = 0.3 + 0.4 * sin(wgf_time() * speed * 6.2831853); /* never quite all gone */
    if (noise < threshold) {
        discard;
    }
    vec3 n = normalize(wgf_normal) * (gl_FrontFacing ? 1.0 : -1.0);
    vec3 light = wgf_ambient();
    for (int i = 0; i < 8; i++) {
        if (i >= wgf_light_count()) {
            break;
        }
        vec3 to_light;
        vec3 radiance = wgf_light(i, wgf_world_pos, to_light);
        light += radiance * max(dot(n, to_light), 0.0) / 3.14159265;
    }
    float edge = 1.0 - smoothstep(0.0, 0.06, noise - threshold);
    wgf_output(color.rgb * light + edge_color * edge, color.a);
}
@end
