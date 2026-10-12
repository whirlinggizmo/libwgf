#!/usr/bin/env python3
"""Pack the racer's shaders: each shaders/NAME.glsl into assets/shaders/NAME.wgfshader,
with libwgf's pack_shader.py (wgf_shader.h; BUILDING.md's "Shaders"), which compiles it
against libwgf's wgf.glsl for every backend. The packed files are committed, as the
assets gen_track.py writes are: building and exporting never run this.

    python3 tools/pack_shaders.py            # pack them all again
    python3 tools/pack_shaders.py --check    # fail if a packed file is stale

libwgf's pack_shader.py is found from --libwgf, else $LIBWGF, else a libwgf checkout beside
this repository's (../../libwgf from game/): its tools/pack_shader.py, or a staged
variant's share/wgf/tools/pack_shader.py. The same source packs to the same bytes from
either, so --check holds against a given libwgf.
"""
import argparse
import os
import subprocess
import sys
import tempfile

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = os.path.join(GAME, 'shaders')
OUT = os.path.join(GAME, 'assets', 'shaders')


def pack_tool(libwgf):
    for root in [libwgf, os.environ.get('LIBWGF'), os.path.join(GAME, '..', '..', 'libwgf')]:
        if not root:
            continue
        for rel in ['tools/pack_shader.py', 'share/wgf/tools/pack_shader.py']:
            path = os.path.join(root, rel)
            if os.path.isfile(path):
                return path
    sys.exit("pack_shaders: libwgf's pack_shader.py not found: give --libwgf DIR or set LIBWGF")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='fail if a packed shader is stale')
    parser.add_argument('--libwgf', help="libwgf's checkout or a staged variant")
    args = parser.parse_args()
    tool = pack_tool(args.libwgf)
    names = sorted(f[:-5] for f in os.listdir(SOURCES) if f.endswith('.glsl'))
    os.makedirs(OUT, exist_ok=True)
    stale = []
    with tempfile.TemporaryDirectory() as tmp:
        for name in names:
            out = os.path.join(tmp if args.check else OUT, name + '.wgfshader')
            done = subprocess.run([sys.executable, tool, os.path.join(SOURCES, name + '.glsl'), '-o', out],
                                  capture_output=True, text=True)
            if done.returncode != 0:
                sys.exit(done.stdout + done.stderr)
            if args.check:
                kept = os.path.join(OUT, name + '.wgfshader')
                if not os.path.isfile(kept) or open(kept, 'rb').read() != open(out, 'rb').read():
                    stale.append(name)
            else:
                print(done.stdout.strip())
    if stale:
        sys.exit(f"pack_shaders: stale: {', '.join(stale)} (run tools/pack_shaders.py)")
    if args.check:
        print(f'pack_shaders: {len(names)} shader(s) up to date')


if __name__ == '__main__':
    main()
