#!/usr/bin/env python3
"""Check that no line of libwgf's C and C++ is over 120 columns, and that none has a tab.

    tools/check_lines.py [file ...]

CONVENTIONS.md, "Code": four spaces, no tabs, lines up to 120 columns. Checked in every
tracked .c, .h, and .cpp file outside deps/ (third-party code, kept as it comes), but for
the generated shader headers (*.glsl.h, sokol-shdc's output). With files named, only those
are checked. Each line over is printed as path:line: its length. Exit 1 when any is.

With no files named it also checks .clang-format-ignore: every tracked file that embeds
JavaScript (EM_JS or EM_ASM) must be listed there by its path, since clang-format reads that
JavaScript as C and breaks it (=== becomes == =). Standard library only.
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIMIT = 120
SUFFIXES = ('.c', '.h', '.cpp')


def tracked():
    """Every tracked C or C++ file libwgf writes, as a path from the root."""
    done = subprocess.run(['git', 'ls-files'], cwd=ROOT, capture_output=True, text=True, check=True)
    return [p for p in done.stdout.splitlines()
            if p.endswith(SUFFIXES) and not p.startswith('deps/') and not p.endswith('.glsl.h')]


def problems(path):
    """Each line of `path` (from the root) that breaks the rule: (line number, what)."""
    found = []
    for number, line in enumerate((ROOT / path).read_text(encoding='utf-8').split('\n'), 1):
        if len(line) > LIMIT:
            found.append((number, f'{len(line)} columns'))
        if '\t' in line:
            found.append((number, 'a tab'))
    return found


def unlisted():
    """The tracked files embedding JavaScript that .clang-format-ignore doesn't list."""
    listed = {line.strip() for line in (ROOT / '.clang-format-ignore').read_text(encoding='utf-8').splitlines()}
    return [path for path in tracked()
            if any(mark in (ROOT / path).read_text(encoding='utf-8') for mark in ('EM_JS(', 'EM_ASM(', 'EM_ASM_'))
            and path not in listed]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='*', help='files to check, from the root (default: every tracked one)')
    args = ap.parse_args()
    files = args.files or tracked()
    count = 0
    for path in files:
        for number, what in problems(path):
            print(f'{path}:{number}: {what}')
            count += 1
    if not args.files:
        for path in unlisted():
            print(f'{path}: embeds JavaScript, and is not in .clang-format-ignore')
            count += 1
    if count:
        print(f'check_lines: FAIL, {count} problem(s): lines over {LIMIT} columns, tabs, or files to list')
        return 1
    print(f'check_lines: {len(files)} file(s), no line over {LIMIT} columns, no tabs')
    return 0


if __name__ == '__main__':
    sys.exit(main())
