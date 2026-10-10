#!/usr/bin/env python3
"""Web sizes, measured: every example and every game export, beside libwgt's and
wgrender-c's same programs, in docs/benchmarks.md; and a check against the committed
baseline.

    tools/measure_sizes.py [--references] [--write] [--check] [--render] [--only NAME[,NAME...]]

libwgf's programs are built in wasm32-release, as a page ships them: each C example
(tools/examples.py), each game's web export (wgf export --web: its trimmed host, its
page, its program), and each JS example (examples/js/, its host and JS binding trimmed
alike: js:<name>). For each, the wasm and the JS, raw, gzip -9, and brotli -q 11 (when
the brotli tool or module is there); a game's JS is its host's, the JS binding's, and
its program's, and the binding's share is given too.

  --references  build libwgt's and wgrender-c's release web examples from their
                checkouts beside this one (../libwgt, ../wgrender-c), read-only: each is
                configured, built, and staged under build/compare/, never in its repo
                (wgrender-c's own build, its WGR_OUT there; libwgt's library installed
                there, each example built against it); their numbers, with their
                commits, go in the baseline. Without it, the baseline's are kept.
  --write       write docs/benchmarks.json (the baseline: every number, and where each
                library was measured from) and docs/benchmarks.md (the tables)
  --render      write docs/benchmarks.md from docs/benchmarks.json, measuring nothing (what
                tools/bench/measure_frames.py runs after recording its frame times there)
  --check       measure libwgf's programs and compare each with the baseline: one more
                than TOLERANCE larger (gzip, wasm and JS together) fails; one smaller
                says to --write a new baseline. CI runs this.

The rows of the comparison are ROWS: libwgf's example, libwgt's, and wgrender-c's that
does the same thing, each marked same or differs (docs/HISTORY.md, "Web sizes, measured",
"Same rows, a target"); a same row is also held to libwgt's size and the autopilot
runner's cost. LADDER is the feature cost ladder: programs in examples/sizes/ that each
add one thing. Standard library only.
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

# (libwgf's program, libwgt's, wgrender-c's, whether libwgf's does the same, and why not):
# a "same" row is the same program in each, held to SAME_TARGET over libwgt's; a "differs"
# row leaves out what libwgf lacks, so its size is never a saving, and turns "same" as
# the milestone that brings what it lacks does (each example's header lists every
# difference).
ROWS = [
    ('app-skeleton', 'app-skeleton', None, 'same', ''),
    ('app-hello', 'app-hello', 'hello', 'same', ''),
    ('app-window', 'app-window', 'window', 'same', ''),
    ('app-tick', 'app-tick', 'tick', 'same', ''),
    ('app-gamepad', 'app-gamepad', 'gamepad', 'same', ''),
    ('app-touch', 'app-touch', 'touch', 'differs',
     'libwgf has no pointer picking: the coin is hit-tested by hand, where libwgt links its pointer and picking'),
    ('gfx-font', 'gfx-font', 'font', 'same', ''),
    ('gfx-hello3d', 'gfx-hello3d', 'hello3d', 'same', ''),
    ('gfx-meshes', None, 'meshes', 'same', ''),
    ('gfx-model', 'gfx-model', 'model', 'differs',
     'libwgf has no skinning or animation yet: a toy car of its own (tools/gen_model.py) in place of the animated '
     'character'),
    ('gfx-materials', None, 'materials', 'differs',
     'libwgf has no skinning yet: a gold torus in place of the animated character, and a generated sphere in place '
     'of the file\'s'),
    ('gfx-lights', None, 'lights', 'differs',
     'libwgf has no skinning or 3D sprites yet: generated capsules in place of the animated characters, and no lit '
     'billboards'),
    ('gfx-shadows', 'gfx-shadows', 'shadows', 'differs',
     'libwgf has no skinning or animation yet: the toy car in place of the walking character, and its readout fixed '
     'labels with no formatting'),
    ('gfx-environment', 'gfx-environment', 'environment', 'differs',
     'libwgf has no skinning or animation yet: the toy car in place of the walking character, and its readout '
     'fixed labels with no formatting'),
    ('gfx-textures', 'gfx-textures', 'textures', 'same', ''),
    ('gfx-sprite2d', 'gfx-sprite2d', 'sprite2d', 'differs',
     'libwgf has no 3D and no alpha picking: the animated model behind the sprites, its lights, and picking are left out'),
    ('gfx-particles', 'gfx-particles', 'particles', 'differs',
     'libwgf has no 3D and no GPU particles: libwgt\'s 3D emitters are projected into 2D CPU emitters, untextured'),
    ('gfx-tilemap', 'gfx-tilemap', 'tilemap', 'differs',
     'libwgf has no 3D sprites and no picking: a 2D stage and a 2D camera, coins hit-tested by hand'),
    ('audio-music', 'audio-music', 'audio', 'same', ''),
    ('asset-fetch', None, 'fetch', 'same', ''),
    ('asset-force-fetch', None, 'force_fetch', 'same', ''),
    ('asset-loading', None, 'loading', 'differs',
     'libwgf has no 3D: textures and sounds of like weight in place of wgrender-c\'s models and environments'),
]
# A "same" row's target: libwgt's size, gzip, and the autopilot runner's cost, which
# every libwgf program carries on purpose, so a shipped build can be played through
# (docs/HISTORY.md, "Same rows, a target"). --check fails a "same" row past it.
RUNNER_COST = 4864  # 4.75 KB: text expectations, recording's hooks, a wait on the loads (HISTORY, "The runner's allowance")
# And the presentation's transform (wgf_presentation.h), which every program's drawing,
# clips, and input go through, a mode set or not: measured at 304 bytes of gzip on
# app-hello (docs/HISTORY.md, "Milestone 2, step 2"); libwgt has no presentation mode.
PRESENTATION_COST = 512
# The feature cost ladder: the skeleton, then each step adding one thing to the one
# before (examples/sizes/), and the steps still to come with what will bring them.
LADDER = [
    ('app-skeleton', 'skeleton: a window cleared each frame'),
    ('ladder-1-text', '+ text'),
    ('ladder-2-textures', '+ textures'),
    ('ladder-3-sprites', '+ 2D sprites'),
    ('ladder-4-ecs', '+ ecs'),
    ('ladder-5-ui', '+ ui'),
    ('ladder-6-model', '+ 3D model'),
    ('ladder-7-physics3d', '+ physics3d'),
    ('ladder-8-shadows', '+ lights and shadows'),
    ('ladder-9-environment', '+ environment'),
    (None, '+ skinning (milestone 2)'),
]
LADDER_DIR = ROOT / 'examples' / 'sizes'


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
    """One program's sizes, bytes: raw, gzip -9, and brotli, for its wasm and its JS; and
    of its JS, the JS binding's (wgf.js), when it has one."""
    out = {}
    binding = [f for f in js_files if Path(f).name == 'wgf.js']
    for kind, files in (('wasm', wasm_files), ('js', js_files), ('binding', binding)):
        if not files:
            continue
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
    for source in sorted(LADDER_DIR.iterdir()) if LADDER_DIR.is_dir() else []:
        if not (source / 'CMakeLists.txt').exists() or (only and source.name not in only):
            continue
        examples.build_at(variant, source.name, source)
        out[source.name] = measure([site / source.name / f'{source.name}.wasm'],
                                   [site / source.name / f'{source.name}.js'])
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
        out[f'game:{name}'] = measure([web / 'wgf-host.wasm'],
                                      [web / 'wgf-host.js', web / 'wgf.js', web / f'{name}.js'])
    return out


def libwgf_js_examples(only):
    """Each JS example as a release export is: its host and binding trimmed to the calls
    and enums it names (tools/webhost.py's build_js_example)."""
    out = {}
    for example in sorted(p for p in webhost.JS_EXAMPLES.iterdir() if (p / 'main.js').is_file()):
        name = f'js:{example.name}'
        if only and name not in only:
            continue
        site = webhost.build_js_example(example, variants.web(debug=False),
                                        ROOT / 'build' / 'sizes' / f'js-{example.name}', trimmed=True)
        modules = sorted(p for p in site.glob('*.js') if p.name != 'wgf-host.js')
        out[name] = measure([site / 'wgf-host.wasm'], [site / 'wgf-host.js', *modules])
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
             'A **same** row is the same program in each library, and libwgf\'s is held to libwgt\'s size and the '
             f'autopilot runner\'s {RUNNER_COST / 1024:g} KB and the presentation\'s {PRESENTATION_COST / 1024:g} KB '
             '(the target); a **differs** row\'s libwgf program '
             'leaves out what libwgf lacks, so its size is not a saving: its reason is linked, and it turns same as '
             'the milestone that brings what it lacks does.', '',
             '| program | | libwgf | target | libwgt | wgrender-c |', '|---|---|---:|---:|---:|---:|']

    def cell(sizes):
        if sizes is None:
            return '-'
        br = total(sizes, 'br')
        return kb(total(sizes)) + (f' ({kb(br)})' if br is not None else '')

    for ours, theirs, wgrender, status, why in ROWS:
        mark = 'same' if status == 'same' else f'[differs](#differs-{ours})'
        goal = target(baseline, ours)
        lines.append(f'| {ours}' + (f' / {theirs}' if theirs and theirs != ours else '')
                     + (f' / {wgrender}' if wgrender else '') + f' | {mark} | {cell(lib.get(ours))} | '
                     + f'{kb(goal) if goal else "-"} | '
                     + f'{cell(wgt.get(theirs)) if theirs else "-"} | {cell(wgr.get(wgrender)) if wgrender else "-"} |')
    lines += ['', '### Why rows differ', '']
    for ours, theirs, wgrender, status, why in ROWS:
        if status != 'same':
            lines.append(f'- <a id="differs-{ours}"></a>**{ours}**: {why} ([its header](../examples/c/{ours}/main.c) '
                         'lists every difference).')
    lines += ['', '## The feature cost ladder', '',
              'Programs that each add one thing to the one before (`examples/sizes/`): what each feature costs on its own '
              'is its row\'s increment, gzip, wasm and JS together; brotli in brackets. Rows to come say what brings them.',
              '', '| program | adds | total | increment |', '|---|---|---:|---:|']
    before = None
    for name, adds in LADDER:
        sizes = lib.get(name) if name else None
        if sizes is None:
            lines.append(f'| {name or "-"} | {adds} | - | - |')
            continue
        now = total(sizes)
        inc = '' if before is None else f'+{kb(now - before)}'
        br = total(sizes, 'br')
        lines.append(f'| {name} | {adds} | {kb(now)}' + (f' ({kb(br)})' if br is not None else '') + f' | {inc} |')
        before = now
    games = sorted(n for n in lib if n.startswith('game:'))
    if games:
        lines += ['', '## Games', '', 'Each game\'s web export: its host trimmed to the calls it makes, and its program '
                  '(the JS column is the host\'s, the JS binding\'s, and the program\'s); its page and assets aren\'t '
                  'counted.', '',
                  '| game | wasm | wasm.gz | js | js.gz | total.gz | total.br | budget |', '|---|---:|---:|---:|---:|---:|---:|---:|']
        for name in games:
            s = lib[name]
            budget = baseline['libwgf'].get('budgets', {}).get(name)
            lines.append(f'| {name[5:]} | {kb(s["wasm"]["raw"])} | {kb(s["wasm"]["gz"])} | {kb(s["js"]["raw"])} | '
                         f'{kb(s["js"]["gz"])} | {kb(total(s))} | {kb(total(s, "br"))} | {budget or ""} |')
    bound = sorted((n for n in lib if 'binding' in lib[n]), key=lambda n: total(lib[n]))
    if bound:
        lines += ['', '## The JS binding\'s share', '',
                  'Every program on the JS binding (`bindings/js/wgf.js`): a game\'s Haxe program reaches the host '
                  'through it, and a JS example is written on it. Its share is its trimmed copy\'s, the calls the program '
                  'makes; '
                  'gzip. Beside the same program on C (app-hello) or Haxe (the game), it is what the layer costs.', '',
                  '| program | total.gz | wgf.js.gz | share |', '|---|---:|---:|---:|']
        for name in bound:
            s = lib[name]
            lines.append(f'| {name} | {kb(total(s))} | {kb(s["binding"]["gz"])} | '
                         f'{100 * s["binding"]["gz"] / total(s):.1f}% |')
    frames = baseline.get('frames', {})
    if frames:
        lines += ['', '## Frame times', '',
                  'Each game\'s web export flown by its autopilot in a browser (`tools/bench/measure_frames.py`): '
                  'each frame\'s main-thread work, in milliseconds, as Chrome traced it, every frame of the run its '
                  'loading among them; and the garbage collections. Each row names the machine, the display, and the '
                  'CPU throttle it was measured on, and the commit: a row from another machine is not comparable.', '',
                  '| program | frames | mean | median | 95th | 99th | worst | over 16.7 | over 33 | collections (ms, longest) '
                  '| on |', '|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|']
        for name, f in sorted(frames.items()):
            lines.append(f'| {name} | {f["frames"]} | {f["mean"]} | {f["median"]} | {f["p95"]} | {f["p99"]} | {f["worst"]} | '
                         f'{f["over16"]} | {f["over33"]} | {f["gc"]} ({f["gcTotal"]}, {f["gcWorst"]}) | '
                         f'{f["cpu"]}, {f["gpu"]}, {f["display"]}, CPU throttled {f["throttle"]:g}x, '
                         f'{f["autopilot"]}, {f["commit"]} |')
    for name, bench in sorted(baseline.get('benches', {}).items()):
        counts = sorted({int(key.split('|')[1]) for key in bench['cases']})
        names = bench.get('order') or list(dict.fromkeys(key.split('|')[0] for key in bench['cases']))
        lines += ['', f'## {name[len("bench:"):]}', '',
                  f'`tools/bench/{name[len("bench:"):]}/` in a browser (`tools/bench/measure_frames.py {name}`): each '
                  'case\'s frames\' main-thread work, its mean in milliseconds by how many models it draws (the 95th '
                  'percentile after it), traced by Chrome. The program\'s header says what each case is. On '
                  f'{bench["cpu"]}, {bench["gpu"]}, {bench["display"]}, CPU throttled {bench["throttle"]:g}x, '
                  f'the median of {bench["runs"]} run(s), {bench["commit"]}: another machine\'s are not comparable.', '',
                  '| case | ' + ' | '.join(f'{c} models' for c in counts) + ' |',
                  '|---|' + '---:|' * len(counts)]
        for case in names:
            cells = [bench['cases'].get(f'{case}|{c}') for c in counts]
            lines.append(f'| {case} | ' + ' | '.join(f'{r["mean"]} ({r["p95"]})' if r else '-' for r in cells) + ' |')
    actors = baseline.get('actors', {})
    if actors.get('rows'):
        before = actors.get('before', {})
        old = before.get('rows', {})
        flecs = actors.get('flecs', {}).get('rows', {})
        lines += ['', '## Actors', '',
                  'What an actor costs (`tools/bench/measure_actors.py`, the program `tools/bench/actors/main.c`, whose '
                  'header says what each row and number is): the heap\'s growth an actor, its pools\' slack included, and '
                  'the frames\' own cost an actor a frame (a find\'s rows: a find, or an actor found), the median of '
                  f'{actors.get("runs", 1)} runs, on {actors.get("cpu", "?")}, `{actors.get("variant", "?")}`, '
                  f'{actors.get("commit", "?")}. A check fails a row past its bytes by 5%, or past its time by twice the run\'s own speed against these (the median of its rows\' ratios), and on this machine past 1.5 times its time. '
                  + (f'Before: the same program on the nodes and entities actors replaced, {before.get("commit", "?")} '
                     '(docs/HISTORY.md, "One kind of object, the actor"); a dash where there was no such call. The store- rows are the ecs\'s store on its own '
                     '(`ecs/bench/wgf_ecs_store_bench.c`), each storage\'s worst case among them.'
                     if before else '')
                  + (' The flecs columns: the same on the store step 3b replaced (docs/HISTORY.md, "flecs or sparse '
                     'sets, measured"), recorded before it went.' if flecs else ''), '',
                  '| row | bytes | ns | flecs: bytes | flecs: ns | before: bytes | before: ns |',
                  '|---|---:|---:|---:|---:|---:|---:|']

        def number(row, key, digits):
            return f'{row[key]:.{digits}f}' if row is not None and key in row else '-'

        for name, row in sorted(actors['rows'].items()):
            was = old.get(name)
            other = flecs.get(name)
            lines.append(f'| {name} | {number(row, "bytes", 1)} | {number(row, "ns", 2)} | '
                         f'{number(other, "bytes", 1)} | {number(other, "ns", 2)} | '
                         f'{number(was, "bytes", 1)} | {number(was, "ns", 2)} |')
    for title, programs in (('libwgf', lib), ('libwgt', wgt), ('wgrender-c', wgr)):
        if not programs:
            continue
        lines += ['', f'## Every {title} program', '', '| program | wasm | js | wasm.gz | js.gz | wasm.br | js.br | total.gz |',
                  '|---|---:|---:|---:|---:|---:|---:|---:|']
        for name, s in sorted(programs.items(), key=lambda kv: total(kv[1]) or 0):
            lines.append(f'| {name} | {kb(s["wasm"]["raw"])} | {kb(s["js"]["raw"])} | {kb(s["wasm"]["gz"])} | '
                         f'{kb(s["js"]["gz"])} | {kb(s["wasm"]["br"])} | {kb(s["js"]["br"])} | {kb(total(s))} |')
    return '\n'.join(lines) + '\n'


def target(baseline, ours):
    """A same row's target, bytes of gzip: libwgt's program and the runner's cost; None
    for a differs row, or one libwgt has no program for."""
    row = next((r for r in ROWS if r[0] == ours), None)
    if row is None or row[3] != 'same' or row[1] is None:
        return None
    theirs = baseline.get('references', {}).get('libwgt', {}).get('programs', {}).get(row[1])
    return None if theirs is None else total(theirs) + RUNNER_COST + PRESENTATION_COST


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
        goal = target(baseline, name)
        if goal is not None and new_total > goal:
            worse.append(f'{name}: {kb(new_total)} KB gzip, past its target {kb(goal)} (libwgt\'s and the runner\'s '
                         f'{RUNNER_COST / 1024:g} KB, the presentation\'s {PRESENTATION_COST / 1024:g}): a "same" row')
    return worse, better


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--references', action='store_true', help="build and measure libwgt's and wgrender-c's too")
    ap.add_argument('--write', action='store_true', help='write docs/benchmarks.json and docs/benchmarks.md')
    ap.add_argument('--check', action='store_true', help='fail on a program grown past the baseline')
    ap.add_argument('--render', action='store_true', help='write docs/benchmarks.md from the baseline alone')
    ap.add_argument('--only', help='comma-separated programs (game:<name> for a game)')
    args = ap.parse_args()
    if args.render:
        TABLE.write_text(markdown(json.loads(BASELINE.read_text())))
        print(f'measure_sizes: wrote {TABLE.relative_to(ROOT)} from {BASELINE.relative_to(ROOT)}')
        return 0
    only = set(args.only.split(',')) if args.only else None
    try:
        webhost.emcc()
    except RuntimeError as e:
        print(f'measure_sizes: SKIPPING ({e})')
        return 77 if args.check else 0
    try:
        measured = libwgf_examples(only)
        measured.update(libwgf_games(only))
        measured.update(libwgf_js_examples(only))
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
