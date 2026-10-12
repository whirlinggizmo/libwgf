/* Vignette (examples/c/gfx-postprocess): the finished frame darkened toward the corners,
 * with a little warmth left in the middle. A screen effect: it includes wgf_screen, so
 * pack_shader makes one program that draws over the frame. libwgt's
 * examples/shaders/vignette.glsl. Packed by tools/pack_shader.py (tools/gen_shaders.py). */
@fs fs
@include_block wgf_screen
layout(binding=2) uniform params {
    float strength; /* how dark the corners go (0: none) */
    float radius;   /* where the darkening starts, a fraction of the half-diagonal */
    vec4 tint;      /* multiplies the middle, linear rgba */
};

void main() {
    vec4 frame = wgf_screen_color();
    vec2 d = (wgf_screen_uv - 0.5) * 2.0; /* -1..1 across the frame */
    float r = length(d) / 1.41421356;    /* 0 in the middle, 1 in a corner */
    float fade = 1.0 - smoothstep(radius, 1.0, r) * clamp(strength, 0.0, 1.0);
    wgf_output(frame.rgb * tint.rgb * fade, frame.a);
}
@end
