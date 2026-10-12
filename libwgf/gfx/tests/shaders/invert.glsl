/* The effect tests' screen effect (wgf_gfx_effect_window_test.c and _web_test.c): the
 * frame's colors turned over, `amount` of the way, so a chain of them shows each one and
 * where each pixel came from. Packed by tools/gen_shaders.py. */
@fs fs
@include_block wgf_screen
layout(binding=2) uniform params {
    float amount; /* 0 the frame as it is, 1 inverted */
};

void main() {
    vec4 c = wgf_screen_color();
    vec3 srgb = wgf_linear_to_srgb(c.rgb);
    wgf_output(wgf_srgb_to_linear(mix(srgb, 1.0 - srgb, amount)), 1.0);
}
@end
