/* Wardens' spawn warning: a red circle on the ground where an enemy is about to appear,
 * filling from its middle out as `fill` goes from 0 to 1 (the game passes it in each frame,
 * over the second before the enemy comes). Worked out from the ground's coordinates. */
@fs fs
@include_block wgf_surface
layout(binding=2) uniform params {
    vec4 color;   /* linear rgba */
    vec2 center;  /* where the enemy will stand: x and z */
    float radius; /* the circle's, metres */
    float fill;   /* 0 to 1: how far out the inside is filled */
};

void main() {
    float d = length(wgf_world_pos.xz - center);
    float edge = fwidth(d) * 1.5;
    float outline = 1.0 - smoothstep(0.06 - edge, 0.06 + edge, abs(d - radius));
    float inside = (1.0 - smoothstep(fill * radius - edge, fill * radius + edge, d)) * step(d, radius);
    float alpha = max(outline, inside * 0.55) * color.a;
    if (alpha < 0.01) {
        discard;
    }
    wgf_output(color.rgb, alpha);
}
@end
