#!/usr/bin/env python3
"""Write libwgf's API reference as HTML, one for each language a game is written in: C by its
headers, Haxe by its types, and JS and TypeScript by the typed layer's objects, each member
linked to the C call it wraps and each C call to its names in the others.

    tools/gen_apidoc.py [--out DIR]

The headers are read as every tool reads them (tools/headers.py: clang's parse, never their
text); each exported call is placed as the bindings place it (tools/mapping.py's Binding, which
tools/gen_binding.py writes them by), its Haxe signature as the Haxe binding writes it
(mapping.haxe_signature), and its TypeScript one as the JS binding declares it
(tools/jsbinding.py's Emitter). Every doc is the headers' (a call's own comment, its group's as
the bindings take it, or, where that is a member's above it on the page, a link to it).

  c/<header>.html    a page a public header, grouped by layer: its overview, enums, structs,
                     and calls in header order, each its C signature and its names in Haxe and
                     JS, or that it is C's alone (a static inline, or one the bindings skip)
  haxe/<Type>.html   a page a Haxe type: an abstract over a handle kind (its calls its
                     methods), one over a kind for a section of calls on it, a class of
                     statics, or an enum abstract, each member's Haxe signature
  js/<Type>.html     the same types as the typed JS layer's frozen objects (wgf-typed.js), each
                     member's TypeScript signature (the handle first, as the raw call takes it)
                     and its raw name (wgf.js), for plain JS and TypeScript alike

The root's index.html opens the three and searches every one of them by name, in the page,
with no server (api-index.json); each language's index searches its own. The out directory
(default build/apidoc/) also holds api.css and api.js; tools/build_pages.py puts it in the
GitHub Pages site at api/. The Haxe runtime's own classes (App, Behavior, Runtime) are
written by hand in bindings/haxe/src/wgf/ and aren't here. Standard library only.
"""
import argparse
import html
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import headers  # noqa: E402
import jsbinding  # noqa: E402
import mapping  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = 'https://github.com/whirlinggizmo/libwgf'
LANGUAGES = {'c': 'C', 'haxe': 'Haxe', 'js': 'JS and TypeScript'}

CSS = """:root { --bg: #0f1115; --panel: #171a21; --line: #2a2f3a; --text: #d3d8e2; --dim: #8b93a3;
  --accent: #7ea2ff; --code: #e6c07b; color-scheme: dark; }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.55 system-ui, sans-serif; }
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }
code, pre { font: 13px/1.5 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
code { color: var(--code); }
pre { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 10px 12px;
  overflow-x: auto; color: var(--text); white-space: pre-wrap; }
.layout { display: flex; min-height: 100vh; }
nav { width: 270px; flex: none; background: var(--panel); border-right: 1px solid var(--line);
  padding: 16px 12px; position: sticky; top: 0; height: 100vh; overflow-y: auto; }
nav .langs { display: flex; gap: 6px; margin: 8px 0 4px; flex-wrap: wrap; }
nav .langs a { padding: 2px 8px; border: 1px solid var(--line); border-radius: 12px; font-size: 13px; }
nav .langs a.here { background: #243050; border-color: #3a4f86; }
nav h2 { font-size: 12px; text-transform: uppercase; letter-spacing: .08em; color: var(--dim); margin: 16px 0 4px; }
nav ul { list-style: none; margin: 0; padding: 0; }
nav li a { display: block; padding: 2px 6px; border-radius: 4px; font: 13px ui-monospace, monospace; }
nav li a.here { background: #243050; }
main { flex: 1; min-width: 0; padding: 24px 32px 64px; max-width: 980px; }
h1 { margin: 0 0 4px; font-size: 26px; }
.sub { color: var(--dim); margin: 0 0 20px; }
h2.section { margin: 36px 0 8px; padding-bottom: 4px; border-bottom: 1px solid var(--line); font-size: 18px; }
.item { margin: 18px 0 26px; }
.item h3 { margin: 0 0 6px; font: 600 15px ui-monospace, monospace; }
.names { color: var(--dim); font-size: 13px; margin: 2px 0 6px; }
.names code { color: var(--text); }
.doc p { margin: 6px 0; }
table { border-collapse: collapse; margin: 6px 0; font-size: 13px; }
td, th { border: 1px solid var(--line); padding: 3px 8px; text-align: left; }
.search { width: 100%; padding: 8px 10px; margin: 8px 0 14px; background: var(--bg); color: var(--text);
  border: 1px solid var(--line); border-radius: 6px; font: inherit; }
.results li { margin: 2px 0; font: 13px ui-monospace, monospace; }
.results .where { color: var(--dim); }
.cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 12px; margin: 16px 0; }
.cards a { display: block; padding: 14px 16px; background: var(--panel); border: 1px solid var(--line);
  border-radius: 8px; color: var(--text); }
.cards a b { color: var(--accent); font-size: 17px; display: block; margin-bottom: 4px; }
@media (max-width: 760px) { .layout { display: block; } nav { width: auto; height: auto; position: static; }
  main { padding: 16px; } }
"""

JS = """// The search: every name in api-index.json (the root's), or one language's (the box's
// data-lang), shown as you type; nothing is fetched but that file.
(async () => {
  const box = document.querySelector('.search'), list = document.querySelector('.results');
  if (!box) return;
  const root = box.dataset.root || '', lang = box.dataset.lang || '';
  const all = await (await fetch(root + 'api-index.json')).json();
  const items = lang ? all.filter(i => i.lang === lang) : all;
  const show = () => {
    const q = box.value.trim().toLowerCase();
    list.innerHTML = '';
    if (!q) return;
    const found = items.filter(i => i.name.toLowerCase().includes(q) || (i.also || '').toLowerCase().includes(q));
    for (const i of found.slice(0, 200)) {
      const li = document.createElement('li');
      const a = Object.assign(document.createElement('a'), {href: root + i.page, textContent: i.name});
      const where = Object.assign(document.createElement('span'), {className: 'where',
        textContent: '  ' + (lang ? '' : i.langName + ', ') + i.where});
      li.append(a, where);
      list.append(li);
    }
    if (found.length > 200) list.append(Object.assign(document.createElement('li'), {textContent: `... ${found.length - 200} more`}));
  };
  box.addEventListener('input', show);
  show();
})();
"""


def inline(text):
    """`code` spans as <code>, the rest escaped."""
    return ''.join(f'<code>{html.escape(part)}</code>' if i % 2 else html.escape(part)
                   for i, part in enumerate(text.split('`')))


def doc_html(text):
    """A doc comment as HTML: its paragraphs (blank lines apart) as paragraphs, a block whose
    lines are indented or laid out in columns (a list of keys and their defaults) as it is."""
    out = []
    for block in (text or '').split('\n\n'):
        lines = block.split('\n')
        if not block.strip():
            continue
        if any(line.startswith(' ') or '   ' in line.strip() for line in lines):
            out.append(f'<pre>{html.escape(block)}</pre>')
        else:
            out.append(f'<p>{inline(" ".join(line.strip() for line in lines))}</p>')
    return '<div class="doc">' + ''.join(out) + '</div>' if out else ''


def layer_of(header):
    """libwgf/gfx/include/wgf_model.h -> 'gfx'; include/wgf.h -> 'libwgf'."""
    parts = Path(header).parts
    return parts[1] if len(parts) > 3 and parts[0] == headers.LAYER_ROOT else 'libwgf'


def layer_order():
    return ['libwgf'] + list(headers.LAYERS)


def c_page(header):
    return Path(header).stem + '.html'


def c_signature(f):
    params = ', '.join(f'{p.type} {p.name}' if p.name else p.type for p in f.params) or 'void'
    if f.variadic:
        params += ', ...'
    ret = f.returns if f.returns.endswith('*') else f.returns + ' '
    return f'{ret}{f.name}({params});'


def ts_signature(emitter, f):
    """The call's TypeScript parameters and result, as wgf.d.ts declares it (a vector result's
    plain overload, not its generic one)."""
    declaration = emitter.call(f)[1].strip().splitlines()[-1]
    start = declaration.index(f'{f.name}(') + len(f.name) + 1
    depth, i = 1, start
    while depth:
        depth += {'(': 1, ')': -1}.get(declaration[i], 0)
        i += 1
    params = declaration[start:i - 1]
    ret = declaration[i:].strip().lstrip(':').strip().rstrip(';')
    return params, ret


class Docs:
    """A page's docs: a member's own, or a link to the member above whose doc its group takes."""

    def __init__(self, docs):
        self.docs, self.owner = docs, {}

    def html(self, f, anchor):
        if f.doc:
            self.owner.setdefault(f.doc, (anchor, f.name))
            return doc_html(f.doc)
        text = self.docs.get(f.name, '')
        if text in self.owner:
            above, name = self.owner[text]
            return f'<p class="names">See <a href="#{html.escape(above)}"><code>{html.escape(name)}</code></a>, above.</p>'
        return doc_html(text)


def nav_html(lang, groups, here=None):
    langs = ''.join(f'<a{" class=" + chr(34) + "here" + chr(34) if key == lang else ""} href="../{key}/index.html">'
                    f'{html.escape(name)}</a>' for key, name in LANGUAGES.items())
    out = ['<nav>', '<a href="../index.html"><b>libwgf API</b></a>', f'<div class="langs">{langs}</div>']
    for layer, entries in groups:
        out.append(f'<h2>{html.escape(layer)}</h2><ul>')
        for pg, label in entries:
            cls = ' class="here"' if pg == here else ''
            out.append(f'<li><a{cls} href="{pg}">{html.escape(label)}</a></li>')
        out.append('</ul>')
    out.append('</nav>')
    return ''.join(out)


def page(title, nav, body, css='../api.css', script='../api.js'):
    return (f'<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">\n<title>{html.escape(title)}</title>\n'
            f'<link rel="stylesheet" href="{css}">\n</head>\n<body>\n<div class="layout">{nav}<main>{body}</main></div>\n'
            f'<script src="{script}"></script>\n</body>\n</html>\n')


def search_box(lang, placeholder):
    data = f' data-lang="{lang}"' if lang else ''
    root = '../' if lang else ''
    return (f'<input class="search" type="search" placeholder="{html.escape(placeholder)}" data-root="{root}"'
            f'{data} autofocus><ul class="results"></ul>')


class Reference:
    def __init__(self, api):
        self.api = api
        self.binding = mapping.Binding(api)
        self.docs = self.binding.docs()
        self.emitter = jsbinding.Emitter(self.binding)
        self.emitter.local, self.emitter.groups_written = jsbinding.local_blocks(), set()  # as jsbinding.outputs sets it
        self.over = self.binding.over()
        self.index = []
        # each exported call's place: C name -> (type, member, instance); a section over a
        # kind's calls on one are that one's methods, as the Haxe binding writes them
        self.placed, self.members = {}, {}
        for f in self.binding.functions:
            if f.name in mapping.SKIPPED:
                continue
            try:
                haxe, member, instance = self.binding.place(f)
            except mapping.MapError:
                continue
            kind = self.over.get(haxe)
            if kind is not None and f.params and self.binding.kinds.get(f.params[0].type) == kind:
                instance = True
            self.placed[f.name] = (haxe, member, instance)
            self.members.setdefault(haxe, []).append(f)
        self.enum_types = {haxe: cname for cname, haxe in self.binding.enums.items()}
        self.class_docs = self.binding.class_docs()

    def type_layer(self, haxe):
        fs = self.members.get(haxe)
        header = fs[0].header if fs else self.api.enums[self.enum_types[haxe]].header
        return layer_of(header)

    def type_groups(self):
        names = set(self.members) | set(self.enum_types)
        layers = {}
        for haxe in names:
            layers.setdefault(self.type_layer(haxe), []).append(haxe)
        return [(layer, [(f'{h}.html', h) for h in sorted(layers[layer])]) for layer in layer_order() if layer in layers]

    def kind_of(self, haxe):
        """What a type is, said in a line."""
        if haxe in self.enum_types:
            return f'an enum abstract over <code>{html.escape(self.enum_types[haxe])}</code>'
        for ctype, kind in self.binding.kinds.items():
            if kind == haxe:
                return f'a handle kind, an abstract over <code>{html.escape(ctype)}</code>; 0 is none'
        if haxe in self.over:
            kind = self.over[haxe]
            return (f'an abstract over <a href="{kind}.html"><code>{html.escape(kind)}</code></a>: '
                    f'a {html.escape(kind)} given these calls')
        return 'a class of statics'

    def write_c(self, out):
        by_header = {}
        for f in self.api.functions.values():
            by_header.setdefault(f.header, {'f': [], 'e': [], 's': []})['f'].append(f)
        for e in self.api.enums.values():
            by_header.setdefault(e.header, {'f': [], 'e': [], 's': []})['e'].append(e)
        for s in self.api.structs.values():
            by_header.setdefault(s.header, {'f': [], 'e': [], 's': []})['s'].append(s)
        shown = [h for h in self.api.headers if h in by_header or self.api.overviews.get(h)]
        layers = {}
        for h in shown:
            layers.setdefault(layer_of(h), []).append(h)
        groups = [(layer, [(c_page(h), Path(h).name) for h in sorted(layers[layer], key=lambda h: Path(h).name)])
                  for layer in layer_order() if layer in layers]
        header_of = {c_page(h): h for h in shown}
        for _, entries in groups:
            for pg, name in entries:
                h = header_of[pg]
                d = by_header.get(h, {'f': [], 'e': [], 's': []})
                body = [f'<h1>{html.escape(name)}</h1>',
                        f'<p class="sub">C &middot; {html.escape(layer_of(h))} &middot; '
                        f'<a href="{REPOSITORY}/blob/main/{html.escape(h)}">{html.escape(h)}</a></p>',
                        doc_html(self.api.overviews.get(h, ''))]
                if d['e']:
                    body.append('<h2 class="section">Enums</h2>')
                    for e in d['e']:
                        haxe = self.binding.enums.get(e.name)
                        rows = ''.join(f'<tr><td><code>{html.escape(k)}</code></td><td>{v}</td></tr>' for k, v in e.values.items())
                        names = (f'<p class="names">Haxe: <a href="../haxe/{haxe}.html"><code>{haxe}</code></a>; JS: '
                                 f'<a href="../js/{haxe}.html"><code>{haxe}</code></a></p>' if haxe else '')
                        body.append(f'<div class="item" id="{html.escape(e.name)}"><h3>{html.escape(e.name)}</h3>{names}'
                                    f'{doc_html(e.doc)}<table>{rows}</table></div>')
                        self.index.append({'lang': 'c', 'name': e.name, 'page': f'c/{pg}#{e.name}', 'where': name})
                if d['s']:
                    body.append('<h2 class="section">Structs</h2>')
                    for s in d['s']:
                        rows = ''.join(f'<tr><td><code>{html.escape(fl.type)}</code></td><td><code>{html.escape(fl.name)}'
                                       f'{f"[{fl.count}]" if fl.count else ""}</code></td></tr>' for fl in s.fields)
                        table = f'<table>{rows}</table>' if rows else ''
                        body.append(f'<div class="item" id="{html.escape(s.name)}"><h3>{html.escape(s.name)}</h3>'
                                    f'{doc_html(s.doc)}{table}</div>')
                        self.index.append({'lang': 'c', 'name': s.name, 'page': f'c/{pg}#{s.name}', 'where': name})
                if d['f']:
                    body.append('<h2 class="section">Calls</h2>')
                    docs = Docs(self.docs)
                    for f in d['f']:
                        if f.name in self.placed:
                            haxe, member, _ = self.placed[f.name]
                            names = (f'<p class="names">Haxe: <a href="../haxe/{haxe}.html#{member}"><code>{haxe}.{member}</code></a>; '
                                     f'JS and TypeScript: <a href="../js/{haxe}.html#{member}"><code>{haxe}.{member}</code></a>, '
                                     f'raw <code>wgf.{html.escape(f.name)}</code></p>')
                        elif f.exported:
                            names = '<p class="names">not in the bindings as a member (their runtime reaches it by hand)</p>'
                        else:
                            names = '<p class="names">C alone (static inline)</p>'
                        body.append(f'<div class="item" id="{html.escape(f.name)}"><h3>{html.escape(f.name)}</h3>'
                                    f'<pre>{html.escape(c_signature(f))}</pre>{names}{docs.html(f, f.name)}</div>')
                        self.index.append({'lang': 'c', 'name': f.name, 'page': f'c/{pg}#{f.name}', 'where': name})
                (out / pg).write_text(page(f'{name}: libwgf C API', nav_html('c', groups, pg), ''.join(body)), encoding='utf-8')
        intro = ('<h1>C</h1><p class="sub">libwgf\'s public headers, as a C program includes them, by layer.</p>'
                 + search_box('c', 'Search the C API'))
        listing = []
        for layer, entries in groups:
            listing.append(f'<h2 class="section">{html.escape(layer)}</h2><ul>')
            for pg, name in entries:
                first = (self.api.overviews.get(header_of[pg], '') or '').split('\n\n')[0].replace('\n', ' ')
                if len(first) > 160:
                    first = first[:157].rsplit(' ', 1)[0] + '...'
                listing.append(f'<li><a href="{pg}"><code>{html.escape(name)}</code></a> {inline(first)}</li>')
            listing.append('</ul>')
        (out / 'index.html').write_text(page('libwgf C API', nav_html('c', groups), intro + ''.join(listing)), encoding='utf-8')
        return len(shown)

    def write_types(self, out, lang):
        """Haxe's or the typed JS layer's pages: a type each."""
        groups = self.type_groups()
        haxe_lang = lang == 'haxe'
        label = LANGUAGES[lang]
        for _, entries in groups:
            for pg, haxe in entries:
                body = [f'<h1>{html.escape(haxe)}</h1>',
                        f'<p class="sub">{html.escape(label)} &middot; {html.escape(self.type_layer(haxe))} &middot; '
                        f'{self.kind_of(haxe)}</p>']
                if haxe in self.enum_types:
                    cname = self.enum_types[haxe]
                    enum = self.api.enums[cname]
                    body.append(doc_html(enum.doc))
                    rows = ''.join(f'<tr><td><code>{haxe}.{html.escape(short)}</code></td><td>{v}</td>'
                                   f'<td><code>{html.escape(c)}</code></td></tr>'
                                   for short, v, c in mapping.enum_members(self.binding, cname))
                    body.append(f'<p class="names">wraps <a href="../c/{c_page(enum.header)}#{cname}"><code>{html.escape(cname)}'
                                f'</code></a></p><table>{rows}</table>')
                    self.index.append({'lang': lang, 'name': haxe, 'page': f'{lang}/{pg}', 'where': 'an enum', 'also': cname})
                else:
                    body.append(doc_html(self.class_docs.get(haxe, '')))
                    if haxe_lang and any(kind == haxe for kind in self.binding.kinds.values()):
                        body.append('<p class="names">Also <code>isNone()</code> (0, none) and <code>isAlive()</code> '
                                    '(not destroyed or released), on every handle kind.</p>')
                    body.append('<h2 class="section">Members</h2>')
                    docs = Docs(self.docs)
                    for f in self.members.get(haxe, []):
                        _, member, instance = self.placed[f.name]
                        if haxe_lang:
                            sig, ret = mapping.haxe_signature(self.binding, f, haxe, member, instance, self.over.get(haxe))
                            declared = f'public {"" if instance else "static "}function {member}({sig}):{ret}'
                        else:
                            params, ret = ts_signature(self.emitter, f)
                            declared = f'{haxe}.{member}({params}): {ret}'
                        raw = '' if haxe_lang else f'; raw <code>wgf.{html.escape(f.name)}</code>'
                        wraps = (f'<p class="names">wraps <a href="../c/{c_page(f.header)}#{f.name}">'
                                 f'<code>{html.escape(f.name)}</code></a>{raw}</p>')
                        body.append(f'<div class="item" id="{html.escape(member)}"><h3>{html.escape(member)}</h3>'
                                    f'<pre>{html.escape(declared)}</pre>{wraps}{docs.html(f, member)}</div>')
                        self.index.append({'lang': lang, 'name': f'{haxe}.{member}', 'page': f'{lang}/{pg}#{member}',
                                           'where': haxe, 'also': f.name})
                (out / pg).write_text(page(f'{haxe}: libwgf {label} API', nav_html(lang, groups, pg), ''.join(body)),
                                      encoding='utf-8')
        if haxe_lang:
            intro = ('<h1>Haxe</h1><p class="sub">The Haxe binding\'s types (<code>import wgf.*;</code>), by layer: each '
                     'handle kind an abstract with its calls as methods, the other calls statics of their header\'s class, '
                     'each enum an enum abstract. The runtime\'s own classes (<code>App</code>, <code>Behavior</code>, '
                     '<code>Runtime</code>) are written by hand in <code>bindings/haxe/src/wgf/</code> and aren\'t here.</p>')
        else:
            intro = ('<h1>JS and TypeScript</h1><p class="sub">The typed JS layer (<code>wgf-typed.js</code>), by layer: '
                     'each type a frozen object of the raw binding\'s own functions, the handle first as the C call takes '
                     'it, each enum an object of its values. Plain JS runs it as it is; TypeScript checks it through '
                     '<code>wgf-typed.d.ts</code>, whose signatures these are. The raw layer (<code>wgf.js</code>) is the '
                     'same calls under their C names.</p>')
        listing = []
        for layer, entries in groups:
            listing.append(f'<h2 class="section">{html.escape(layer)}</h2><p>'
                           + ', '.join(f'<a href="{pg}"><code>{html.escape(name)}</code></a>' for pg, name in entries) + '</p>')
        (out / 'index.html').write_text(page(f'libwgf {label} API', nav_html(lang, groups),
                                             intro + search_box(lang, f'Search the {label} API') + ''.join(listing)),
                                        encoding='utf-8')
        return sum(len(e) for _, e in groups)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'apidoc')
    args = ap.parse_args()
    api = headers.read(ROOT, None, None)
    if api is None:
        print('gen_apidoc: no clang (Emscripten brings one); nothing written', file=sys.stderr)
        return 1
    out = args.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    for lang in LANGUAGES:
        (out / lang).mkdir(parents=True)
    ref = Reference(api)
    header_count = ref.write_c(out / 'c')
    type_count = ref.write_types(out / 'haxe', 'haxe')
    ref.write_types(out / 'js', 'js')
    for item in ref.index:
        item['langName'] = LANGUAGES[item['lang']]
    cards = ''.join(f'<a href="{key}/index.html"><b>{html.escape(name)}</b>{text}</a>' for key, name, text in (
        ('c', 'C', f'{header_count} public headers, by layer'),
        ('haxe', 'Haxe', f'{type_count} types: the handle kinds\' methods, the statics, the enums'),
        ('js', 'JS and TypeScript', 'the typed layer\'s objects, with their TypeScript signatures')))
    body = ('<h1>libwgf API</h1>'
            f'<p class="sub">The API of <a href="{REPOSITORY}">libwgf</a>, a C game framework, in each language a game is '
            'written in, from the headers\' own doc comments. Each Haxe and JS member links to the C call it wraps.</p>'
            f'<div class="cards">{cards}</div>' + search_box('', 'Search every language'))
    nav = '<nav><a href="index.html"><b>libwgf API</b></a><div class="langs">' + ''.join(
        f'<a href="{key}/index.html">{html.escape(name)}</a>' for key, name in LANGUAGES.items()) + '</div></nav>'
    (out / 'index.html').write_text(page('libwgf API', nav, body, 'api.css', 'api.js'), encoding='utf-8')
    (out / 'api-index.json').write_text(json.dumps(ref.index, separators=(',', ':')), encoding='utf-8')
    (out / 'api.css').write_text(CSS, encoding='utf-8')
    (out / 'api.js').write_text(JS, encoding='utf-8')
    names = sum(1 for i in ref.index if i['lang'] == 'c')
    print(f'gen_apidoc: C {header_count} header(s), Haxe and JS {type_count} type(s) each, {names} C name(s), in {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
