# Example assets

The files the C and Haxe examples, and some tests, load. Each one not made in this repository says where it came from and its license.

## Third party

From libwgt's `examples/assets/`, which took them from wgrender's, unchanged:

| File | Work | Author | Source | License |
|---|---|---|---|---|
| `fonts/JetBrainsMono/JetBrainsMono-Regular.ttf`, `-Bold.ttf`, `-Italic.ttf` (the last two from the project's repository, fonts/ttf/, 2026-11-20) | JetBrains Mono | JetBrains | [jetbrains.com/lp/mono](https://www.jetbrains.com/lp/mono/) | [SIL OFL 1.1](fonts/JetBrainsMono/OFL.txt) |
| `fonts/Komika/KOMIKAH_.ttf` | Komika Hand | © 1999-2001 WolfBainX & Apostrophic Labs | [apostrophiclab.com](https://www.apostrophiclab.com) | Freeware, unmodified; terms in `fonts/Komika/readme.txt`, the authors' notes in `Komika.txt` |
| `music/a_hero_is_born.mp3` | "A Hero Is Born", its first 90 s | HoliznaCC0 | [Free Music Archive](https://freemusicarchive.org/music/holiznacc0/retro-gamer-soundtrack/a-hero-is-born/) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `models/kit/*.glb` | seven of Racing Kit 2.0's models (the long straight, the grandstand, the barrier wall, two trees, the large light post, the pylon), for scene-stream | Kenney | [kenney.nl/assets/racing-kit](https://kenney.nl/assets/racing-kit); copied from `games/racer/assets/kit/`, written there by the racer's `tools/gen_kit.py` (triangles unchanged, colors linear, a roughness) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `ui/kenney/*.png`, `ui/kenney/font.ttf` | UI Pack (2.0): a panel, a button's four states, rails, a handle, a check box and its check, three icons, a star button, and the Kenney Future font, for ui-widgets | Kenney | [kenney.nl](https://kenney.nl); copied from helix-raylib's `examples/extras/gui_skins/web/assets/` | [CC0](https://creativecommons.org/publicdomain/zero/1.0/), `ui/kenney/License.txt` |
| `sounds/click_004.ogg` | UI Audio | Kenney | [kenney.nl](https://kenney.nl) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `environments/venice_sunset_1k.hdr` | Venice Sunset | Greg Zaal | [Poly Haven](https://polyhaven.com/a/venice_sunset) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `textures/wabbit_alpha.png` | the bunnymark's bunny, for `tools/bench/bunnymark` | unknown | raylib 5.5's `examples/textures/resources/`, unchanged; redistributed with the bunnymarks of raylib, PixiJS, and others | unknown: raylib's resources table lists its author and license as unknown; carried by Rob's decision (2026-11-14) |
| `environments/studio_small_09_1k.hdr` | Studio Small 09 | Sergej Majboroda | [Poly Haven](https://polyhaven.com/a/studio_small_09) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `models/robot.glb` | the robot, skinned, with its 14 animations, for gfx-skinning and the robots benchmark | @Quaternius ([patreon.com/quaternius](https://www.patreon.com/quaternius)) | raylib 6.2's `examples/models/resources/models/gltf/`, unchanged, copied from helix's `engines/raylib/parity/assets/` | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `models/woman_casual/woman_casual.glb` | A Quaternius woman, one outfit, with its Idle_Neutral, Run and Walk clips (wgrender baked its colours into a palette texture, and drew the blob shadow under it): the character of libwgt's and wgrender-c's examples, for the programs that are theirs in the size table | Quaternius | [Ultimate Modular Women](https://quaternius.com) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `models/characters/Adventurer_*.glb`, `Animations.glb` | Ultimate Modular Women: the Adventurer outfit (a head, body, legs and feet, each a part model on one skeleton) and the pack's 24 animations, for gfx-skinning's modular character and Wardens | Quaternius | [quaternius.com](https://quaternius.com), converted from the pack's FBX files in Blender (materials opaque, normals smoothed by angle), copied from helix's Wardens trial assets | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |

## Whirling Gizmo's own

Made by Whirling Gizmo for its libraries, under libwgf's MIT license:

- `sheets/gem.png`, `sheets/gem.json`: a gem turning, four frames drawn in a script and packed by `tools/pack_sprite_sheet.py` (trimmed, 120 ms each); the feature test's atlas.
- `ui/options.rml`, `ui/options.rcss`, `ui/skin.png`: the ui-document example's menu, written here; the sprite sheet made by `tools/gen_ui_skin.py`.
- `textures/flame.png`, `textures/particle.png`: from libwgt's `examples/assets/`, made by wgrender's `tools/gen_particle_textures.py`; `textures/flame.{bc7,astc,etc2}.ktx` made from the PNG by `tools/compress_textures.py`.
- `textures/tiles_normal.png`: from libwgt's `examples/assets/`, made by wgrender's `tools/gen_material_assets.py`.
- `sprites/logo/wg-logo-bw-alpha.png`, `sprites/logo/wg-logo-white-alpha.png`: the Whirling Gizmo logo, from libwgt's `examples/assets/`; `sprites/logo/wg-logo-bw-alpha.{bc7,astc,etc2}.ktx` made from the PNG by `tools/compress_textures.py`.
- `textures/skybox.png`: a sky over a checkered plain as a cubemap's 4 by 3 cross, drawn in `tools/gen_skybox.py`.
- `fonts/gizmo5/gizmo5.fnt`, `gizmo5_0.png`, `gizmo5_image.png`: Gizmo 5, a pixel font of printable ASCII drawn in `tools/gen_bitmap_font.py`, as an AngelCode BMFont and as an image of glyphs.
- `textures/noise.png`: made by `tools/gen_noise.py` (wgrender's `tools/gen_noise_texture.py`, written again): the dissolve shader's.
- `textures/tiles.png`: from libwgt's `examples/assets/`, made by wgrender's `tools/gen_tiles.py`; the page `tools/check_asset_cache.py` visits draws it.
- `maps/island.tmj`, `maps/tiles.tsj`, `scenes/island.scene`: gfx-tilemap's Tiled map (a `.tmj` and its tileset `.tsj` over `textures/tiles.png`, Tiled's JSON written by hand, not in Tiled), and the prefabs its objects spawn, written here.
- `textures/water.png`: four frames of rippling water, gfx-tilemap's animated tile, drawn in a throwaway script.
- `textures/iso_tiles.png`, `textures/hex_tiles.png`, `textures/terrain.png`, `textures/clouds.png`, and `maps/iso_tiles.tsj`, `maps/hex_tiles.tsj`, `maps/terrain.tsj`, `maps/grids_iso.tmj`, `maps/grids_staggered.tmj`, `maps/grids_hex.tmj`, `maps/grids_infinite.tmj`: gfx-tilemap-grids' maps and their tiles, gfx-tilemap's dirt trail (a corner terrain set) and clouds (an image layer), drawn and written in `tools/gen_tilemaps.py`.
