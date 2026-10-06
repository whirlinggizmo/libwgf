#!/usr/bin/env python3
"""Generate the Haxe binding's C surface and typed API from the public headers.

    tools/gen_binding.py [--check]

Reads every public header through clang (tools/headers.py) and writes, under
bindings/haxe/src/wgf/:
  impl/Raw.js.hx     each exported call for the JS target: the wasm host's export by its
                     quoted key, strings on the wasm stack for the call alone, arrays and
                     returned vectors through the binding's slots (impl/Host.js.hx)
  impl/Raw.cpp.hx    the same calls for hxcpp: the C call itself, its arguments cast
  impl/BuiltVersion.hx  the version and the headers' digest the binding was made from
  <Type>.hx          the typed API: an abstract per handle kind, with its calls as its
                     methods; an abstract over a kind for each section whose calls take
                     that kind first (a shape is a node); an enum abstract per enum; and
                     the other calls as statics of their header's class
and hosts/web/exports.json, the full host's exports. One name per C call: every exported
call is exactly one member of the typed API, or listed in SKIPPED with its reason
(docs/BINDINGS.md). Every generated file says so at its top; --check writes nothing and
fails when one is stale or missing. A call it can't map stops it, named. Standard library
only.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import binding as places  # noqa: E402
import headers  # noqa: E402

ROOT = places.ROOT
OUT = places.SOURCE
EXPORTS = places.EXPORTS
MARK = places.MARK

# Exported calls the typed API doesn't make one member of, and why. The runtime's own
# (hand-written in bindings/haxe/src/wgf/) still reach C exactly once.
SKIPPED = {
    'wgf_app_run': 'the runtime: wgf.App.run installs its trampolines once (callbacks are C function pointers)',
}

# The runtime's calls into the host besides the API's: kept by every host.
HOST_EXTRA = ['_malloc', '_free']

VECTORS = {'wgf_vec2_t': ('Vec2', 2), 'wgf_vec3_t': ('Vec3', 3), 'wgf_vec4_t': ('Vec4', 4)}


def pascal(snake):
    return ''.join(part[:1].upper() + part[1:] for part in snake.split('_') if part)


def camel(snake):
    name = pascal(snake)
    return name[:1].lower() + name[1:]


class MapError(RuntimeError):
    pass


class Binding:
    """What the headers say, sorted into the binding's types."""

    def __init__(self, api):
        self.api = api
        self.kinds = {}  # handle typedef -> Haxe type
        for name, (target, header) in api.typedefs.items():
            if name == 'wgf_handle_t' or target == 'wgf_handle_t':
                self.kinds[name] = pascal(name[len('wgf_'):-len('_t')])
        self.enums = {name: pascal(name[len('wgf_'):-len('_t')]) for name in api.enums}
        self.ints = {name for name, (target, _) in api.typedefs.items()
                     if name not in self.kinds and target in ('uint32_t', 'int32_t', 'int', 'unsigned int')}
        self.functions = [f for f in api.functions.values() if f.exported]

    # ---- types ---------------------------------------------------------------------

    def scalar(self, ctype):
        """A parameter or result type that crosses as one value: its Haxe type, its C cast
        (for hxcpp), and how JS passes it ('int', 'float', 'bool')."""
        ctype = ctype.replace('const ', '').strip() if not ctype.endswith('*') else ctype
        if ctype in self.kinds:
            return self.kinds[ctype], ctype, 'int'
        if ctype in self.enums:
            return self.enums[ctype], ctype, 'int'
        if ctype in self.ints or ctype in ('int', 'unsigned int', 'uint32_t', 'int32_t'):
            return 'Int', ctype, 'int'
        if ctype in ('float', 'double'):
            return 'Float', ctype, 'float'
        if ctype == '_Bool':
            return 'Bool', 'bool', 'bool'
        return None

    def element(self, ctype):
        """An array's element: 'float', 'int', or a handle kind's Haxe type; None if not one."""
        base = ctype.replace('const ', '').replace('*', '').strip()
        if base == 'float':
            return 'float'
        if base == 'int':
            return 'int'
        if base in self.kinds:
            return self.kinds[base]
        return None

    def params_of(self, f):
        """The call's parameters as the binding takes them: (kind, name, ctype, extra) for
        each, a pointer and its count folded into one array, a data span into Bytes."""
        out, params, i = [], f.params, 0
        while i < len(params):
            p = params[i]
            nxt = params[i + 1] if i + 1 < len(params) else None
            if p.type == 'const unsigned char *' and nxt is not None and nxt.type == 'int' and nxt.name == 'size':
                out.append(('bytes', p.name, p.type, None))
                i += 2
                continue
            if p.type.endswith('*') and p.type != 'const char *':
                element = self.element(p.type)
                if element is None or nxt is None or nxt.type != 'int' or not (
                        nxt.name == 'count' or nxt.name.endswith('_count')):
                    raise MapError(f'{f.name}: parameter {p.name} ({p.type}) is neither text, a byte span, '
                                   f'nor an array of numbers or handles with its count')
                out.append(('array_in' if p.type.startswith('const ') else 'array_out', p.name, p.type, element))
                i += 2
                continue
            if p.type == 'const char *':
                out.append(('string', p.name, p.type, None))
            else:
                s = self.scalar(p.type)
                if s is None:
                    raise MapError(f'{f.name}: parameter {p.name} has a type the binding can\'t pass: {p.type}')
                out.append(('scalar', p.name, p.type, s))
            i += 1
        return out

    def result_of(self, f):
        """('void'|'string'|'bytes'|'vector'|'scalar', detail)."""
        r = f.returns
        if r == 'void':
            return 'void', None
        if r == 'const char *':
            return 'string', None
        if r == 'const unsigned char *':
            if not f.name.endswith('_get_data') or f.name[:-len('data')] + 'size' not in self.api.functions:
                raise MapError(f'{f.name}: a byte span out needs a _get_data beside a _get_size')
            return 'bytes', f.name[:-len('data')] + 'size'
        if r in VECTORS:
            return 'vector', VECTORS[r]
        s = self.scalar(r)
        if s is None:
            raise MapError(f'{f.name}: returns a type the binding can\'t carry: {r}')
        return 'scalar', s

    # ---- where each call goes ------------------------------------------------------

    def section_of(self, f):
        """The header's section: wgf_node.h -> 'node'; wgf.h -> 'version'."""
        stem = Path(f.header).stem
        return 'version' if stem == 'wgf' else stem[len('wgf_'):]

    def place(self, f):
        """(Haxe type, member name, instance?) for a call. A call named for a handle kind
        goes on that kind's abstract, a method when it takes that kind first; any other on
        its section's type, a method when its section is over a kind it takes first."""
        first = f.params[0].type if f.params else None
        for ctype, haxe in sorted(self.kinds.items(), key=lambda kv: -len(kv[0])):
            stem = ctype[len('wgf_'):-len('_t')]
            if f.name.startswith(f'wgf_{stem}_'):
                if stem == 'handle' and self.section_of(f) != 'handle':
                    continue  # wgf_handle_* is the handle section's own, nothing else's
                return haxe, camel(f.name[len(f'wgf_{stem}_'):]), first == ctype
        section = self.section_of(f)
        prefix = f'wgf_{section}_'
        if not f.name.startswith(prefix):
            raise MapError(f'{f.name}: not named for its header\'s section ({section}) or a handle kind')
        return pascal(section), camel(f.name[len(prefix):]), False

    def over(self):
        """The sections that are over a handle kind: Haxe type -> the kind's Haxe type. A
        section is over a kind when every call of it takes that kind first or makes one (a
        shape's create), and isn't the kind's own; any handle (wgf_handle_t) has none."""
        calls = {}
        for f in self.functions:
            if f.name in SKIPPED:
                continue
            haxe, _, _ = self.place(f)
            if haxe not in self.kinds.values():
                calls.setdefault(haxe, []).append(f)
        found = {}
        for haxe, fs in calls.items():
            firsts = {f.params[0].type for f in fs if f.params and f.params[0].type in self.kinds} - {'wgf_handle_t'}
            fits = [kind for kind in sorted(firsts)
                    if all((f.params and f.params[0].type == kind) or f.returns == kind for f in fs)]
            if len(fits) == 1:  # a sprite's create takes a texture first, and makes a node
                found[haxe] = self.kinds[fits[0]]
        return found

    def docs(self):
        """Each call's doc: its own comment, or the comment of the group it is in (the
        calls after a comment, up to the next comment or a blank line between them, in
        the header's order)."""
        out, last_doc, last_header = {}, '', None
        for f in self.api.functions.values():
            if f.header != last_header:
                last_doc, last_header = '', f.header
            if f.doc:
                last_doc = f.doc
            out[f.name] = f.doc or last_doc
        return out


# ---- Haxe text ---------------------------------------------------------------------------

def doc_block(text, indent):
    if not text:
        return ''
    lines = text.replace('*/', '* /').split('\n')
    return indent + '/**\n' + ''.join(f'{indent}    {line}'.rstrip() + '\n' for line in lines) + indent + '**/\n'


def haxe_params(binding, f, raw):
    """The Haxe parameter list: (name, type) each, `into` last for a vector result."""
    out = []
    for kind, name, ctype, extra in binding.params_of(f):
        safe = name + '_' if name in ('in', 'default', 'function', 'var', 'class', 'new', 'this', 'cast') else name
        if kind == 'scalar':
            out.append((safe, 'Int' if raw and extra[2] == 'int' else extra[0]))
        elif kind == 'string':
            out.append((safe, 'String'))
        elif kind == 'bytes':
            out.append((safe, 'haxe.io.Bytes'))
        elif extra == 'float':
            out.append((safe, 'Array<Float>'))
        elif extra == 'int' or raw:
            out.append((safe, 'Array<Int>'))
        else:
            out.append((safe, f'Array<{extra}>'))
    result, detail = binding.result_of(f)
    if result == 'vector':
        out.append(('into', f'Null<wgf.{detail[0]}>'))
    return out


def haxe_result(binding, f, raw):
    result, detail = binding.result_of(f)
    if result == 'void':
        return 'Void'
    if result == 'string':
        return 'String'
    if result == 'bytes':
        return 'haxe.io.Bytes'
    if result == 'vector':
        return f'wgf.{detail[0]}'
    return 'Int' if raw and detail[2] == 'int' else detail[0]


def js_raw(binding, f):
    """One call for the JS target."""
    params = haxe_params(binding, f, raw=True)
    sig = ', '.join(f'{n}:{t}' for n, t in params)
    result, detail = binding.result_of(f)
    lines, args, after = [], [], []
    needs_mark = False
    slot = 0
    for (kind, name, ctype, extra), (safe, _) in zip(binding.params_of(f), params):
        if kind == 'scalar':
            args.append(f'({safe} ? 1 : 0)' if extra[2] == 'bool' else safe)
        elif kind == 'string':
            needs_mark = True
            args.append(f'Host.cstr({safe})')
        elif kind == 'bytes':
            lines.append(f'final {safe}Pointer = Host.bytesIn({safe}, {slot});')
            args += [f'{safe}Pointer', f'({safe} == null ? 0 : {safe}.length)']
            slot += 1
        else:
            float_ = extra == 'float'
            if kind == 'array_in':
                lines.append(f'final {safe}Pointer = Host.{"floatsIn" if float_ else "intsIn"}({safe}, {slot});')
            else:
                lines.append(f'final {safe}Pointer = Host.arrayOut({safe}, {slot});')
                after.append(f'Host.{"floatsOut" if float_ else "intsOut"}({safe}Pointer, {safe});')
            args += [f'{safe}Pointer', f'({safe} == null ? 0 : {safe}.length)']
            slot += 1
    key = f'Raw.host["_{f.name}"]'
    if result == 'vector':
        haxe_type, count = detail
        lines.append(f'final result = Host.result({4 * count});')
        call = f'{key}({", ".join(["result"] + args)})'
    else:
        call = f'{key}({", ".join(args)})'
    body = []
    if needs_mark:
        body.append('final mark = Host.stackSave();')
    body += lines
    if result == 'void':
        body.append(f'{call};')
    elif result == 'vector':
        body.append(f'{call};')
    else:
        body.append(f'final value:Dynamic = {call};')
    body += after
    if needs_mark:
        body.append('Host.stackRestore(mark);')
    if result == 'string':
        body.append('return Host.str(value);')
    elif result == 'bytes':
        body.append(f'return Host.bytesOut(value, (Raw.host["_{detail}"]({args[0]}) : Int));')
    elif result == 'vector':
        body.append(f'return Host.vec{detail[1]}(result, into);')
    elif result == 'scalar':
        body.append({'bool': 'return value != 0;', 'float': 'return value;', 'int': 'return value;'}[detail[2]])
    ret = haxe_result(binding, f, raw=True)
    inner = ''.join(f'\t\t{line}\n' for line in body)
    return f'\tpublic static function {f.name}({sig}):{ret} {{\n{inner}\t}}\n'


def cpp_raw(binding, f):
    """One call for hxcpp: the C call written out, each argument cast to its C type."""
    params = haxe_params(binding, f, raw=True)
    sig = ', '.join(f'{n}:{t}' for n, t in params)
    result, detail = binding.result_of(f)
    lines, args, after, values = [], [], [], []
    for (kind, name, ctype, extra), (safe, _) in zip(binding.params_of(f), params):
        n = len(values)
        if kind == 'scalar':
            values.append(safe)
            args.append(f'({extra[1]}){{{n}}}')
        elif kind == 'string':
            values.append(safe)
            args.append(f'({{{n}}}.raw_ptr() ? {{{n}}}.utf8_str() : (const char *)0)')
        elif kind == 'bytes':
            values.append(f'{safe} == null ? null : {safe}.getData()')
            values.append(f'{safe} == null ? 0 : {safe}.length')
            args += [f'({{{n}}}.mPtr ? (const unsigned char *){{{n}}}->getBase() : (const unsigned char *)0)',
                     f'(int){{{n + 1}}}']
        else:
            base = ctype.replace('const ', '').replace('*', '').strip()
            const = 'const ' if kind == 'array_in' else ''
            if extra == 'float':
                lines.append(f'final {safe}Floats = Host.floats({safe}, {"true" if kind == "array_in" else "false"});')
                values.append(f'{safe}Floats')
                if kind == 'array_out':
                    after.append(f'Host.floatsBack({safe}Floats, {safe});')
            else:
                values.append(safe)  # hxcpp keeps an Array<Int> as ints: passed as it is
            values.append(f'{safe} == null ? 0 : {safe}.length')
            args += [f'({{{n}}}.mPtr ? ({const}{base} *){{{n}}}->getBase() : ({const}{base} *)0)', f'(int){{{n + 1}}}']
    # hxcpp splices each value into the C as it is, unparenthesized: each is a local first
    named = []
    for i, v in enumerate(values):
        if v.isidentifier():
            named.append(v)
        else:
            lines.append(f'final arg{i} = {v};')
            named.append(f'arg{i}')
    values = named
    call = f'::{f.name}({", ".join(args)})'
    vals = ''.join(f', {v}' for v in values)
    body = list(lines)
    if result == 'void':
        body.append(f'untyped __cpp__("{call}"{vals});')
    elif result == 'string':
        body.append(f'final value:String = untyped __cpp__("::String({call})"{vals});')
    elif result == 'bytes':
        body.append(f'final size:Int = untyped __cpp__("::{detail}({{0}})", {values[0]});')
        body.append(f'final bytes = haxe.io.Bytes.alloc(size < 0 ? 0 : size);')
        body.append(f'if (size > 0) untyped __cpp__("memcpy({{0}}->getBase(), ::{f.name}({{1}}), {{2}})", '
                    f'bytes.getData(), {values[0]}, size);')
    elif result == 'vector':
        haxe_type, count = detail
        body.append(f'final value:CVec{count} = untyped __cpp__("{call}"{vals});')
    else:
        haxe_type, ccast, how = detail
        cast = {'bool': 'Bool', 'float': 'Float', 'int': 'Int'}[how]
        conv = {'bool': '(bool)', 'float': '(double)', 'int': '(int)'}[how]
        body.append(f'final value:{cast} = untyped __cpp__("{conv}{call}"{vals});')
    body += after
    if result in ('string', 'scalar'):
        body.append('return value;')
    elif result == 'bytes':
        body.append('return bytes;')
    elif result == 'vector':
        fields = 'xyzw'[:detail[1]]
        body.append(f'final out = into != null ? into : new wgf.{detail[0]}();')
        body += [f'out.{c} = value.{c};' for c in fields]
        body.append('return out;')
    ret = haxe_result(binding, f, raw=True)
    inner = ''.join(f'\t\t{line}\n' for line in body)
    return f'\tpublic static function {f.name}({sig}):{ret} {{\n{inner}\t}}\n'


def typed_member(binding, f, haxe, member, instance, docs, over_kind=None):
    params = haxe_params(binding, f, raw=False)
    args = [n for n, _ in params]
    if instance:
        params = params[1:]
        args = ['this'] + args[1:]
    result, detail = binding.result_of(f)
    if result == 'vector':
        params = params[:-1] + [('?into', params[-1][1].replace('Null<', '').rstrip('>'))]
    sig = ', '.join(f'{n}:{t}' for n, t in params)
    ret = haxe_result(binding, f, raw=False)
    if over_kind is not None and ret == over_kind:
        ret = haxe  # a section's create makes one of the section: Shape2d.create() is a Shape2d
    # a kind or an enum crosses Raw as its Int, through its from/to; an array of a kind
    # is the same array of Ints, but Haxe's arrays don't convert, so it is cast
    kinds = {safe for (kind, _, _, extra), (safe, _) in zip(binding.params_of(f), haxe_params(binding, f, raw=False))
             if kind in ('array_in', 'array_out') and extra not in ('float', 'int')}
    args = [f'cast {a}' if a in kinds else a for a in args]
    call = f'Raw.{f.name}({", ".join(args)})'
    if over_kind is not None and ret == haxe:
        call = f'(({call} : {over_kind}) : {haxe})'  # Int to the kind to the section: one conversion each
    static = '' if instance else 'static '
    body = call if ret == 'Void' else f'return {call}'
    return (doc_block(docs.get(f.name, ''), '\t') +
            f'\tpublic {static}inline function {member}({sig}):{ret}\n\t\t{body};\n')


def typed_modules(binding):
    """Haxe module name -> its text."""
    docs = binding.docs()
    over = binding.over()
    members = {}
    seen = {}
    for f in binding.functions:
        if f.name in SKIPPED:
            continue
        haxe, member, instance = binding.place(f)
        kind = over.get(haxe)
        if kind is not None and f.params and binding.kinds.get(f.params[0].type) == kind:
            instance = True  # a section over a kind: its calls on one are that one's methods
        key = (haxe, member)
        if key in seen:
            raise MapError(f'{f.name} and {seen[key]} would both be {haxe}.{member}')
        seen[key] = f.name
        members.setdefault(haxe, []).append(typed_member(binding, f, haxe, member, instance, docs, kind))
    modules = {}
    header = f'// {MARK}\npackage wgf;\n\nimport wgf.impl.Raw;\n\n'
    for ctype, haxe in binding.kinds.items():
        doc = binding.api.typedefs[ctype][1]
        to = '' if haxe == 'Handle' else ' to wgf.Handle'
        text = header + (f'/** A handle of kind {ctype} ({doc}): 0 is none. **/\n'
                         f'abstract {haxe}(Int) from Int to Int{to} {{\n'
                         f'\t/** Whether this is no handle (0); right on every target, where `== null` is not. **/\n'
                         f'\tpublic inline function isNone():Bool\n\t\treturn this == 0;\n\n' +
                         '\n'.join(members.pop(haxe, [])) + '}\n')
        modules[haxe] = text
    for haxe, kind in over.items():
        text = header + (f'/** {haxe}\'s calls, on a {kind}: `var x:{haxe} = handle` gives it them, and a {haxe} is '
                         f'still a {kind}. **/\n'
                         f'@:forward abstract {haxe}({kind}) from {kind} to {kind} {{\n' +
                         '\n'.join(members.pop(haxe, [])) + '}\n')
        modules[haxe] = text
    for haxe, items in members.items():
        modules[haxe] = header + f'class {haxe} {{\n' + '\n'.join(items) + '}\n'
    for cname, haxe in binding.enums.items():
        enum = binding.api.enums[cname]
        names = list(enum.values)
        prefix = names[0]
        for n in names[1:]:
            while not n.startswith(prefix):
                prefix = prefix[:-1]
        prefix = prefix[:prefix.rfind('_') + 1] if '_' in prefix else ''
        body = ''
        for n, v in enum.values.items():
            short = n[len(prefix):]
            if not short or short[0].isdigit():  # WGF_KEY_0 -> KEY_0: a Haxe name can't start with a digit
                short = prefix.rstrip('_').split('_')[-1] + '_' + short
            body += f'\tvar {short} = {v};\n'
        if haxe in modules:
            raise MapError(f'enum {cname} and a section are both wgf.{haxe}')
        modules[haxe] = (f'// {MARK}\npackage wgf;\n\n' + doc_block(enum.doc, '') +
                         f'enum abstract {haxe}(Int) from Int to Int {{\n{body}}}\n')
    return modules


def digest(api):
    text = json.dumps([[f.name, f.returns, [p.type for p in f.params]] for f in api.functions.values() if f.exported] +
                      [[e.name, list(e.values.items())] for e in api.enums.values()])
    return hashlib.sha256(text.encode()).hexdigest()[:16]


def outputs(api):
    binding = Binding(api)
    version = (ROOT / 'VERSION').read_text().strip()
    major, minor, patch = version.split('.')
    stamp = digest(api)
    files = {}
    funcs = [f for f in binding.functions if f.name not in SKIPPED] + [binding.api.functions[n] for n in SKIPPED]
    js = ''.join(js_raw(binding, f) for f in funcs if f.name not in SKIPPED)
    cpp = ''.join(cpp_raw(binding, f) for f in funcs if f.name not in SKIPPED)
    files[OUT / 'impl' / 'Raw.js.hx'] = (
        f'// {MARK}\npackage wgf.impl;\n\n'
        '/** Each exported C call for the JS target: the host\'s export by its quoted key. **/\n'
        'class Raw {\n\t/** The wasm host every call goes through (Host.attach). **/\n'
        '\tpublic static var host:haxe.DynamicAccess<Dynamic> = Host.notAttached();\n\n' + js + '}\n')
    includes = ''.join(f'#include <{Path(h).name}>\\n' for h in api.headers)
    cvec = ''.join(f'@:native("wgf_vec{n}_t") @:structAccess @:unreflective extern class CVec{n} {{\n' +
                   ''.join(f'\tvar {c}:cpp.Float32;\n' for c in 'xyzw'[:n]) + '}\n\n' for n in (2, 3, 4))
    files[OUT / 'impl' / 'Raw.cpp.hx'] = (
        f'// {MARK}\npackage wgf.impl;\n\n' + cvec +
        '/** Each exported C call for hxcpp: the call written out, its arguments cast. **/\n'
        f'@:cppFileCode("{includes}#include <string.h>\\n")\n'
        '@:buildXml("<include name=\'${wgf_binding}/project/Build.xml\' if=\'wgf_binding\'/>'
        '<include name=\'${haxelib:wgf}/project/Build.xml\' unless=\'wgf_binding\'/>")\nclass Raw {\n' + cpp + '}\n')
    files[OUT / 'impl' / 'BuiltVersion.hx'] = (
        f'// {MARK}\npackage wgf.impl;\n\n/** The library the binding was generated from. **/\nclass BuiltVersion {{\n'
        f'\tpublic static inline final MAJOR = {int(major)};\n\tpublic static inline final MINOR = {int(minor)};\n'
        f'\tpublic static inline final PATCH = {int(patch)};\n\tpublic static inline final HEADERS = "{stamp}";\n}}\n')
    for name, text in typed_modules(binding).items():
        files[OUT / f'{name}.hx'] = text
    exports = sorted('_' + f.name for f in binding.functions) + HOST_EXTRA
    files[EXPORTS] = json.dumps({'generated': MARK, 'version': version, 'headers': stamp, 'exports': exports},
                                indent=1) + '\n'
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='write nothing; fail when a generated file is stale')
    args = ap.parse_args()
    api = headers.read(ROOT, None, None)
    if api is None:
        print('gen_binding: no clang (Emscripten brings one); nothing generated', file=sys.stderr)
        return 1
    try:
        files = outputs(api)
    except MapError as e:
        print(f'gen_binding: {e}', file=sys.stderr)
        return 1
    stale = []
    generated = {p for p in OUT.rglob('*.hx') if MARK in p.read_text(encoding='utf-8')} if OUT.exists() else set()
    for path, text in files.items():
        old = path.read_text(encoding='utf-8') if path.exists() else None
        if old != text:
            stale.append(path)
            if not args.check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding='utf-8', newline='\n')
    gone = sorted(generated - set(files))
    for path in gone:
        stale.append(path)
        if not args.check:
            path.unlink()
    if args.check:
        if stale:
            for path in stale:
                print(f'gen_binding: stale: {path.relative_to(ROOT).as_posix()}', file=sys.stderr)
            print('gen_binding: run tools/gen_binding.py', file=sys.stderr)
            return 1
        print(f'gen_binding: {len(files)} file(s) up to date')
        return 0
    print(f'gen_binding: {len(stale)} of {len(files)} file(s) written')
    return 0


if __name__ == '__main__':
    sys.exit(main())
