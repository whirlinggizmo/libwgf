#!/usr/bin/env python3
"""Build the GitHub Pages site: every game's web export, every C example and scene, the API
reference, and a page linking them.

    tools/build_pages.py [--out DIR] [--no-games] [--no-examples] [--no-api]

Each game in games/ (a wgf.json) is exported for the web by the wgf tool (wgf export
--web), which trims its host, smoke-tests it in a headless browser, and holds it to its
size budget; any that fails fails the site. Each C example and combination scene
(tools/examples.py's) is built against wasm32-release, as the browser check builds them,
and copied to <out>/examples/ with examples/assets/ beside them, the launcher written for
them (tools/finish_site.py: a dropdown, each example in a frame, its code versioned); one
that doesn't build fails the site (the browser check runs them; this doesn't again). The API
reference is tools/gen_apidoc.py's, at <out>/api/. The site (default build/pages/) is the
games at <out>/<game>/, those, and an index.html linking them, what
.github/workflows/pages.yml deploys. --no-games, --no-examples, and --no-api leave a part
out, for a quicker look at the rest. Standard library only.
"""
import argparse
import html
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import examples  # noqa: E402
import variants  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
GAMES = ROOT / 'games'
EXAMPLES_VARIANT = 'wasm32-release'

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>libwgf</title>
<style>
  body {{ margin: 0; padding: 32px 16px; background: #0b0d14; color: #d8dde8;
    font: 16px/1.5 system-ui, sans-serif; }}
  main {{ max-width: 640px; margin: 0 auto; }}
  a {{ color: #7fd4ff; }}
  li {{ margin: 8px 0; }}
  h2 {{ margin-top: 32px; font-size: 20px; }}
</style>
</head>
<body>
<main>
<h1>libwgf</h1>
<p><a href="https://github.com/whirlinggizmo/libwgf">libwgf</a> is a C game framework with games in Haxe, JS, or C. Everything here is built for the web from <code>main</code>.</p>
<h2>Games</h2>
<ul>
{items}
</ul>
<h2>Examples</h2>
<p>{examples}</p>
<h2>API</h2>
<p>{api}</p>
</main>
</body>
</html>
"""


def build_games(out):
    """Every game exported into the site: its list items, and the games that failed."""
    items, failed = [], []
    for game in sorted(d for d in GAMES.iterdir() if (d / 'wgf.json').exists()):
        info = json.loads((game / 'wgf.json').read_text(encoding='utf-8'))
        print(f'== {info["name"]}', flush=True)
        done = subprocess.run([sys.executable, str(ROOT / 'wgf'), 'export', '--web', '--out', str(out / 'export' / info['name'])],
                              cwd=game, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
        lines = [line for line in done.stdout.splitlines() if line.startswith('wgf: ')]
        print('\n'.join(lines) if done.returncode == 0 else done.stdout, flush=True)
        if done.returncode != 0:
            failed.append(info['name'])
            continue
        shutil.move(str(out / 'export' / info['name'] / 'web'), str(out / info['name']))
        size = next((line.split('; ')[-1] for line in lines if 'gzipped in all' in line), '')
        items.append(f'<li><a href="{html.escape(info["name"])}/">{html.escape(info.get("title", info["name"]))}</a>'
                     f' <small>({html.escape(size)})</small></li>')
    shutil.rmtree(out / 'export', ignore_errors=True)
    return items, failed


def build_examples(out):
    """Every C example and scene built and copied into <out>/examples/, the assets and the
    launcher beside them: their names, and those that failed."""
    print('== examples', flush=True)
    try:
        examples.prepare(EXAMPLES_VARIANT)
    except RuntimeError as e:
        print(e, flush=True)
        return [], ['the examples\' variant']
    site, names, failed = out / 'examples', [], []
    site.mkdir()
    for name in examples.names():
        try:
            examples.build(EXAMPLES_VARIANT, name)
        except RuntimeError as e:
            print(e, flush=True)
            failed.append(name)
            continue
        built = variants.programs(EXAMPLES_VARIANT) / name
        (site / name).mkdir()
        for file in built.iterdir():  # the page and its program; not the size tools' link map
            if file.is_file() and file.suffix != '.map':
                shutil.copy2(file, site / name / file.name)
        names.append(name)
    shutil.copytree(ROOT / 'examples' / 'assets', site / 'assets')
    subprocess.run([sys.executable, str(ROOT / 'tools' / 'finish_site.py'), '--site', str(site)], check=True)
    print(f'build_pages: {len(names)} example(s)', flush=True)
    return names, failed


def build_api(out):
    """The API reference at <out>/api/: whether it was written."""
    print('== api', flush=True)
    done = subprocess.run([sys.executable, str(ROOT / 'tools' / 'gen_apidoc.py'), '--out', str(out / 'api')])
    return done.returncode == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'pages')
    ap.add_argument('--no-games', action='store_true', help='leave the games out')
    ap.add_argument('--no-examples', action='store_true', help='leave the examples out')
    ap.add_argument('--no-api', action='store_true', help='leave the API reference out')
    args = ap.parse_args()
    out = args.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    items, failed = ([], []) if args.no_games else build_games(out)
    shown, example_failures = ([], []) if args.no_examples else build_examples(out)
    failed += example_failures
    api = False if args.no_api else build_api(out)
    if not args.no_api and not api:
        failed.append('the API reference')
    examples_text = (f'<a href="examples/">{len(shown)} examples</a>, each a page of its own, chosen in the launcher: '
                     + ', '.join(f'<a href="examples/{html.escape(n)}/">{html.escape(n)}</a>' for n in shown)
                     if shown else 'Not in this build.')
    api_text = ('<a href="api/">The API reference</a>: every public header, from its own doc comments, each call '
                'beside its name in the Haxe and JS bindings.' if api else 'Not in this build.')
    (out / 'index.html').write_text(PAGE.format(items='\n'.join(items) or '<li>Not in this build.</li>',
                                                examples=examples_text, api=api_text), encoding='utf-8')
    (out / '.nojekyll').write_text('')
    if failed:
        print(f'build_pages: FAIL ({", ".join(failed)})')
        return 1
    print(f'build_pages: {len(items)} game(s), {len(shown)} example(s), the API {"in" if api else "left out"}, in {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
