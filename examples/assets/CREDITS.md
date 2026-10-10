# Example assets

The files the C and Haxe examples, and some tests, load. Each one not made in this repository says where it came from and its license.

## Third party

From libwgt's `examples/assets/`, which took them from wgrender's, unchanged:

| File | Work | Author | Source | License |
|---|---|---|---|---|
| `fonts/JetBrainsMono/JetBrainsMono-Regular.ttf` | JetBrains Mono | JetBrains | [jetbrains.com/lp/mono](https://www.jetbrains.com/lp/mono/) | [SIL OFL 1.1](fonts/JetBrainsMono/OFL.txt) |
| `fonts/Komika/KOMIKAH_.ttf` | Komika Hand | © 1999-2001 WolfBainX & Apostrophic Labs | [apostrophiclab.com](https://www.apostrophiclab.com) | Freeware, unmodified; terms in `fonts/Komika/readme.txt`, the authors' notes in `Komika.txt` |
| `music/a_hero_is_born.mp3` | "A Hero Is Born", its first 90 s | HoliznaCC0 | [Free Music Archive](https://freemusicarchive.org/music/holiznacc0/retro-gamer-soundtrack/a-hero-is-born/) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `sounds/click_004.ogg` | UI Audio | Kenney | [kenney.nl](https://kenney.nl) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `environments/venice_sunset_1k.hdr` | Venice Sunset | Greg Zaal | [Poly Haven](https://polyhaven.com/a/venice_sunset) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `environments/studio_small_09_1k.hdr` | Studio Small 09 | Sergej Majboroda | [Poly Haven](https://polyhaven.com/a/studio_small_09) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |

## Whirling Gizmo's own

Made by Whirling Gizmo for its libraries, under libwgf's MIT license:

- `textures/flame.png`, `textures/particle.png`: from libwgt's `examples/assets/`, made by wgrender's `tools/gen_particle_textures.py`; `textures/flame.{bc7,astc,etc2}.ktx` made from the PNG by `tools/compress_textures.py`.
- `textures/tiles_normal.png`: from libwgt's `examples/assets/`, made by wgrender's `tools/gen_material_assets.py`.
- `sprites/logo/wg-logo-bw-alpha.png`, `sprites/logo/wg-logo-white-alpha.png`: the Whirling Gizmo logo, from libwgt's `examples/assets/`; `sprites/logo/wg-logo-bw-alpha.{bc7,astc,etc2}.ktx` made from the PNG by `tools/compress_textures.py`.
- `textures/tiles.png`: from libwgt's `examples/assets/`, made by wgrender's `tools/gen_tiles.py`; the page `tools/check_asset_cache.py` visits draws it.
