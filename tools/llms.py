"""libwgf's index for agents, as llms.txt (llmstxt.org; PixiJS's is the model): a line a Haxe
type of the binding, by layer, and a line a C example, each its link and its doc's first
sentence, from the headers as tools/headers.py reads them and the types as tools/mapping.py
places them. tools/gen_apidoc.py writes it beside the API reference, linking its pages;
tools/run_entrant.py into a trial entrant's docs/, linking its copies. Standard library only."""
from pathlib import Path

import headers
import mapping

ROOT = Path(__file__).resolve().parents[1]


def layer_of(header):
    """libwgf/gfx/include/wgf_model.h -> 'gfx'; include/wgf.h -> 'libwgf'."""
    parts = Path(header).parts
    return parts[1] if len(parts) > 3 and parts[0] == headers.LAYER_ROOT else 'libwgf'


def layer_order():
    return ['libwgf'] + list(headers.LAYERS)


LAYER_ROLES = {  # docs/CONVENTIONS.md's layer table, its "Role" column
    'libwgf': 'the version', 'math': 'vectors, matrices, and the math on them',
    'core': 'handles, logging, time, files, loading, resources, probes, random numbers',
    'platform': 'the window, its events, input, an autopilot\'s input', 'asset': 'where a resource\'s file comes from',
    'gfx': 'drawing', 'audio': 'sounds and voices', 'ecs': 'components and behaviors on actors, systems, scenes',
    'physics3d': 'bodies, sensors, and wheeled vehicles, on Jolt', 'ui': 'layout and widgets',
    'app': 'the runtime: run, the loop, ticks'}
HAND_WRITTEN = ('Runtime', 'Behavior', 'Vec2', 'Vec3', 'Vec4')  # the Haxe binding's own classes, not generated


def first_sentence(text, most=180):
    """A doc's first sentence, on one line, cut at a colon or a comma past `most` characters."""
    flat = ' '.join(text.split())
    at = flat.find(': a handle of kind')
    if at > 0:
        flat = flat[:at] + '.'
    at = flat.find('. ')
    if at > 0:
        flat = flat[:at + 1]
    for mark in (': ', ', ', ' '):
        if len(flat) <= most:
            break
        at = flat.find(mark, 60)
        if 0 < at < most:
            flat = flat[:at] + '...'
    return flat


def listed(names, most=8):
    return ', '.join(f'`{n}`' for n in names[:most]) + (', ...' if len(names) > most else '')


def haxe_doc(path):
    """A hand-written Haxe file's first doc comment."""
    text = path.read_text(encoding='utf-8')
    start = text.find('/**')
    return text[start + 3:text.find('**/', start)] if start >= 0 else ''


def example_doc(path):
    """A C example's own comment: the first block comment in its main.c."""
    text = path.read_text(encoding='utf-8')
    start = text.find('/*')
    return text[start + 2:text.find('*/', start)].replace('\n *', '\n') if start >= 0 else ''


def index(api, type_link, example_link, root=ROOT):
    """The index's text for the API `api` (headers.read's): type_link(name) and
    example_link(name) give the links; `root` is the checkout whose hand-written Haxe files
    and examples it reads."""
    binding = mapping.Binding(api)
    members, enum_types = {}, {haxe: cname for cname, haxe in binding.enums.items()}
    layers = {}
    for f in binding.functions:
        if f.name in mapping.SKIPPED:
            continue
        try:
            haxe, member, _ = binding.place(f)
        except mapping.MapError:
            continue
        if haxe not in members:
            layers.setdefault(layer_of(f.header), []).append(haxe)
        members.setdefault(haxe, []).append(member)
    for haxe, cname in enum_types.items():
        if haxe not in members:
            layers.setdefault(layer_of(api.enums[cname].header), []).append(haxe)
    class_docs = binding.class_docs()
    lines = ['# libwgf', '',
             '> libwgf is a C framework for games, natively and on the web. This is its Haxe binding, package `wgf` '
             '(`import wgf.*;`): each kind of handle is a type with its calls as methods (`Actor`, `Sprite`, `Model`), each '
             'header\'s other calls a class of statics (`Window.setTitle`, `Keyboard.isDown`), and each enum an enum '
             'abstract. A program starts with `Runtime.run(init, tick, frame, shutdown)`.', '',
             'Each line names a type, links its file (every call there with its doc comment), and says what it is for. '
             'Read the types a task needs, and the C examples for how the calls fit together: the Haxe names are the C '
             'calls\' own (`wgf_sprite_set_texture` is `sprite.setTexture`).', '',
             '## runtime (the Haxe binding\'s own)', '']
    lines += [f'- [{name}]({type_link(name)}): {first_sentence(haxe_doc(root / "bindings/haxe/src/wgf" / (name + ".hx")))}'
              for name in HAND_WRITTEN]
    for layer in (name for name in layer_order() if name in layers):
        lines += ['', f'## {layer}: {LAYER_ROLES.get(layer, "")}', '']
        said = set()
        for haxe in sorted(layers[layer]):
            if haxe in enum_types:
                cname = enum_types[haxe]
                line = first_sentence(api.enums[cname].doc or '')
                values = 'its values ' + listed([short for short, _, _ in mapping.enum_members(binding, cname)])
                line = f'{line} An enum, {values}.' if line and line not in said else f'An enum, {values}.'
            else:
                line = first_sentence(class_docs.get(haxe) or '')
                calls = listed(members.get(haxe, []))
                line = line if line and line not in said else f'Calls: {calls}.'
            said.add(line)
            lines.append(f'- [{haxe}]({type_link(haxe)}): {line}')
    lines += ['', '## examples, in C', '']
    for example in sorted(p for p in (root / 'examples' / 'c').iterdir() if (p / 'main.c').exists()):
        lines.append(f'- [{example.name}]({example_link(example.name)}): {first_sentence(example_doc(example / "main.c"))}')
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('llms.py: a module the other tools import: there is nothing to run')
