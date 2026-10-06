#!/usr/bin/env python3
"""Web sizes, measured: every example and every game export, beside libwgt's and
wgrender-c's same programs, in docs/benchmarks.md; and a check against the committed
baseline.

    tools/measure_sizes.py [--references] [--write] [--check] [--only NAME[,NAME...]]

libwgf's programs are built in wasm32-release, as a page ships them: each C example
(tools/examples.py) and each game's web export (wgf export --web: its trimmed host, its
page, its program). For each, the wasm and the JS, raw, gzip -9, and brotli -q 11 (when
the brotli tool or module is there); a game's JS is its host's and its program's.

  --references  build libwgt's and wgrender-c's release web examples from their
                checkouts beside this one (../libwgt, ../wgrender-c), read-only: each is
                configured, built, and staged under build/compare/, never in its repo
                (wgrender-c's own build, its WGR_OUT there; libwgt's library installed
                there, each example built against it); their numbers, with their
                commits, go in the baseline. Without it, the baseline's are kept.
  --write       write docs/benchmarks.json (the baseline: every number, and where each
                library was measured from) and docs/benchmarks.md (the tables)
  --check       measure libwgf's programs and compare each with the baseline: one more
                than TOLERANCE larger (gzip, wasm and JS together) fails; one smaller
                says to --write a new baseline. CI runs this.

The rows of the comparison are ROWS: libwgf's example, libwgt's, and wgrender-c's that
does the same thing (docs/HISTORY.md, "Web sizes, measured"). Standard library only.
"""
import argparse
import gzip
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import examples  # noqa: E402
import variants  # noqa: E402
import webhost  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / 'docs' / 'benchmarks.json'
TABLE = ROOT / 'docs' / 'benchmarks.md'
COMPARE = ROOT / 'build' / 'compare'
LIBWGT = ROOT.parent / 'libwgt'
WGRENDER = ROOT.parent / 'wgrender-c'
TOLERANCE = (0.01, 1024)  # a regression is more than 1%, and more than 1 KB, of gzip

# (libwgf's program, libwgt's, wgrender-c's): the same program in each, or None
ROWS = [
    ('app-skeleton', 'app-skeleton', None),
    ('app-hello', 'app-hello', 'hello'),
    ('app-window', 'app-window', 'window'),
    ('app-tick', 'app-tick', 'tick'),
    ('app-gamepad', 'app-gamepad', 'gamepad'),
    ('app-touch', 'app-touch', 'touch'),
    ('gfx-font', 'gfx-font', 'font'),
    ('gfx-textures', 'gfx-textures', 'textures'),
    ('gfx-sprite2d', 'gfx-sprite2d', 'sprite2d'),
    ('gfx-particles', 'gfx-particles', 'particles'),
    ('gfx-tilemap', 'gfx-tilemap', 'tilemap'),
    ('audio-music', 'audio-music', 'audio'),
    ('asset-fetch', None, 'fetch'),
    ('asset-force-fetch', None, 'force_fetch'),
    ('asset-loading', None, 'loading'),
]


def brotli_size(data):
    try:
        import brotli
        return len(brotli.compress(data, quality=11))
    except ImportError:
        tool = shutil.which('brotli')
        if tool is None:
            return None
        return len(subprocess.run([tool, '-q', '11', '-c'], input=data, capture_output=True, check=True).stdout)


def measure(wasm_files, js_files):
    """One program's sizes, bytes: raw, gzip -9, and brotli, for its wasm and its JS."""
    out = {}
    for kind, files in (('wasm', wasm_files), ('js', js_files)):
        data = b''.join(Path(f).read_bytes() for f in files)
        out[kind] = {'raw': len(data), 'gz': sum(len(gzip.compress(Path(f).read_bytes(), 9)) for f in files)}
        br = [brotli_size(Path(f).read_bytes()) for f in files]
        out[kind]['br'] = None if None in br else sum(br)
    return out


def total(sizes, key='gz'):
    if sizes is None or sizes['wasm'][key] is None:
        return None
    return sizes['wasm'][key] + sizes['js'][key]


def run(command, cwd=ROOT, what='', log=None):
    done = subprocess.run([str(c) for c in command], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors='replace')
    if log is not None:
        Path(log).write_text(done.stdout)
    if done.returncode != 0:
        raise RuntimeError(f'{what} failed:\n' + '\n'.join(done.stdout.strip().splitlines()[-20:]))
    return done.stdout


def commit(repo):
    return run(['git', '-C', repo, 'log', '-1', '--format=%h %cs'], what='git').strip()


def toolchain():
    return examples.emscripten_toolchain()


# ---- libwgf ------------------------------------------------------------------------------

def libwgf_examples(only):
    variant = variants.web(debug=False)
    examples.prepare(variant)
    site = variants.programs(variant)
    out = {}
    for name in examples.names():
        if only and name not in only:
            continue
        examples.build(variant, name)
        out[name] = measure([site / name / f'{name}.wasm'], [site / name / f'{name}.js'])
    return out


def libwgf_games(only):
    out = {}
    games = ROOT / 'games'
    for game in sorted(d for d in games.iterdir() if (d / 'wgf.json').exists()) if games.is_dir() else []:
        name = json.loads((game / 'wgf.json').read_text())['name']
        if only and f'game:{name}' not in only and name not in only:
            continue
        dest = ROOT / 'build' / 'sizes' / name
        run([sys.executable, ROOT / 'wgf', 'export', '--web', '--out', dest], cwd=game, what=f'exporting {name}')
        web = dest / 'web'
        out[f'game:{name}'] = measure([web / 'wgf-host.wasm'], [web / 'wgf-host.js', web / f'{name}.js'])
    return out


# ---- the references --------------------------------------------------------------------

def build_wgrender():
    """wgrender-c's release web examples, built from its checkout into build/compare/."""
    work, out = COMPARE / 'wgrender-wasm', COMPARE / 'wgrender-out'
    run(['cmake', '-S', WGRENDER, '-B', work, '-G', 'Ninja', f'-DCMAKE_TOOLCHAIN_FILE={toolchain()}',
         '-DWGR_EXAMPLES=ON', '-DWGR_WEB=ON', f'-DWGR_OUT={out}'], what='configuring wgrender-c')
    run(['cmake', '--build', work], what='building wgrender-c')
    site = out / 'site'
    return {js.stem: measure([js.with_suffix('.wasm')], [js]) for js in sorted(site.glob('*.js'))
            if js.with_suffix('.wasm').exists()}


def build_libwgt():
    """libwgt's release web examples: its library built and installed into build/compare/,
    each example built against that, as a program outside libwgt is."""
    work, staged, site = COMPARE / 'libwgt-wasm', COMPARE / 'libwgt-out', COMPARE / 'libwgt-site'
    run(['cmake', '-S', LIBWGT, '-B', work, '-G', 'Ninja', f'-DCMAKE_TOOLCHAIN_FILE={toolchain()}', '-DWGT_WEB=ON',
         '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_INSTALL_PREFIX={staged}'], what='configuring libwgt')
    run(['cmake', '--build', work], what='building libwgt')
    run(['cmake', '--install', work], what='staging libwgt')
    out = {}
    for d in sorted((LIBWGT / 'examples' / 'c').iterdir()):
        if not (d / 'CMakeLists.txt').exists() or not (d / 'index.html').exists():
            continue  # one without its page would write it into libwgt's checkout
        build = COMPARE / 'libwgt-examples' / d.name
        run(['cmake', '-S', d, '-B', build, '-G', 'Ninja', f'-DCMAKE_TOOLCHAIN_FILE={toolchain()}',
             '-DCMAKE_BUILD_TYPE=Release', f'-DWGT_OUT={staged}', f'-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={site}'],
            what=f'configuring libwgt {d.name}')
        run(['cmake', '--build', build], what=f'building libwgt {d.name}')
        out[d.name] = measure([site / d.name / f'{d.name}.wasm'], [site / d.name / f'{d.name}.js'])
    return out


def references():
    found = {}
    for name, repo, build in (('libwgt', LIBWGT, build_libwgt), ('wgrender-c', WGRENDER, build_wgrender)):
        if not (repo / 'CMakeLists.txt').exists():
            print(f'measure_sizes: SKIPPING {name} (no checkout at {repo})')
            continue
        before = run(['git', '-C', repo, 'status', '--porcelain'], what='git status')
        print(f'measure_sizes: building {name} ({commit(repo)}) under {COMPARE}', flush=True)
        found[name] = {'commit': commit(repo), 'programs': build()}
        if run(['git', '-C', repo, 'status', '--porcelain'], what='git status') != before:
            raise RuntimeError(f'building {name} changed its checkout ({repo}): it must be read only')
    return found


# ---- the table -------------------------------------------------------------------------

def kb(n):
    return '' if n is None else f'{n / 1024:.1f}'


def markdown(baseline):
    lib = baseline['libwgf']['programs']
    refs = baseline.get('references', {})
    wgt = refs.get('libwgt', {}).get('programs', {})
    wgr = refs.get('wgrender-c', {}).get('programs', {})
    lines = ['# Benchmarks: web sizes', '',
             'Generated by `tools/measure_sizes.py --write` from `benchmarks.json`, the baseline CI holds libwgf to '
             '(`--check`: a program more than 1% and 1 KB larger, gzipped, fails). Every program is a release web build, '
             'as a page ships it; sizes in KB. Why each number is what it is, and how it was found, is HISTORY.md\'s '
             '("Web sizes, measured" and after).', '',
             f'Measured from libwgf {baseline["libwgf"]["commit"]}'
             + ''.join(f', {n} {r["commit"]}' for n, r in refs.items()) + ', with Emscripten '
             + baseline.get('emscripten', '?') + '.', '',
             '## Side by side', '',
             'The same program in each library (each libwgf example\'s header says how it matches), gzip -9, its wasm '
             'and its JS together; brotli -q 11 in brackets.', '',
             '| program | libwgf | libwgt | wgrender-c |', '|---|---:|---:|---:|']

    def cell(sizes):
        if sizes is None:
            return '-'
        br = total(sizes, 'br')
        return kb(total(sizes)) + (f' ({kb(br)})' if br is not None else '')

    for ours, theirs, wgrender in ROWS:
        lines.append(f'| {ours}' + (f' / {theirs}' if theirs and theirs != ours else '')
                     + (f' / {wgrender}' if wgrender else '') + f' | {cell(lib.get(ours))} | '
                     + f'{cell(wgt.get(theirs)) if theirs else "-"} | {cell(wgr.get(wgrender)) if wgrender else "-"} |')
    games = sorted(n for n in lib if n.startswith('game:'))
    if games:
        lines += ['', '## Games', '', 'Each game\'s web export: its host trimmed to the calls it makes, and its program '
                  '(the JS column is both scripts); its page and assets aren\'t counted.', '',
                  '| game | wasm | wasm.gz | js | js.gz | total.gz | total.br | budget |', '|---|---:|---:|---:|---:|---:|---:|---:|']
        for name in games:
            s = lib[name]
            budget = baseline['libwgf'].get('budgets', {}).get(name)
            lines.append(f'| {name[5:]} | {kb(s["wasm"]["raw"])} | {kb(s["wasm"]["gz"])} | {kb(s["js"]["raw"])} | '
                         f'{kb(s["js"]["gz"])} | {kb(total(s))} | {kb(total(s, "br"))} | {budget or ""} |')
    for title, programs in (('libwgf', lib), ('libwgt', wgt), ('wgrender-c', wgr)):
        if not programs:
            continue
        lines += ['', f'## Every {title} program', '', '| program | wasm | js | wasm.gz | js.gz | wasm.br | js.br | total.gz |',
                  '|---|---:|---:|---:|---:|---:|---:|---:|']
        for name, s in sorted(programs.items(), key=lambda kv: total(kv[1]) or 0):
            lines.append(f'| {name} | {kb(s["wasm"]["raw"])} | {kb(s["js"]["raw"])} | {kb(s["wasm"]["gz"])} | '
                         f'{kb(s["js"]["gz"])} | {kb(s["wasm"]["br"])} | {kb(s["js"]["br"])} | {kb(total(s))} |')
    return '\n'.join(lines) + '\n'


def emscripten_version():
    return (ROOT / 'cmake' / 'emscripten-version.txt').read_text().split()[0]


def check(measured, baseline):
    """The programs grown past the tolerance, and those that shrank."""
    worse, better = [], []
    for name, sizes in measured.items():
        old = baseline['libwgf']['programs'].get(name)
        if old is None:
            better.append(f'{name}: new ({kb(total(sizes))} KB): --write it into the baseline')
            continue
        new_total, old_total = total(sizes), total(old)
        allowed = old_total + max(old_total * TOLERANCE[0], TOLERANCE[1])
        if new_total > allowed:
            worse.append(f'{name}: {kb(new_total)} KB gzip, was {kb(old_total)} (allowed {kb(allowed)})')
        elif new_total < old_total - max(old_total * TOLERANCE[0], TOLERANCE[1]):
            better.append(f'{name}: {kb(new_total)} KB, was {kb(old_total)}: --write a new baseline')
    return worse, better


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--references', action='store_true', help="build and measure libwgt's and wgrender-c's too")
    ap.add_argument('--write', action='store_true', help='write docs/benchmarks.json and docs/benchmarks.md')
    ap.add_argument('--check', action='store_true', help='fail on a program grown past the baseline')
    ap.add_argument('--only', help='comma-separated programs (game:<name> for a game)')
    args = ap.parse_args()
    only = set(args.only.split(',')) if args.only else None
    try:
        webhost.emcc()
    except RuntimeError as e:
        print(f'measure_sizes: SKIPPING ({e})')
        return 77 if args.check else 0
    try:
        measured = libwgf_examples(only)
        measured.update(libwgf_games(only))
        refs = references() if args.references else None
    except RuntimeError as e:
        print(f'measure_sizes: {e}', file=sys.stderr)
        return 1
    baseline = json.loads(BASELINE.read_text()) if BASELINE.exists() else {'libwgf': {'programs': {}}}
    for name in sorted(measured, key=lambda n: total(measured[n])):
        s = measured[name]
        print(f'{name:<20} wasm {kb(s["wasm"]["gz"]):>8} js {kb(s["js"]["gz"]):>7} total {kb(total(s)):>8} KB gz')
    status = 0
    if args.check:
        worse, better = check(measured, baseline)
        for line in better:
            print(f'measure_sizes: {line}')
        for line in worse:
            print(f'measure_sizes: FAIL {line}')
        status = 1 if worse else 0
        print(f'measure_sizes: {"FAIL" if worse else "PASS"}: {len(measured)} program(s) against the baseline')
    if args.write:
        programs = dict(baseline['libwgf']['programs']) if only else {}
        programs.update(measured)
        budgets = {}
        for game in (ROOT / 'games').glob('*/wgf.json'):
            info = json.loads(game.read_text())
            if info.get('web_budget_kb'):
                budgets[f'game:{info["name"]}'] = info['web_budget_kb']
        baseline['libwgf'] = {'commit': commit(ROOT), 'programs': programs, 'budgets': budgets}
        baseline['emscripten'] = emscripten_version()
        if refs is not None:
            baseline['references'] = refs
        BASELINE.write_text(json.dumps(baseline, indent=1, sort_keys=True) + '\n')
        TABLE.write_text(markdown(baseline))
        print(f'measure_sizes: wrote {BASELINE.relative_to(ROOT)} and {TABLE.relative_to(ROOT)}')
    return status


if __name__ == '__main__':
    sys.exit(main())
