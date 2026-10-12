/* The custom shader tests' surface shader (wgf_gfx_shader_window_test.c and _web_test.c):
 * `color` times its texture, unlit, and a vertex hook moving the surface `shift` along x,
 * so each block and binding shows in the pixels. Packed by tools/gen_shaders.py. */
@block vertex
layout(binding=3) uniform hook {
    float shift; /* object space */
};
void wgf_vertex(inout vec3 position, inout vec3 normal) {
    position.x += shift;
}
@end

@fs fs
@include_block wgf_surface
layout(binding=2) uniform params {
    vec4 color; /* linear rgba */
};
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

void main() {
    vec4 t = texture(sampler2D(tex, smp), wgf_uv0);
    wgf_output(color.rgb * wgf_srgb_to_linear(t.rgb), color.a * t.a);
}
@end
