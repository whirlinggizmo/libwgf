/* 2D sprites drawn in runs (libwgf/gfx/src/sprite/wgf_gfx_sprite_run.c): the sprites a 2D
 * stage draws one after another, one instanced draw, each sprite a 48-byte record made as it
 * is drawn. A run holds up to 8 textures, and each record says which of them it shows, so a
 * run doesn't end where the texture changes. Written once in sokol-shdc's annotated GLSL;
 * tools/gen_shaders.py makes wgf_gfx_sprites.glsl.h, committed.
 *
 * A record's attributes: its quad's top-left corner and its two edges, already placed in
 * the frame's logical pixels, so any 2D transform is drawn; its region of the texture, as
 * u0, v0, u1, v1; its tint; and its texture's slot. Colors are sRGB values, like immediate
 * mode's: texture x tint. */

@vs vs_sprites
layout(binding=0) uniform sprites_params {
    mat4 projection; /* the frame's 2D projection (OpenGL's clip space) */
};
in vec2 corner;      /* 0, 0 top-left .. 1, 1 bottom-right */
in vec2 inst_origin; /* the top-left corner, placed */
in vec4 inst_edges;  /* xy: the top edge, left to right; zw: the left edge, top to bottom */
in vec4 inst_uv;     /* u0, v0 (top-left), u1, v1 */
in vec4 inst_color;
in float inst_slot;  /* 0 .. 7: which of the run's textures */
out vec2 uv;
out vec4 color;
out float slot;

void main() {
    vec2 placed = inst_origin + corner.x * inst_edges.xy + corner.y * inst_edges.zw;
    gl_Position = projection * vec4(placed, 0.0, 1.0);
    uv = mix(inst_uv.xy, inst_uv.zw, corner);
    color = inst_color;
    slot = inst_slot;
}
@end

@fs fs_sprites
layout(binding=0) uniform texture2D sprites_tex0;
layout(binding=1) uniform texture2D sprites_tex1;
layout(binding=2) uniform texture2D sprites_tex2;
layout(binding=3) uniform texture2D sprites_tex3;
layout(binding=4) uniform texture2D sprites_tex4;
layout(binding=5) uniform texture2D sprites_tex5;
layout(binding=6) uniform texture2D sprites_tex6;
layout(binding=7) uniform texture2D sprites_tex7;
layout(binding=0) uniform sampler sprites_smp0;
layout(binding=1) uniform sampler sprites_smp1;
layout(binding=2) uniform sampler sprites_smp2;
layout(binding=3) uniform sampler sprites_smp3;
layout(binding=4) uniform sampler sprites_smp4;
layout(binding=5) uniform sampler sprites_smp5;
layout(binding=6) uniform sampler sprites_smp6;
layout(binding=7) uniform sampler sprites_smp7;
in vec2 uv;
in vec4 color;
in float slot;
out vec4 frag_color;

void main() {
    /* the slot can differ between neighboring pixels, so the derivatives a mipmap needs are
       taken here, where every pixel takes them, and each branch samples with them */
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);
    vec4 texel;
    if (slot < 0.5) {
        texel = textureGrad(sampler2D(sprites_tex0, sprites_smp0), uv, dx, dy);
    } else if (slot < 1.5) {
        texel = textureGrad(sampler2D(sprites_tex1, sprites_smp1), uv, dx, dy);
    } else if (slot < 2.5) {
        texel = textureGrad(sampler2D(sprites_tex2, sprites_smp2), uv, dx, dy);
    } else if (slot < 3.5) {
        texel = textureGrad(sampler2D(sprites_tex3, sprites_smp3), uv, dx, dy);
    } else if (slot < 4.5) {
        texel = textureGrad(sampler2D(sprites_tex4, sprites_smp4), uv, dx, dy);
    } else if (slot < 5.5) {
        texel = textureGrad(sampler2D(sprites_tex5, sprites_smp5), uv, dx, dy);
    } else if (slot < 6.5) {
        texel = textureGrad(sampler2D(sprites_tex6, sprites_smp6), uv, dx, dy);
    } else {
        texel = textureGrad(sampler2D(sprites_tex7, sprites_smp7), uv, dx, dy);
    }
    frag_color = texel * color;
}
@end

@program sprites vs_sprites fs_sprites
