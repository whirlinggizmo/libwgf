#!/usr/bin/env python3
"""That a program links none of what it doesn't use: a web program's link map (wasm-ld's
-Wl,--Map) read for object files and functions that must not be in it.

    tools/check_link_map.py MAP --absent NAME [NAME ...]

Each NAME is an object file's source (`wgf_ui_skin.c`: its `.c.o` in any archive) or a
function (`wgf_ui_slider`), and fails the check when the map's code holds it: a part of
libwgf that a program reaches only by calling it (CONVENTIONS.md, "Layers and
dependencies") reached anyway, by shared code that names it. wasm-ld drops every function
nothing reaches in any build, so a debug link answers as a release one does. Standard
library only.
"""
import argparse
import sys
from pathlib import Path


def code_entries(text):
    """(object file, function) for each piece of the map's code: wasm-ld writes a section's
    line ("- <addr> <size> CODE"), then each input as `<archive>(<object>):(<function>)`."""
    section = None
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[0] == '-' and parts[3].isalpha() and parts[3].isupper():
            section = parts[3]
            continue
        if section != 'CODE' or len(parts) < 4 or ':(' not in parts[3]:
            continue
        where, function = parts[3].rsplit(':(', 1)
        obj = where.rsplit('(', 1)[1].rstrip(')') if where.endswith(')') else Path(where).name
        yield obj, function.rstrip(')')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('map', type=Path)
    ap.add_argument('--absent', nargs='+', required=True, metavar='NAME',
                    help='an object file\'s source (name.c) or a function the program must not link')
    args = ap.parse_args()
    try:
        text = args.map.read_text(errors='replace')
    except OSError as e:
        sys.exit(f'check_link_map: {e}')
    entries = list(code_entries(text))
    if not entries:
        sys.exit(f'check_link_map: {args.map}: no code in it (not a wasm-ld map?)')
    found = []
    for name in args.absent:
        hits = [f'{o}:{f}' for o, f in entries if (name.endswith('.c') and o == name + '.o') or f == name]
        if hits:
            found.append(f'  {name}: {", ".join(hits[:4])}{" ..." if len(hits) > 4 else ""}')
    if found:
        print(f'check_link_map: FAIL: {args.map.name} links what it doesn\'t call:')
        print('\n'.join(found))
        return 1
    print(f'check_link_map: PASS: {args.map.name} links none of {len(args.absent)} name(s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
