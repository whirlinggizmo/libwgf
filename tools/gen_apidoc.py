#!/usr/bin/env python3
"""Write libwgf's API reference as HTML: a page a public header, from the headers' own doc
comments, each call beside its names in the bindings.

    tools/gen_apidoc.py [--out DIR]

The headers are read as every tool reads them (tools/headers.py: clang's parse, never their
text), and each exported call is placed in the bindings as the generator places it
(tools/mapping.py's Binding, which tools/gen_binding.py writes them by: its Haxe and typed JS type and member; the raw JS call is the
C name). A page a header, grouped by layer: the header's overview (its comments no
declaration owns), its enums with their values, its structs with their fields, and its calls
in header order, each its C signature, its names in the bindings (or that it is C's alone: a
static inline, or one the bindings skip), and its doc (its own comment, its group's as the
bindings take it, or, where that is a call's above it on the page, a link to that call). The index lists the layers' headers and searches every call, enum,
and struct by name, in the page, with no server. The out directory (default build/apidoc/)
is index.html, a page a header, api.css, and api.js; tools/build_pages.py puts it in the
GitHub Pages site at api/. Standard library only.
"""
import argparse
import html
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import headers  # noqa: E402
import mapping  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = 'https://github.com/whirlinggizmo/libwgf'

CSS = """:root { --bg: #0f1115; --panel: #171a21; --line: #2a2f3a; --text: #d3d8e2; --dim: #8b93a3;
  --accent: #7ea2ff; --code: #e6c07b; color-scheme: dark; }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.55 system-ui, sans-serif; }
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }
code, pre { font: 13px/1.5 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
code { color: var(--code); }
pre { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 10px 12px;
  overflow-x: auto; color: var(--text); }
.layout { display: flex; min-height: 100vh; }
nav { width: 260px; flex: none; background: var(--panel); border-right: 1px solid var(--line);
  padding: 16px 12px; position: sticky; top: 0; height: 100vh; overflow-y: auto; }
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
.sig { margin: 4px 0; }
.names { color: var(--dim); font-size: 13px; margin: 2px 0 6px; }
.names code { color: var(--text); }
.doc p { margin: 6px 0; }
table { border-collapse: collapse; margin: 6px 0; font-size: 13px; }
td, th { border: 1px solid var(--line); padding: 3px 8px; text-align: left; }
#search { width: 100%; padding: 8px 10px; margin: 8px 0 14px; background: var(--bg); color: var(--text);
  border: 1px solid var(--line); border-radius: 6px; font: inherit; }
#results li { margin: 2px 0; font: 13px ui-monospace, monospace; }
#results .where { color: var(--dim); }
@media (max-width: 760px) { .layout { display: block; } nav { width: auto; height: auto; position: static; }
  main { padding: 16px; } }
"""

JS = """// The index's search: every call, enum, and struct by name (api-index.json beside it),
// shown as you type; nothing is fetched but that file.
(async () => {
  const box = document.getElementById('search'), list = document.getElementById('results');
  if (!box) return;
  const items = await (await fetch('api-index.json')).json();
  const show = () => {
    const q = box.value.trim().toLowerCase();
    list.innerHTML = '';
    if (!q) return;
    const found = items.filter(i => i.name.toLowerCase().includes(q) || (i.binding || '').toLowerCase().includes(q));
    for (const i of found.slice(0, 200)) {
      const li = document.createElement('li');
      const a = document.createElement('a');
      a.href = i.page + '#' + i.name;
      a.textContent = i.name;
      li.append(a, ' ');
      const where = document.createElement('span');
      where.className = 'where';
      where.textContent = i.header + (i.binding ? '  ' + i.binding : '');
      li.append(where);
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
    parts = text.split('`')
    out = []
    for i, part in enumerate(parts):
        out.append(f'<code>{html.escape(part)}</code>' if i % 2 else html.escape(part))
    return ''.join(out)


def doc_html(text):
    """A doc comment as HTML: its paragraphs (blank lines apart) as paragraphs, a block whose
    lines are indented or laid out in columns (a list of keys and their defaults) as it is."""
    if not text:
        return ''
    out = []
    for block in text.split('\n\n'):
        lines = block.split('\n')
        if not block.strip():
            continue
        if any(line.startswith(' ') or '   ' in line.strip() for line in lines):
            out.append(f'<pre>{html.escape(block)}</pre>')
        else:
            out.append(f'<p>{inline(" ".join(line.strip() for line in lines))}</p>')
    return '<div class="doc">' + ''.join(out) + '</div>'


def layer_of(header):
    """libwgf/gfx/include/wgf_model.h -> 'gfx'; include/wgf.h -> 'libwgf'."""
    parts = Path(header).parts
    return parts[1] if len(parts) > 3 and parts[0] == headers.LAYER_ROOT else 'libwgf'


def page_of(header):
    return Path(header).stem + '.html'


def signature(f):
    params = ', '.join(f'{p.type} {p.name}' if p.name else p.type for p in f.params) or 'void'
    if f.variadic:
        params += ', ...'
    ret = f.returns if f.returns.endswith('*') else f.returns + ' '
    return f'{ret}{f.name}({params});'


def nav_html(groups, here=None):
    out = ['<nav>', f'<a href="index.html"><b>libwgf API</b></a>']
    for layer, hs in groups:
        out.append(f'<h2>{html.escape(layer)}</h2><ul>')
        for h in hs:
            cls = ' class="here"' if h == here else ''
            out.append(f'<li><a{cls} href="{page_of(h)}">{html.escape(Path(h).name)}</a></li>')
        out.append('</ul>')
    out.append('</nav>')
    return ''.join(out)


def page(title, nav, body):
    return (f'<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">\n<title>{html.escape(title)}</title>\n'
            f'<link rel="stylesheet" href="api.css">\n</head>\n<body>\n<div class="layout">{nav}<main>{body}</main></div>\n'
            f'<script src="api.js"></script>\n</body>\n</html>\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'apidoc')
    args = ap.parse_args()
    api = headers.read(ROOT, None, None)
    if api is None:
        print('gen_apidoc: no clang (Emscripten brings one); nothing written', file=sys.stderr)
        return 1
    binding = mapping.Binding(api)
    docs = binding.docs()
    out = args.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    by_header = {}
    for h in api.headers:
        by_header[h] = {'functions': [], 'enums': [], 'structs': []}
    for f in api.functions.values():
        by_header.setdefault(f.header, {'functions': [], 'enums': [], 'structs': []})['functions'].append(f)
    for e in api.enums.values():
        by_header.setdefault(e.header, {'functions': [], 'enums': [], 'structs': []})['enums'].append(e)
    for s in api.structs.values():
        by_header.setdefault(s.header, {'functions': [], 'enums': [], 'structs': []})['structs'].append(s)
    shown = [h for h, d in by_header.items() if d['functions'] or d['enums'] or d['structs'] or api.overviews.get(h)]
    layers = {}
    for h in shown:
        layers.setdefault(layer_of(h), []).append(h)
    order = ['libwgf'] + [layer for layer in headers.LAYERS]
    groups = [(layer, sorted(layers[layer], key=lambda h: Path(h).name)) for layer in order if layer in layers]

    index, calls = [], 0
    for _, hs in groups:
        for h in hs:
            d = by_header[h]
            body = [f'<h1>{html.escape(Path(h).name)}</h1>',
                    f'<p class="sub">{html.escape(layer_of(h))} &middot; '
                    f'<a href="{REPOSITORY}/blob/main/{html.escape(h)}">{html.escape(h)}</a></p>']
            body.append(doc_html(api.overviews.get(h, '')))
            if d['enums']:
                body.append('<h2 class="section">Enums</h2>')
                for e in d['enums']:
                    haxe = binding.enums.get(e.name, '')
                    rows = ''.join(f'<tr><td><code>{html.escape(k)}</code></td><td>{v}</td></tr>' for k, v in e.values.items())
                    body.append(f'<div class="item" id="{html.escape(e.name)}"><h3>{html.escape(e.name)}</h3>'
                                + (f'<p class="names">bindings: <code>{html.escape(haxe)}</code></p>' if haxe else '')
                                + doc_html(e.doc) + f'<table>{rows}</table></div>')
                    index.append({'name': e.name, 'page': page_of(h), 'header': Path(h).name, 'binding': haxe})
            if d['structs']:
                body.append('<h2 class="section">Structs</h2>')
                for s in d['structs']:
                    rows = ''.join(f'<tr><td><code>{html.escape(fl.type)}</code></td><td><code>{html.escape(fl.name)}'
                                   f'{f"[{fl.count}]" if fl.count else ""}</code></td></tr>' for fl in s.fields)
                    body.append(f'<div class="item" id="{html.escape(s.name)}"><h3>{html.escape(s.name)}</h3>'
                                + doc_html(s.doc) + (f'<table>{rows}</table>' if rows else '') + '</div>')
                    index.append({'name': s.name, 'page': page_of(h), 'header': Path(h).name, 'binding': ''})
            if d['functions']:
                body.append('<h2 class="section">Calls</h2>')
                owner = {}  # a doc's text -> the call above that has it as its own
                for f in d['functions']:
                    names, bound = '', ''
                    if f.exported and f.name not in mapping.SKIPPED:
                        try:
                            haxe, member, instance = binding.place(f)
                            bound = f'{haxe}.{member}'
                            how = 'a method of' if instance else 'a static of'
                            names = (f'<p class="names">Haxe and typed JS: <code>{html.escape(bound)}</code> ({how} '
                                     f'<code>{html.escape(haxe)}</code>); raw JS: <code>wgf.{html.escape(f.name)}</code></p>')
                        except mapping.MapError:
                            names = '<p class="names">not in the bindings</p>'
                    elif f.exported:
                        names = '<p class="names">not in the bindings (reached by their runtime by hand)</p>'
                    else:
                        names = '<p class="names">C alone (static inline)</p>'
                    if f.doc:
                        owner.setdefault(f.doc, f.name)
                        doc = doc_html(f.doc)
                    elif docs.get(f.name) in owner:  # its group's, written above a call before it
                        above = owner[docs[f.name]]
                        doc = f'<p class="names">See <a href="#{html.escape(above)}"><code>{html.escape(above)}</code></a>, above.</p>'
                    else:
                        doc = doc_html(docs.get(f.name, ''))
                    body.append(f'<div class="item" id="{html.escape(f.name)}"><h3>{html.escape(f.name)}</h3>'
                                f'<pre class="sig">{html.escape(signature(f))}</pre>{names}{doc}</div>')
                    index.append({'name': f.name, 'page': page_of(h), 'header': Path(h).name, 'binding': bound})
                    calls += 1
            (out / page_of(h)).write_text(page(f'{Path(h).name}: libwgf API', nav_html(groups, h), ''.join(body)),
                                          encoding='utf-8')

    intro = ('<h1>libwgf API</h1>'
             f'<p class="sub">The public headers of <a href="{REPOSITORY}">libwgf</a>, a C game framework, from their own '
             'doc comments, each call beside its name in the Haxe and JS bindings.</p>'
             '<input id="search" type="search" placeholder="Search calls, enums, and structs" autofocus>'
             '<ul id="results"></ul>')
    listing = []
    for layer, hs in groups:
        listing.append(f'<h2 class="section">{html.escape(layer)}</h2><ul>')
        for h in hs:
            first = (api.overviews.get(h, '') or '').split('\n\n')[0].replace('\n', ' ')
            if len(first) > 160:
                first = first[:157].rsplit(' ', 1)[0] + '...'
            listing.append(f'<li><a href="{page_of(h)}"><code>{html.escape(Path(h).name)}</code></a> {inline(first)}</li>')
        listing.append('</ul>')
    (out / 'index.html').write_text(page('libwgf API', nav_html(groups), intro + ''.join(listing)), encoding='utf-8')
    (out / 'api-index.json').write_text(json.dumps(index, separators=(',', ':')), encoding='utf-8')
    (out / 'api.css').write_text(CSS, encoding='utf-8')
    (out / 'api.js').write_text(JS, encoding='utf-8')
    print(f'gen_apidoc: {sum(len(hs) for _, hs in groups)} header(s), {calls} call(s) in {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
