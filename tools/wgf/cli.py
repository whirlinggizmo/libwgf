"""The wgf tool: make, build, run, check, serve, and export a libwgf game.

    wgf new <dir> [--name NAME]          a game, from libwgf's template (templates/game/)
    wgf build [--web|--desktop|--headless] [--release]
                                         the game, into build/<target>/ (default web)
    wgf run [--headless] [--frames N] [--autopilot FILE] [--no-build]
                                         the desktop build, in a window (or headless);
                                         --frames ends it after N frames
    wgf autopilot FILE [--web] [--screenshots DIR] [--timeout S] [--no-build]
                                         the game flown by an autopilot file (its inputs
                                         at frames, its expectations: app's format),
                                         headless or in a browser: PASS or FAIL, as its
                                         exit code says, and why when it fails;
                                         --screenshots saves a --web run's named ones
    wgf autopilot FILE --record          the desktop build, played by hand in a window,
                                         every input written to FILE as an autopilot
    wgf screenshot [--frame N] [--autopilot FILE] [--out FILE] [--no-build]
                                         the web build at frame N, in a headless browser,
                                         saved as a PNG (default build/screenshot.png)
    wgf dump [--frame N] [--autopilot FILE] [--no-build]
                                         the ecs's world at frame N, as a scene's text,
                                         from a headless run
  (screenshot's and dump's --autopilot flies the game there first: the file's inputs up
  to the frame, which is the file's end when --frame isn't given)
    wgf serve [--port N]                 the game in a browser, reloaded as its Haxe is
                                         saved, its state kept
    wgf export [--web] [--desktop] [--out DIR] [--autopilot FILE]
                                         export/web (a static folder with a trimmed host)
                                         and export/desktop, each smoke-tested, the web
                                         held to the game's size budget (default both);
                                         --autopilot flies each with FILE, not the smoke run

Run it in a game's directory (or below): the game is the wgf.json there or above. Every
command but new and serve exits 0 when it did what it says, and non-zero, saying why,
when it didn't. Standard library only.
"""
import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import devserver  # noqa: E402
import game as games  # noqa: E402  (which puts tools/ on the path, for the two below)
import jsbinding  # noqa: E402
import webhost  # noqa: E402

ROOT = games.ROOT
TEMPLATE = ROOT / 'templates' / 'game'


def say(text):
    print(f'wgf: {text}', flush=True)


def launcher():
    """How to run this wgf from anywhere: `wgf` when it is the one on PATH, else its path
    (with python first on Windows, which runs no script by its first line)."""
    found = shutil.which('wgf')
    mine = ROOT / 'wgf'
    if found and Path(found).resolve() == mine.resolve():
        return 'wgf'
    return f'python {mine}' if os.name == 'nt' else str(mine)


# ---- new -------------------------------------------------------------------------------

def cmd_new(args):
    dest = Path(args.dir).resolve()
    name = args.name or dest.name
    if not name.replace('-', '').replace('_', '').isalnum():
        raise games.GameError(f'"{name}" isn\'t a name of letters, digits, - and _ (--name)')
    if dest.exists() and any(dest.iterdir()):
        raise games.GameError(f'{dest} isn\'t empty: a game starts in a new directory')
    for path in sorted(TEMPLATE.rglob('*')):
        target = dest / path.relative_to(TEMPLATE)
        if path.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            text = path.read_text(encoding='utf-8')
            target.write_text(text.replace('@NAME@', name), encoding='utf-8')
        except UnicodeDecodeError:
            shutil.copyfile(path, target)
    say(f'{name} made in {dest}: `cd {dest} && {launcher()} serve` runs it in a browser, reloading as you save')
    return 0


# ---- build and run ---------------------------------------------------------------------

def target_of(args, default='web'):
    for target in games.TARGETS:
        if getattr(args, target, False):
            return target
    return default


def cmd_build(args):
    game = games.find()
    target = target_of(args)
    if target == 'web':
        out = games.build_web(game, release=args.release)
        say(f'{game.name} for the web in {out} (its page: index.html)')
    else:
        exe = games.build_native(game, target, release=args.release)
        say(f'{game.name} for {target} in {exe}')
    return 0


def native_exe(game, target, build):
    if build:
        return games.build_native(game, target)
    exe = game.build_dir(target) / (game.name + ('.exe' if os.name == 'nt' else ''))
    if not exe.exists():
        raise games.GameError(f'no {target} build ({exe}): run without --no-build')
    return exe


def verdict(output, flown):
    """Exit code of a run from what it logged: its autopilot's PASS or FAIL; without
    one, any error fails it."""
    result = games.judged(output)
    if flown:
        return 0 if result else 1
    return 1 if result is False else 0


def cmd_run(args):
    game = games.find()
    target = 'headless' if args.headless else 'desktop'
    exe = native_exe(game, target, not args.no_build)
    autopilot = None
    if args.autopilot:
        autopilot = Path(args.autopilot).read_text(encoding='utf-8')
    elif args.frames:
        autopilot = games.frames_autopilot(args.frames)
    code, output = games.run_native(exe, autopilot)
    return code or verdict(output, autopilot is not None)


def flown_to(path, frame, command, after):
    """An autopilot that flies the file at `path` (if any) to `frame` -- the file's end
    when `frame` is None -- then does `command` there, and ends `after` frames on. The
    file's own end is left out, and so are its own screenshot and dump lines, which would
    fire first and win over this one's; how many were left out is said. (Its text, the
    frame.)"""
    lines, end, dropped = ['wgf-autopilot 1'], None, 0
    if path:
        for line in Path(path).read_text(encoding='utf-8').splitlines()[1:]:
            words = line.split('#', 1)[0].split()
            if len(words) >= 3 and words[0] == 'at' and words[2] == 'end':
                end = int(words[1])
                continue
            if len(words) >= 3 and words[0] == 'at' and words[2] in ('screenshot', 'dump'):
                dropped += 1
                continue
            lines.append(line)
    if dropped:
        say(f'{path}: its own {dropped} screenshot or dump line(s) left out, so the one asked for is the one made')
    at = frame if frame is not None else (end if end is not None else 60)
    lines += [f'at {at} {command}', f'at {at + after} end']
    return '\n'.join(lines) + '\n', at


def web_dir(game, build):
    out = game.build_dir('web')
    if build:
        return games.build_web(game)
    if not (out / 'index.html').exists():
        raise games.GameError(f'no web build ({out}): run without --no-build')
    return out


def cmd_autopilot(args):
    game = games.find()
    if args.record:
        return record(game, Path(args.file))
    autopilot = Path(args.file).read_text(encoding='utf-8')
    if args.web:
        site = web_dir(game, not args.no_build)
        shots = Path(args.screenshots).resolve() if args.screenshots else game.root / 'build' / 'screenshots'
        lines = games.run_page(site, 'index.html', autopilot, screenshots=shots,
                               timeout=args.timeout or games.autopilot_seconds(autopilot))
        result = games.judged(lines)
    else:
        exe = native_exe(game, 'headless', not args.no_build)
        code, output = games.run_native(exe, autopilot, timeout=args.timeout or 600)
        lines = output.splitlines()
        result = games.judged(output) if code == 0 else False
    if not result:
        for line in games.why_failed(lines):
            say(f'autopilot {args.file}: {line}')
    say(f'autopilot {args.file}: {"PASS" if result else "FAIL"}')
    return 0 if result else 1


def record(game, out):
    """The desktop build made to record (build/desktop-record: the only one with the
    recorder in it) in a window, played by hand, every input written to `out` as an
    autopilot as it was given (BUILDING.md, "Autopilot files": recording), until the game
    quits or the window closes. Always built, --no-build or not."""
    exe = games.build_native(game, 'desktop', record=True)  # the only build with the recorder in it
    out = out.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    say(f'recording to {out}: play, then quit the game or close its window')
    code, _ = games.run_native(exe, None, timeout=24 * 3600, record=out)
    if not out.exists():
        raise games.GameError(f'nothing recorded (exit {code}): the run ended before its first frame')
    say(f'recorded {out}: add its expectations, then fly it with `wgf autopilot {out.name}`')
    return code


def cmd_screenshot(args):
    game = games.find()
    site = web_dir(game, not args.no_build)
    out = Path(args.out).resolve() if args.out else game.root / 'build' / 'screenshot.png'
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    autopilot, frame = flown_to(args.autopilot, args.frame, 'screenshot shot', 60)
    games.run_page(site, 'index.html', autopilot, screenshot=out, echo=False)
    if not out.exists():
        raise games.GameError(f'no screenshot: the page never reached frame {frame}')
    say(f'frame {frame} saved: {out}')
    return 0


def cmd_dump(args):
    game = games.find()
    exe = native_exe(game, 'headless', not args.no_build)
    autopilot, frame = flown_to(args.autopilot, args.frame, 'dump', 0)
    code, output = games.run_native(exe, autopilot, echo=False)
    text = games.dumped(output)
    if code != 0 or 'wgf_autopilot: DUMP ecs BEGIN' not in output:
        if code != 0:
            print(output)
        raise games.GameError(f'no dump at frame {frame}: the run ended early, or the game has no entities (an ecs '
                              'to dump)' if code == 0 else f'the run failed (exit {code})')
    print(text)
    return 0


def cmd_serve(args):
    game = games.find()
    return devserver.Server(game, args.port).run()


# ---- export ----------------------------------------------------------------------------

def trimmed_exports(program_js, out, assets=None):
    """The host's exports a release program calls, written as a trimmed host's list: every
    call the program makes is the JS binding's by its quoted key (Raw.binding["wgf_..."]),
    wherever dead-code elimination and inlining left it, so the keys in the program's JS
    that are calls are what it calls; the binding's run adds its own, and a scene of the
    game's `assets` naming a glTF file adds glTF's create. Read from the program's JS
    (Haxe's output, not C)."""
    text = Path(program_js).read_text(encoding='utf-8')
    known = set(json.loads(webhost.FULL.read_text(encoding='utf-8'))['exports'])
    calls = {'_' + name for name in re.findall(r'"(wgf_[a-z0-9_]+)"', text)} & known
    if games.names_a_file(assets):
        calls.add('_wgf_mesh_create')
    exports = sorted(calls | set(jsbinding.RUN_CALLS))
    out.write_text(json.dumps({'exports': exports}, indent=1) + '\n', encoding='utf-8')
    return exports


def smoke_autopilot(game):
    path = game.autopilot / 'smoke.autopilot'
    return path.read_text(encoding='utf-8') if path.exists() else games.frames_autopilot(120)


def export_web(game, out, autopilot=None):
    """The web export: a release build, then its host trimmed to what it calls, the
    assets copied in, smoke-tested in a browser, and measured against the budget."""
    work = game.root / 'build' / 'export-web'
    games.build_web(game, release=True, out=work)  # the program first: what it calls makes the host
    listing = work / 'exports.json'
    exports = trimmed_exports(work / f'{game.name}.js', listing, game.assets)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    games.build_web(game, release=True, out=out, exports=listing)
    assets = out / 'assets'
    if assets.is_symlink():
        assets.unlink()
        shutil.copytree(game.assets, assets) if game.assets.is_dir() else assets.mkdir()
    for notice in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
        shutil.copyfile(ROOT / notice, out / f'libwgf-{notice}')
    files = [out / 'wgf-host.wasm', out / 'wgf-host.js', out / 'wgf.js', out / f'{game.name}.js', out / 'index.html']
    sizes = {f.name: games.gzip_size(f) for f in files}
    total = sum(sizes.values())
    say(f'web export: {len(exports)} calls in its host; '
        + ', '.join(f'{name} {size / 1024:.1f} KB' for name, size in sizes.items())
        + f'; {total / 1024:.1f} KB gzipped in all')
    ok = True
    if game.budget_kb is not None and total > game.budget_kb * 1024:
        say(f'web export: FAIL: {total / 1024:.1f} KB is over the budget of {game.budget_kb} KB (wgf.json)')
        ok = False
    flown = autopilot.read_text(encoding='utf-8') if autopilot else smoke_autopilot(game)
    lines = games.run_page(out, 'index.html', flown, echo=False, timeout=games.autopilot_seconds(flown))
    smoke = games.judged(lines)
    for line in lines:
        if '[ERROR]' in line or 'FAIL' in line:
            print(line)
    say(f'web export: {autopilot.name if autopilot else "smoke"} {"PASS" if smoke else "FAIL"} ({out})')
    return ok and bool(smoke)


def export_desktop(game, out, autopilot=None):
    """The desktop export: a release build with its assets copied beside it, run with the
    smoke autopilot (in a window: on Linux with no display, on Xvfb's)."""
    if out.exists():
        shutil.rmtree(out)
    exe = games.build_native(game, 'desktop', release=True, out=out)
    assets = out / 'assets'
    if assets.is_symlink():
        assets.unlink()
        shutil.copytree(game.assets, assets) if game.assets.is_dir() else assets.mkdir()
    for notice in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
        shutil.copyfile(ROOT / notice, out / f'libwgf-{notice}')
    xvfb = None
    if sys.platform.startswith('linux') and not os.environ.get('DISPLAY'):
        if games.browser.find_xvfb() is None:
            say('desktop export: SKIPPING its smoke run (no display, and no Xvfb)')
            say(f'desktop export: {exe} (not smoke-tested)')
            return True
        run = games.browser.RunProcesses('wgf-export')
        run.install_handlers()
        display = games.browser.start_xvfb(run)
        os.environ['DISPLAY'] = display
        xvfb = run
    try:
        flown = autopilot.read_text(encoding='utf-8') if autopilot else smoke_autopilot(game)
        code, output = games.run_native(exe, flown, echo=False)
    finally:
        if xvfb is not None:
            xvfb.stop()
            del os.environ['DISPLAY']
    smoke = code == 0 and games.judged(output)
    if not smoke:
        print('\n'.join(output.splitlines()[-20:]))
    say(f'desktop export: {autopilot.name if autopilot else "smoke"} {"PASS" if smoke else "FAIL"} ({exe})')
    return bool(smoke)


def cmd_export(args):
    game = games.find()
    out = Path(args.out).resolve() if args.out else game.root / 'export'
    both = not args.web and not args.desktop
    ok = True
    autopilot = Path(args.autopilot).resolve() if args.autopilot else None
    if args.web or both:
        ok = export_web(game, out / 'web', autopilot) and ok
    if args.desktop or both:
        ok = export_desktop(game, out / 'desktop', autopilot) and ok
    return 0 if ok else 1


# ---- the commands ----------------------------------------------------------------------

def parser():
    ap = argparse.ArgumentParser(prog='wgf', description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True, metavar='COMMAND')
    p = sub.add_parser('new', help='a game, from libwgf\'s template')
    p.add_argument('dir')
    p.add_argument('--name', help='its name (default: the directory\'s)')
    p.set_defaults(run=cmd_new)
    p = sub.add_parser('build', help='the game, into build/<target>/')
    for target in games.TARGETS:
        p.add_argument(f'--{target}', action='store_true', help=f'for {target}')
    p.add_argument('--release', action='store_true')
    p.set_defaults(run=cmd_build)
    p = sub.add_parser('run', help='the desktop build, in a window or headless')
    p.add_argument('--headless', action='store_true')
    p.add_argument('--frames', type=int, help='end after this many frames')
    p.add_argument('--autopilot', help='an autopilot file to fly it (app\'s format)')
    p.add_argument('--no-build', action='store_true')
    p.set_defaults(run=cmd_run)
    p = sub.add_parser('autopilot', help='the game flown by an autopilot file: PASS or FAIL')
    p.add_argument('file', help='the autopilot to fly, or with --record the one to write')
    p.add_argument('--web', action='store_true', help='in a headless browser (default: headless)')
    p.add_argument('--screenshots',
                   help='with --web: the folder its named screenshots go in (default build/screenshots)')
    p.add_argument('--record', action='store_true',
                   help='play the desktop build by hand in a window, writing every input to the file as an autopilot')
    p.add_argument('--timeout', type=float,
                   help='seconds the run may take (default: in a browser, two minutes and the file\'s last frame at '
                        '3 a second; headless, 600)')
    p.add_argument('--no-build', action='store_true')
    p.set_defaults(run=cmd_autopilot)
    p = sub.add_parser('screenshot', help='the web build at a frame, as a PNG')
    p.add_argument('--frame', type=int, help='the frame (default: the autopilot\'s end, or 60)')
    p.add_argument('--autopilot', help='an autopilot file to fly it there first')
    p.add_argument('--out')
    p.add_argument('--no-build', action='store_true')
    p.set_defaults(run=cmd_screenshot)
    p = sub.add_parser('dump', help='the ecs\'s world at a frame, as a scene\'s text')
    p.add_argument('--frame', type=int, help='the frame (default: the autopilot\'s end, or 60)')
    p.add_argument('--autopilot', help='an autopilot file to fly it there first')
    p.add_argument('--no-build', action='store_true')
    p.set_defaults(run=cmd_dump)
    p = sub.add_parser('serve', help='in a browser, reloaded as its Haxe is saved')
    p.add_argument('--port', type=int, default=8080)
    p.set_defaults(run=cmd_serve)
    p = sub.add_parser('export', help='export/web and export/desktop, smoke-tested')
    p.add_argument('--web', action='store_true')
    p.add_argument('--desktop', action='store_true')
    p.add_argument('--autopilot', help='an autopilot to fly against each export in place of its smoke run '
                   '(its playthrough, say)')
    p.add_argument('--out')
    p.set_defaults(run=cmd_export)
    return ap


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        return args.run(args)
    except games.GameError as e:
        print(f'wgf: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
