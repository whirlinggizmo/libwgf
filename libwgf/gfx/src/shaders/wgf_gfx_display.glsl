/* What a stage's shaders share (wgf_gfx_model.glsl, wgf_gfx_background.glsl): sRGB's two
 * ways, Khronos PBR Neutral and ACES tone mapping, a linear color to the framebuffer's sRGB
 * with exposure and tone mapping (`tonemap` as fs_scene's u_tonemap), and a direction in an
 * environment's frame. libwgt's wgt_gfx_pbr.glsl's color block. Included, not a program of
 * its own (tools/gen_shaders.py skips a file with none). */

@block display
vec3 srgb_to_linear(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow((max(c, vec3(0.04045)) + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 linear_to_srgb(vec3 c) {
    c = clamp(c, vec3(0.0), vec3(1.0));
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0031308)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

/* Khronos PBR Neutral (https://github.com/KhronosGroup/ToneMapping) */
vec3 tonemap_neutral(vec3 color) {
    const float start_compression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < start_compression) {
        return color;
    }
    const float d = 1.0 - start_compression;
    float new_peak = 1.0 - d * d / (peak + d - start_compression);
    color *= new_peak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return mix(color, vec3(new_peak), g);
}

/* ACES filmic curve fit (Narkowicz 2015) */
vec3 tonemap_aces(vec3 color) {
    color *= 0.6;
    return clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
}

/* Linear scene color to the framebuffer's sRGB, with exposure and tone mapping. */
vec3 to_display(vec3 color, vec4 tonemap) {
    color *= tonemap.y;
    int mode = int(tonemap.x + 0.5);
    if (mode == 1) {
        color = tonemap_neutral(color);
    } else if (mode == 2) {
        color = tonemap_aces(color);
    }
    return linear_to_srgb(color);
}

/* A world direction in an environment's frame, turned by -rotation about +y (`env`'s z and
 * w its cosine and sine). */
vec3 env_dir(vec3 dir, vec4 env) {
    return vec3(env.z * dir.x - env.w * dir.z, dir.y, env.w * dir.x + env.z * dir.z);
}
@end
