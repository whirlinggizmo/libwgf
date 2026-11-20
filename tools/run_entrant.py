#!/usr/bin/env python3
"""Run one entrant of helix's Wardens trial on libwgf: a fresh `claude -p` building the game
from helix's spec alone, in Haxe on libwgf's binding, under the conditions helix's entrants
had (../helix's docs/trials/README.md, "Running one"; its tools/run_entrant.py), so its
cost and its grade read beside theirs.

    tools/run_entrant.py <entrant directory> --libwgf <worktree> [--helix <checkout>]
        [--model sonnet] [--hours 3] [--command CMD]

The entrant directory is made, and must not be there yet unless --command is given, as a
game `wgf new` makes (wgf.json, src/Main.hx), with:
- brief.md: games/wardens/trial/brief.md, its {DIR} the directory, {NAME} the game's name,
  and {SHARE} the staged variant's share/wgf/ (wgf.glsl and pack_shader.py, which
  wgf_shader.h names);
- spec.md and assets/: helix's docs/trials/templates/wardens/, unchanged (assets/CREDITS.md
  is helix's);
- docs/api/: the Haxe binding's API, bindings/haxe/src/wgf/*.hx (generated from the
  headers, each call with its doc comment; not impl/), with its README.md and
  docs/BINDINGS.md: what helix's entrants had as raylib's headers;
- docs/examples/: libwgf's C examples, each example's .c and .h files, as helix's had
  raylib's.
A directory inside a git repository is refused, as the entrant would read its commits.

--libwgf names the worktree of libwgf the entrant builds against, at the commit being
tried: a checkout of its own, so nothing changes under the entrant, its linux-x64-debug
variant built and staged (`wgf build --desktop` in any game does it). Its build/ and out/
are writable to the entrant, as `wgf build` stages the variant again (a no-op when
current); its games/ and docs/ are hidden, as they hold libwgf's own Wardens and its
history.

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
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import browser  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BRIEF = ROOT / 'games' / 'wardens' / 'trial' / 'brief.md'
NAME = 'wardens'  # the game's name in wgf.json: its program is build/desktop/wardens
VARIANT_OUT = Path('out') / 'linux-x64' / 'debug'  # what `wgf build --desktop` builds against
# Claude Code's own session variables, so the entrant starts as a session of its own, not a child of this one (helix's list).
CLAUDE_VARS = ('CLAUDECODE', 'CLAUDE_CODE_ENTRYPOINT', 'CLAUDE_CODE_CHILD_SESSION', 'CLAUDE_CODE_SESSION_ID', 'CLAUDE_PID',
               'CLAUDE_CODE_MESSAGING_SOCKET', 'CLAUDE_CODE_MESSAGING_TOKEN', 'CLAUDE_CODE_BRIDGE_SESSION_ID',
               'CLAUDE_CODE_SESSION_ATTENDED', 'CLAUDE_CODE_EXECPATH', 'CLAUDE_EFFORT')


def log(entrant, line):
    with open(entrant / 'run.log', 'a', encoding='utf-8') as f:
        f.write(datetime.now().astimezone().isoformat(timespec='seconds') + ' ' + line + '\n')


def make(entrant, libwgf, helix):
    """The entrant's directory: a new game, helix's spec and assets, the brief, the docs."""
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
    parser.add_argument('--libwgf', type=Path, required=True, help='the worktree of libwgf being tried')
    parser.add_argument('--helix', type=Path, default=ROOT.parent / 'helix', help="helix's checkout (its Wardens template)")
    parser.add_argument('--model', default='sonnet')
    parser.add_argument('--hours', type=float, default=3)
    parser.add_argument('--command')
    args = parser.parse_args()
    entrant, libwgf, helix = args.entrant.resolve(), args.libwgf.resolve(), args.helix.resolve()
    if not (libwgf / VARIANT_OUT / 'lib').is_dir():
        sys.exit(f'run_entrant: {libwgf} has no staged {VARIANT_OUT.as_posix()}: run `wgf build --desktop` in a game there first')
    if not (helix / 'docs/trials/templates/wardens/spec.md').exists():
        sys.exit(f"run_entrant: {helix} has no docs/trials/templates/wardens/spec.md (helix's checkout: --helix)")
    if not shutil.which('bwrap') or not browser.find_xvfb():
        sys.exit('run_entrant: needs bubblewrap (bwrap) and Xvfb')
    probe = entrant if entrant.exists() else entrant.parent
    probe.mkdir(parents=True, exist_ok=True)
    if subprocess.run(['git', '-C', str(probe), 'rev-parse'], capture_output=True).returncode == 0:
        sys.exit(f'run_entrant: {entrant} is inside a git repository, whose commit messages the entrant would read')
    if not entrant.exists():
        make(entrant, libwgf, helix)
    elif not args.command:
        sys.exit(f'run_entrant: {entrant} is there already; an entrant starts from a directory of its own')

    run = browser.RunProcesses('entrant')
    run.install_handlers()
    try:
        env = environment(entrant, libwgf, browser.start_xvfb(run))
        with open(Path.home() / '.claude.json', 'rb') as claude_json:
            wrap = sandbox(entrant, libwgf, claude_json)
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
