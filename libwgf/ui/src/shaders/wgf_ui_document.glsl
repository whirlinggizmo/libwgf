/* What documents draw past plain triangles (libwgf/ui/src/wgf_ui_document_effects.c): RmlUi's
 * shaders (linear, radial, and conic gradients, repeating or not) on a geometry's
 * triangles, and its filters and layers as passes over a whole layer: a layer copied
 * through a color matrix (opacity, brightness, contrast, invert, grayscale, sepia,
 * hue-rotate, saturate, all premultiplied), blurred along one axis, made a tinted shadow,
 * or cut by a mask's alpha. Colors are premultiplied, as RmlUi's and the layers' are.
 * Written once in sokol-shdc's annotated GLSL; tools/gen_shaders.py makes
 * wgf_ui_document.glsl.h, committed. */

@vs vs_gradient
layout(binding=0) uniform gradient_vs_params {
    mat4 mvp; /* the context's pixels into clip space */
};
in vec2 position;
in vec2 texcoord; /* the element's pixels, which the gradient is laid out in */
in vec4 color0;   /* straight */
out vec2 at;
out vec4 tint;

void main() {
    gl_Position = mvp * vec4(position, 0.0, 1.0);
    at = texcoord;
    tint = vec4(color0.rgb * color0.a, color0.a);
}
@end

@fs fs_gradient
layout(binding=1) uniform gradient_fs_params {
    vec4 shape;        /* x: 0 linear, 1 radial, 2 conic; y: 1 repeating; z: the stops */
    vec4 pv;           /* xy: p; zw: v (linear: to its end; radial: 1 / its radii; conic: its angle's cos, sin) */
    vec4 positions[16]; /* x: each stop's position, along the gradient */
    vec4 colors[16];   /* premultiplied */
};
in vec2 at;
in vec4 tint;
out vec4 frag_color;

float stop_at(int i) {
    return positions[i].x;
}

void main() {
    int count = int(shape.z);
    vec2 d = at - pv.xy;
    float t;
    if (shape.x < 0.5) {
        t = dot(pv.zw, d) / max(dot(pv.zw, pv.zw), 1e-6);
    } else if (shape.x < 1.5) {
        t = length(pv.zw * d);
    } else {
        vec2 q = mat2(pv.z, -pv.w, pv.w, pv.z) * d;
        t = 0.5 + atan(-q.x, q.y) / 6.28318531;
    }
    if (shape.y > 0.5 && count > 1) {
        float t0 = stop_at(0);
        float t1 = stop_at(count - 1);
        if (t1 > t0) t = t0 + mod(t - t0, t1 - t0);
    }
    vec4 c = colors[0];
    for (int i = 1; i < 16; i++) {
        if (i >= count) break;
        float a = stop_at(i - 1);
        float b = stop_at(i);
        float f = b > a ? clamp((t - a) / (b - a), 0.0, 1.0) : (t >= b ? 1.0 : 0.0);
        c = mix(c, colors[i], f * f * (3.0 - 2.0 * f));
    }
    frag_color = tint * c;
}
@end

@vs vs_pass
layout(binding=0) uniform pass_vs_params {
    mat4 proj;    /* the pass's pixels into clip space */
    vec4 rect;    /* the quad drawn: x0, y0, x1, y1 */
    vec4 uv_rect; /* what of the layer it shows: u0, v0, u1, v1 */
};
out vec2 uv;

void main() {
    int i = gl_VertexIndex; /* two triangles, no vertices: 0, 1, 2 and 0, 2, 3 of the corners */
    vec2 c = vec2((i == 1 || i == 2 || i == 4) ? 1.0 : 0.0, (i == 2 || i == 4 || i == 5) ? 1.0 : 0.0);
    gl_Position = proj * vec4(mix(rect.xy, rect.zw, c), 0.0, 1.0);
    uv = mix(uv_rect.xy, uv_rect.zw, c);
}
@end

@fs fs_blit
layout(binding=1) uniform blit_fs_params {
    mat4 matrix; /* premultiplied color in, out: opacity and the color filters */
};
layout(binding=0) uniform texture2D blit_tex;
layout(binding=0) uniform sampler blit_smp;
in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = matrix * texture(sampler2D(blit_tex, blit_smp), uv);
}
@end

@fs fs_blur
layout(binding=1) uniform blur_fs_params {
    vec4 blur; /* xy: the uv between two taps; z: sigma in taps; w: the taps each side */
};
layout(binding=0) uniform texture2D blur_tex;
layout(binding=0) uniform sampler blur_smp;
in vec2 uv;
out vec4 frag_color;

void main() {
    vec4 sum = vec4(0.0);
    float total = 0.0;
    int n = int(blur.w);
    for (int i = -32; i <= 32; i++) {
        if (i < -n || i > n) continue;
        float x = float(i);
        float w = exp(-0.5 * x * x / (blur.z * blur.z));
        sum += w * texture(sampler2D(blur_tex, blur_smp), uv + x * blur.xy);
        total += w;
    }
    frag_color = sum / total;
}
@end

@fs fs_shadow
layout(binding=1) uniform shadow_fs_params {
    vec4 shadow_color; /* premultiplied */
    vec4 offset;       /* xy: the shadow's offset, in uv */
};
layout(binding=0) uniform texture2D shadow_tex;
layout(binding=0) uniform sampler shadow_smp;
in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = shadow_color * texture(sampler2D(shadow_tex, shadow_smp), uv - offset.xy).a;
}
@end

@fs fs_mask
layout(binding=0) uniform texture2D mask_tex;
layout(binding=0) uniform sampler mask_smp;
layout(binding=1) uniform texture2D mask_mask;
in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(mask_tex, mask_smp), uv) * texture(sampler2D(mask_mask, mask_smp), uv).a;
}
@end

@program ui_gradient vs_gradient fs_gradient
@program ui_blit vs_pass fs_blit
@program ui_blur vs_pass fs_blur
@program ui_shadow vs_pass fs_shadow
@program ui_mask vs_pass fs_mask
