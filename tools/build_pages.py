#!/usr/bin/env python3
"""Build the GitHub Pages site: every game's web export, and a page listing them.

    tools/build_pages.py [--out DIR]

Each game in games/ (a wgf.json) is exported for the web by the wgf tool (wgf export
--web), which trims its host, smoke-tests it in a headless browser, and holds it to its
size budget; any that fails fails the site. The site (default build/pages/) is the games'
exports at <out>/<game>/ and an index.html linking them, what
.github/workflows/pages.yml deploys on every push to main. Standard library only.
"""
import argparse
import html
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAMES = ROOT / 'games'

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>libwgf's games</title>
<style>
  body {{ margin: 0; padding: 32px 16px; background: #0b0d14; color: #d8dde8;
    font: 16px/1.5 system-ui, sans-serif; }}
  main {{ max-width: 640px; margin: 0 auto; }}
  a {{ color: #7fd4ff; }}
  li {{ margin: 8px 0; }}
</style>
</head>
<body>
<main>
<h1>libwgf's games</h1>
<p>Games on <a href="https://github.com/whirlinggizmo/libwgf">libwgf</a>, a C game framework with games in Haxe, built for the web from <code>main</code>.</p>
<ul>
{items}
</ul>
</main>
</body>
</html>
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'pages')
    args = ap.parse_args()
    out = args.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
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
    (out / 'index.html').write_text(PAGE.format(items='\n'.join(items)), encoding='utf-8')
    (out / '.nojekyll').write_text('')
    if failed:
        print(f'build_pages: FAIL ({", ".join(failed)})')
        return 1
    print(f'build_pages: {len(items)} game(s) in {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
