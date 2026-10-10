#!/usr/bin/env python3
"""Regenerate gfx's committed shaders.

    tools/gen_shaders.py [--check]

Each shader in libwgf/gfx/src/shaders/, written once in sokol-shdc's annotated GLSL, becomes
the *.glsl.h beside it: the sources and reflection for every backend gfx runs on -- GL
4.1 natively and WebGL2 (GLSL 300 es) on the web -- each behind #if
defined(SOKOL_<backend>) (--ifdef), so a build carries only its own. Run it after
changing a shader, and commit the headers with it: building libwgf never runs
sokol-shdc. The pinned sokol-shdc comes from tools/setup_shdc.py, which downloads it
once. --check generates them into a scratch folder and fails when a committed header
differs (a shader changed without its header); where sokol-shdc can't be had (no
network, no binary for this machine), the check says SKIPPING and passes, as checks do
(CONVENTIONS.md, "Tooling"). Standard library only.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / 'libwgf' / 'gfx' / 'src' / 'shaders'
SLANG = 'glsl410:glsl300es'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='fail when a committed header is stale')
    opts = parser.parse_args()
    found = subprocess.run([sys.executable, str(ROOT / 'tools' / 'setup_shdc.py')], stdout=subprocess.PIPE, text=True)
    if found.returncode != 0:
        if opts.check:
            print('gen_shaders: SKIPPING the check (no sokol-shdc to be had: setup_shdc.py said why)')
            return 0
        return found.returncode
    shdc = found.stdout.strip()
    stale = []
    with tempfile.TemporaryDirectory() as scratch:
        for source in sorted(SHADERS.glob('*.glsl')):
            if '@program' not in source.read_text(encoding='utf-8'):
                continue  # blocks the others include (wgf_gfx_display.glsl), no program of its own
            header = source.with_name(source.name + '.h')
            out = Path(scratch) / header.name if opts.check else header
            done = subprocess.run([shdc, '-i', source.name, '-o', str(out), '-l', SLANG, '--ifdef',
                                   '--no-log-cmdline'], cwd=SHADERS)
            if done.returncode != 0:
                print(f'gen_shaders: {source.relative_to(ROOT)} failed')
                return 1
            if opts.check and (not header.exists() or header.read_bytes() != out.read_bytes()):
                stale.append(str(header.relative_to(ROOT)))
            elif not opts.check:
                print(f'gen_shaders: {header.relative_to(ROOT)}')
    if stale:
        print('gen_shaders: stale (run tools/gen_shaders.py and commit): ' + ', '.join(stale))
        return 1
    if opts.check:
        print('gen_shaders: every shader header is current')
    return 0


if __name__ == '__main__':
    sys.exit(main())
