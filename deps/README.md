# Dependencies

Third-party code libwgf compiles, copied in and pinned. Nothing is fetched at build time: an update is a deliberate step, reviewed and committed like any other change.

Each one's license, in full, and what a binary built with libwgf must ship are in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). Keep it current when a dependency is added, updated, or altered: an altered source says so at its top and is listed there. Each directory's `VERSION` file records exactly what was copied, from where.

| Dependency | Pinned to | License | Used by | Update |
|------------|-----------|---------|---------|--------|
| [sokol](https://github.com/floooh/sokol), through the Whirling Gizmo fork [robknopf/sokol](https://github.com/robknopf/sokol) | the fork commit and its upstream base in `sokol/VERSION` | zlib | platform (app, glue, time), gfx (gfx, gl, fontstash), audio | copy the headers in `sokol/` and `sokol/util/` together from the fork commit wanted, and record both commits in `sokol/VERSION` |
| [sokol_utils](https://github.com/squk/sokol_utils) | commit 65cbd2a, with wgrender's changes, in `sokol_utils/VERSION` | zlib | platform (window size, style, visibility, focus) | copy `sokol_app_utils.h`, reapply the marked changes, and check it compiles: it reads sokol_app's private state, so check it again whenever `sokol/` is updated |
| [fontstash](https://github.com/memononen/fontstash) | commit 203fec2 in `fontstash/VERSION` | zlib | gfx (text) | copy `src/fontstash.h` unchanged and record the commit |
| [stb_truetype, stb_image](https://github.com/nothings/stb) | v1.26 and v2.30, the version lines at their tops | public domain, or MIT | gfx (fonts; PNG, JPEG, BMP, TGA, GIF) | copy the header from nothings/stb, as it comes |
| [dr_wav, dr_mp3](https://github.com/mackron/dr_libs) | dr_libs master at dfe8377 in `dr/VERSION` (past the last tags, for their fixes for malformed files) | public domain, or MIT No Attribution | audio, natively | copy both together from the commit wanted, unchanged |
| [libogg](https://github.com/xiph/ogg), [libvorbis](https://github.com/xiph/vorbis) | masters at 06a5e02 and 1b75110 in `xiph/VERSION`, past the releases (libogg 1.3.6, libvorbis 1.3.7, its newest) for the decoder's fuzz fixes since; both covered by OSS-Fuzz | BSD 3-Clause (`COPYING` in each) | audio, natively (decoding Ogg Vorbis, through vorbisfile) | copy the files `xiph/VERSION` lists from the commits wanted, unchanged, keeping libwgf's `ogg/include/ogg/config_types.h`, and record the commits and their archives' sha256 there |
| [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono), printable ASCII | as wgrender generated it (the header in `jetbrains-mono/`) | SIL Open Font License 1.1 (`jetbrains-mono/OFL.txt`) | gfx (the built-in font) | subset the release's Regular to U+0020-U+007E, without hinting or layout tables, into a byte array |
| [flecs](https://github.com/SanderMertens/flecs) | v4.0.5's amalgamation in `flecs/VERSION` | MIT | ecs | copy `distr/flecs.c` and `distr/flecs.h` from the release wanted, unchanged |
| [Clay](https://github.com/nicbarker/clay), through the Whirling Gizmo fork [robknopf/clay](https://github.com/robknopf/clay) | the fork commit and its upstream base in `clay/VERSION` | zlib | ui | copy `clay.h` from the fork commit wanted, and record both commits |

## Untrusted input

stb_image and stb_truetype don't check every offset a file gives them, so a broken or hostile file can make them read past its end. gfx checks a font's table directory fits in its file before stb_truetype reads it. Audio files natively go to dr_wav, dr_mp3, and Xiph's vorbisfile, each pinned past its last release for its fuzz fixes; `audio/tests/wgf_audio_decoders_test.c` feeds them cut-short, header-damaged, and byte-flipped WAV, MP3, and Ogg files under the sanitizer presets (libwgt dropped stb_vorbis for corrupting memory on such files). The sanitizer presets' tests load broken and cut-short files of each kind. Scene files and input scripts are libwgf's own formats, read by libwgf's own parsers, which bound every read and refuse what they don't understand.
