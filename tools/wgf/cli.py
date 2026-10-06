"""The wgf tool: make, build, run, check, serve, and export a libwgf game.

    wgf new <dir> [--name NAME]          a game, from libwgf's template (templates/game/)
    wgf build [--web|--desktop|--headless] [--release]
                                         the game, into build/<target>/ (default web)
    wgf run [--headless] [--frames N] [--script FILE] [--no-build]
                                         the desktop build, in a window (or headless);
                                         --frames ends it after N frames
    wgf play SCRIPT [--web] [--no-build] a scripted run (app's script format), headless or
                                         in a browser: PASS or FAIL, as its exit code says
    wgf screenshot [--frame N] [--out FILE] [--no-build]
                                         the web build at frame N, in a headless browser,
                                         saved as a PNG (default build/screenshot.png)
    wgf dump [--frame N] [--no-build]    the ecs's world at frame N, as a scene's text,
                                         from a headless run
    wgf serve [--port N]                 the game in a browser, reloaded as its Haxe is
                                         saved, its state kept
    wgf export [--web] [--desktop] [--out DIR]
                                         export/web (a static folder with a trimmed host)
                                         and export/desktop, each smoke-tested, the web
                                         held to the game's size budget (default both)

Run it in a game's directory (or below): the game is the wgf.json there or above. Every
command but new and serve exits 0 when it did what it says, and non-zero, saying why,
when it didn't. Standard library only.
"""
import argparse
import os
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import devserver  # noqa: E402
import game as games  # noqa: E402

ROOT = games.ROOT
TEMPLATE = ROOT / 'templates' / 'game'


def say(text):
    print(f'wgf: {text}', flush=True)


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
    say(f'{name} made in {dest}: `cd {dest} && wgf serve` runs it in a browser, reloading as you save')
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


def verdict(output, scripted):
    """Exit code of a run from what it logged: a script's PASS or FAIL; without a script,
    any error fails it."""
    result = games.judged(output)
    if scripted:
        return 0 if result else 1
    return 1 if result is False else 0


def cmd_run(args):
    game = games.find()
    target = 'headless' if args.headless else 'desktop'
    exe = native_exe(game, target, not args.no_build)
    script = None
    if args.script:
        script = Path(args.script).read_text(encoding='utf-8')
    elif args.frames:
        script = games.frames_script(args.frames)
    code, output = games.run_native(exe, script)
    return code or verdict(output, script is not None)


def web_dir(game, build):
    out = game.build_dir('web')
    if build:
        return games.build_web(game)
    if not (out / 'index.html').exists():
        raise games.GameError(f'no web build ({out}): run without --no-build')
    return out


def cmd_play(args):
    game = games.find()
    script = Path(args.script).read_text(encoding='utf-8')
    if args.web:
        site = web_dir(game, not args.no_build)
        lines = games.run_page(site, 'index.html', script)
        result = games.judged(lines)
    else:
        exe = native_exe(game, 'headless', not args.no_build)
        code, output = games.run_native(exe, script)
        result = games.judged(output) if code == 0 else False
    say(f'play {args.script}: {"PASS" if result else "FAIL"}')
    return 0 if result else 1


def cmd_screenshot(args):
    game = games.find()
    site = web_dir(game, not args.no_build)
    out = Path(args.out).resolve() if args.out else game.root / 'build' / 'screenshot.png'
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    script = f'wgf-script 1\nat {args.frame} screenshot shot\nat {args.frame + 60} end\n'
    games.run_page(site, 'index.html', script, screenshot=out, echo=False)
    if not out.exists():
        raise games.GameError(f'no screenshot: the page never reached frame {args.frame}')
    say(f'frame {args.frame} saved: {out}')
    return 0


def cmd_dump(args):
    game = games.find()
    exe = native_exe(game, 'headless', not args.no_build)
    code, output = games.run_native(exe, f'wgf-script 1\nat {args.frame} dump\nat {args.frame} end\n', echo=False)
    text = games.dumped(output)
    if code != 0 or 'wgf_script: DUMP ecs BEGIN' not in output:
        if code != 0:
            print(output)
        raise games.GameError('no dump: the run ended early, or the game has no entities (an ecs to dump)'
                              if code == 0 else f'the run failed (exit {code})')
    print(text)
    return 0


def cmd_serve(args):
    game = games.find()
    return devserver.Server(game, args.port).run()


# ---- export ----------------------------------------------------------------------------

RUNTIME_CALLS = ('_wgf_app_run', '_wgf_version_get', '_wgf_version_get_major', '_wgf_version_get_minor')


def trimmed_exports(program_js, out):
    """The host's exports a release program calls, written as a trimmed host's list: every
    call into the host is by its quoted key (host["_wgf_..."]), wherever dead-code
    elimination and inlining left it, so the keys in the program's JS are what it calls.
    Read from the program's JS (Haxe's output, not C)."""
    text = Path(program_js).read_text(encoding='utf-8')
    calls = set(re.findall(r'"(_wgf_[a-z0-9_]+)"', text))
    exports = sorted(calls | set(RUNTIME_CALLS))
    import json
    out.write_text(json.dumps({'exports': exports}, indent=1) + '\n', encoding='utf-8')
    return exports


def smoke_script(game):
    path = game.scripts / 'smoke.wgfscript'
    return path.read_text(encoding='utf-8') if path.exists() else games.frames_script(120)


def export_web(game, out):
    """The web export: a release build, then its host trimmed to what it calls, the
    assets copied in, smoke-tested in a browser, and measured against the budget."""
    work = game.root / 'build' / 'export-web'
    games.build_web(game, release=True, out=work)  # the program first: what it calls makes the host
    listing = work / 'exports.json'
    exports = trimmed_exports(work / f'{game.name}.js', listing)
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
    files = [out / 'wgf-host.wasm', out / 'wgf-host.js', out / f'{game.name}.js', out / 'index.html']
    sizes = {f.name: games.gzip_size(f) for f in files}
    total = sum(sizes.values())
    say(f'web export: {len(exports)} calls in its host; '
        + ', '.join(f'{name} {size / 1024:.1f} KB' for name, size in sizes.items())
        + f'; {total / 1024:.1f} KB gzipped in all')
    ok = True
    if game.budget_kb is not None and total > game.budget_kb * 1024:
        say(f'web export: FAIL: {total / 1024:.1f} KB is over the budget of {game.budget_kb} KB (wgf.json)')
        ok = False
    lines = games.run_page(out, 'index.html', smoke_script(game), echo=False)
    smoke = games.judged(lines)
    for line in lines:
        if '[ERROR]' in line or 'FAIL' in line:
            print(line)
    say(f'web export: smoke {"PASS" if smoke else "FAIL"} ({out})')
    return ok and bool(smoke)


def export_desktop(game, out):
    """The desktop export: a release build with its assets copied beside it, run with the
    smoke script (in a window: on Linux with no display, on Xvfb's)."""
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
        code, output = games.run_native(exe, smoke_script(game), echo=False)
    finally:
        if xvfb is not None:
            xvfb.stop()
            del os.environ['DISPLAY']
    smoke = code == 0 and games.judged(output)
    if not smoke:
        print('\n'.join(output.splitlines()[-20:]))
    say(f'desktop export: smoke {"PASS" if smoke else "FAIL"} ({exe})')
    return bool(smoke)


def cmd_export(args):
    game = games.find()
    out = Path(args.out).resolve() if args.out else game.root / 'export'
    both = not args.web and not args.desktop
    ok = True
    if args.web or both:
        ok = export_web(game, out / 'web') and ok
    if args.desktop or both:
        ok = export_desktop(game, out / 'desktop') and ok
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
    p.add_argument('--script', help='a scripted run (app\'s script format)')
    p.add_argument('--no-build', action='store_true')
    p.set_defaults(run=cmd_run)
    p = sub.add_parser('play', help='a scripted run: PASS or FAIL')
    p.add_argument('script')
    p.add_argument('--web', action='store_true', help='in a headless browser (default: headless)')
    p.add_argument('--no-build', action='store_true')
    p.set_defaults(run=cmd_play)
    p = sub.add_parser('screenshot', help='the web build at a frame, as a PNG')
    p.add_argument('--frame', type=int, default=60)
    p.add_argument('--out')
    p.add_argument('--no-build', action='store_true')
    p.set_defaults(run=cmd_screenshot)
    p = sub.add_parser('dump', help='the ecs\'s world at a frame, as a scene\'s text')
    p.add_argument('--frame', type=int, default=60)
    p.add_argument('--no-build', action='store_true')
    p.set_defaults(run=cmd_dump)
    p = sub.add_parser('serve', help='in a browser, reloaded as its Haxe is saved')
    p.add_argument('--port', type=int, default=8080)
    p.set_defaults(run=cmd_serve)
    p = sub.add_parser('export', help='export/web and export/desktop, smoke-tested')
    p.add_argument('--web', action='store_true')
    p.add_argument('--desktop', action='store_true')
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
