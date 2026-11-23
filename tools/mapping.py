"""How the bindings map libwgf's C API: each exported call placed on a Haxe and typed-JS type
as one member, the types its parameters and result cross as, and the doc each takes, from the
headers as tools/headers.py reads them. The model tools/gen_binding.py writes both bindings
from, and tools/gen_apidoc.py names each call's binding by. Standard library only."""
from pathlib import Path

# Exported calls the typed API doesn't make one member of, and why. The runtime's own
# (hand-written in bindings/haxe/src/wgf/) still reach C exactly once.
SKIPPED = {
    'wgf_app_run': 'the runtime: wgf.App.run installs its trampolines once (callbacks are C function pointers)',
}

VECTORS = {'wgf_vec2_t': ('Vec2', 2), 'wgf_vec3_t': ('Vec3', 3), 'wgf_vec4_t': ('Vec4', 4)}

# A section whose Haxe type can't be its own name, and why. `Behavior` is the runtime's
# base class for a game's behaviors (bindings/haxe/src/wgf/Behavior.hx, SPEC's name);
# wgf_behavior_*'s calls, on an entity's behavior component, are BehaviorComponent's.
TYPE_NAMES = {'behavior': 'BehaviorComponent'}
# A call whose member takes another name than its C name gives, with why.
MEMBER_NAMES = {
    'wgf_scene_find_prefab': 'prefab',  # scene.prefab("bullet"), then bullet.spawnAt(...): read as what it gives
}


def spelled(ctype):
    """A type as one spelling on every machine: clang spells C's boolean `_Bool` or, newer
    and on some systems, `bool` (Windows' runner's), the same type."""
    return '_Bool' if ctype == 'bool' else ctype


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
        ctype = spelled(ctype.replace('const ', '').strip() if not ctype.endswith('*') else ctype)
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
        """The header's section: wgf_actor.h -> 'node'; wgf.h -> 'version'."""
        stem = Path(f.header).stem
        return 'version' if stem == 'wgf' else stem[len('wgf_'):]

    def place(self, f):
        """(Haxe type, member name, instance?) for a call. A call named for a handle kind
        goes on that kind's abstract, a method when it takes that kind first, unless its
        header's section is longer than the kind's name and the call is named for it (the
        tilemap_layer section's, on a placed map's actors); any other on its section's type,
        a method when its section is over a kind it takes first."""
        first = f.params[0].type if f.params else None
        for ctype, haxe in sorted(self.kinds.items(), key=lambda kv: -len(kv[0])):
            stem = ctype[len('wgf_'):-len('_t')]
            if f.name.startswith(f'wgf_{stem}_'):
                if stem == 'handle' and self.section_of(f) != 'handle':
                    continue  # wgf_handle_* is the handle section's own, nothing else's
                section = self.section_of(f)
                if section.startswith(f'{stem}_') and f.name.startswith(f'wgf_{section}_'):
                    break  # a section named longer than the kind (wgf_tilemap_layer_*): its own type
                return haxe, MEMBER_NAMES.get(f.name, camel(f.name[len(f'wgf_{stem}_'):])), first == ctype
        section = self.section_of(f)
        prefix = f'wgf_{section}_'
        if not f.name.startswith(prefix):
            raise MapError(f'{f.name}: not named for its header\'s section ({section}) or a handle kind')
        return TYPE_NAMES.get(section, pascal(section)), MEMBER_NAMES.get(f.name, camel(f.name[len(prefix):])), False

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

    def class_docs(self):
        """Each type's own doc: a handle kind's typedef's comment, then the overview of the
        header whose first call it holds (headers.py's: the comments no declaration owns),
        so a program reading the binding reads what the header says of the whole."""
        out, placed = {}, set()
        for ctype, haxe in self.kinds.items():
            if self.api.type_docs.get(ctype):
                out[haxe] = self.api.type_docs[ctype]
        for f in self.functions:
            if f.header in placed or f.name in SKIPPED:
                continue
            placed.add(f.header)
            overview = self.api.overviews.get(f.header)
            if overview:
                haxe = self.place(f)[0]
                out[haxe] = out[haxe] + '\n\n' + overview if haxe in out and overview not in out[haxe] else overview
        return out

    def docs(self):
        """Each call's doc: its own comment, or the comment of the group it is in: the
        nearest comment above it in its header, a call's or an enum's (an enum's comment
        written for the call right after it, as wgf_draw_texture_fill's)."""
        enums = {}
        for e in self.api.enums.values():
            if e.doc and e.offset >= 0:
                enums.setdefault(e.header, []).append(e)
        out, last_doc, last = {}, '', None
        for f in self.api.functions.values():
            if last is None or f.header != last.header:
                last_doc = ''
            start = last.offset if last is not None and f.header == last.header else -1
            between = [e for e in enums.get(f.header, ()) if start < e.offset < f.offset]
            if between:
                last_doc = max(between, key=lambda e: e.offset).doc
            if f.doc:
                last_doc = f.doc
            out[f.name] = f.doc or last_doc
            last = f
        return out


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


def enum_members(binding, cname):
    """An enum's values as the typed APIs name them: (short name, value, C name), their
    shared prefix off (WGF_KEY_0 -> KEY_0: a name can't start with a digit)."""
    enum = binding.api.enums[cname]
    names = list(enum.values)
    prefix = names[0]
    for n in names[1:]:
        while not n.startswith(prefix):
            prefix = prefix[:-1]
    prefix = prefix[:prefix.rfind('_') + 1] if '_' in prefix else ''
    out = []
    for n, v in enum.values.items():
        short = n[len(prefix):]
        if not short or short[0].isdigit():
            short = prefix.rstrip('_').split('_')[-1] + '_' + short
        out.append((short, v, n))
    return out


def haxe_signature(binding, f, haxe, member, instance, over_kind=None):
    """A typed member's Haxe signature, as the binding writes it: (its parameters as Haxe
    text, its result type), the handle off an instance method's, a vector result's `?into`
    last, and a section's create making one of the section (Shape2d.create() is a Shape2d)."""
    params = haxe_params(binding, f, raw=False)
    if instance:
        params = params[1:]
    result, _ = binding.result_of(f)
    if result == 'vector':
        params = params[:-1] + [('?into', params[-1][1].replace('Null<', '').rstrip('>'))]
    ret = haxe_result(binding, f, raw=False)
    if over_kind is not None and ret == over_kind and member.startswith('create'):
        ret = haxe
    return ', '.join(f'{n}:{t}' for n, t in params), ret


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('mapping.py: a module the other tools import: there is nothing to run')
