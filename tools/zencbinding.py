"""The Zen-C binding: libwgf's C API declared for zc, so a Zen-C program knows every libwgf
type, constant, and call, written by tools/gen_binding.py into bindings/zenc/wgf.zc from
the headers' model (tools/headers.py). Standard library only.

A Zen-C program imports it (`import "wgf.zc"`) and builds against a staged variant's
include/ and library. The file includes every public header, so the C compiler sees the
real declarations, and declares to zc what they hold, emitting nothing that could clash:
  - a handle type, and wgf_color_t, as an alias of u32 (a typedef the C compiler takes
    again, being the same);
  - a struct (the math values) as `extern struct`, opaque to zc: the C compiler checks
    its fields;
  - an enum's or a callback's type as a bare `extern`, and each enum value, and each
    #define of a number or a color, as a typed `extern let`, which emit nothing;
  - each exported call as `extern fn`, its doc comment above it, its types as the header
    spells them, so the C compiler checks the prototype zc emits against the header's:
    a generator that maps a type wrong fails the build;
  - each `static inline` call (the math) and each function-like macro (wgf_log_info) as
    a bare `extern`, which emits nothing: callable, as C defines them.
A parameter named like a Zen-C keyword gets a trailing underscore.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'bindings' / 'zenc' / 'wgf.zc'

# C spellings to Zen-C's: what the headers use (a new one stops the generator, named)
NUMBERS = {
    'void': 'void', 'bool': 'bool', 'char': 'char', 'float': 'f32', 'double': 'f64',
    'int': 'i32', 'unsigned int': 'u32', 'unsigned char': 'u8', 'size_t': 'usize',
    'int8_t': 'i8', 'uint8_t': 'u8', 'int16_t': 'i16', 'uint16_t': 'u16', 'int32_t': 'i32',
    'uint32_t': 'u32', 'int64_t': 'i64', 'uint64_t': 'u64',
}

# names Zen-C reserves, which a parameter can't take
KEYWORDS = {
    'alias', 'and', 'as', 'asm', 'assert', 'async', 'await', 'break', 'comptime', 'const', 'continue', 'def',
    'defer', 'do', 'else', 'enum', 'expect', 'extern', 'false', 'fn', 'for', 'if', 'impl', 'import', 'in',
    'include', 'let', 'loop', 'match', 'mut', 'not', 'null', 'opaque', 'or', 'return', 'self', 'sizeof',
    'string', 'struct', 'test', 'trait', 'true', 'type', 'union', 'use', 'volatile', 'while',
}


class ZencError(Exception):
    pass


def zen_type(spelling, api, where):
    """A C type as the header spells it, in Zen-C: libwgf's own names kept, numbers
    mapped, `const` and pointers carried."""
    text = spelling.strip()
    const = text.startswith('const ')
    if const:
        text = text[len('const '):].strip()
    stars = text.count('*')
    base = text.replace('*', '').strip()
    if base in api.typedefs or base in api.structs or base in api.enums:
        name = base
    elif base in NUMBERS:
        name = NUMBERS[base]
    else:
        raise ZencError(f'{where}: no Zen-C type for C type `{spelling}`')
    return ('const ' if const else '') + name + '*' * stars


def doc_lines(doc, indent=''):
    return ''.join(f'{indent}/// {line}'.rstrip() + '\n' for line in doc.strip().splitlines()) if doc.strip() else ''


def param_name(name):
    return name + '_' if name in KEYWORDS else name


def alias_target(spelling, api, where):
    if spelling in api.typedefs:
        return spelling
    if spelling in NUMBERS:
        return NUMBERS[spelling]
    raise ZencError(f'{where}: no Zen-C type for C typedef target `{spelling}`')


def write(api, mark):
    """bindings/zenc/wgf.zc's text."""
    out = [f'// {mark}\n',
           '//\n',
           "// libwgf's C API for Zen C (tools/zencbinding.py says how it is declared): `import \"wgf.zc\"`,\n",
           "// built against a staged variant's include/ and library (bindings/zenc/README.md).\n\n"]
    for header in api.headers:
        out.append(f'include <{Path(header).name}>\n')
    out.append('\n// Types: handles are numbers; the math values and every enum are C\'s own.\n')
    for name, (target, header) in api.typedefs.items():
        if '(' in target:  # a function pointer (wgf_app_callback_t): C's own, named for zc
            out.append(f'extern {name};\n')
        else:
            out.append(f'alias {name} = {alias_target(target, api, name)};\n')
    for name in api.structs:
        out.append(f'extern struct {name};\n')
    by_header = {}
    for name, enum in api.enums.items():
        by_header.setdefault(enum.header, []).append(('enum', enum.offset, name))
    for name, fn in api.functions.items():
        by_header.setdefault(fn.header, []).append(('fn', fn.offset, name))
    for name, header in api.macros.items():
        by_header.setdefault(header, []).append(('macro', 1 << 40, name))
    defines = {}
    for name, (value, header) in api.defines.items():
        value = value.strip()
        if value.isdigit():
            defines.setdefault(header, []).append((name, 'i32'))
        elif value.startswith('wgf_color_get('):
            defines.setdefault(header, []).append((name, 'wgf_color_t'))
    for header in api.headers:
        items = sorted(by_header.get(header, []), key=lambda item: item[1])
        if not items and header not in defines:
            continue
        out.append(f'\n// ---- {Path(header).name} ----\n')
        for name, kind in defines.get(header, []):
            out.append(f'extern let {name}: {kind};\n')
        for kind, _, name in items:
            if kind == 'enum':
                enum = api.enums[name]
                out.append('\n' + doc_lines(api.type_docs.get(name, '') or enum.doc))
                out.append(f'extern {name};\n')
                for value in enum.values:
                    out.append(f'extern let {value}: {name};\n')
            elif kind == 'macro':
                out.append(f'\n/// C\'s macro {name}(...), as the header defines it.\nextern {name};\n')
            else:
                fn = api.functions[name]
                out.append('\n' + doc_lines(fn.doc))
                if fn.static:  # C sugar, callable as the header defines it
                    out.append(f'extern {name};\n')
                    continue
                if not fn.exported:
                    continue
                if fn.variadic:
                    raise ZencError(f'{name}: a variadic call has no Zen-C declaration yet')
                params = ', '.join(f'{param_name(p.name)}: {zen_type(p.type, api, name)}' for p in fn.params)
                returns = zen_type(fn.returns, api, name)
                line = f'extern fn {name}({params})' + ('' if returns == 'void' else f' -> {returns}') + ';\n'
                if len(line) > 120:  # one parameter a line
                    inner = ',\n'.join(f'    {param_name(p.name)}: {zen_type(p.type, api, name)}' for p in fn.params)
                    line = f'extern fn {name}(\n{inner}\n)' + ('' if returns == 'void' else f' -> {returns}') + ';\n'
                out.append(line)
    return ''.join(out)


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('zencbinding.py: a module the other tools import: there is nothing to run')
