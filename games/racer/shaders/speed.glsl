/* The racer's speed effect (src/Main.hx's speedEffect): over the finished frame, toward
 * its edges, a blur along the line from the middle (the world streaming past), a darkening
 * that closes in, and a little warmth over everything, all from `strength` (0 at rest, 1
 * at the top end). The HUD is drawn before an effect runs, so it would be blurred too:
 * `keep0`..`keep2` are rectangles (uv: x, y, width, height from the top-left) left as
 * they are, the HUD's panels and the mirror. A screen effect against libwgf's wgf.glsl
 * (wgf_screen), packed by tools/pack_shaders.py into assets/shaders/speed.wgfshader. */
@fs fs
@include_block wgf_screen
layout(binding=2) uniform params {
    float strength; /* 0 none .. 1 full */
    float radius;   /* where it starts: a fraction of the half-diagonal */
    float blur;     /* how far toward the middle an edge pixel reaches, a fraction of its distance */
    float dark;     /* how dark the corners go at full strength */
    vec4 warm;      /* the tint at full strength, linear rgb */
    vec4 keep0;
    vec4 keep1;
    vec4 keep2;
};

/* 1 inside the rectangle, 0 outside, across a few pixels */
float inside(vec4 r, vec2 uv) {
    vec2 px = wgf_screen_texel() * 3.0;
    vec2 lo = smoothstep(r.xy - px, r.xy, uv);
    vec2 hi = 1.0 - smoothstep(r.xy + r.zw, r.xy + r.zw + px, uv);
    return lo.x * lo.y * hi.x * hi.y;
}

void main() {
    vec4 frame = wgf_screen_color();
    float s = clamp(strength, 0.0, 1.0);
    vec2 d = wgf_screen_uv - 0.5;
    float r = length(d * 2.0) / 1.41421356; /* 0 in the middle, 1 in a corner */
    float edge = smoothstep(radius, 1.0, r) * s;
    float keep = max(inside(keep0, wgf_screen_uv), max(inside(keep1, wgf_screen_uv), inside(keep2, wgf_screen_uv)));
    edge *= 1.0 - keep;
    vec3 c = frame.rgb;
    if (edge > 0.001) {
        /* eight taps from here toward the middle */
        vec2 step = -d * blur * edge / 8.0;
        vec3 sum = vec3(0.0);
        for (int i = 0; i < 8; i++) {
            sum += wgf_screen_color_at(wgf_screen_uv + step * float(i)).rgb;
        }
        c = sum / 8.0;
    }
    c *= 1.0 - edge * dark;
    c *= mix(vec3(1.0), warm.rgb, s * (1.0 - keep));
    wgf_output(c, frame.a);
}
@end
