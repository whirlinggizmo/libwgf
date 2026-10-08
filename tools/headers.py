"""libwgf's public API as clang reads it: every function, enum, struct, typedef, and
#define the public headers declare, with the doc comment above each. The tools that need
to know what is in the headers ask this, never the headers' text (CONVENTIONS: no tool
scans source).

    from headers import read
    api = read()                   # None when there is no clang (see find_clang)
    api.functions['wgf_model_create'].params   # [Param(name='mesh', type='wgf_handle_t', ...)]

One translation unit includes every public header (include/ and each layer's
<layer>/include/), and clang parses it once: its JSON AST for the declarations
(-fparse-all-comments attaches each comment to what it precedes), and its preprocessor
(-E -dD) for the #defines, which the AST doesn't keep. Types are clang's spelling
(`const char *`, `wgf_handle_t`) and, beside it, what they are through every typedef
(`unsigned int`): a parameter's from clang, a return's through the typedefs the parse
declared, since clang's JSON gives a function's type only as a spelling. Standard
library only. From wgrender's tools/headers.py.
"""
import json
import os
import shutil
import subprocess
import textwrap
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LAYERS = ('math', 'core', 'platform', 'asset', 'gfx', 'audio', 'ecs', 'physics3d', 'ui', 'app')


@dataclass
class Param:
    name: str
    type: str        # clang's spelling: 'const char *', 'wgf_handle_t', 'wgf_vec3_t'
    canonical: str   # through every typedef: 'unsigned int', 'struct wgf_vec3_t'


@dataclass
class Function:
    name: str
    returns: str     # clang's spelling
    returns_canonical: str  # through every typedef; None for a function pointer
    params: list     # [Param]; empty for (void)
    header: str      # its path from the root: 'gfx/include/wgf_model.h'
    doc: str         # the comment above it, as text ('' when none)
    variadic: bool = False
    exported: bool = False   # marked WGF_API: clang's visibility attribute
    static: bool = False     # static inline: C sugar no binding sees


@dataclass
class Field:
    name: str
    type: str        # the element type for an array: 'int' of `int keys[256]`
    count: int       # 0 when it isn't an array


@dataclass
class Struct:
    name: str
    fields: list     # [Field]
    header: str
    doc: str


@dataclass
class Enum:
    name: str
    values: dict     # {'WGF_LIGHT_SPOT': 2, ...} in declaration order
    header: str
    doc: str


@dataclass
class Api:
    functions: dict = field(default_factory=dict)   # name -> Function, in header order
    structs: dict = field(default_factory=dict)     # typedef name -> Struct
    enums: dict = field(default_factory=dict)       # typedef name -> Enum
    typedefs: dict = field(default_factory=dict)    # name -> (type, header): every other typedef
    defines: dict = field(default_factory=dict)     # name -> (value text, header): object-like
    macros: dict = field(default_factory=dict)      # name -> header: function-like (wgf_log_debug)
    headers: list = field(default_factory=list)     # every public header, from the root


def find_clang(emcc=None):
    """(clang, the Emscripten sysroot to parse against), or (None, None). Emscripten's
    is the one the web build already uses: `emcc` when given (a build passes its
    compiler), $EMSDK, or the emcc on PATH; else a clang on PATH, with the system's
    headers."""
    roots = []
    if emcc:
        roots.append(Path(emcc).resolve().parent.parent)  # upstream/emscripten/emcc -> upstream
    if os.environ.get('EMSDK'):
        roots.append(Path(os.environ['EMSDK']) / 'upstream')
    emcc = shutil.which('emcc') or shutil.which('emcc.bat')
    if emcc:
        roots.append(Path(emcc).resolve().parent.parent)  # upstream/emscripten/emcc -> upstream
    for root in roots:
        for name in ('clang', 'clang.exe'):
            clang = root / 'bin' / name
            sysroot = root / 'emscripten' / 'cache' / 'sysroot'
            if clang.exists() and sysroot.exists():
                return str(clang), str(sysroot)
    clang = shutil.which('clang')
    return (clang, None) if clang else (None, None)


def include_dirs(root=ROOT):
    root = Path(root)
    return [d for d in [root / 'include'] + [root / layer / 'include' for layer in LAYERS] if d.is_dir()]


def public_headers(root=ROOT):
    """Every layer's public headers, from the root, layer by layer; include/'s
    wgf_api.h, which only defines the export macro, is read through them."""
    root = Path(root)
    return [h for layer in LAYERS for h in sorted((root / layer / 'include').glob('*.h'))]


class ClangError(RuntimeError):
    pass


def _run(clang, sysroot, args, source, cwd):
    target = ['--target=wasm32-unknown-emscripten', f'--sysroot={sysroot}'] if sysroot else []
    done = subprocess.run([clang, *target, *args, '-x', 'c', '-'], input=source, cwd=cwd,
                          capture_output=True, text=True)
    if done.returncode != 0:
        first = next((line for line in done.stderr.splitlines() if 'error' in line), done.stderr.strip())
        raise ClangError(f'clang could not parse the public headers:\n  {first}')
    return done.stdout


def walk_files(node, current=None):
    """Carry the last file clang named forward through the dump, which is what its JSON
    means: a location omits the file when it's the one before it. Sets '_file' on every
    node that has a location."""
    def files_in(value):
        if isinstance(value, dict):
            if 'file' in value:
                yield value['file']
            for key in ('spellingLoc', 'expansionLoc', 'begin', 'end'):
                if key in value:
                    yield from files_in(value[key])

    for key in ('loc', 'range'):
        for f in files_in(node.get(key, {})):
            current = f
    if 'loc' in node:
        node['_file'] = current
    for kid in node.get('inner') or ():
        current = walk_files(kid, current)
    return current


def _doc(node, sources):
    """The comment clang attached to a declaration, as text with its lines kept: a list or
    an example reads as the header lays it out.

    Clang says which comment belongs to the declaration and where its text starts; the
    text comes from the header's bytes there, since clang's own pieces can't give it back
    (they lose the lines, a backslash it reads as a command, and "<cache>/<app>", taken
    as HTML: libwgt's TASKS, fixed as wgrender-c's 977b52c fixed it). What is taken off is
    the decoration: the opener, a " * " starting a line, and the indent the lines share."""
    comment = next((k for k in node.get('inner') or () if k.get('kind') == 'FullComment'), None)
    path = node.get('_file')
    if not comment or not path:
        return ''
    if path not in sources:
        sources[path] = Path(path).read_bytes()
    data = sources[path]
    first = comment['range']['begin']['offset']
    start = first  # clang's start is the first text: back over whitespace and * to the opener
    while start > 0 and data[start - 1:start] in (b' ', b'\t', b'\r', b'\n', b'*'):
        start -= 1
    if data[start - 1:start + 1] != b'/*':
        raise ClangError(f'the comment before {node.get("name")} ({path}) is not a /* */ comment; only those are read')
    lines = data[start + 1:data.index(b'*/', first)].decode('utf-8').replace('\r\n', '\n').split('\n')
    lines[0] = lines[0].lstrip('*')
    for i in range(1, len(lines)):
        stripped = lines[i].lstrip()
        if stripped.startswith('*') and not stripped.startswith('*/'):
            lines[i] = stripped[1:]
    lines = [line.rstrip() for line in lines]
    while lines and not lines[-1]:
        lines.pop()
    while lines and not lines[0]:
        lines.pop(0)
    return textwrap.dedent('\n'.join(lines))


def _split_array(spelling):
    """'int[256]' -> ('int', 256); anything else -> (spelling, 0)."""
    if spelling.endswith(']') and '[' in spelling:
        base, bound = spelling[:-1].split('[', 1)
        return base.strip(), int(bound)
    return spelling, 0


def returns_of(fn_type):
    """The return type in a function type's spelling: 'const char *(wgf_handle_t)' ->
    'const char *'; None when it returns a function pointer. clang's JSON gives a
    function's type only as this spelling, whose parameters are its last parenthesized
    group."""
    depth = 0
    for i in range(len(fn_type) - 1, -1, -1):
        c = fn_type[i]
        if c == ')':
            depth += 1
        elif c == '(':
            depth -= 1
            if depth == 0:
                ret = fn_type[:i].strip()
                return None if '(' in ret else ret
    return None


def canonical(spelling, aliases):
    """A type through every typedef, const kept, and under any pointers: `aliases` maps
    each typedef the parse declared to what clang says it is through every typedef. So
    'const wgf_x_t *' is the pointer to what wgf_x_t is."""
    star = spelling.find('*')
    base, pointers = (spelling[:star], spelling[star:]) if star >= 0 else (spelling, '')
    const = base.startswith('const ')
    name = base[len('const '):].strip() if const else base.strip()
    if name in aliases:
        name = aliases[name]
    if const and not name.startswith('const '):
        name = f'const {name}'
    return f'{name} {pointers.strip()}' if pointers else name


_cache = {}


def read(root=ROOT, headers=None, emcc=None):
    """The public API of the libwgf at `root`, read by clang; None when there is no
    clang. `headers` (default: every public header) is what the translation unit
    includes; `emcc`, Emscripten's compiler to find clang beside (find_clang). Raises
    ClangError when clang can't parse it. Cached per process."""
    root = Path(root).resolve()
    headers = [Path(h).resolve() for h in (headers or public_headers(root))]
    key = (root, tuple(headers))
    if key in _cache:
        return _cache[key]
    clang, sysroot = find_clang(emcc)
    if clang is None:
        return None
    source = ''.join(f'#include "{h.as_posix()}"\n' for h in headers)
    # WGF_API_PARSE: the export mark as every platform but Windows has it (wgf_api.h), so a
    # parse on Windows reads it too
    flags = ['-std=c11', '-DWGF_API_PARSE', *[f'-I{d}' for d in include_dirs(root)]]
    ast = json.loads(_run(clang, sysroot, ['-fsyntax-only', '-fparse-all-comments', '-Xclang', '-ast-dump=json',
                                           *flags], source, root))
    walk_files(ast)
    ours = {h: h.relative_to(root).as_posix() for h in headers}
    api = Api(headers=list(ours.values()))
    sources = {}  # each header's bytes, for its comments

    def header_of(node):
        f = node.get('_file')
        return ours.get(Path(f).resolve()) if f else None

    # every typedef the parse declared, the system's included, to what it is through all of them
    aliases = {n['name']: n['type'].get('desugaredQualType', n['type']['qualType'])
               for n in ast.get('inner') or () if n.get('kind') == 'TypedefDecl' and 'name' in n}
    tags, pending = {}, []
    for node in ast.get('inner') or ():
        header = header_of(node)
        if header is None:
            continue
        kind = node.get('kind')
        if kind in ('RecordDecl', 'EnumDecl'):
            tags[node['id']] = node
        elif kind == 'FunctionDecl' and not node.get('isImplicit'):
            params = [Param(p.get('name', ''), p['type']['qualType'],
                            p['type'].get('desugaredQualType', p['type']['qualType']))
                      for p in node.get('inner') or () if p.get('kind') == 'ParmVarDecl']
            returns = returns_of(node['type']['qualType'])
            api.functions.setdefault(node['name'], Function(
                node['name'], returns or node['type']['qualType'],
                canonical(returns, aliases) if returns is not None else None, params, header, _doc(node, sources),
                variadic=bool(node.get('variadic')),
                exported=any(k.get('kind') == 'VisibilityAttr' for k in node.get('inner') or ()),
                static=node.get('storageClass') == 'static'))
        elif kind == 'TypedefDecl':
            pending.append((node, header))

    for node, header in pending:
        name = node['name']
        owned = None
        for kid in node.get('inner') or ():
            decl = kid.get('ownedTagDecl') or kid.get('decl')
            if decl and decl.get('id') in tags:
                owned = tags[decl['id']]
            for grandkid in kid.get('inner') or ():  # an elaborated type: the tag one level down
                decl = grandkid.get('ownedTagDecl') or grandkid.get('decl')
                if decl and decl.get('id') in tags:
                    owned = tags[decl['id']]
        if owned is not None and owned['kind'] == 'RecordDecl':
            fields = []
            for f in owned.get('inner') or ():
                if f.get('kind') != 'FieldDecl':
                    continue
                base, count = _split_array(f['type']['qualType'])
                fields.append(Field(f['name'], base, count))
            api.structs[name] = Struct(name, fields, header, _doc(node, sources) or _doc(owned, sources))
        elif owned is not None and owned['kind'] == 'EnumDecl':
            values, last = {}, -1
            for c in owned.get('inner') or ():
                if c.get('kind') != 'EnumConstantDecl':
                    continue
                value = next((int(k['value']) for k in c.get('inner') or () if 'value' in k), last + 1)
                values[c['name']] = last = value
            api.enums[name] = Enum(name, values, header, _doc(node, sources) or _doc(owned, sources))
        else:
            api.typedefs[name] = (node['type']['qualType'], header)

    # The #defines: the preprocessor's own listing, with the line markers that say which
    # file each came from.
    current = None
    for line in _run(clang, sysroot, ['-E', '-dD', *flags], source, root).splitlines():
        if line.startswith('# ') and '"' in line:
            # a line marker: the file, its backslashes escaped (Windows); or a pseudo-file
            # such as <built-in> or <command line>, which names nothing on disk
            named = line.split('"')[1].replace('\\\\', '\\')
            current = None if named.startswith('<') else Path(named).resolve()
        elif line.startswith('#define ') and current in ours:
            name, _, value = line[len('#define '):].partition(' ')
            if '(' in name:
                api.macros[name.split('(')[0]] = ours[current]
            elif not name.endswith('_H'):
                api.defines[name] = (value.strip(), ours[current])
    _cache[key] = api
    return api


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('headers.py: a module the other tools import: there is nothing to run')
