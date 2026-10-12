/* CRT scanlines (examples/c/gfx-postprocess): dark lines across the frame, a slight color
 * shift left and right, and a slow flicker; a second screen effect, to show a chain.
 * libwgt's examples/shaders/scanlines.glsl. Packed by tools/pack_shader.py
 * (tools/gen_shaders.py). */
@fs fs
@include_block wgf_screen
layout(binding=2) uniform params {
    float lines;    /* dark lines down the frame */
    float darkness; /* how dark they go (0..1) */
    float offset;   /* the color shift, in pixels */
    float flicker;  /* brightness wobble over time (0..1) */
};

void main() {
    vec2 texel = wgf_screen_texel();
    /* red and blue from a little left and right of this pixel */
    vec3 color = vec3(wgf_screen_color_at(wgf_screen_uv - vec2(offset * texel.x, 0.0)).r,
                      wgf_screen_color().g,
                      wgf_screen_color_at(wgf_screen_uv + vec2(offset * texel.x, 0.0)).b);
    float line = 0.5 + 0.5 * cos(wgf_screen_uv.y * max(lines, 1.0) * 6.2831853);
    color *= 1.0 - clamp(darkness, 0.0, 1.0) * line;
    color *= 1.0 + clamp(flicker, 0.0, 1.0) * 0.06 * sin(wgf_time() * 11.0);
    wgf_output(color, 1.0);
}
@end
