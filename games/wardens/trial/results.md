# Wardens on libwgf, by fresh agents (2026-11-20)

What an agent pays to build Wardens with libwgf, beside what helix's entrants paid with helix-raylib and with raylib in C (helix's
`docs/trials/2026-11-15-wardens-baseline/results.md`). Three fresh Sonnet agents built helix's [Wardens](../../../../helix/docs/trials/templates/wardens/spec.md)
natively from the spec alone, in Haxe on libwgf's binding, 3 hours each at most, under the conditions helix's entrants had:
`tools/run_entrant.py` (a bubblewrap sandbox, a private display drawn on the CPU, no sound, an empty home, a worktree of libwgf at
`8d20ceb` of its own each), the brief [brief.md](brief.md) (helix's native brief in libwgf's words: the binding's API files with their
doc comments and libwgf's C examples in place of raylib's headers and examples, `wgf build --desktop` in place of `haxe build.hxml`,
and where the shader packer is). Graded as helix grades: helix's `take_screenshots.py` on each program, then a fresh agent each with
helix's `review-brief.md` unchanged, from the program's source and its screenshots, reading no notes.

The runs started at 15:24, 15:26 and 15:27 on coinspinner, the machine helix's entrants ran on.

## Runs

| run | cost | time | turns | lines | builds | failed | runs | input | cache write | cache read | output | items met |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| libwgf.004 | $2.93 | 17.9 min | 79 | 2,194 | 17 | 2 | 21 | 132 | 232k | 11.1M | 89k | 36 |
| libwgf.005 | $2.77 | 18.8 min | 73 | 2,052 | 16 | 1 | 22 | 142 | 207k | 11.0M | 84k | 36 |
| libwgf.006 | $2.91 | 22.8 min | 82 | 1,920 | 16 | 2 | 27 | 134 | 227k | 11.9M | 82k | 36 |

Lines are the program's `.hx` and `.glsl`. Builds are its `wgf build` commands, failed the ones with a compile error, and runs its runs
of the native program. Tokens from `result.json`. Items met are of the 37 in helix's `grading.md`; the grades are
[grades-004.md](grades-004.md), [grades-005.md](grades-005.md), [grades-006.md](grades-006.md).

Beside helix's (its baseline's means; Sonnet then too, on 2026-11-15):

| | cost | turns | cache read | output | failed builds | items met |
|---|---:|---:|---:|---:|---:|---:|
| raylib C | $2.40 | 61 | 6.2M | 108k | 0 to 1 | 36.7 |
| helix-raylib | $2.51 | 74 | 7.3M | 106k | 4 to 5 | 35.0 |
| libwgf | $2.87 | 78 | 11.3M | 85k | 1 to 2 | 36.0 |

## What it found

**libwgf costs about what helix-raylib does, and grades a little higher.** $2.87 a run against helix-raylib's $2.51 and C's $2.40:
14% over helix-raylib, where each group's runs spread by more than the gap (helix-raylib's $1.96 to $3.02). Every run met 36 of 37,
against helix-raylib's 34 to 36 and C's 36 to 37. The cost is reading, not writing: the most cache read (11.3M against 7.3M) and the
least output (85k against 106k). An agent knows raylib's API from training; libwgf's it read, from 151 API files and 46 examples.

**Fewer failed builds than helix-raylib, all Haxe's.** One or two each: `null` for a handle (two of three: handles are numbers,
`isNone()` and 0), a second type in a module's file (two), a constant expression in an inline variable (one). None was libwgf's API.

**What was graded wrong:**

| run | items wrong |
|---|---|
| libwgf.004 | 9: she doesn't move in move.png, D held 0.75 s (her code reads it; 005 and 006 moved under the same input) |
| libwgf.005 | 14: the attack and the enemies' hit and death blend over 0.1 s, not 0.2 |
| libwgf.006 | 13: each strike restarts the legs' run cycle (helix.001's wrong item too) |

**What the agents met in libwgf** (their notes, read after grading):

- **No `user:` in the binding's docs.** All three saved with `Fs.write` to a plain path, which lands in the program's folder beside
  its assets: the header's overview, which says `user:` is a save's, never reached the Haxe files, and nor did any header's overview
  or any handle's own comment (`Shader.hx` didn't say how a shader is packed; the brief did). Fixed after the trial (`2e2799e`):
  each type's doc is now its typedef's comment and its header's overview.
- **A comment on the wrong call.** A call with no comment of its own after an enum took the comment of the call before the enum
  (`textureFill` the nine-slice's, the device's queries the orientation's). Fixed before the runs (`8d20ceb`), found reading the
  binding as an entrant would.
- **A file root's tint and visibility stop at the root.** Two of three tinted each model under an enemy's parts, and 004 hid each
  of the off weapon's meshes, as libwgf's own Wardens did (its NOTES.md).
- **No world-space health bar.** All three drew it themselves: two in 2D at the projected head (`Camera3d.worldToScreen`), one as a
  rectangle in a 3D draw turned to the camera.
- **Menus drawn by hand.** All three drew their menus' panels and buttons with `Draw.textureNineSlice` and did their own hover and
  clicks (006: the immediate `Ui` can't be skinned); libwgf's skinned controls are RmlUi documents', which none reached for.
- **200 enemies at about 10 fps** on the software renderer here, as every engine's entrants found: four skinned parts each,
  1.3 million triangles a frame; each game's own update took 1 to 3 ms.

## The first round

A first round (`libwgf.001` to `.003`, 14:51 to 15:17, at the same commit) isn't counted. The three shared one worktree, which
`wgf build` stages again, fresh, each time, so one's staging replaced `libwgf.a` as another linked, and the machine's disk filled
(build output of finished work), failing `.003`'s builds with link errors and "No space left on device". `run_entrant.py` now gives
each entrant a worktree of its own and refuses a nearly full disk (`d0d1e85`). Those runs cost $1.83, $3.49 and $2.57.
