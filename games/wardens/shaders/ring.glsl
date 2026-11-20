/* Wardens' target ring: a thin ring on the ground around the warden, as wide as her sword's
 * reach, pulsing once a second. Worked out from the ground's coordinates (the surface's world
 * position), not from an image. Drawn on a flat quad laid over the ground under her, blended. */
@fs fs
@include_block wgf_surface
layout(binding=2) uniform params {
    vec4 color;   /* linear rgba */
    vec2 center;  /* the warden's x and z */
    float radius; /* the sword's reach, metres */
    float width;  /* the ring's width, metres */
};

void main() {
    float d = length(wgf_world_pos.xz - center);
    float half_width = width * 0.5;
    float edge = fwidth(d) * 1.5;
    float ring = 1.0 - smoothstep(half_width - edge, half_width + edge, abs(d - radius));
    float pulse = 0.65 + 0.35 * sin(wgf_time() * 6.2831853);
    float alpha = ring * color.a * pulse;
    if (alpha < 0.01) {
        discard;
    }
    wgf_output(color.rgb * (0.8 + 0.6 * pulse), alpha);
}
@end
