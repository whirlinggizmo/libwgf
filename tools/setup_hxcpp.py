#!/usr/bin/env python3
"""Set up hxcpp, pinned (tools/hxcpp.py): install the release every native Haxe build here
uses into libwgf's haxelib folder if it isn't there yet, from lib.haxe.org, and print
where it is. Run it once on a machine that builds natively (the tools that build with
hxcpp skip, saying so, until it has); CI runs it on a cache miss, and reads the pin
with --version.

    tools/setup_hxcpp.py [--version]

Exits 0 when the pinned release is ready. Standard library only.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import hxcpp  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--version', action='store_true', help='print the pinned release alone, setting nothing up')
    args = ap.parse_args()
    if args.version:
        print(hxcpp.VERSION)
        return 0
    why = hxcpp.install()
    if why:
        print(f'setup_hxcpp: {why}', file=sys.stderr)
        return 1
    print(f'setup_hxcpp: hxcpp {hxcpp.VERSION} in {hxcpp.resolved()}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
