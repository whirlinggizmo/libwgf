#!/usr/bin/env python3
"""Pack a custom material shader into a .wgfshader file (wgf_shader.h).

    tools/pack_shader.py name.glsl [-o name.wgfshader]

Your file has a fragment shader `@fs fs` and may have a vertex hook `@block vertex`;
wgf.glsl (libwgf/gfx/shaders/ in the checkout, ../shaders/ beside this tool in a staged
variant) says what libwgf gives them. This puts wgf.glsl in front of your file, adds
libwgf's model vertex shader, compiles the result for every backend libwgf runs on (GL
4.1 and WebGL2) with sokol-shdc, and writes one .wgfshader file: each backend's sources,
what sokol needs to know about them, and the parameters by name. Load it with
wgf_shader_create(path). Only needed to make the file, never at run time; sokol-shdc is
downloaded the first time (tools/setup_shdc.py). The parameters are read from the
compiled SPIR-V (tools/spirv.py), not from the source.

A fragment shader that includes wgf_screen instead of wgf_surface is a screen effect
(wgf_render_add_effect): it gets one program, drawn over the finished frame.

libwgt's tools/pack_shader.py (wgrender's), with one model program where libwgt has
static, skinned, and two sprite ones (libwgf has no skinning yet, and custom sprites are
milestone 2.5's). Standard library only.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import spirv  # noqa: E402

HERE = Path(__file__).resolve().parent
# the interface: beside this tool in a staged variant (share/wgf/tools and share/wgf/shaders),
# or in the checkout
INTERFACES = [HERE.parent / 'shaders' / 'wgf.glsl', HERE.parent / 'libwgf' / 'gfx' / 'shaders' / 'wgf.glsl']
SLANGS = ['glsl410', 'glsl300es']
FORMAT_VERSION = 1

LIBRARY_BLOCKS = (0, 1)  # libwgf's: the vertex shader's (camera, time, first record), the fragment's frame
FS_PARAMS_BINDING = 2
VS_PARAMS_BINDING = 3
MAX_TEXTURES = 8
MAX_PARAMS = 32   # libwgf/gfx/src/material/wgf_gfx_shader_priv.h
NAME_MAX = 32
PARAM_TYPES = {  # std140: size, alignment
    'float': (4, 4),
    'int': (4, 4),
    'vec2': (8, 8),
    'vec3': (12, 16),
    'vec4': (16, 16),
}


def fail(message):
    sys.exit(f'pack_shader: {message}')


def shdc_path():
    """The pinned sokol-shdc (tools/setup_shdc.py, which fetches it the first time)."""
    found = subprocess.run([sys.executable, str(HERE / 'setup_shdc.py')], stdout=subprocess.PIPE, text=True)
    if found.returncode != 0:
        fail('no sokol-shdc (tools/setup_shdc.py said why)')
    return found.stdout.strip()


def parse_yaml(text):
    """The subset of YAML sokol-shdc writes: maps of `key: value`, and lists of maps ("-"
    alone on a line, the map indented below it)."""
    lines = [line for line in text.splitlines() if line.strip()]
    pos = 0

    def indent(line):
        return len(line) - len(line.lstrip(' '))

    def scalar(value):
        value = value.strip()
        digits = value[1:] if value.startswith('-') else value
        if digits.isdigit():
            return int(value)
        if value in ('true', 'false'):
            return value == 'true'
        return value

    def block(level):
        nonlocal pos
        if lines[pos].strip() == '-':
            items = []
            while pos < len(lines) and indent(lines[pos]) == level and lines[pos].strip() == '-':
                pos += 1
                items.append(block(indent(lines[pos])))
            return items
        result = {}
        while pos < len(lines) and indent(lines[pos]) == level and lines[pos].strip() != '-':
            key, _, value = lines[pos].strip().partition(':')
            pos += 1
            result[key] = scalar(value) if value.strip() else block(indent(lines[pos]))
        return result

    return block(0)


def shdc_errors(output, temp_path, user_path, user_first_line):
    """sokol-shdc's errors and warnings, once each, the temporary file's line numbers turned
    back into the user's (or a word that the line is libwgf's)."""
    def remap(line):
        if not line.startswith(temp_path + ':'):
            return line
        number, sep, tail = line[len(temp_path) + 1:].partition(':')
        if not number.isdigit():
            return line
        n = int(number)
        where = (f'{user_path}:{n - user_first_line + 1}' if n >= user_first_line
                 else f'wgf.glsl or the vertex shader added to it (line {n})')
        return where + sep + tail
    lines = [remap(line.strip()) for line in output.splitlines() if ': error' in line or ': warning' in line]
    return '\n'.join(dict.fromkeys(lines)) or 'pack_shader: sokol-shdc failed'


def structure(shdc, path, work, report):
    """How sokol-shdc parses `path`: {block name: input lines}, {fragment shader name: input
    lines}, the lines being where each came from in the input, with every block it includes
    expanded in. From shdc's own parse (its --dump); nothing is compiled."""
    done = subprocess.run([shdc, '-i', path, '-o', str(Path(work) / 'parse'), '-t', work, '-l', 'glsl410',
                           '-f', 'bare_yaml', '-d'], capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit(report(done.stdout + done.stderr))
    snippets, maps, current, section = {}, {}, None, None
    for line in done.stderr.splitlines():  # the dump is on stderr
        if line.startswith('  ') and not line.startswith('   ') and line.strip().endswith(':'):
            section = line.strip()[:-1]  # snippets, block_map, fs_map, ...
        elif section == 'snippets':
            text = line.strip()
            if text.startswith('snippet ') and text.endswith(':'):
                current = snippets.setdefault(int(text[len('snippet '):-1]), {'lines': set()})
            elif current is not None and text.startswith('name: '):
                current['name'] = text[len('name: '):]
            elif current is not None and '(' in text and text.split('(', 1)[0].isdigit():
                current['lines'].add(int(text.split('(', 1)[1].split(')', 1)[0]))
        elif section in ('block_map', 'fs_map') and ' => snippet ' in line:
            name, index = line.strip().split(' => snippet ')
            maps.setdefault(section, {})[name] = snippets[int(index)]['lines']
    return maps.get('block_map', {}), maps.get('fs_map', {})


def own_lines(blocks, name):
    """A block's own lines: its lines, less those of each block it includes (any block whose
    lines it wholly contains)."""
    mine = set(blocks.get(name, ()))
    for other, lines in blocks.items():
        if other != name and lines and lines < blocks.get(name, set()):
            mine -= lines
    return mine


def parameters(work, binding, where):
    """The parameter block at `binding`, as the compiler laid it out: its name, and each
    member's (name, type, offset), from the SPIR-V sokol-shdc saved. (None, []) when no
    shader has one."""
    for module in sorted(Path(work).glob('*.spv')):
        found = spirv.uniform_blocks(module.read_bytes()).get(binding)
        if found:
            name, members = found
            for member, kind, _ in members:
                if kind not in PARAM_TYPES:
                    fail(f'{where} parameter \'{member}\' is {kind}: parameters are float, int, vec2, vec3 or vec4')
            return name, members
    return None, []


def check_program(slang, program, fs_params, vs_params):
    """Refuse what libwgf can't bind: a uniform block or texture outside the bindings
    wgf.glsl gives a shader of its own, or a parameter block of another size."""
    for block in program.get('uniform_blocks', []):
        slot, stage, size = block['slot'], block['stage'], block['size']
        if slot in LIBRARY_BLOCKS:
            continue
        expected = {FS_PARAMS_BINDING: ('fragment', fs_params), VS_PARAMS_BINDING: ('vertex', vs_params)}.get(slot)
        if expected is None or stage != expected[0]:
            fail(f'uniform block binding {slot} ({stage}): parameters go in binding {FS_PARAMS_BINDING} '
                 f'(fragment) or {VS_PARAMS_BINDING} (vertex hook)')
        end = max((offset + PARAM_TYPES[kind][0] for _, kind, offset in expected[1]), default=0)
        if (end + 15) // 16 * 16 != size:
            fail(f'{slang}: the parameter block at binding {slot} is {size} bytes; expected {(end + 15) // 16 * 16}')
    for view in program.get('views', []):
        texture = view['texture']
        if texture['name'].startswith('wgf_'):
            continue  # libwgf's, at bindings 8 and up
        if len(texture['name']) >= NAME_MAX:
            fail(f'texture {texture["name"]}: names are at most {NAME_MAX - 1} characters')
        if texture['stage'] != 'fragment' or texture['type'] != '2d' or texture['slot'] >= MAX_TEXTURES:
            fail(f'texture {texture["name"]}: textures are texture2D in the fragment shader, bindings 0-{MAX_TEXTURES - 1}')


def describe(program):
    """What sokol needs to know about a program, a line each."""
    lines = []
    for attr in program.get('attrs', []):
        lines.append(f'attr {attr["slot"]} {attr.get("glsl_name", "-")}')
    for block in program.get('uniform_blocks', []):
        glsl = (block.get('glsl_uniforms') or [{}])[0]
        lines.append(f'ub {block["slot"]} {block["stage"]} {block["size"]} {glsl.get("glsl_name", "-")} '
                     f'{glsl.get("array_count", 0)}')
    for view in program.get('views', []):
        texture = view['texture']
        lines.append(f'view {texture["slot"]} {texture["stage"]} {texture["name"]} {texture["type"]} '
                     f'{texture["sample_type"]}')
    for sampler in program.get('samplers', []):
        lines.append(f'sampler {sampler["slot"]} {sampler["stage"]} {sampler["name"]} {sampler["sampler_type"]}')
    for pair in program.get('texture_sampler_pairs', []):
        lines.append(f'pair {pair["slot"]} {pair["stage"]} {pair["view_slot"]} {pair["sampler_slot"]} '
                     f'{pair.get("glsl_name", "-")}')
    return lines


def pack(source, out):
    """Pack `source` (a .glsl file) into `out`; a line saying what it wrote."""
    interface_path = next((p for p in INTERFACES if p.exists()), None)
    if interface_path is None:
        fail('no wgf.glsl beside this tool (../shaders/wgf.glsl) or in the checkout (libwgf/gfx/shaders/wgf.glsl)')
    shdc = shdc_path()
    user = source.read_text(encoding='utf-8')
    interface = interface_path.read_text(encoding='utf-8')
    first = interface.count('\n') + 2  # the user's line 1 in the files made here

    # What the file holds, as sokol-shdc parses it: whether it has a vertex hook, and whether
    # its fragment shader includes wgf_screen (a screen effect, not a surface).
    with tempfile.TemporaryDirectory(prefix='pack_shader-parse.') as parse_dir:
        parse_path = str(Path(parse_dir) / source.name)
        Path(parse_path).write_text(interface + '\n' + user, encoding='utf-8')
        blocks, fragments = structure(shdc, parse_path, parse_dir,
                                      lambda output: shdc_errors(output, parse_path, str(source), first))
    if 'fs' not in fragments:
        fail(f'{source}: needs a fragment shader `@fs fs` (see wgf.glsl)')
    vertex_hook = 'vertex' in blocks
    screen = bool(own_lines(blocks, 'wgf_screen') & fragments['fs'])
    if screen and vertex_hook:
        fail(f'{source}: a screen effect has no vertex hook (it draws over the finished frame)')
    if screen:
        generated = '\n@vs wgf_vs_screen\n@include_block wgf_vs_screen_main\n@end\n\n@program screen wgf_vs_screen fs\n'
    else:
        generated = ('\n@vs wgf_vs_model\n@include_block wgf_vs_model_uniforms\n'
                     f'@include_block {"vertex" if vertex_hook else "wgf_vertex_default"}\n'
                     '@include_block wgf_vs_model_main\n@end\n\n@program model wgf_vs_model fs\n')

    with tempfile.TemporaryDirectory(prefix='pack_shader.') as work:
        combined_path = str(Path(work) / source.name)
        Path(combined_path).write_text(interface + '\n' + user + '\n' + generated, encoding='utf-8')
        done = subprocess.run([shdc, '-i', combined_path, '-o', str(Path(work) / 'out'), '-t', work,
                               '-l', ':'.join(SLANGS), '-f', 'bare_yaml', '--save-intermediate-spirv'],
                              capture_output=True, text=True)
        if done.returncode != 0:
            sys.exit(shdc_errors(done.stdout + done.stderr, combined_path, str(source), first))
        reflection = parse_yaml((Path(work) / 'out_reflection.yaml').read_text(encoding='utf-8'))

        fs_block, fs_params = parameters(work, FS_PARAMS_BINDING, 'fragment')
        vs_block, vs_params = parameters(work, VS_PARAMS_BINDING, 'vertex')
        names = [p[0] for p in fs_params + vs_params]
        if any(len(n) >= NAME_MAX for n in names):
            fail(f'parameter names are at most {NAME_MAX - 1} characters')
        if len(names) > MAX_PARAMS:
            fail(f'at most {MAX_PARAMS} parameters')
        if len(set(names)) != len(names):
            fail('parameter names must differ between the vertex and fragment blocks')

        lines = [f'wgfshader {FORMAT_VERSION}', f'kind {"screen" if screen else "surface"}']
        lines += [f'param {name} {kind} fs {offset}' for name, kind, offset in fs_params]
        lines += [f'param {name} {kind} vs {offset}' for name, kind, offset in vs_params]
        blobs, textures = [], None
        for shader in reflection['shaders']:
            for program in shader['programs']:
                check_program(shader['slang'], program, fs_params, vs_params)
                if textures is None:
                    textures = sorted(v['texture']['name'] for v in program.get('views', [])
                                      if not v['texture']['name'].startswith('wgf_'))
                    lines += [f'texture {name}' for name in textures]
                lines.append(f'program {program["name"]} {shader["slang"]}')
                lines += describe(program)
                for stage, key in (('vs', 'vertex_func'), ('fs', 'fragment_func')):
                    code = Path(program[key]['path']).read_bytes()
                    lines.append(f'source {stage} {len(code)}')
                    blobs.append((len(lines), code))
        lines.append('end')

    at = dict(blobs)
    data = bytearray()
    for i, line in enumerate(lines, start=1):
        data += line.encode() + b'\n'
        if i in at:
            data += at[i] + b'\n'
    out.write_bytes(bytes(data))
    return (f'pack_shader: wrote {out} ({"screen effect" if screen else "surface shader"}, '
            f'{len(fs_params) + len(vs_params)} parameter(s), {len(textures or [])} texture(s))')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('source', help='the shader, name.glsl')
    parser.add_argument('-o', dest='out', help='where to write it (default: name.wgfshader beside it)')
    opts = parser.parse_args()
    source = Path(opts.source)
    if source.suffix != '.glsl':
        fail(f'{source}: a shader is a .glsl file')
    print(pack(source, Path(opts.out) if opts.out else source.with_suffix('.wgfshader')))


if __name__ == '__main__':
    main()
