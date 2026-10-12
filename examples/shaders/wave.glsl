/* Water (examples/c/gfx-shaders): a vertex hook moves the surface along its normals in
 * travelling waves; the fragment shader colors it by height and reflects the stage's
 * environment, most at grazing angles. libwgt's examples/shaders/wave.glsl. Packed by
 * tools/pack_shader.py (tools/gen_shaders.py). */
@block vertex
layout(binding=3) uniform wave_params {
    float amplitude;  /* object-space units */
    float frequency;  /* waves across one unit */
    float wave_speed; /* radians a second */
};
void wgf_vertex(inout vec3 position, inout vec3 normal) {
    float phase = (position.x + position.z) * frequency * 6.2831853 + wgf_time() * wave_speed;
    position += normal * sin(phase) * amplitude;
}
@end

@fs fs
@include_block wgf_surface
layout(binding=2) uniform params {
    vec4 low_color;     /* linear rgba */
    vec4 high_color;
    float roughness;    /* of the reflections, 0..1 */
    float reflectivity; /* facing the viewer (f0); rises to 1 at grazing angles */
};

void main() {
    vec3 n = normalize(wgf_normal);
    vec3 v = normalize(wgf_camera_position() - wgf_world_pos);
    vec3 light = wgf_ambient() + wgf_environment_diffuse(n);
    for (int i = 0; i < 8; i++) {
        if (i >= wgf_light_count()) {
            break;
        }
        vec3 to_light;
        light += wgf_light(i, wgf_world_pos, to_light) * max(dot(n, to_light), 0.0) / 3.14159265;
    }
    float height = clamp(wgf_world_pos.y * 0.5 + 0.5, 0.0, 1.0);
    vec4 color = mix(low_color, high_color, height);
    vec2 ab = wgf_environment_brdf(max(dot(n, v), 1e-4), roughness);
    vec3 reflection = wgf_environment_specular(n, v, roughness) * (vec3(reflectivity) * ab.x + ab.y);
    wgf_output(color.rgb * light * (1.0 - reflectivity) + reflection, color.a);
}
@end
