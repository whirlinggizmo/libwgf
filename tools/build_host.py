#!/usr/bin/env python3
"""Build the web host a Haxe program runs on: libwgf linked for the web with no main,
exporting its calls.

    tools/build_host.py [--variant NAME] [--exports FILE] [--out DIR] [--no-stage]

The variant (default wasm32-release; wasm32-debug, or wasm32-debug-headless for node) is
built and staged (as tools/examples.py stages one), unless --no-stage, then linked from
its archive, out/wasm32/<variant>/lib/libwgf.a, as any program outside libwgf links it:

  <out>/wgf-host.js and wgf-host.wasm   an ES module whose default export,
                                        createWgfHost(options), resolves to the host

The full host (the default) exports every call in hosts/web/exports.json, which
tools/gen_binding.py writes from the headers: a program may call anything, as hot
reload needs. --exports names a trimmed list (a JSON file of {"exports": [...]}) for a
game's export, which keeps only what its program calls. Out defaults to the variant's
out/wasm32/<variant>/host/. A headless variant's host is for node: no canvas, its
module built for node alone. Standard library only.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import variants  # noqa: E402
import webhost  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--variant', default=None, help='a web variant (default wasm32-release)')
    ap.add_argument('--exports', help='a trimmed export list, {"exports": [...]} (default: every call)')
    ap.add_argument('--out', help='where to write wgf-host.js and .wasm (default out/wasm32/<variant>/host)')
    ap.add_argument('--no-stage', action='store_true', help='link what is staged, without building it first')
    args = ap.parse_args()
    variant = args.variant or variants.web(debug=False)
    try:
        out = webhost.build(variant, args.exports, args.out, stage=not args.no_stage)
    except RuntimeError as e:
        print(f'build_host: {e}', file=sys.stderr)
        return 1
    size = (out / 'wgf-host.wasm').stat().st_size
    print(f'build_host: {variant} host in {out.relative_to(ROOT).as_posix() if out.is_relative_to(ROOT) else out} '
          f'({size // 1024} KB of wasm)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
