#!/usr/bin/env python3
"""Finish a web build's site (out/wasm32/<variant>/site): the versions, the list, and the
launcher, for a site of one page per example.

    tools/finish_site.py [--site DIR] [--launcher FILE]

Each example built for the web is a folder in the site, <name>/, holding its own page
(index.html, the example's, from examples/c/<name>/) and its program, <name>.js and
<name>.wasm. This stamps each page's /*wgf:versions*/ mark with a short hash of its
.js and .wasm, so the page loads name.js?v=<hash> and name.wasm?v=<hash>: a host can
then let browsers keep those for good (they change name when they change;
tools/serve_site.py --cache does), and a returning visit fetches no code at all. The
versions are in the page itself, not a file beside it, which would be one more round
trip before the code could start downloading. A page whose mark is gone (edited away) is left as it is,
and said so; one stamped before is stamped again, as its program is now.

Also writes the site's examples.json, the examples built, and its launcher,
index.html, from examples/c/wgf_launcher.html, a dropdown of them each shown in a frame;
no example depends on it. tools/examples.py runs this after building a web variant's
examples. Standard library only.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import variants  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
MARK = '/*wgf:versions*/{'  # the mark is the comment followed at once by the versions' JSON object: {} until
# stamped; the page's prose may name the comment, which this never matches
LAUNCHER = ROOT / 'examples' / 'c' / 'wgf_launcher.html'


def version(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()[:12]


def finish(site, launcher=LAUNCHER):
    """Stamp, list, and write the launcher: the names finished, and the pages whose mark
    was gone."""
    site = Path(site)
    names, unmarked = [], []
    for folder in sorted(p for p in site.iterdir() if p.is_dir() and (p / f'{p.name}.js').is_file()):
        name = folder.name
        names.append(name)
        versions = {'js': version(folder / f'{name}.js')}
        if (folder / f'{name}.wasm').is_file():
            versions['wasm'] = version(folder / f'{name}.wasm')
        page = folder / 'index.html'
        if not page.is_file():
            continue
        text = page.read_text(encoding='utf-8')
        start = text.find(MARK)
        end = text.find('}', start) if start >= 0 else -1  # the object stamped before, or the {} to fill
        if start < 0 or end < 0:
            unmarked.append(name)
            continue
        stamped = text[:start] + MARK[:-1] + json.dumps(versions, separators=(',', ':')) + text[end + 1:]
        if stamped != text:
            page.write_text(stamped, encoding='utf-8')
    (site / 'examples.json').write_text(json.dumps(names, separators=(',', ':')) + '\n', encoding='utf-8')
    (site / 'index.html').write_text(Path(launcher).read_text(encoding='utf-8'), encoding='utf-8')
    return names, unmarked


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--site', type=Path, default=variants.programs(variants.web(debug=False)),
                    help='the web build\'s site (default: the release one)')
    ap.add_argument('--launcher', type=Path, default=LAUNCHER)
    opts = ap.parse_args()
    if not opts.site.is_dir():
        sys.exit(f'finish_site: no site at {opts.site} (build a web variant\'s examples first: tools/examples.py)')
    names, unmarked = finish(opts.site, opts.launcher)
    for name in unmarked:
        print(f'finish_site: {name}/index.html has no {MARK} mark: left unversioned')
    print(f'finish_site: {opts.site}: {len(names)} example(s), the launcher at index.html')
    return 0


if __name__ == '__main__':
    sys.exit(main())
