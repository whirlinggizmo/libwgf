#!/usr/bin/env python3
"""Run one entrant of helix's Wardens trial on libwgf: a fresh `claude -p` building the game
from helix's spec alone, in Haxe on libwgf's binding, under the conditions helix's entrants
had (../helix's docs/trials/README.md, "Running one"; its tools/run_entrant.py), so its
cost and its grade read beside theirs.

    tools/run_entrant.py <entrant directory> [--engine libwgf|pixi|three] [--libwgf <worktree>]
        [--helix <checkout>] [--model sonnet] [--hours 3] [--command CMD]

--engine names what the entrant builds with: libwgf (the default), or a JS engine in a page
to set beside it, PixiJS with PixiJS 3D (pixi) or three.js (three). Each has the same spec
and assets; a JS engine's entrant has its brief (pixi: games/wardens/trial/pixi/brief.md;
three: helix's templates/wardens/three/brief.md, unchanged), its package.json and lock
installed by `npm ci`, and helix's three.js run.cjs and index.html, which bundle the game
and run it in a headless Chromium (helix's Playwright, read-only). A pixi entrant's docs/
is PixiJS's docs for agents, mirrored from its llms.txt (https://pixijs.com/llms.txt, and
PixiJS 3D's under it): every page an index links, its links made the copies' (fetched once
into libwgf's user cache, trial-docs/). A three entrant has three.js's sources alone, as
helix's had.

A libwgf entrant's directory is made, and must not be there yet unless --command is given,
as a game `wgf new` makes (wgf.json, src/Main.hx), with:
- brief.md: games/wardens/trial/brief.md, its {DIR} the directory, {NAME} the game's name,
  and {SHARE} the staged variant's share/wgf/ (wgf.glsl and pack_shader.py, which
  wgf_shader.h names);
- spec.md and assets/: helix's docs/trials/templates/wardens/, unchanged (assets/CREDITS.md
  is helix's);
- docs/api/: the Haxe binding's API, bindings/haxe/src/wgf/*.hx (generated from the
  headers, each call with its doc comment; not impl/), with its README.md and
  docs/BINDINGS.md: what helix's entrants had as raylib's headers;
- docs/examples/: libwgf's C examples, each example's .c and .h files, as helix's had
  raylib's;
- docs/llms.txt: an index of the two for agents, a line a type and an example
  (tools/llms.py's), as PixiJS's llms.txt is its docs'.
A directory inside a git repository is refused, as the entrant would read its commits.

--libwgf names the worktree of libwgf the entrant builds against, at the commit being
tried: a checkout of the entrant's own, so nothing changes under it, its linux-x64-debug
variant built and staged (`wgf build --desktop` in any game does it). Its build/ and out/
are writable to the entrant, as `wgf build` stages the variant again, fresh, each time:
two entrants on one worktree would replace each other's libwgf.a as the other links, so
the worktree is marked with its entrant (.entrant) and refused to another. Its games/ and
docs/ are hidden, as they hold libwgf's own Wardens and its history.

The entrant runs in a bubblewrap sandbox: an empty home with the Claude Code binary, the
toolchains, and libwgf's user cache (hxcpp, sokol-shdc) read-only, and only ~/.claude (its
session), the worktree's build/ and out/, its directory, and its saves (.home/<name>
beside it) writable; ~/.claude.json copied in. /dev's devices, the user's /run sockets
(PulseAudio, D-Bus) and /tmp are hidden but for a private display's socket: Xvfb, drawn
by Mesa on the CPU, with no sound (ALSA's default device null, no PulseAudio server). The
JSON result (usage, cost, turns, duration) goes to result.json, stderr to stderr.log, the
start and end to run.log.

--command runs a shell command in the entrant's environment in place of claude, to check
the harness. Linux only (bubblewrap, Xvfb). Standard library only.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import browser  # noqa: E402
import usercache  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
TRIAL = ROOT / 'games' / 'wardens' / 'trial'
BRIEF = TRIAL / 'brief.md'
PIXI_DOCS = 'https://pixijs.com/llms.txt'
PLAYWRIGHT_BROWSERS = Path(os.environ.get('PLAYWRIGHT_BROWSERS_PATH', Path.home() / '.cache/ms-playwright'))
NAME = 'wardens'  # the game's name in wgf.json: its program is build/desktop/wardens
VARIANT_OUT = Path('out') / 'linux-x64' / 'debug'  # what `wgf build --desktop` builds against
FREE_GB = 20  # the disk an entrant's run wants free: its builds, hxcpp's, and the variant staged again
# Claude Code's own session variables, so the entrant starts as a session of its own, not a child of this one (helix's list).
CLAUDE_VARS = ('CLAUDECODE', 'CLAUDE_CODE_ENTRYPOINT', 'CLAUDE_CODE_CHILD_SESSION', 'CLAUDE_CODE_SESSION_ID', 'CLAUDE_PID',
               'CLAUDE_CODE_MESSAGING_SOCKET', 'CLAUDE_CODE_MESSAGING_TOKEN', 'CLAUDE_CODE_BRIDGE_SESSION_ID',
               'CLAUDE_CODE_SESSION_ATTENDED', 'CLAUDE_CODE_EXECPATH', 'CLAUDE_EFFORT')


def log(entrant, line):
    with open(entrant / 'run.log', 'a', encoding='utf-8') as f:
        f.write(datetime.now().astimezone().isoformat(timespec='seconds') + ' ' + line + '\n')


def links(text):
    """A Markdown text's link targets, in order."""
    out, at = [], 0
    while True:
        at = text.find('](', at)
        if at < 0:
            return out
        end = text.find(')', at)
        if end < 0:
            return out
        out.append(text[at + 2:end])
        at = end


def fetched(url, cache):
    """A page's text, from the cache, else fetched into it."""
    parts = urllib.parse.urlsplit(url)
    path = cache / parts.netloc / parts.path.lstrip('/')
    if not path.exists():
        request = urllib.request.Request(url, headers={'User-Agent': 'wgf-trial-docs'})
        for attempt in range(3):
            try:
                with urllib.request.urlopen(request, timeout=60) as response:
                    data = response.read()
                break
            except urllib.error.HTTPError:
                raise
            except OSError:
                if attempt == 2:
                    raise
                time.sleep(2)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        time.sleep(0.2)
    return path.read_text(encoding='utf-8')


def mirror_docs(index, out):
    """Docs for agents, from an llms.txt and every page it links (an llms.txt it links the
    same way), and every page in a page's folder that a page links, copied under `out` with
    their links made the copies': the first index is out/llms.txt, one under its folder
    (https://pixijs.com/3d/llms.txt) out/3d/llms.txt, and a page out/<host>/<path>. A link
    elsewhere stays as it was."""
    cache = usercache.cache_dir('trial-docs')
    root = urllib.parse.urlsplit(index)
    base = root.path.rsplit('/', 1)[0] + '/'

    def local(url):
        parts = urllib.parse.urlsplit(url)
        if parts.netloc == root.netloc and parts.path.startswith(base):
            return Path(parts.path[len(base):])
        return Path(parts.netloc) / parts.path.lstrip('/')

    pages, queue = {}, [index]
    while queue:
        url = queue.pop(0)
        if url in pages:
            continue
        try:
            pages[url] = fetched(url, cache)
        except OSError as e:  # a page a page links that isn't there: its link stays
            print(f'run_entrant: {url}: {e}', file=sys.stderr)
            continue
        folder = url.rsplit('/', 1)[0] + '/'
        for target in links(pages[url]):
            whole = urllib.parse.urljoin(url, target.split('#')[0])
            if url.endswith('llms.txt') and (whole.endswith('.md') or whole.endswith('llms.txt')):
                queue.append(whole)
            elif whole.endswith('.html') and whole.startswith(folder) and '/' not in whole[len(folder):]:
                queue.append(whole + '.md')  # a page's HTML: its Markdown beside it
            elif whole.endswith('.md') and whole.startswith(folder) and '/' not in whole[len(folder):]:
                queue.append(whole)
    for url, text in pages.items():
        here = local(url)
        for target in sorted(set(links(text)), key=len, reverse=True):
            whole, _, anchor = urllib.parse.urljoin(url, target).partition('#')
            if whole + '.md' in pages:  # a page's HTML, linked where its Markdown is here
                whole += '.md'
            if whole in pages:
                mine = os.path.relpath(local(whole), here.parent) + ('#' + anchor if anchor else '')
                text = text.replace('](' + target + ')', '](' + mine + ')')
        (out / here).parent.mkdir(parents=True, exist_ok=True)
        (out / here).write_text(text, encoding='utf-8')
    return len(pages)


def make_web(entrant, engine, helix):
    """A JS engine's entrant: its brief, its packages installed, helix's run.cjs and
    index.html, the spec and assets, and (pixi) PixiJS's docs."""
    wardens = helix / 'docs' / 'trials' / 'templates' / 'wardens'
    own = TRIAL / 'pixi' if engine == 'pixi' else wardens / 'three'
    entrant.mkdir(parents=True)
    brief = (own / 'brief.md').read_text(encoding='utf-8')
    (entrant / 'brief.md').write_text(brief.replace('{DIR}', str(entrant)), encoding='utf-8')
    for name in ('package.json', 'package-lock.json'):
        shutil.copy(own / name, entrant / name)
    for name in ('run.cjs', 'index.html'):
        shutil.copy(wardens / 'three' / name, entrant / name)
    (entrant / 'src').mkdir()
    subprocess.run(['npm', 'ci', '--no-audit', '--no-fund'], cwd=entrant, check=True, stdout=subprocess.DEVNULL)
    shutil.copy(wardens / 'spec.md', entrant / 'spec.md')
    shutil.copytree(wardens / 'assets', entrant / 'assets')
    if engine == 'pixi':
        print(f'run_entrant: {mirror_docs(PIXI_DOCS, entrant / "docs")} pages of PixiJS\'s docs in {entrant / "docs"}')


def make(entrant, libwgf, helix):
    """The entrant's directory: a new game, helix's spec and assets, the brief, the docs."""
    import headers
    import llms
    wardens = helix / 'docs' / 'trials' / 'templates' / 'wardens'
    subprocess.run([sys.executable, str(libwgf / 'wgf'), 'new', str(entrant), '--name', NAME], check=True,
                   stdout=subprocess.DEVNULL)
    shutil.rmtree(entrant / 'assets')
    shutil.copytree(wardens / 'assets', entrant / 'assets')
    shutil.copy(wardens / 'spec.md', entrant / 'spec.md')
    brief = BRIEF.read_text(encoding='utf-8')
    for key, value in (('{DIR}', entrant), ('{NAME}', NAME), ('{SHARE}', libwgf / VARIANT_OUT / 'share' / 'wgf')):
        brief = brief.replace(key, str(value))
    (entrant / 'brief.md').write_text(brief, encoding='utf-8')
    api = entrant / 'docs' / 'api'
    api.mkdir(parents=True)
    for source in sorted((libwgf / 'bindings' / 'haxe' / 'src' / 'wgf').glob('*.hx')):
        shutil.copy(source, api / source.name)
    shutil.copy(libwgf / 'bindings' / 'haxe' / 'README.md', api / 'README.md')
    shutil.copy(libwgf / 'docs' / 'BINDINGS.md', api / 'BINDINGS.md')
    for example in sorted(p for p in (libwgf / 'examples' / 'c').iterdir() if p.is_dir()):
        sources = sorted(p for p in example.iterdir() if p.suffix in ('.c', '.h'))
        if sources:
            (entrant / 'docs' / 'examples' / example.name).mkdir(parents=True)
            for source in sources:
                shutil.copy(source, entrant / 'docs' / 'examples' / example.name / source.name)
    api_read = headers.read(libwgf, None, None)
    if api_read is None:
        sys.exit('run_entrant: no clang (Emscripten brings one) to read the headers for docs/llms.txt')
    (entrant / 'docs' / 'llms.txt').write_text(
        llms.index(api_read, lambda name: f'api/{name}.hx',
                             lambda name: f'examples/{name}/main.c', libwgf), encoding='utf-8')


def environment(entrant, libwgf, display):
    """The entrant's environment: the private display's, with no sound, D-Bus, or parent
    session, `wgf` on its PATH, and saves and a cache of its own (libwgf's user cache
    linked in, read-only)."""
    env = dict(os.environ, DISPLAY=display, LIBGL_ALWAYS_SOFTWARE='1', XDG_SESSION_TYPE='x11', HXCPP_COMPILE_THREADS='4')
    for k in ('XAUTHORITY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS', 'LIBWGF_HAXELIB', 'HAXELIB_PATH') + CLAUDE_VARS:
        env.pop(k, None)
    env['PATH'] = f'{libwgf}{os.pathsep}{env.get("PATH", "")}'
    home = entrant.parent / '.home' / entrant.name
    env.update(XDG_DATA_HOME=str(home / 'data'), XDG_CACHE_HOME=str(home / 'cache'))
    (home / 'data').mkdir(parents=True, exist_ok=True)
    (home / 'cache').mkdir(parents=True, exist_ok=True)
    cache = home / 'cache' / 'libwgf'
    if not cache.exists():
        cache.symlink_to(Path.home() / '.cache' / 'libwgf')
    alsa = home / 'alsa.conf'
    alsa.write_text('pcm.!default { type null }\n', encoding='utf-8')
    env.update(PULSE_SERVER='unix:/nonexistent', ALSA_CONFIG_PATH=str(alsa))
    return env


def playwright(helix):
    """Playwright's package, for a JS engine's run.cjs: HELIX_PLAYWRIGHT's, else helix's tools'."""
    return Path(os.environ.get('HELIX_PLAYWRIGHT', helix / 'build/tools/node_modules/playwright'))


def web_environment(entrant, helix, display):
    """A JS engine's entrant's environment: the libwgf entrant's, less libwgf, with
    Playwright and its browsers for run.cjs."""
    env = environment(entrant, ROOT, display)
    env['PATH'] = env['PATH'].split(os.pathsep, 1)[1]
    env.pop('NODE_EXTRA_CA_CERTS', None)  # outside the sandbox's home
    env.update(HELIX_PLAYWRIGHT=str(playwright(helix)), PLAYWRIGHT_BROWSERS_PATH=str(PLAYWRIGHT_BROWSERS))
    return env


def web_sandbox(entrant, helix, claude_json):
    """bubblewrap's arguments for a JS engine's entrant: the libwgf entrant's walls, with
    Playwright and its browsers read-only, and no libwgf."""
    home = Path.home()
    args = ['bwrap', '--ro-bind', '/', '/', '--dev', '/dev', '--proc', '/proc', '--tmpfs', '/tmp', '--ro-bind', '/tmp/.X11-unix',
            '/tmp/.X11-unix', '--tmpfs', f'/run/user/{os.getuid()}', '--tmpfs', str(home)]
    for path in (home / '.local/bin', home / '.local/share/claude', home / 'toolchains', PLAYWRIGHT_BROWSERS,
                 playwright(helix).parent):
        if path.exists():
            args += ['--ro-bind', str(path), str(path)]
    for path in (home / '.claude', entrant, entrant.parent / '.home' / entrant.name):
        args += ['--bind', str(path), str(path)]
    return args + ['--file', str(claude_json.fileno()), str(home / '.claude.json'), '--unshare-pid', '--die-with-parent',
                   '--chdir', str(entrant), '--']


def sandbox(entrant, libwgf, claude_json):
    """bubblewrap's arguments for the entrant's sandbox; claude_json is ~/.claude.json open
    for reading, copied into its home."""
    home = Path.home()
    args = ['bwrap', '--ro-bind', '/', '/', '--dev', '/dev', '--proc', '/proc', '--tmpfs', '/tmp', '--ro-bind', '/tmp/.X11-unix',
            '/tmp/.X11-unix', '--tmpfs', f'/run/user/{os.getuid()}', '--tmpfs', str(home)]
    for path in (home / '.local/bin', home / '.local/share/claude', home / 'toolchains', home / '.cache/libwgf', libwgf):
        if path.exists():
            args += ['--ro-bind', str(path), str(path)]
    for path in (home / '.claude', libwgf / 'build', libwgf / 'out', entrant, entrant.parent / '.home' / entrant.name):
        args += ['--bind', str(path), str(path)]
    for path in (libwgf / 'games', libwgf / 'docs'):
        args += ['--tmpfs', str(path)]
    return args + ['--file', str(claude_json.fileno()), str(home / '.claude.json'), '--unshare-pid', '--die-with-parent',
                   '--chdir', str(entrant), '--']


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('entrant', type=Path)
    parser.add_argument('--engine', choices=('libwgf', 'pixi', 'three'), default='libwgf')
    parser.add_argument('--libwgf', type=Path, help='the worktree of libwgf being tried (--engine libwgf)')
    parser.add_argument('--helix', type=Path, default=ROOT.parent / 'helix', help="helix's checkout (its Wardens template)")
    parser.add_argument('--model', default='sonnet')
    parser.add_argument('--hours', type=float, default=3)
    parser.add_argument('--command')
    args = parser.parse_args()
    entrant, helix = args.entrant.resolve(), args.helix.resolve()
    if not (helix / 'docs/trials/templates/wardens/spec.md').exists():
        sys.exit(f"run_entrant: {helix} has no docs/trials/templates/wardens/spec.md (helix's checkout: --helix)")
    if args.engine != 'libwgf':
        return run_web(entrant, args, helix)
    if args.libwgf is None:
        sys.exit('run_entrant: --libwgf names the worktree a libwgf entrant builds against')
    libwgf = args.libwgf.resolve()
    if not (libwgf / VARIANT_OUT / 'lib').is_dir():
        sys.exit(f'run_entrant: {libwgf} has no staged {VARIANT_OUT.as_posix()}: run `wgf build --desktop` in a game there first')
    if not shutil.which('bwrap') or not browser.find_xvfb():
        sys.exit('run_entrant: needs bubblewrap (bwrap) and Xvfb')
    free = shutil.disk_usage(libwgf).free
    if free < FREE_GB * 2**30:
        sys.exit(f'run_entrant: {free / 2**30:.1f} GB free; an entrant wants {FREE_GB} (a full disk fails its builds as if its code did)')
    mark = libwgf / '.entrant'
    if mark.exists() and mark.read_text(encoding='utf-8').strip() != str(entrant):
        sys.exit(f'run_entrant: {libwgf} is {mark.read_text(encoding="utf-8").strip()}\'s; each entrant builds against a worktree of its own')
    mark.write_text(str(entrant) + '\n', encoding='utf-8')
    probe = entrant if entrant.exists() else entrant.parent
    probe.mkdir(parents=True, exist_ok=True)
    if subprocess.run(['git', '-C', str(probe), 'rev-parse'], capture_output=True).returncode == 0:
        sys.exit(f'run_entrant: {entrant} is inside a git repository, whose commit messages the entrant would read')
    if not entrant.exists():
        make(entrant, libwgf, helix)
    elif not args.command:
        sys.exit(f'run_entrant: {entrant} is there already; an entrant starts from a directory of its own')

    return start(entrant, args, lambda display: environment(entrant, libwgf, display),
                 lambda claude_json: sandbox(entrant, libwgf, claude_json))


def run_web(entrant, args, helix):
    """A JS engine's entrant: made, then run as a libwgf one is."""
    if not (playwright(helix) / 'package.json').exists():
        sys.exit(f"run_entrant: no Playwright at {playwright(helix)} (helix's build/tools, or HELIX_PLAYWRIGHT)")
    if not shutil.which('bwrap') or not browser.find_xvfb():
        sys.exit('run_entrant: needs bubblewrap (bwrap) and Xvfb')
    probe = entrant if entrant.exists() else entrant.parent
    probe.mkdir(parents=True, exist_ok=True)
    if subprocess.run(['git', '-C', str(probe), 'rev-parse'], capture_output=True).returncode == 0:
        sys.exit(f'run_entrant: {entrant} is inside a git repository, whose commit messages the entrant would read')
    if not entrant.exists():
        make_web(entrant, args.engine, helix)
    elif not args.command:
        sys.exit(f'run_entrant: {entrant} is there already; an entrant starts from a directory of its own')
    return start(entrant, args, lambda display: web_environment(entrant, helix, display),
                 lambda claude_json: web_sandbox(entrant, helix, claude_json))


def start(entrant, args, make_env, make_wrap):
    """The entrant's session, on a private display, in its sandbox, with its deadline."""
    run = browser.RunProcesses('entrant')
    run.install_handlers()
    try:
        env = make_env(browser.start_xvfb(run))
        with open(Path.home() / '.claude.json', 'rb') as claude_json:
            wrap = make_wrap(claude_json)
            fds = (claude_json.fileno(),)
            if args.command:
                return subprocess.run(wrap + ['bash', '-c', args.command], env=env, pass_fds=fds).returncode
            log(entrant, 'start')
            brief = (entrant / 'brief.md').read_text(encoding='utf-8')
            command = ['claude', '-p', brief, '--model', args.model, '--output-format', 'json',
                       '--allowedTools', 'Bash,Read,Write,Edit,Glob,Grep']
            with open(entrant / 'result.json', 'w') as out, open(entrant / 'stderr.log', 'w') as err:
                try:
                    code = subprocess.run(wrap + command, env=env, stdout=out, stderr=err, pass_fds=fds,
                                          timeout=args.hours * 3600).returncode
                except subprocess.TimeoutExpired:
                    code = 'timeout'
            log(entrant, f'end exit {code}')
            return 0
    finally:
        run.stop()


if __name__ == '__main__':
    sys.exit(main())
