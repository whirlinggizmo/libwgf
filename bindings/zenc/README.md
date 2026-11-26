# libwgf's Zen-C binding

libwgf from [Zen C](https://github.com/robknopf/zenc): one file, `wgf.zc`, that declares libwgf's whole C API to `zc`. It's generated from libwgf's public headers (`tools/gen_binding.py`, written by `tools/zencbinding.py`), so a new or changed call reaches Zen C with the next regeneration, and CI fails a binding left stale.

Zen C compiles to C, so the binding calls libwgf's C directly: no boundary, nothing copied. On the web the same program builds with Emscripten into one wasm with libwgf.

## What it declares

- **Every exported call,** as `extern fn`, with its header comment above it (`///`). The C compiler checks each prototype `zc` emits against the header's, so a call's types can't drift.
- **Handle types and `wgf_color_t`,** as `alias`es of `u32`: `wgf_actor_t`, `wgf_texture_t`, and the rest.
- **The math values** (`wgf_vec3_t`, `wgf_quat_t`, `wgf_mat4_t`, ...), opaque to `zc`. Their fields work, and the C compiler checks them.
- **Every enum and its values,** and the `#define`s of numbers and colors (`WGF_COLOR_RED`), each typed.
- **The math helpers** (`wgf_vec3_add`, `wgf_mat4_mul`, ...), which the headers define `static inline`, and the log macros (`wgf_log_info`), declared by name: callable as C defines them.

Nothing in it is emitted twice: the file includes libwgf's headers for the C compiler, and declares to `zc` only in forms that emit no C of their own.

## Use

```
import "wgf.zc";

fn frame(_user: void*) {
    let sprite: wgf_actor_t = wgf_sprite_create(0);
    wgf_actor_set_position(sprite, 10.0, 20.0, 0.0);
    wgf_sprite_set_tint(sprite, WGF_COLOR_RED);
}

fn main() -> i32 {
    wgf_app_run(NULL, NULL, frame, NULL, NULL);
    return 0;
}
```

Build against a staged variant (`tools/stage_variant.py`), its headers and its archive:

```
zc transpile -I out/linux-x64/debug/include main.zc -o main.c
cc -I out/linux-x64/debug/include main.c out/linux-x64/debug/lib/libwgf.a -lstdc++ -lpthread -ldl -lm \
   -lX11 -lXi -lXcursor -lXrandr -lGL -o main
```

A headless variant needs only `-lstdc++ -lpthread -ldl -lm` (the archive holds C++: RmlUi and Jolt). tcc builds it the same way, given libstdc++ by path (`g++ -print-file-name=libstdc++.so`), which is how Zen C's hot reload compiles a module.

## What zc checks, and what the C compiler does

Once a program includes any C header, `zc` lets a name it doesn't know through to C, and it never checks a name in capitals (it takes it for a macro). So a misspelled `wgf_` call, constant, or field is caught by the C compiler, at its `.zc` line, not by `zc`. What the binding adds is that `zc` knows every libwgf type, so `let at: wgf_vec3_t` and typed handles need no workaround, and every declaration carries its documentation.

## Checks

`tools/check_binding.py --only generated,zenc`: the binding isn't stale, and its test (`test/main.zc`) is transpiled by `zc`, then built against the staged headless variant and run, by the C compiler and by tcc when there is one. The step is skipped, and says so, without `zc` on PATH, as on CI for now.
