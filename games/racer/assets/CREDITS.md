# Assets

The files racer loads, from `assets/` beside its program (`Asset.setHost("assets")`). Each one not made for the game says where it came from and its license.

| File | What | Source | License |
|---|---|---|---|
| `kit/*.glb` | seventeen of Kenney's Racing Kit 2.0 (110 models): the road's straights, start grid and corners, the barrier wall, the overhead gantry, and two trees; and the trackside props: the pylon (the cones), red, green and checkered flags, red and green banner towers, the large light post, and the grandstand | [kenney.nl/assets/racing-kit](https://kenney.nl/assets/racing-kit) (`kenney_racing-kit.zip`, sha256 8a71ea16...c6c0), by Kenney; each `.glb` written from the kit's by `tools/gen_kit.py`, its triangles unchanged (its node's offset dropped, its colors linear, a roughness) | CC0 1.0 (crediting is the kit's request, not a condition) |
| `sky/sky.hdr` | a clear afternoon sky, 1024 by 512 equirectangular Radiance HDR, its sun 25 degrees up, 40 from -z toward +x | libwgf's `examples/assets/environments/sky.hdr`, made by libwgf's `tools/gen_sky.py`, copied unchanged | MIT, © 2026 Whirling Gizmo, LLC (libwgf's LICENSE) |

The track is the game's own, written by `tools/gen_track.py` from its layout of the kit's pieces: `track/collision.glb`, `track/section_*.scene`, `scenes/track.scene`, and the `manifest.json` in each directory. So are its two images, drawn by the same tool: `signs/signs.png` (the signs' atlas: chevrons, a board, a disc) and `fx/puff.png` (the tire smoke's particle).
