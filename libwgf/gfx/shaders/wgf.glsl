/* libwgf's custom shaders: what libwgf gives a material's shader (wgf_shader.h), libwgt's
 * wgt.glsl (wgrender's wgr.glsl) with libwgt's names wgf_ for wgt_, trimmed to what libwgf
 * draws: models without skinning (libwgf has none yet), and no sprites (custom sprites are
 * milestone 2.5's). tools/pack_shader.py puts this file in front of yours, adds libwgf's
 * vertex shader, and compiles the result for every backend libwgf runs on into a .wgfshader
 * file (wgf_shader_create).
 *
 * Your file has a fragment shader named `fs`, and may have a vertex hook:
 *
 *     @block vertex                           (optional)
 *     void wgf_vertex(inout vec3 position, inout vec3 normal) { ... }   object space;
 *     @end                                    wgf_time() is available
 *
 *     @fs fs
 *     @include_block wgf_surface
 *     layout(binding=2) uniform params { vec4 tint; float speed; };  your parameters
 *     layout(binding=0) uniform texture2D noise_tex;                  your textures
 *     layout(binding=0) uniform sampler noise_smp;
 *     void main() {
 *         vec4 n = texture(sampler2D(noise_tex, noise_smp), wgf_uv0);
 *         wgf_output(tint.rgb * n.r, 1.0);
 *     }
 *     @end
 *
 * Textures 8 and up, samplers 8 and up, and uniform blocks 0 and 1 are libwgf's.
 * Parameters: the members of uniform block binding 2 in the fragment shader and binding 3
 * in the vertex hook, set by name with wgf_material_set_float / _vec2 / _vec3 / _vec4 /
 * _int / _color (float, vec2, vec3, vec4, int; not arrays or matrices). Names must differ
 * between the two blocks. Colors given as wgf_color_t are converted to linear, as built-in
 * materials' are. Textures: each texture2D (up to 8, in the fragment shader, bindings 0-7)
 * is set by its name with wgf_material_set_texture, and sampled with the sampler it is
 * paired with in texture(sampler2D(...)), which wgf_material_set_texture_sampling sets up.
 * A texture that isn't set, or is still loading, is white.
 *
 * Fragment inputs (wgf_surface), world space:
 *   wgf_world_pos   vec3   the surface point
 *   wgf_normal      vec3   the interpolated vertex normal (not normalized; facing out of
 *                         the front face)
 *   wgf_tangent     vec4   xyz tangent, w bitangent sign (glTF's)
 *   wgf_uv0, wgf_uv1 vec2  texture coordinate sets 0 and 1
 *   wgf_color       vec4   the vertex color, linear (white where the mesh has none)
 *   wgf_tint        vec4   this placement's tint, linear rgba (wgf_model_set_tint). It
 *                         comes from the placement's record, so models that differ only by
 *                         tint still share one draw
 * and functions:
 *   wgf_time()             seconds since the program started
 *   wgf_camera_position()  world space
 *   wgf_ambient()          the stage's ambient light, linear rgb
 *   wgf_light_count()      the lights reaching this model (0..8)
 *   wgf_light(i, pos, out to_light)   light i's radiance arriving at world position `pos`
 *                         (linear rgb, attenuated), and the unit direction from `pos`
 *                         toward the light
 *   wgf_environment_intensity()   the stage's environment lighting strength (0: none;
 *                         the functions below then return black)
 *   wgf_environment_diffuse(n)    light from the environment arriving around normal n
 *                         (irradiance / pi, linear rgb): times the diffuse color
 *   wgf_environment_specular(n, v, roughness)   the environment reflected toward the
 *                         viewer (v: unit vector from the surface to the camera), blurred
 *                         for perceptual roughness 0..1 (linear rgb)
 *   wgf_environment_brdf(n_dot_v, roughness)   split-sum scale (x) and bias (y): specular
 *                         reflection = wgf_environment_specular(...) * (f0 * x + y)
 *   wgf_shadow(i, pos, n)  how much of light i reaches `pos` on a surface facing `n`: 1 in
 *                         the open, 0 in shadow. A light that casts (up to four a stage)
 *                         has a map; the rest, and every light on a model that doesn't
 *                         receive shadows, are 1. Multiply it into that light's share, as
 *                         built-in materials do; it has the light's shadow strength in it,
 *                         and fades out where the light's map ends
 *   Built-in materials use exactly these.
 *   wgf_srgb_to_linear(c), wgf_linear_to_srgb(c)
 *   wgf_output(color, alpha)   write the pixel: `color` is linear rgb. Applies the
 *                         placement's tint, the material's alpha cutoff (MASK), and the
 *                         stage's exposure and tone mapping, then encodes sRGB. Call it
 *                         once, at the end.
 * Lighting is linear and, as for built-in materials, the framebuffer holds sRGB. Names
 * starting with wgf_ are libwgf's.
 *
 * SCREEN EFFECTS (wgf_render_add_effect): a fragment shader that includes wgf_screen
 * instead of wgf_surface is a screen effect: it redraws the finished frame, pixel by pixel,
 * and has no surface, lighting or vertex hook. Its material can't be given to a model, and
 * a surface shader's can't be added as an effect.
 *
 *     @fs fs
 *     @include_block wgf_screen
 *     layout(binding=2) uniform params { float strength; };
 *     void main() {
 *         vec2 d = wgf_screen_uv - 0.5;
 *         wgf_output(wgf_screen_color().rgb * (1.0 - dot(d, d) * strength), 1.0);
 *     }
 *     @end
 *
 * Parameters and textures work as above (block binding 2, textures 0-7). Inputs:
 *   wgf_screen_uv     vec2   this pixel, 0..1 from the top-left corner
 *   wgf_screen_color()       the frame here, linear rgba
 *   wgf_screen_color_at(uv)  the frame elsewhere (a blur, chromatic aberration)
 *   wgf_screen_size()  vec2  the frame in pixels, and wgf_screen_texel() = 1 / that
 *   wgf_time(), wgf_srgb_to_linear(c), wgf_linear_to_srgb(c)
 *   wgf_output(color, alpha)  write the pixel (linear rgb, encoded to sRGB). No tint,
 *                         cutoff or tone mapping: an effect draws over the screen. */

@block wgf_color
vec3 wgf_srgb_to_linear(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow((max(c, vec3(0.04045)) + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 wgf_linear_to_srgb(vec3 c) {
    c = clamp(c, vec3(0.0), vec3(1.0));
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0031308)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}
@end

/* ----------------------------------------------------------------- vertex ---- */

@block wgf_vertex_default
void wgf_vertex(inout vec3 position, inout vec3 normal) {
}
@end

/* The model vertex shader's uniforms and inputs, before the hook (which may call
 * wgf_time()). Each placement's matrices and tint are its record in the frame's instance
 * texture, as the built-in shading's are (libwgf/gfx/src/shaders/wgf_gfx_instance.glsl):
 * the draw says where its first record is, and gl_InstanceIndex counts from there. */
@block wgf_vs_model_uniforms
layout(binding=0) uniform wgf_object {
    mat4 wgf_view_proj;
    vec4 wgf_object_time; /* x seconds, y this draw's first record */
};
layout(binding=11) uniform texture2D wgf_instance_tex;
layout(binding=10) uniform sampler wgf_data_smp;
@image_sample_type wgf_instance_tex unfilterable_float
@sampler_type wgf_data_smp nonfiltering
/* locations match libwgf's vertex layout (wgf_gfx_mesh) */
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord0;
layout(location=3) in vec2 texcoord1;
layout(location=4) in vec4 tangent;
layout(location=5) in vec4 color0;
layout(location=0) out vec3 wgf_world_pos;
layout(location=1) out vec3 wgf_normal;
layout(location=2) out vec4 wgf_tangent;
layout(location=3) out vec2 wgf_uv0;
layout(location=4) out vec2 wgf_uv1;
layout(location=5) out vec4 wgf_color;
layout(location=6) out vec4 wgf_tint;

float wgf_time() { return wgf_object_time.x; }

vec4 wgf_instance_texel(int record, int texel) {
    return texelFetch(sampler2D(wgf_instance_tex, wgf_data_smp), ivec2((record % 128) * 8 + texel, record / 128), 0);
}
@end

@block wgf_vs_model_main
void main() {
    vec3 p = position;
    vec3 n = normal;
    int record = int(wgf_object_time.y) + gl_InstanceIndex;
    vec4 r0 = wgf_instance_texel(record, 0), r1 = wgf_instance_texel(record, 1), r2 = wgf_instance_texel(record, 2);
    mat4 model = mat4(vec4(r0.x, r1.x, r2.x, 0.0), vec4(r0.y, r1.y, r2.y, 0.0),
                      vec4(r0.z, r1.z, r2.z, 0.0), vec4(r0.w, r1.w, r2.w, 1.0));
    vec4 n0 = wgf_instance_texel(record, 3), n1 = wgf_instance_texel(record, 4), n2 = wgf_instance_texel(record, 5);
    mat3 normal_mat = mat3(vec3(n0.x, n1.x, n2.x), vec3(n0.y, n1.y, n2.y), vec3(n0.z, n1.z, n2.z));
    wgf_vertex(p, n);
    vec4 world = model * vec4(p, 1.0);
    gl_Position = wgf_view_proj * world;
    wgf_world_pos = world.xyz;
    wgf_normal = normal_mat * n;
    /* the record's last texel: y the tangents' handedness, -1 where the placement mirrors */
    wgf_tangent = vec4(mat3(model) * tangent.xyz, tangent.w * wgf_instance_texel(record, 7).y);
    wgf_uv0 = texcoord0;
    wgf_uv1 = texcoord1;
    wgf_color = color0;
    wgf_tint = wgf_instance_texel(record, 6);
}
@end

/* Screen effects (wgf_render_add_effect): one triangle covering the frame, drawn over the
 * finished one. No vertex buffer and no vertex hook: the fragment shader is the whole
 * effect. */
@block wgf_vs_screen_main
layout(binding=0) uniform wgf_screen_view {
    vec4 wgf_screen_clip; /* y: -1 where clip space is drawn upside down (into a texture, on GL) */
};
layout(location=0) out vec2 wgf_screen_uv;
void main() {
    /* (0,0), (2,0), (0,2): a triangle whose middle is the frame */
    vec2 c = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(c.x * 2.0 - 1.0, (c.y * 2.0 - 1.0) * wgf_screen_clip.y, 0.0, 1.0);
    wgf_screen_uv = vec2(c.x, 1.0 - c.y); /* the top-left corner's 0,0 */
}
@end

/* --------------------------------------------------------------- fragment ---- */

@block wgf_screen
@include_block wgf_color
layout(binding=1) uniform wgf_screen_frame {
    vec4 wgf_screen_info; /* xy size in pixels, z seconds */
};
layout(binding=8) uniform texture2D wgf_screen_tex;
layout(binding=8) uniform sampler wgf_screen_smp;
layout(location=0) in vec2 wgf_screen_uv;
out vec4 wgf_frag_color;

float wgf_time() { return wgf_screen_info.z; }
vec2 wgf_screen_size() { return wgf_screen_info.xy; }
vec2 wgf_screen_texel() { return 1.0 / max(wgf_screen_info.xy, vec2(1.0)); }

/* The frame at `uv` (from the top-left corner), linear rgba. libwgf's frame textures are
 * stored top row first on every backend (a texture drawn into on GL is drawn upside down,
 * libwgf/gfx/src/render/wgf_gfx_render.c), so where libwgt flips by a flag, this doesn't. */
vec4 wgf_screen_color_at(vec2 uv) {
    vec4 c = texture(sampler2D(wgf_screen_tex, wgf_screen_smp), uv);
    return vec4(wgf_srgb_to_linear(c.rgb), c.a);
}

vec4 wgf_screen_color() { return wgf_screen_color_at(wgf_screen_uv); }

void wgf_output(vec3 color, float alpha) {
    wgf_frag_color = vec4(wgf_linear_to_srgb(color), alpha);
}
@end

@block wgf_surface
@include_block wgf_color
layout(binding=1) uniform wgf_frame {
    vec4 wgf_camera_time;    /* xyz camera position, w seconds */
    vec4 wgf_ambient_count;  /* rgb ambient (linear), w number of lights */
    vec4 wgf_output_params;  /* x alpha cutoff (0 none), y tone mapping (0 none, 1 neutral, 2 ACES), z exposure scale */
    vec4 wgf_light_pos_range[8];  /* xyz position, w range (0 unlimited) */
    vec4 wgf_light_dir_type[8];   /* xyz direction the light travels, w type (0 directional, 1 point, 2 spot) */
    vec4 wgf_light_radiance[8];   /* rgb color x intensity, linear */
    vec4 wgf_light_spot[8];       /* x cos(inner angle), y cos(outer angle), z shadow slot (-1: none) */
    vec4 wgf_env;                 /* x intensity (0 none), y the cube's last mip, z/w cos/sin of its rotation */
    vec4 wgf_sh[9];               /* the environment's irradiance / pi, spherical harmonics (xyz) */
    /* shadows: up to four casting lights, a layer of one map each. A light's slot is
     * wgf_light_spot[i].z (-1: none); the rest is per slot */
    mat4 wgf_shadow_mat[4];       /* world -> that light's clip space */
    vec4 wgf_shadow_params[4];    /* x 1/map size, y a texel in world units, z/w bias constant, slope */
    vec4 wgf_shadow_tint[4];      /* rgb what a shadow keeps (linear), w bias texels -> depth */
    vec4 wgf_shadow_extra[4];     /* x strength; a perspective (spot) map: y/z its near and far, w 1 */
    vec4 wgf_shadow_map;          /* x 1 = the map's first row is its top */
};
layout(binding=8) uniform textureCube wgf_env_tex;
layout(binding=8) uniform sampler wgf_env_smp; /* the BRDF table shares it: both are linear, clamped */
layout(binding=9) uniform texture2D wgf_brdf_tex;
layout(binding=10) uniform texture2DArray wgf_shadow_tex;
layout(binding=9) uniform sampler wgf_shadow_smp;
@image_sample_type wgf_shadow_tex depth
@sampler_type wgf_shadow_smp comparison
layout(location=0) in vec3 wgf_world_pos;
layout(location=1) in vec3 wgf_normal;
layout(location=2) in vec4 wgf_tangent;
layout(location=3) in vec2 wgf_uv0;
layout(location=4) in vec2 wgf_uv1;
layout(location=5) in vec4 wgf_color;
layout(location=6) in vec4 wgf_tint;
out vec4 wgf_frag_color;

float wgf_time() { return wgf_camera_time.w; }
vec3 wgf_camera_position() { return wgf_camera_time.xyz; }
vec3 wgf_ambient() { return wgf_ambient_count.rgb; }
int wgf_light_count() { return int(wgf_ambient_count.w + 0.5); }

/* The same falloff as built-in materials (libwgf/gfx/src/shaders/wgf_gfx_model.glsl). */
vec3 wgf_light(int i, vec3 pos, out vec3 to_light) {
    int type = int(wgf_light_dir_type[i].w + 0.5);
    if (type == 0) {
        to_light = -normalize(wgf_light_dir_type[i].xyz);
        return wgf_light_radiance[i].rgb;
    }
    vec3 d = pos - wgf_light_pos_range[i].xyz;
    float dist = length(d);
    vec3 dir = dist > 1e-6 ? d / dist : vec3(0.0, -1.0, 0.0);
    to_light = -dir;
    float falloff = 1.0 / max(dist * dist, 0.01);
    float range = wgf_light_pos_range[i].w;
    if (range > 0.0) {
        float r = dist / range;
        float w = clamp(1.0 - r * r * r * r, 0.0, 1.0);
        falloff *= w * w;
    }
    if (type == 2) {
        float cos_angle = dot(dir, normalize(wgf_light_dir_type[i].xyz));
        float cos_inner = wgf_light_spot[i].x;
        float cos_outer = wgf_light_spot[i].y;
        falloff *= cos_inner - cos_outer <= 1e-6 ? (cos_angle >= cos_outer ? 1.0 : 0.0)
                                                 : smoothstep(cos_outer, cos_inner, cos_angle);
    }
    return wgf_light_radiance[i].rgb * falloff;
}

float wgf_environment_intensity() { return wgf_env.x; }

/* A world direction in the environment's frame (turned by -rotation about +y). */
vec3 wgf_environment_dir(vec3 dir) {
    return vec3(wgf_env.z * dir.x - wgf_env.w * dir.z, dir.y, wgf_env.w * dir.x + wgf_env.z * dir.z);
}

vec3 wgf_environment_diffuse(vec3 n) {
    vec3 d = wgf_environment_dir(n);
    vec3 irradiance = wgf_sh[0].xyz * 0.282095
                    + wgf_sh[1].xyz * (0.488603 * d.y)
                    + wgf_sh[2].xyz * (0.488603 * d.z)
                    + wgf_sh[3].xyz * (0.488603 * d.x)
                    + wgf_sh[4].xyz * (1.092548 * d.x * d.y)
                    + wgf_sh[5].xyz * (1.092548 * d.y * d.z)
                    + wgf_sh[6].xyz * (0.315392 * (3.0 * d.z * d.z - 1.0))
                    + wgf_sh[7].xyz * (1.092548 * d.x * d.z)
                    + wgf_sh[8].xyz * (0.546274 * (d.x * d.x - d.y * d.y));
    return max(irradiance, vec3(0.0)) * wgf_env.x;
}

vec3 wgf_environment_specular(vec3 n, vec3 v, float roughness) {
    vec3 r = wgf_environment_dir(reflect(-v, n));
    return textureLod(samplerCube(wgf_env_tex, wgf_env_smp), r, clamp(roughness, 0.0, 1.0) * wgf_env.y).rgb * wgf_env.x;
}

vec2 wgf_environment_brdf(float n_dot_v, float roughness) {
    return texture(sampler2D(wgf_brdf_tex, wgf_env_smp), vec2(clamp(n_dot_v, 0.0, 1.0), clamp(roughness, 0.0, 1.0))).rg;
}

/* How much of light `i` reaches `pos`: 1 in the open, 0 in shadow, in between across the
 * edge; the built-in shading's shadow_factor, whole (its comments say why each step is as
 * it is). Every light without a map returns 1. */
float wgf_shadow(int i, vec3 pos, vec3 n) {
    int slot = int(wgf_light_spot[i].z);
    if (slot < 0) {
        return 1.0; /* no map: this light casts none, or this model receives none */
    }
    vec3 normal = normalize(n);
    vec3 to_light = -normalize(wgf_light_dir_type[i].xyz);
    float slant = 1.0 - clamp(dot(normal, to_light), 0.0, 1.0);
    vec4 clip = wgf_shadow_mat[slot] * vec4(pos + normal * (wgf_shadow_params[slot].y * (1.0 + 2.0 * slant)), 1.0);
    vec3 ndc = clip.xyz / max(abs(clip.w), 1e-6) * sign(clip.w);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (wgf_shadow_map.x > 0.5) {
        uv.y = 1.0 - uv.y;
    }
    float texels = wgf_shadow_params[slot].z + wgf_shadow_params[slot].w * slant;
    vec4 extra = wgf_shadow_extra[slot];
    float ortho_depth = ndc.z * 0.5 + 0.5 - texels * wgf_shadow_tint[slot].w;
    float near_z = max(extra.y, 1e-4);
    float far_z = max(extra.z, near_z + 1e-3);
    float z = 2.0 * near_z * far_z / max(far_z + near_z - ndc.z * (far_z - near_z), 1e-6);
    float z_slack = max(z - texels * wgf_shadow_params[slot].y * (z / far_z), near_z);
    float spot_depth = (far_z + near_z - 2.0 * near_z * far_z / z_slack) / (far_z - near_z) * 0.5 + 0.5;
    float depth = mix(ortho_depth, spot_depth, step(0.5, extra.w));
    vec2 to_edge = 1.0 - abs(ndc.xy);
    float inside = step(0.0, min(to_edge.x, to_edge.y)) * step(depth, 1.0) * step(0.0, clip.w);
    float edge = clamp(min(to_edge.x, to_edge.y) / 0.1, 0.0, 1.0) * inside;
    float texel = wgf_shadow_params[slot].x;
    vec2 at = clamp(uv, vec2(texel), vec2(1.0 - texel));
    float lit = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            lit += texture(sampler2DArrayShadow(wgf_shadow_tex, wgf_shadow_smp),
                           vec4(at + vec2(float(x), float(y)) * texel, float(slot), clamp(depth, 0.0, 1.0)));
        }
    }
    return mix(1.0, lit / 9.0, edge * wgf_shadow_extra[slot].x);
}

vec3 wgf_tonemap_neutral(vec3 color) {
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

vec3 wgf_tonemap_aces(vec3 color) {
    color *= 0.6;
    return clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
}

void wgf_output(vec3 color, float alpha) {
    color *= wgf_tint.rgb;
    alpha *= wgf_tint.a;
    if (alpha < wgf_output_params.x) {
        discard;
    }
    color *= wgf_output_params.z;
    int mode = int(wgf_output_params.y + 0.5);
    if (mode == 1) {
        color = wgf_tonemap_neutral(color);
    } else if (mode == 2) {
        color = wgf_tonemap_aces(color);
    }
    wgf_frag_color = vec4(wgf_linear_to_srgb(color), alpha);
}
@end
