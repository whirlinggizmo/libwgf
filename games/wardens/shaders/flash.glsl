/* Wardens' hit flash: a struck enemy drawn white for a tenth of a second, still posed by its
 * animation (libwgf's skinned program runs the joints before this) and lit by the stage's
 * lights, as its own material would be. Each material slot of a part has one of these, its
 * `base` that slot's color, so what shows through the white is the part as it was. */
@fs fs
@include_block wgf_surface
layout(binding=2) uniform params {
    vec4 base;   /* linear rgba: the slot's own base color */
    float flash; /* 0 to 1: how white */
};

void main() {
    vec3 n = normalize(wgf_normal) * (gl_FrontFacing ? 1.0 : -1.0);
    vec3 light = wgf_ambient();
    for (int i = 0; i < 8; i++) {
        if (i >= wgf_light_count()) {
            break;
        }
        vec3 to_light;
        vec3 radiance = wgf_light(i, wgf_world_pos, to_light);
        light += radiance * max(dot(n, to_light), 0.0) * wgf_shadow(i, wgf_world_pos, n) / 3.14159265;
    }
    vec3 lit = base.rgb * wgf_color.rgb * light;
    vec3 white = vec3(1.0) * (0.6 + 0.4 * clamp(light, 0.0, 1.0));
    wgf_output(mix(lit, white * 1.4, flash), 1.0);
}
@end
