#!/usr/bin/env python3
"""Update scene files from wgf-scene 1 (entities) to wgf-scene 2 (actors), in place.

    tools/update_scene.py FILE... [--check]

The rule (BUILDING.md, "Scene files"):

  - `entity [<name>]` becomes `actor [<name>]`; `prefab`, `from`, `end`, comments and
    blank lines are kept as they are.
  - A block's first kind line (shape2d, sprite, text, emitter2d, model) is the actor's own
    kind. Each further kind line becomes an actor of its own under it, named after its
    kind (`actor ship/emitter2d`), its block right after its parent's. A version 1 entity
    drew each of its kinds from its own place, so that is the same picture.
  - An unnamed entity with such a part is named `_<n>`, n counting from 1 in the file, so
    the part has a path to its parent.
  - Every other line -- the transform, the components, behaviors -- stays the actor's.

A block starting `from` a prefab and adding a kind its prefab hasn't is refused (in
version 2 it would make a second part): the file is left as it is, and the line named.
--check changes nothing and fails naming each file still at version 1. Standard library
only; words are split as the scene parser splits them, by spaces, a comment's `#`
outside quotes.
"""
import argparse
import sys
from pathlib import Path

KINDS = ('shape2d', 'sprite', 'text', 'emitter2d', 'model')


class Refused(Exception):
    pass


def first_word(line):
    words = line.split()
    return words[0] if words and not words[0].startswith('#') else ''


def parse_blocks(lines):
    """The file as items: ('line', text) outside blocks, and ('block', header words,
    [body lines], first line number)."""
    items, block = [], None
    for number, line in enumerate(lines, 1):
        word = first_word(line)
        if block is None:
            if word in ('entity', 'prefab'):
                block = ['block', line.split(), [], number]
            else:
                items.append(('line', line))
        elif word == 'end':
            items.append(tuple(block))
            block = None
        elif word in ('entity', 'prefab'):
            raise Refused(f'line {number}: a block inside another')
        else:
            block[2].append((number, line))
    if block is not None:
        raise Refused(f'line {block[3]}: a block without its end')
    return items


def own_kind(name, prefabs):
    """The kind a prefab's actor has, following `from`: its first kind line's."""
    seen = set()
    while name in prefabs and name not in seen:
        seen.add(name)
        base = None
        for _, line in prefabs[name]:
            word = first_word(line)
            if word in KINDS:
                return word
            if word == 'from':
                base = line.split()[1] if len(line.split()) > 1 else None
        name = base
    return None


def update(text):
    """Version 1's text, as version 2's."""
    lines = text.split('\n')
    if not lines or lines[0].strip() != 'wgf-scene 1':
        raise Refused('line 1: not a wgf-scene 1 file')
    items = parse_blocks(lines[1:])
    prefabs = {words[1]: body for kind, *rest in items if kind == 'block'
               for words, body, _ in [rest] if words[0] == 'prefab' and len(words) > 1}
    out, unnamed = ['wgf-scene 2'], 0
    for item in items:
        if item[0] == 'line':
            out.append(item[1])
            continue
        _, words, body, number = item
        base = None
        for n, line in body:
            if first_word(line) == 'from':
                base = own_kind(line.split()[1], prefabs) if len(line.split()) > 1 else None
                break
        own, mine, parts = base, [], []
        for n, line in body:
            word = first_word(line)
            if word in KINDS and own is None:
                own = word
                mine.append(line)
            elif word in KINDS and word != own:
                if base is not None:
                    raise Refused(f'line {n + 1}: a {word} added to a block from a prefab: '
                                  'write it as its own actor by hand')
                parts.append((word, line))
            else:
                mine.append(line)
        keyword = 'actor' if words[0] == 'entity' else 'prefab'
        name = words[1] if len(words) > 1 else ''
        if parts and not name:
            unnamed += 1
            name = f'_{unnamed}'
        out.append(f'{keyword} {name}'.rstrip())
        out.extend(mine)
        out.append('end')
        for word, line in parts:
            out.append(f'{keyword} {name}/{word}')
            out.append('  ' + line.strip())
            out.append('end')
    return '\n'.join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('files', nargs='+', type=Path)
    parser.add_argument('--check', action='store_true', help='change nothing; fail naming each file at version 1')
    args = parser.parse_args()
    failed = 0
    for path in args.files:
        text = path.read_text(encoding='utf-8')
        version = text.split('\n', 1)[0].strip()
        if version == 'wgf-scene 2':
            print(f'{path}: already wgf-scene 2')
            continue
        if args.check:
            print(f'{path}: {version or "not a scene"}: tools/update_scene.py updates it')
            failed += 1
            continue
        try:
            path.write_text(update(text), encoding='utf-8')
            print(f'{path}: updated to wgf-scene 2')
        except Refused as e:
            print(f'{path}: {e}; left as it is', file=sys.stderr)
            failed += 1
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
