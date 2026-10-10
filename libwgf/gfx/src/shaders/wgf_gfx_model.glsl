/* A stage's models (libwgf/gfx/src/stage/wgf_gfx_stage.c): glTF metallic-roughness materials,
 * lit by up to 8 lights, tone mapped. libwgt's wgt_gfx_model.glsl and wgt_gfx_pbr.glsl
 * (wgrender's), trimmed to what milestone 2 draws so far: one model a draw, its
 * matrices uniforms (libwgt's per-instance texture comes with instancing, step 11),
 * shadows (step 8), and an environment's light (step 9). Written once in sokol-shdc's annotated
 * GLSL; tools/gen_shaders.py makes wgf_gfx_model.glsl.h, committed, for GL 4.1 and
 * WebGL2 (GLSL 300 es).
 *
 * Color space: the framebuffer holds sRGB values. Color textures are decoded from sRGB,
 * factors and light radiance are linear, lighting happens in linear space, and the
 * result is tone mapped and encoded back to sRGB. Vertex colors (linear rgba; white for
 * generated meshes) multiply the base color.
 *
 * fs_params, the material's (times the model's tint):
 *   u_base_color      linear rgba
 *   u_emissive        rgb linear emissive, w normal scale
 *   u_pbr             x metallic, y roughness, z occlusion strength, w 1 = lit, 0 = unlit
 *   u_material        x alpha cutoff (0: no alpha test), y number of lights (0..8),
 *                     z 1 = it receives shadows
 *   u_uv_row0[i], u_uv_row1[i]  texture slot i (base color, metallic-roughness, normal,
 *                     occlusion, emissive): xyz the rows of its 2x3 texture transform,
 *                     u_uv_row0[i].w its texture coordinate set (0 or 1)
 * fs_scene, the stage's: u_camera_pos (xyz), u_ambient (rgb, linear, times intensity),
 *   u_tonemap (x mode: 0 none, 1 Khronos PBR Neutral, 2 ACES; y exposure scale, 2^EV),
 *   its environment's light: u_env (x intensity, 0 none; y the prefiltered cube's last mip;
 *   z/w the cosine and sine of its rotation about +y) and u_sh[9] (its irradiance over pi,
 *   9 spherical-harmonic coefficients, xyz each), with env_tex (its GGX-prefiltered cube, a
 *   mip a roughness step) and brdf_tex (the split-sum table: A, B by n.v and roughness),
 *   and its shadows, a slot a casting light (up to 4, each a layer of one depth array):
 *   u_shadow_mat[s]     world -> that light's clip space (GL's -1..1 depth)
 *   u_shadow_params[s]  x 1 / map size, y a texel in world units, z/w bias constant, slope (texels)
 *   u_shadow_tint[s]    rgb what a shadow leaves behind (linear), w bias texels -> depth units
 *   u_shadow_extra[s]   x strength; a perspective (spot) map: y/z its near and far, w 1
 *   u_shadow_map        x 1 = the map's first row is its top
 * fs_lights, the model's lights:
 *   u_light_pos_range[i]  xyz position, w range (0 = unlimited)
 *   u_light_dir_type[i]   xyz direction the light travels, w type (0 directional, 1 point, 2 spot)
 *   u_light_radiance[i]   rgb color * intensity (linear)
 *   u_light_spot[i]       x cos(inner), y cos(outer), z its shadow slot (-1: none)
 * The BRDF follows the glTF 2.0 specification, appendix B (Lambert diffuse, GGX / Smith
 * height-correlated specular, Schlick Fresnel).
 */

@include wgf_gfx_display.glsl

@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
    mat4 model;
    mat4 normal_mat; /* the model matrix's inverse transpose, its upper 3x3 */
    vec4 extra;      /* x: the tangents' handedness, -1 where the placement mirrors them */
};
in vec3 position;
in vec3 normal;
in vec2 texcoord0;
in vec2 texcoord1;
in vec4 tangent;
in vec4 color0;
out vec3 v_normal;
out vec4 v_tangent;
out vec2 v_uv0;
out vec2 v_uv1;
out vec4 v_color;
out vec3 v_world_pos;
void main() {
    vec4 world = model * vec4(position, 1.0);
    gl_Position = mvp * vec4(position, 1.0);
    v_normal = mat3(normal_mat) * normal;
    v_tangent = vec4(mat3(model) * tangent.xyz, tangent.w * extra.x);
    v_uv0 = texcoord0;
    v_uv1 = texcoord1;
    v_color = color0;
    v_world_pos = world.xyz;
}
@end

@fs fs
const float PI = 3.14159265359;
layout(binding=1) uniform fs_params {
    vec4 u_base_color;
    vec4 u_emissive;
    vec4 u_pbr;
    vec4 u_material;
    vec4 u_uv_row0[5];
    vec4 u_uv_row1[5];
};
layout(binding=2) uniform fs_scene {
    vec4 u_camera_pos;
    vec4 u_ambient;
    vec4 u_tonemap;
    vec4 u_env;
    vec4 u_sh[9];
    mat4 u_shadow_mat[4];
    vec4 u_shadow_params[4];
    vec4 u_shadow_tint[4];
    vec4 u_shadow_extra[4];
    vec4 u_shadow_map;
};
layout(binding=3) uniform fs_lights {
    vec4 u_light_pos_range[8];
    vec4 u_light_dir_type[8];
    vec4 u_light_radiance[8];
    vec4 u_light_spot[8];
};
layout(binding=0) uniform texture2D base_color_tex;
layout(binding=1) uniform texture2D metallic_roughness_tex;
layout(binding=2) uniform texture2D normal_tex;
layout(binding=3) uniform texture2D occlusion_tex;
layout(binding=4) uniform texture2D emissive_tex;
layout(binding=0) uniform sampler base_color_smp;
layout(binding=1) uniform sampler metallic_roughness_smp;
layout(binding=2) uniform sampler normal_smp;
layout(binding=3) uniform sampler occlusion_smp;
layout(binding=4) uniform sampler emissive_smp;
layout(binding=5) uniform textureCube env_tex;
layout(binding=6) uniform texture2D brdf_tex;
layout(binding=5) uniform sampler env_smp;
layout(binding=6) uniform sampler brdf_smp;
layout(binding=8) uniform texture2DArray shadow_tex;
layout(binding=8) uniform sampler shadow_smp;
@image_sample_type shadow_tex depth
@sampler_type shadow_smp comparison
in vec3 v_normal;
in vec4 v_tangent;
in vec2 v_uv0;
in vec2 v_uv1;
in vec4 v_color;
in vec3 v_world_pos;
out vec4 frag_color;

@include_block display

/* An environment's irradiance over pi at a direction (in its frame). */
vec3 eval_sh(vec3 n) {
    return u_sh[0].xyz * 0.282095
         + u_sh[1].xyz * (0.488603 * n.y)
         + u_sh[2].xyz * (0.488603 * n.z)
         + u_sh[3].xyz * (0.488603 * n.x)
         + u_sh[4].xyz * (1.092548 * n.x * n.y)
         + u_sh[5].xyz * (1.092548 * n.y * n.z)
         + u_sh[6].xyz * (0.315392 * (3.0 * n.z * n.z - 1.0))
         + u_sh[7].xyz * (1.092548 * n.x * n.z)
         + u_sh[8].xyz * (0.546274 * (n.x * n.x - n.y * n.y));
}

/* Texture coordinates for texture slot i: its coordinate set, then its transform. */
vec2 tex_uv(int i) {
    vec3 uv = vec3(u_uv_row0[i].w < 0.5 ? v_uv0 : v_uv1, 1.0);
    return vec2(dot(u_uv_row0[i].xyz, uv), dot(u_uv_row1[i].xyz, uv));
}

float attenuation(float d, float range) {
    float inv_sq = 1.0 / max(d * d, 0.01);
    if (range <= 0.0) {
        return inv_sq;
    }
    float r = d / range;
    float w = clamp(1.0 - r * r * r * r, 0.0, 1.0);
    return w * w * inv_sq;
}

float spot_factor(float cos_angle, float cos_inner, float cos_outer) {
    if (cos_inner - cos_outer <= 1e-6) {
        return cos_angle >= cos_outer ? 1.0 : 0.0;
    }
    return smoothstep(cos_outer, cos_inner, cos_angle);
}

/* How much of the casting light in shadow slot `slot` reaches this point: 1 in the open, 0
 * in shadow, and in between across a shadow's edge (a 3x3 kernel the GPU filters). Lit when
 * this surface doesn't receive, or the point is outside what the light's map covers.
 * libwgt's (wgrender's), whole. */
float shadow_factor(int slot, vec3 world_pos, vec3 n, float n_dot_l) {
    if (u_material.z < 0.5) {
        return 1.0; /* this surface doesn't receive */
    }
    /* Look the map up a texel or so along the normal rather than pushing the depth far
     * back: that stops a surface striping itself without lifting its shadow off its
     * feet, which a big depth bias does. */
    float slant = 1.0 - clamp(n_dot_l, 0.0, 1.0);
    vec4 clip = u_shadow_mat[slot] * vec4(world_pos + n * (u_shadow_params[slot].y * (1.0 + 2.0 * slant)), 1.0);
    vec3 ndc = clip.xyz / max(abs(clip.w), 1e-6) * sign(clip.w);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (u_shadow_map.x > 0.5) {
        uv.y = 1.0 - uv.y; /* where the map's first row is its top, not its bottom */
    }
    /* A little depth slack on top, in texels (what acne is made of). An orthographic
     * (directional) map's depth is linear, so the slack converts to depth units once. A
     * perspective (spot) map's isn't: converted once, the slack is far too much near the
     * far end and lifts a thin caster's shadow off its foot, so there it is taken in world
     * units at this fragment's own distance, where a texel covers texel_world * z / far,
     * and converted back. Both are worked out and one picked arithmetically: a branch
     * around these reads is what Adreno's compiler can't bound; and the clamps keep an
     * orthographic slot's planes, which are 0, finite, since mix() with a weight of 0
     * still carries a NaN through. */
    float texels = u_shadow_params[slot].z + u_shadow_params[slot].w * slant;
    vec4 extra = u_shadow_extra[slot];
    float ortho_depth = ndc.z * 0.5 + 0.5 - texels * u_shadow_tint[slot].w;
    float near_z = max(extra.y, 1e-4);
    float far_z = max(extra.z, near_z + 1e-3);
    float z = 2.0 * near_z * far_z / max(far_z + near_z - ndc.z * (far_z - near_z), 1e-6);
    float z_slack = max(z - texels * u_shadow_params[slot].y * (z / far_z), near_z);
    float spot_depth = (far_z + near_z - 2.0 * near_z * far_z / z_slack) / (far_z - near_z) * 0.5 + 0.5;
    float depth = mix(ortho_depth, spot_depth, step(0.5, extra.w));

    /* Inside the map at all? Past its sides or its far end, everything is lit, and it
     * fades out over the last tenth so a shadow running off the edge dissolves instead of
     * being cut through. A weight rather than an early return: the comparison below must
     * run for every pixel of the draw, as filtering needs neighbouring pixels. */
    vec2 to_edge = 1.0 - abs(ndc.xy);
    float inside = step(0.0, min(to_edge.x, to_edge.y)) * step(depth, 1.0) * step(0.0, clip.w);
    float edge = clamp(min(to_edge.x, to_edge.y) / 0.1, 0.0, 1.0) * inside;

    float texel = u_shadow_params[slot].x;
    vec2 at = clamp(uv, vec2(texel), vec2(1.0 - texel));
    float lit = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            lit += texture(sampler2DArrayShadow(shadow_tex, shadow_smp),
                           vec4(at + vec2(float(x), float(y)) * texel, float(slot), clamp(depth, 0.0, 1.0)));
        }
    }
    /* strength says how much of the light a shadow takes away */
    return mix(1.0, lit / 9.0, edge * u_shadow_extra[slot].x);
}

void main() {
    vec4 base_sample = texture(sampler2D(base_color_tex, base_color_smp), tex_uv(0));
    vec4 base = vec4(srgb_to_linear(base_sample.rgb), base_sample.a) * u_base_color * v_color;
    if (base.a < u_material.x) {
        discard;
    }
    if (u_pbr.w < 0.5) {
        frag_color = vec4(to_display(base.rgb, u_tonemap), base.a); /* unlit */
        return;
    }

    /* shading frame; back faces of double-sided surfaces face the viewer */
    vec3 n = normalize(v_normal);
    vec3 t = v_tangent.xyz - n * dot(n, v_tangent.xyz);
    float face = gl_FrontFacing ? 1.0 : -1.0;
    if (dot(t, t) > 1e-8) {
        t = normalize(t);
        vec3 b = cross(n, t) * v_tangent.w;
        vec3 tn = texture(sampler2D(normal_tex, normal_smp), tex_uv(2)).xyz * 2.0 - 1.0;
        tn.xy *= u_emissive.w;
        n = normalize(mat3(t, b, n) * tn);
    }
    n *= face;

    vec3 mr = texture(sampler2D(metallic_roughness_tex, metallic_roughness_smp), tex_uv(1)).rgb;
    float metallic = clamp(u_pbr.x * mr.b, 0.0, 1.0);
    float roughness = clamp(u_pbr.y * mr.g, 0.03, 1.0);
    float alpha = roughness * roughness;
    float alpha_sq = alpha * alpha;
    vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
    vec3 c_diff = base.rgb * (1.0 - metallic);

    vec3 v = normalize(u_camera_pos.xyz - v_world_pos);
    float n_dot_v = clamp(abs(dot(n, v)), 1e-4, 1.0);

    float ao = 1.0 + u_pbr.z * (texture(sampler2D(occlusion_tex, occlusion_smp), tex_uv(3)).r - 1.0);
    vec3 color = u_ambient.rgb * (c_diff + f0) * ao;

    int count = int(u_material.y);
    for (int i = 0; i < 8; i++) {
        if (i >= count) {
            break;
        }
        vec3 light_dir;
        float falloff = 1.0;
        int type = int(u_light_dir_type[i].w + 0.5);
        if (type == 0) {
            light_dir = normalize(u_light_dir_type[i].xyz);
        } else {
            vec3 to_frag = v_world_pos - u_light_pos_range[i].xyz;
            float d = length(to_frag);
            light_dir = d > 1e-6 ? to_frag / d : vec3(0.0, -1.0, 0.0);
            falloff = attenuation(d, u_light_pos_range[i].w);
            if (type == 2) {
                falloff *= spot_factor(dot(light_dir, normalize(u_light_dir_type[i].xyz)),
                                       u_light_spot[i].x, u_light_spot[i].y);
            }
        }
        vec3 l = -light_dir;
        float n_dot_l = dot(n, l);
        if (n_dot_l <= 0.0 || falloff <= 0.0) {
            continue;
        }
        vec3 h = normalize(l + v);
        float n_dot_h = clamp(dot(n, h), 0.0, 1.0);
        float v_dot_h = clamp(dot(v, h), 0.0, 1.0);

        vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - v_dot_h, 5.0);
        float dd = n_dot_h * n_dot_h * (alpha_sq - 1.0) + 1.0;
        float distribution = alpha_sq / (PI * dd * dd);
        float gv = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - alpha_sq) + alpha_sq);
        float gl = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - alpha_sq) + alpha_sq);
        float visibility = 0.5 / max(gv + gl, 1e-6);

        vec3 diffuse = (1.0 - fresnel) * c_diff / PI;
        vec3 specular = fresnel * distribution * visibility;
        /* a light with a map is shadowed by it; the rest light the stage as before */
        int slot = int(u_light_spot[i].z);
        if (slot >= 0) {
            float lit_here = shadow_factor(slot, v_world_pos, n, n_dot_l);
            /* the tint stands in for light a shadow "keeps", deepest where it's darkest */
            color += u_shadow_tint[slot].rgb * (1.0 - lit_here) * c_diff;
            falloff *= lit_here;
        }
        color += u_light_radiance[i].rgb * falloff * n_dot_l * (diffuse + specular);
    }

    if (u_env.x > 0.0) {
        /* split-sum image-based lighting, libwgt's: diffuse from the irradiance, specular
         * from the prefiltered cube's mip for this roughness and the BRDF table */
        float lod_roughness = sqrt(clamp(alpha, 0.0, 1.0)); /* the perceptual roughness the mips are made by */
        vec3 irradiance = max(eval_sh(env_dir(n, u_env)), vec3(0.0));
        vec3 r = env_dir(reflect(-v, n), u_env);
        vec3 prefiltered = textureLod(samplerCube(env_tex, env_smp), r, lod_roughness * u_env.y).rgb;
        vec2 ab = texture(sampler2D(brdf_tex, brdf_smp), vec2(n_dot_v, lod_roughness)).rg;
        color += (irradiance * c_diff + prefiltered * (f0 * ab.x + ab.y)) * ao * u_env.x;
    }

    color += u_emissive.rgb * srgb_to_linear(texture(sampler2D(emissive_tex, emissive_smp), tex_uv(4)).rgb);
    frag_color = vec4(to_display(color, u_tonemap), base.a);
}
@end

@program model vs fs
