/* Particles (libwgf/gfx/src/emitter/wgf_gfx_emitter.c): libwgt's vs_particle and its
 * fragment stage, from wgt_gfx_sprite.glsl (wgrender's, docs/PLAN-sprites.md, steps 4 and
 * 5), whole. A particle is written once, when it is born; where it is, how big, what color,
 * and how turned all follow from how long ago that was. One instanced draw an emitter, a
 * quad a particle. Written once in sokol-shdc's annotated GLSL; tools/gen_shaders.py makes
 * wgf_gfx_particle.glsl.h, committed.
 *
 * Colors are the framebuffer's: sRGB values, 0..1, as immediate mode's.
 */

@vs vs_particle
layout(binding=0) uniform particle_params {
    mat4 view_proj;
    vec4 axis_x;      /* xyz: the quad's right before spin (3D: the camera's right; 2D: +x on screen) */
    vec4 axis_y;      /* xyz: its up (3D: the camera's up; 2D: up the screen) */
    vec4 gravity_now; /* xyz gravity, w the emitter's time now (seconds) */
    vec4 counts;      /* x size keys, y color keys, z alpha (> 0 the cutoff, < 0 opaque, 0 as it is), w palette colors */
    vec4 source;      /* the texture region: u0, v0, u1, v1 */
    vec4 dynamics;    /* x drag (per second), y stretch (seconds of motion; 0 none) */
    vec4 frames;      /* flipbook: x columns, y rows, z frames (< 2: none), w per second (0: over life) */
    vec4 size_times[2];  /* up to 8 keys over life (0..1), in order */
    vec4 size_values[2];
    vec4 color_times[2];
    vec4 color_values[8];
    vec4 palette[8];     /* each particle's tint, picked at birth */
};
in vec2 corner;
in vec4 born;   /* xyz where, w when */
in vec4 motion; /* xyz velocity, w life (seconds) */
in vec4 shape;  /* x size scale, y spin (radians / s), z angle at birth, w a random 0..1 */
out vec2 uv;
out vec4 color;
out float alpha_mode;

/* Key i (0..7) of a curve packed 4 keys to a vec4, two vec4s to a curve.

   Both halves are read every time and one is chosen arithmetically. The obvious
   `curve[i / 4]`, and the ternary that replaced it, both break Adreno's GLES compiler
   (a 610 on a 2020 driver, and likely its relatives): shdc flattens the block to one
   `uniform vec4 particle_params[33]`, and the driver wants to clamp a dynamic index
   into it. It manages that only when the index is a constant it can see -- a divided
   index it can't bound, and a *branch* around the read hides the index from it just as
   well. Either way the link fails with `cannot compute gv size for oob` and the emitter
   silently draws nothing. A plain `params[i + 17]` is fine, and so is the dynamic
   component `q[i % 4]`; it is the array index alone that has to be constant and
   unconditional. `mix`/`step` keeps it that way through shdc -- check the generated
   glsl300es stays branch-free if you touch this. (libwgt's note.) */
float curve_key(vec4 lo, vec4 hi, int i) { vec4 q = mix(lo, hi, step(3.5, float(i))); return q[i % 4]; }
float size_time(int i) { return curve_key(size_times[0], size_times[1], i); }
float size_value(int i) { return curve_key(size_values[0], size_values[1], i); }
float color_time(int i) { return curve_key(color_times[0], color_times[1], i); }

/* A curve at t: its first key's value before it, its last's after, and between two keys
   the line between them. */
float size_at(float t) {
    int n = int(counts.x);
    float value = size_value(0);
    float prev_t = size_time(0);
    for (int i = 1; i < 8; i++) {
        if (i >= n || t <= prev_t) break;
        float key_t = size_time(i);
        float key_v = size_value(i);
        value = t >= key_t ? key_v : mix(value, key_v, (t - prev_t) / max(key_t - prev_t, 0.000001));
        prev_t = key_t;
    }
    return value;
}

vec4 color_at(float t) {
    int n = int(counts.y);
    vec4 value = color_values[0];
    float prev_t = color_time(0);
    for (int i = 1; i < 8; i++) {
        if (i >= n || t <= prev_t) break;
        float key_t = color_time(i);
        value = t >= key_t ? color_values[i] : mix(value, color_values[i], (t - prev_t) / max(key_t - prev_t, 0.000001));
        prev_t = key_t;
    }
    return value;
}

void main() {
    float age = gravity_now.w - born.w;
    float t = age / motion.w;
    if (age < 0.0 || t >= 1.0) { /* not born yet, or dead: nothing to draw */
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        uv = vec2(0.0);
        color = vec4(0.0);
        alpha_mode = 0.0;
        return;
    }
    /* gravity, and drag: slowing in proportion to speed, towards gravity / drag */
    vec3 pos;
    vec3 vel;
    float drag = dynamics.x;
    if (drag > 0.0001) {
        vec3 terminal = gravity_now.xyz / drag;
        float decay = exp(-drag * age);
        pos = born.xyz + terminal * age + (motion.xyz - terminal) * ((1.0 - decay) / drag);
        vel = terminal + (motion.xyz - terminal) * decay;
    } else {
        pos = born.xyz + motion.xyz * age + 0.5 * gravity_now.xyz * age * age;
        vel = motion.xyz + gravity_now.xyz * age;
    }
    float size = size_at(t) * shape.x;
    float along = size;
    vec3 right;
    vec3 up;
    /* stretched: the quad's up follows the velocity across the screen, as long as the
       distance moved in `stretch` seconds (plus its size), trailing behind */
    vec2 screen_vel = vec2(dot(vel, axis_x.xyz), dot(vel, axis_y.xyz));
    float speed = length(screen_vel);
    if (dynamics.y > 0.0 && speed > 0.00001) {
        vec2 d = screen_vel / speed;
        up = axis_x.xyz * d.x + axis_y.xyz * d.y;
        right = axis_x.xyz * d.y - axis_y.xyz * d.x;
        float extra = speed * dynamics.y;
        along = size + extra;
        pos -= up * (extra * 0.5);
    } else {
        float angle = shape.z + shape.y * age; /* positive turns clockwise on screen */
        float c = cos(angle);
        float s = sin(angle);
        right = axis_x.xyz * c - axis_y.xyz * s;
        up = axis_x.xyz * s + axis_y.xyz * c;
    }
    gl_Position = view_proj * vec4(pos + right * (corner.x * size) + up * (corner.y * along), 1.0);
    /* the flipbook's frame: played once over the life, or looped from a random frame */
    vec4 cell = source;
    if (frames.z > 1.5) {
        float frame = frames.w > 0.0 ? mod(floor(age * frames.w + fract(shape.w * 7.31) * frames.z), frames.z)
                                     : min(floor(t * frames.z), frames.z - 1.0);
        vec2 step = (source.zw - source.xy) / frames.xy;
        cell.xy = source.xy + step * vec2(mod(frame, frames.x), floor(frame / frames.x));
        cell.zw = cell.xy + step;
    }
    uv = vec2(mix(cell.x, cell.z, corner.x + 0.5), mix(cell.y, cell.w, 0.5 - corner.y));
    color = color_at(t);
    int colors = int(counts.w);
    if (colors > 0) {
        color *= palette[min(int(shape.w * float(colors)), colors - 1)];
    }
    alpha_mode = counts.z;
}
@end

@fs fs_particle
layout(binding=0) uniform texture2D particle_tex;
layout(binding=0) uniform sampler particle_smp;
in vec2 uv;
in vec4 color;
in float alpha_mode;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(particle_tex, particle_smp), uv) * color;
    if (alpha_mode > 0.0 && frag_color.a < alpha_mode) {
        discard;
    }
    if (alpha_mode != 0.0) {
        frag_color.a = 1.0;
    }
}
@end

@program particle vs_particle fs_particle
