# Wardens

Helix's engine trial on [libwgf](https://github.com/whirlinggizmo/libwgf), in Haxe: a warden in a walled arena holding out against waves of enemies, wearing the gear she finds. The game is helix's `docs/trials/templates/wardens/spec.md`, unchanged, and so are its assets (`assets/CREDITS.md`); it is graded on that trial's `grading.md`. WASD or the arrows (a pad's left stick, or a finger anywhere on the play area) move her; her weapons strike on their own; Q (a shoulder button, or the bottom-right button on a touch screen) switches them once she has both; Escape or P (Start, the top-right button) pauses.

```sh
wgf serve                                   # in a browser, reloading as you save
wgf run                                     # on the desktop
wgf autopilot playthrough.autopilot         # the playthrough, headless (--web: in a browser)
wgf autopilot shots.autopilot --web --screenshots DIR   # the same, its play, cards and after saved as PNGs
wgf export                                  # export/web and export/desktop, each smoke-tested
```

- **Run options:** `--enemies=N` (N alive from the start, the cap held there), `--seed=N` (the spawns' seed: the same seed plays the same enemies), `--frames=N` (quit after N frames), `--play` (past the title); on the web, `?enemies=200&seed=1`.
- **For helix's `take_screenshots.py`:** a run directory holding `native/wardens` (the desktop export's program) with `assets/` beside it and beside the directory, run from that directory. Play is at (495, 561) on its title in a 1280 by 720 window.
- `src/Main.hx`: the screens (title, play, pause, level up, game over), the arena, the camera and lights, the HUD and the health bar, the controls. `src/Game.hx`: the play, at a fixed 60 ticks a second. `src/Character.hx`: a modular character, four part models on one skeleton given Animations.glb's clips, parts swapped in step. `src/Fx.hx`: sparks, the target ring, spawn warnings, the hit flash's materials. `src/Menu.hx`: the menus' widgets, nine-sliced from `assets/ui/`. `src/Save.hx`: the best time and the volume, in `user:wardens.save`.
- **Shaders:** `shaders/ring.glsl` (the target ring), `warning.glsl` (spawn warnings, their fill passed in), `flash.glsl` (the hit flash on the skinned, lit enemies), packed by `python3 tools/pack_shaders.py` into `assets/shaders/*.wgfshader`, which are committed (`--check` fails when one is stale).
- **Probes** (for the autopilots): `wardens.ready`, `wardens.state` (0 loading, 1 title, 2 play, 3 paused, 4 level up, 5 game over), `wardens.alive`, `wardens.kills`, `wardens.level`, `wardens.hp`, `wardens.time`, `wardens.x`, `wardens.z`, `wardens.gun`.
- What it took, and where libwgf had no call for what the spec asks: [NOTES.md](NOTES.md).
