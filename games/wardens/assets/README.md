# Wardens assets

Third-party files, all CC0, are credited one by one in [CREDITS.md](CREDITS.md).

## ui/
- `panel.png`: a 100x100 nine-slice panel (Kenney's brown panel), margins 16 on every side.
- `button.png`: a 190x196 sheet of four 190x49 button states stacked: normal, focused, pressed, disabled. Each state is nine-sliced
  alone, margins 14. The pressed state's art is 45 rows tall, its cell's last 4 rows are transparent.
- `slices.txt`: the margins and the button's layout. The format is unchanged: image, then left, top, right, bottom, then notes. The
  button's cell size is now 190x49.
- `icons.png`, `icons.txt`: 21 icons of 48x48 in an atlas of 8 per row, named by `icons.txt` ("index name" per line): the five cards,
  the ten outfits, the four gear slots and the two weapons. They are drawn by `../gen_assets.py`, the only generated files left. The outfits' icons are portraits of the warden
  in each outfit, rendered in Blender from its four parts by `../render_outfits.py` into `../icon-sources/outfits/`.
  Kenney's Game Icons are generic interface symbols with nothing for swords, guns, bullets or outfits, so none are used.
- `font.ttf`: Kenney Future, CC0, its license text in `font-license.txt`.

## sound/
`slash.wav`, `shot.wav`, `hit.wav`, `gem.wav` and `level.wav` (Kenney, 44.1 kHz 16-bit mono) and `music.ogg` (HoliznaCC0's "A Hero
Is Born", 120 s, faded at both ends, stereo Vorbis). The music doesn't loop seamlessly, its end fades out before it restarts.

## models/
KayKit's Dungeon Remastered pieces as GLB with their textures inside: `floor_tile_large.glb` (4 x 4 units, 0.15 thick),
`floor_tile_rocks.glb` (the same with rocks), `wall.glb` (4 long, 1 thick, 4 high), `wall_half.glb` (2 long), `pillar.glb`
(1.5 square, 4 high), and the props `barrel.glb`, `box.glb`, `crates.glb`, `keg.glb` and `chest.glb`. They are in the kit's own
units, so a game scales them (a barrel is 2 high, so 0.5 makes a meter-high one). Every piece stands on y = 0 and is centered on x
and z, except `wall_half.glb`, which runs from x = 0 to 2. Their license text is `models/LICENSE.txt`. They are optional.

## characters/
Quaternius's Ultimate Modular Women (CC0): each of the ten outfits' head, body, legs and feet (`<Outfit>_Head.glb` and so on),
`Animations.glb`, `Sword.glb` and `Gun.glb`.
