# Wardens' assets

Every file under `characters/`, `models/`, `sound/` and `ui/`, and `README.md` (what each file is), is helix's Wardens trial's, copied unchanged from helix's `docs/trials/templates/wardens/assets/` (helix commit 9dbb6b6c), as the trial's spec asks: each one CC0, credited below as helix credits it. The `../gen_assets.py` and `../render_outfits.py` named below are helix's, beside that folder. `shaders/*.wgfshader` are this game's own, packed from `../shaders/` by `../tools/pack_shaders.py` (MIT, as libwgf).

| File | Work | Author | Source | License |
| --- | --- | --- | --- | --- |
| `ui/panel.png` | UI Pack: RPG Expansion, `panel_brown.png` | Kenney | [kenney.nl](https://kenney.nl/assets/ui-pack-rpg-expansion), license.txt in the zip | [CC0](https://creativecommons.org/publicdomain/zero/1.0/), verified |
| `ui/button.png` | UI Pack: RPG Expansion, `buttonLong_brown.png`, `buttonLong_beige.png`, `buttonLong_brown_pressed.png` and `buttonLong_grey.png` stacked as normal, focused, pressed and disabled | Kenney | [kenney.nl](https://kenney.nl/assets/ui-pack-rpg-expansion), license.txt in the zip | [CC0](https://creativecommons.org/publicdomain/zero/1.0/), verified |
| `ui/font.ttf` | Kenney Future (UI Pack 2.0, `Font/Kenney Future.ttf`) | Kenney | [kenney.nl](https://kenney.nl/assets/ui-pack), License.txt in the zip | [CC0](ui/font-license.txt), verified |
| `sound/slash.wav` | Game "RPG Audio", `knifeSlice.ogg`, converted to 44.1 kHz 16-bit mono wav | Kenney | [kenney.nl](https://kenney.nl/assets/rpg-audio), License.txt in the zip | [CC0](https://creativecommons.org/publicdomain/zero/1.0/), verified |
| `sound/shot.wav` | Impact Sounds, `impactMetal_light_001.ogg`, converted | Kenney | [kenney.nl](https://kenney.nl/assets/impact-sounds), License.txt in the zip | [CC0](https://creativecommons.org/publicdomain/zero/1.0/), verified |
| `sound/hit.wav` | Impact Sounds, `impactPunch_medium_001.ogg`, converted | Kenney | [kenney.nl](https://kenney.nl/assets/impact-sounds), License.txt in the zip | [CC0](https://creativecommons.org/publicdomain/zero/1.0/), verified |
| `sound/gem.wav` | Interface Sounds, `pluck_001.ogg`, converted | Kenney | [kenney.nl](https://kenney.nl/assets/interface-sounds), License.txt in the zip | [CC0](https://creativecommons.org/publicdomain/zero/1.0/), verified |
| `sound/level.wav` | Interface Sounds, `confirmation_004.ogg`, converted | Kenney | [kenney.nl](https://kenney.nl/assets/interface-sounds), License.txt in the zip | [CC0](https://creativecommons.org/publicdomain/zero/1.0/), verified |
| `sound/music.ogg` | "A Hero Is Born", the first 120 s of the full track, 2 s fade in and 4 s fade out, Ogg Vorbis q3. Original: `~/media/music/holiznacc0/a_hero_is_born-full.mp3` | HoliznaCC0 | [Free Music Archive](https://freemusicarchive.org/music/holiznacc0/retro-gamer-soundtrack/a-hero-is-born/) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/), verified (the page's statement, recorded in `~/media/music/holiznacc0/README.txt`) |
| `characters/*.glb` | Ultimate Modular Women: its animations (`Animations.glb`, 24 clips), all ten outfits (Adventurer, Casual, Formal, Medieval, Punk, SciFi, Soldier, Suit, Witch, Worker: each a head, body, legs and feet), the sword and the gun, converted from the pack's FBX files in Blender, materials made opaque and normals smoothed by angle (75°) | Quaternius | [quaternius.com](https://quaternius.com), License.txt in the pack | [CC0](https://creativecommons.org/publicdomain/zero/1.0/), verified |
| `models/floor_tile_large.glb`, `floor_tile_rocks.glb`, `wall.glb`, `wall_half.glb`, `pillar.glb`, `barrel.glb`, `box.glb`, `crates.glb`, `keg.glb`, `chest.glb` | Dungeon Remastered 1.0, the pieces of the same names (`floor_tile_large`, `floor_tile_large_rocks`, `wall`, `wall_half`, `pillar`, `barrel_large`, `box_small`, `crates_stacked`, `keg`, `chest`), copied unchanged and renamed | Kay Lousberg (KayKit) | [GitHub](https://github.com/KayKit-Game-Assets/KayKit-Dungeon-Remastered-1.0), [kaylousberg.com](https://www.kaylousberg.com) | [CC0](models/LICENSE.txt), verified |

The Kenney sounds, UI art and font were downloaded from kenney.nl into a scratch directory, not taken from `~/media`.

CC0 asks for no credit, they're credited anyway.

## The trial's own

- `ui/icons.png`, `ui/icons.txt`: the 21 card, outfit, gear-slot and weapon icons, drawn by `../gen_assets.py`. The ten outfit icons
  hold portraits rendered by `../render_outfits.py` from Quaternius's Ultimate Modular Women (CC0), as HeliX's examples have them.
