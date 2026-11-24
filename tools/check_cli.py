#!/usr/bin/env python3
"""Check the wgf tool: each of its commands, on a game it makes from the template.

    tools/check_cli.py [--only STEP[,STEP...]] [--keep]

In a scratch directory (under this machine's headless build), `wgf new` makes a game,
then each command is run on it as a developer would, and judged by what it made and
what it said:
  new         the game's files, its name in them; a second new into it refused
  build       --headless and --web: the program, its page, its host, its assets beside it
  run         --headless --frames 30, and --autopilot: runs that end at their frame, passing
  autopilot   the game's smoke autopilot, headless and --web: PASS; a failing one: FAIL,
              and why; one named without its folder found in the game's autopilot
              folder, and a missing one named, not a traceback;
              --web saving an autopilot's two named screenshots
  dump        the ship, as a scene's text; flown by --autopilot to its end, turned on
  screenshot  a PNG of frame 30, from a browser, and one flown there by --autopilot
  serve       the page reached in a browser, its Haxe edited while it runs: the new code
              runs, with the frame count it had (state kept, not a restart); then a
              texture, a glTF, a sound, two fonts, an image, a cubemap, and a shader
              saved changed:
              the page shows the new ones, its count going on; then each saved broken:
              the old ones kept, one error logged for each;
              then a file nothing was made from saved: the page loads again
  export      export/web (a trimmed host, under the budget, its assets' manifests
              written, none in the game's) and export/desktop, each flown by the
              autopilot --autopilot names (the smoke one, here, by its name alone)
A step that can't run here (no haxe, hxcpp, Emscripten, or browser) says
`check_cli: SKIPPING <step> (<why>)`, and the last line repeats every skip. Standard
library only.
"""
import argparse
import json
import os
import re
import struct
import shutil
import subprocess
import sys
import threading
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import binding  # noqa: E402
import browser  # noqa: E402
import variants  # noqa: E402
import webhost  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
WGF = ROOT / 'wgf'
STEPS = ('new', 'build', 'run', 'autopilot', 'dump', 'screenshot', 'serve', 'export')


def wgf(game_dir, *args, timeout=900):
    """Run `wgf <args>` in `game_dir`: (exit code, its output)."""
    done = subprocess.run([sys.executable, str(WGF), *args], cwd=game_dir, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, errors='replace', timeout=timeout)
    return done.returncode, done.stdout


def problem(what, output=''):
    print(f'check_cli: {what}')
    if output:
        print('\n'.join(output.strip().splitlines()[-25:]))
    return False


def needs(web=False, native=False, browser_too=False):
    """Why a step can't run here, or None."""
    if binding.haxe() is None:
        return 'no haxe on PATH'
    if native and not binding.has_hxcpp():
        return f'no hxcpp: {binding.hxcpp.ensure()}'
    if web:
        try:
            webhost.emcc()
        except RuntimeError as e:
            return str(e)
    if browser_too:
        try:
            browser.find_browser()
        except RuntimeError as e:
            return str(e)
    return None


def step_new(base, game):
    code, out = wgf(base, 'new', str(game), '--name', 'clitest')
    if code != 0:
        return problem('wgf new failed', out)
    made = out
    data = json.loads((game / 'wgf.json').read_text())
    if data.get('name') != 'clitest' or 'clitest' not in (game / 'src' / 'Main.hx').read_text():
        return problem('wgf new: the name isn\'t in wgf.json and Main.hx')
    code, out = wgf(base, 'new', str(game))
    if code == 0:
        return problem('wgf new into a game\'s directory wasn\'t refused', out)
    said = [line for line in made.splitlines() if 'serve` runs it' in line]
    if not said or not (f'{WGF} serve' in said[0] or ' wgf serve' in said[0] or f'{WGF}.cmd serve' in said[0]):
        return problem('wgf new: its message doesn\'t name a wgf that runs from the game', made)
    shells = ''
    if os.name == 'nt':  # Windows' launcher, as a developer types it: usage through, an exit code back
        cmd = str(WGF) + '.cmd'
        for shell in ('cmd', 'PowerShell'):
            for args, wanted in ((['--help'], 0), (['no-such-command'], 2)):
                run_it = (['cmd', '/c', cmd, *args] if shell == 'cmd' else  # PowerShell: the call in its own text
                          ['powershell', '-NoProfile', '-Command', f"& '{cmd}' {' '.join(args)}; exit $LASTEXITCODE"])
                done = subprocess.run(run_it, cwd=game, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                      text=True, errors='replace', timeout=120)
                if done.returncode != wanted or (wanted == 0 and 'usage: wgf' not in done.stdout):
                    return problem(f'wgf.cmd in {shell}: {" ".join(args)} exited {done.returncode}, not {wanted}',
                                   done.stdout)
        shells = '; wgf.cmd from cmd and PowerShell, its exit codes kept'
    print(f'check_cli: new: made, named, its message naming the wgf to run; and a second new into it refused{shells}')
    return True


def step_build(game):
    """Each target this machine can build: headless with hxcpp, the web with Emscripten."""
    native, web = needs(native=True), needs(web=True)
    if native and web:
        return f'{native}; {web}'
    done = []
    if not native:
        code, out = wgf(game, 'build', '--headless')
        exe = game / 'build' / 'headless' / ('clitest.exe' if sys.platform == 'win32' else 'clitest')
        if code != 0 or not exe.exists() or not (exe.parent / 'assets').exists():
            return problem('wgf build --headless: no program, or no assets beside it', out)
        done.append('headless')
    if not web:
        code, out = wgf(game, 'build', '--web')
        site = game / 'build' / 'web'
        missing = [n for n in ('index.html', 'clitest.js', 'wgf-host.js', 'wgf-host.wasm', 'assets')
                   if not (site / n).exists()]
        if code != 0 or missing:
            return problem(f'wgf build --web: missing {missing}', out)
        done.append('web')
    skipped = '; '.join(f'{t} skipped ({why})' for t, why in (('headless', native), ('web', web)) if why)
    print(f'check_cli: build: {" and ".join(done)} built, each with its assets beside it'
          + (f'; {skipped}' if skipped else ''))
    return True


def step_run(game):
    why = needs(native=True)
    if why:
        return why
    code, out = wgf(game, 'run', '--headless', '--frames', '30', '--no-build')
    if code != 0 or 'wgf_autopilot: PASS (0 of 0 expectations failed, 30 frames)' not in out:
        return problem('wgf run --headless --frames 30: not a passing 30-frame run', out)
    fly = game / 'autopilot' / 'fly.autopilot'
    fly.write_text('wgf-autopilot 1\nat 40 expect frames >= 40\nat 90 end\n')
    code, out = wgf(game, 'run', '--headless', '--autopilot', 'autopilot/fly.autopilot', '--no-build')
    if code != 0 or 'wgf_autopilot: PASS (0 of 1 expectations failed, 91 frames)' not in out:
        return problem('wgf run --headless --autopilot: not the file\'s passing 91-frame run', out)
    print('check_cli: run: 30 frames, and an autopilot file\'s 91, headless, passed')
    return True


def step_autopilot(game):
    """The smoke autopilot headless (and a failing one), and in a browser: each where it can."""
    native, web = needs(native=True), needs(web=True, browser_too=True)
    if native and web:
        return f'{native}; {web}'
    said = []
    if not native:
        code, out = wgf(game, 'autopilot', 'autopilot/smoke.autopilot', '--no-build')
        if code != 0 or 'autopilot autopilot/smoke.autopilot: PASS' not in out:
            return problem('wgf autopilot: the smoke autopilot didn\'t pass headless', out)
        failing = game / 'autopilot' / 'failing.autopilot'
        failing.write_text('wgf-autopilot 1\nat 10 expect frames > 1000\nat 10 end\n')
        code, out = wgf(game, 'autopilot', 'autopilot/failing.autopilot', '--no-build')
        if code == 0 or 'FAIL' not in out:
            return problem('wgf autopilot: a failing one passed', out)
        if not any(line.startswith('wgf: autopilot') and 'frames' in line and 'FAIL' not in line[-6:]
                   for line in out.splitlines()):
            return problem('wgf autopilot: a failing one didn\'t say why under its FAIL', out)
        verdict = [line for line in out.splitlines() if line.startswith('wgf: autopilot')][-1]
        if ': FAIL: ' not in verdict or 'frames' not in verdict:
            return problem('wgf autopilot: a failing one\'s last line didn\'t keep its reason with its FAIL', out)
        said.append('the smoke autopilot passed headless, a failing one failed, saying why on its FAIL line')
        code, out = wgf(game, 'autopilot', 'smoke.autopilot', '--no-build')
        if code != 0 or 'autopilot smoke.autopilot: PASS' not in out:
            return problem('wgf autopilot: one named without its folder wasn\'t found in the game\'s autopilot folder', out)
        code, out = wgf(game, 'autopilot', 'nowhere.autopilot', '--no-build')
        if code == 0 or 'no autopilot file nowhere.autopilot' not in out or 'Traceback' in out:
            return problem('wgf autopilot: a missing file wasn\'t named as missing', out)
        said.append('one found by its name alone, a missing one named')
    if not web:
        code, out = wgf(game, 'autopilot', 'autopilot/smoke.autopilot', '--web', '--no-build')
        if code != 0 or 'PASS' not in out:
            return problem('wgf autopilot --web: the smoke autopilot didn\'t pass in a browser', out)
        said.append('the smoke autopilot passed in a browser')
        shots = game / 'autopilot' / 'shots.autopilot'
        shots.write_text('wgf-autopilot 1\nat 20 screenshot first\nat 40 screenshot second\nat 60 end\n')
        folder = game / 'build' / 'check-shots'
        if folder.exists():
            shutil.rmtree(folder)
        code, out = wgf(game, 'autopilot', 'autopilot/shots.autopilot', '--web', '--no-build', '--screenshots',
                        str(folder))
        missing = [n for n in ('first.png', 'second.png') if not (folder / n).exists()]
        if code != 0 or missing:
            return problem(f'wgf autopilot --web --screenshots: {", ".join(missing) or "the run failed"}', out)
        said.append('its two named screenshots saved')
    skipped = '; '.join(f'{t} skipped ({why})' for t, why in (('headless', native), ('web', web)) if why)
    print(f'check_cli: autopilot: {"; ".join(said)}' + (f'; {skipped}' if skipped else ''))
    return True


def step_dump(game):
    why = needs(native=True)
    if why:
        return why
    code, out = wgf(game, 'dump', '--frame', '20', '--no-build')
    if code != 0 or not out.startswith('wgf-scene 2') or 'actor "ship"' not in out or 'motion ' not in out:
        return problem('wgf dump: not the ship as a scene', out)
    early = turned(out)
    fly = game / 'autopilot' / 'fly.autopilot'
    fly.write_text('wgf-autopilot 1\nat 40 expect frames >= 40\nat 90 end\n')
    code, out = wgf(game, 'dump', '--autopilot', 'autopilot/fly.autopilot', '--no-build')
    late = turned(out) if code == 0 else None
    if late is None or early is None or not late > early:
        return problem(f'wgf dump --autopilot: not the ship turned on to frame 90 ({early} at 20, {late} after)', out)
    print(f'check_cli: dump: the ship, as a scene\'s text; flown on by an autopilot, turned {early:.2f} -> {late:.2f}')
    return True


def turned(dump):
    """The ship's turn in a dump, radians: its transform's rotation z."""
    for line in dump.splitlines():
        if 'transform ' in line and 'rotation=' in line:
            return float(line.split('rotation=')[1].split()[0].split(',')[2])
    return None


def step_screenshot(game):
    why = needs(web=True, browser_too=True)
    if why:
        return why
    shot = game / 'build' / 'shot.png'
    code, out = wgf(game, 'screenshot', '--frame', '30', '--out', str(shot), '--no-build')
    if code != 0 or not shot.exists() or shot.read_bytes()[:8] != b'\x89PNG\r\n\x1a\n':
        return problem('wgf screenshot: no PNG', out)
    flown = game / 'build' / 'flown.png'
    fly = game / 'autopilot' / 'fly.autopilot'
    fly.write_text('wgf-autopilot 1\nat 40 expect frames >= 40\nat 90 end\n')
    code, out = wgf(game, 'screenshot', '--autopilot', 'autopilot/fly.autopilot', '--out', str(flown), '--no-build')
    if code != 0 or not flown.exists() or 'frame 90 saved' not in out:
        return problem('wgf screenshot --autopilot: no PNG at the autopilot\'s end (frame 90)', out)
    broken = game / 'autopilot' / 'broken.autopilot'  # an expectation failed on the way: the run goes on
    broken.write_text('wgf-autopilot 1\nat 10 expect frames > 1000\nat 90 end\n')
    flown.unlink()
    code, out = wgf(game, 'screenshot', '--autopilot', 'autopilot/broken.autopilot', '--out', str(flown), '--no-build')
    if not flown.exists() or 'frame 90 saved' not in out:
        return problem('wgf screenshot --autopilot: a failed expectation on the way stopped the run before frame 90', out)
    own = game / 'autopilot' / 'own.autopilot'  # its own screenshot line, earlier: --frame must win
    own.write_text('wgf-autopilot 1\nat 20 screenshot early\nat 40 expect frames >= 40\nat 90 end\n')
    code, out = wgf(game, 'screenshot', '--autopilot', 'autopilot/own.autopilot', '--frame', '60', '--out',
                    str(flown), '--no-build')
    if code != 0 or 'frame 60 saved' not in out or 'left out' not in out:
        return problem('wgf screenshot --frame: the autopilot\'s own screenshot line won, or wasn\'t said', out)
    code, out = wgf(game, 'screenshot', '--frame', '100000', '--timeout', '3', '--out', str(flown), '--no-build')
    if code == 0 or 'within 3 s' not in out or '--timeout' not in out:
        return problem('wgf screenshot --timeout: a frame past the time given wasn\'t said to have run out of time', out)
    print(f'check_cli: screenshot: frame 30, {shot.stat().st_size} bytes of PNG; frame 90, flown there, a failed '
          'expectation on the way or not; frame 60 over the autopilot\'s own screenshot line, said; and a frame past '
          '--timeout said to have run out of time')
    return True


def png(size, height=None):
    """A PNG of `size` by `size` red pixels, or `size` wide and `height` high."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)
    height = size if height is None else height
    rows = b''.join(b'\x00' + b'\xff\x00\x00\xff' * size for _ in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def wav(milliseconds):
    """A WAV of a quiet tone, mono, 16 bits at 8 kHz, `milliseconds` long."""
    frames = 8 * milliseconds
    data = b''.join(struct.pack('<h', 2000 if (i // 8) % 2 else -2000) for i in range(frames))
    return (b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
            struct.pack('<IHHIIHH', 16, 1, 1, 8000, 16000, 2, 16) + b'data' + struct.pack('<I', len(data)) + data)


def font_file(which):
    """One of the examples' two typefaces' bytes: 'mono', or 'komika', which is wider."""
    name = 'JetBrainsMono/JetBrainsMono-Regular.ttf' if which == 'mono' else 'Komika/KOMIKAH_.ttf'
    return (ROOT / 'examples' / 'assets' / 'fonts' / name).read_bytes()


def car_gltf(x):
    """A glTF of one triangle, its node "body" at `x`, its buffer inside it."""
    return json.dumps({
        'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
        'nodes': [{'name': 'body', 'mesh': 0, 'translation': [x, 0, 0]}],
        'meshes': [{'primitives': [{'attributes': {'POSITION': 0}}]}],
        'buffers': [{'uri': 'data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA',
                     'byteLength': 36}],
        'bufferViews': [{'buffer': 0, 'byteLength': 36}],
        'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': 3, 'type': 'VEC3',
                       'min': [0, 0, 0], 'max': [1, 1, 0]}]})


def served_assets():
    """What the serve step's game loads, one of each kind a page takes on differently from a
    desktop build (the browser decodes it, or the web has code of its own for it). Each: the
    word its value follows in the page's log, its file, the Haxe that declares, makes, and
    says it, its file's bytes first, saved changed, and saved broken (None: never broken),
    and whether a value is the first file's, or (given the first's) the changed one's."""
    shaders = ROOT / 'examples' / 'assets' / 'shaders'

    def fnt(line):
        return (f'common lineHeight={line} base=8 scaleW=8 scaleH=8 pages=1 packed=0\n'
                'page id=0 file="tiny.png"\n'
                'char id=65 x=0 y=0 width=5 height=7 xoffset=0 yoffset=1 xadvance=6 page=0 chnl=15\n').encode()

    def asset(word, file, declare, make, say, first, changed, broken, was, now):
        return {'word': word, 'file': file, 'declare': declare, 'make': make, 'say': say, 'first': first,
                'changed': changed, 'broken': broken, 'was': was, 'now': now}

    return [
        asset('rock', 'rock.png', 'rock:Texture', 'rock = Texture.create("rock.png");', 'rock.getWidth()',
              png(2), png(4), b'not an image any more', lambda v: v == 2, lambda v, first: v == 4),
        asset('body', 'car.gltf', 'car:Model', 'car = Model.create(Mesh.create("car.gltf"));',
              'Std.int(car.find("body").getPosition().x)', car_gltf(1).encode(), car_gltf(3).encode(), None,
              lambda v: v == 1, lambda v, first: v == 3),
        # a sound's length in milliseconds
        asset('beep', 'beep.wav', 'beep:Sound', 'beep = Sound.create("beep.wav");',
              'Std.int(beep.getDuration() * 1000)', wav(100), wav(300), b'not a sound any more',
              lambda v: 50 < v < 200, lambda v, first: v > 200),
        # a line's width in a TrueType font: another typeface's is another width
        asset('words', 'face.ttf', 'face:Font', 'face = Font.create("face.ttf");',
              'Std.int(face.measure("Hello, world", 40).x)', font_file('mono'), font_file('komika'),
              b'not a font any more', lambda v: v > 0, lambda v, first: v > 0 and v != first),
        asset('pic', 'pic.png', 'pic:Image', 'pic = Image.create("pic.png");', 'pic.getWidth()',
              png(2), png(6), b'not an image any more', lambda v: v == 2, lambda v, first: v == 6),
        # a bitmap font's line height; its page is tiny.png
        asset('line', 'tiny.fnt', 'tiny:Font', 'Font.enableBitmaps();\n\t\ttiny = Font.create("tiny.fnt");',
              'Std.int(tiny.getSize())', fnt(10), fnt(12), b'not a font any more',
              lambda v: v == 10, lambda v, first: v == 12),
        # a cubemap of one image, a row of six faces: a face's size
        asset('sky', 'sky.png', 'sky:Cubemap', 'sky = Cubemap.create("sky.png", CubemapLayout.AUTO);',
              'sky.getSize()', png(12, 2), png(24, 4), b'not an image any more',
              lambda v: v == 2, lambda v, first: v == 4),
        # a custom shader: whether it has the parameter only the changed one has
        asset('speed', 'shaders/look.wgfshader', 'look:Material',
              'look = Material.createCustom(Shader.create("shaders/look.wgfshader"));',
              '(look.setFloat("speed", 1.0) ? 1 : 0)', (shaders / 'toon.wgfshader').read_bytes(),
              (shaders / 'dissolve.wgfshader').read_bytes(), b'not a shader any more',
              lambda v: v == 0, lambda v, first: v == 1),
    ]


def write_assets(game, assets, which):
    """Each asset's `which` bytes ('first', 'changed', 'broken') saved over its file."""
    for a in assets:
        if a[which] is not None:
            path = game / 'assets' / a['file']
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(a[which])


def step_serve(game):
    """The page in a browser, its program with an initialized instance final and one asset of
    each kind served_assets lists (a texture, a glTF, a sound, a TrueType font, an image, a
    bitmap font, a cubemap, a custom shader), served twice. As `wgf serve` alone: Main.hx edited while
    it runs, and the page loads again with the new code's line, its frame count from the
    start; then every asset saved changed, and it loads again with them. Then as
    `wgf serve --hot`, where the machine has hotreload-hx (said as skipped where it hasn't):
    Main.hx edited, the new code's line with the frame count going on from where it was;
    every asset saved changed, each one's new value in its log, the count going on; each
    saved broken, the old ones kept, and one error logged for each; then a file nothing
    was made from saved, and the page loads again (its count from the start)."""
    why = needs(web=True, browser_too=True)
    if why:
        return why
    assets = served_assets()
    # a class with an initialized instance final, which a hot build once refused
    (game / 'src' / 'Held.hx').write_text('class Held {\n\tpublic final count = [0];\n\n\tpublic function new() {}\n}\n')
    write_assets(game, assets, 'first')
    (game / 'assets' / 'tiny.png').write_bytes(png(8))  # the bitmap font's page
    main = game / 'src' / 'Main.hx'
    declared = ''.join(f'\tstatic var {a["declare"]} = 0;\n' for a in assets)
    made = ''.join(f'\t\t{a["make"]}\n' for a in assets)
    said = ', '.join(f'{a["word"]} ${{{a["say"]}}}' for a in assets)
    main.write_text(main.read_text()
                    .replace('\tstatic var flips = 0;\n',
                             '\tstatic var flips = 0;\n\tstatic final held = new Held();\n' + declared)
                    .replace('\t\tframes++;\n', '\t\tframes++;\n\t\theld.count[0] = frames;\n')
                    .replace('\t\tworld = Stage2d.create();\n', '\t\tworld = Stage2d.create();\n' + made)
                    .replace("\t\t\tLog.message(LogLevel.INFO, 'hello, frame $frames');\n",
                             f"\t\t\tLog.message(LogLevel.INFO, 'hello, frame $frames; {said}');\n"))
    text = main.read_text()
    if 'held.count' not in text or any(a['make'] not in text or a['say'] not in text for a in assets):
        return problem('wgf serve: the template\'s Main.hx changed; give the check its instance final and assets '
                       'again', '')
    sys.path.insert(0, str(ROOT / 'tools' / 'wgf'))
    import game as games  # the tool's own module: its page runner, and whether the machine has hotreload-hx
    served = serve_once(game, main, games, assets, hot=False)
    if served is not True:
        return served
    if not games.has_hot():
        print(f'check_cli: serve: --hot SKIPPED: no hotreload-hx among this machine\'s haxelibs ({games.HOT_INSTALL})')
        return True
    # as it was before the first serve changed them
    main.write_text(main.read_text().replace("'reloaded, frame $frames", "'hello, frame $frames"))
    write_assets(game, assets, 'first')
    return serve_once(game, main, games, assets, hot=True)


def serve_once(game, main, games, assets, hot):
    """One `wgf serve` of the game (step_serve), hot or not; True, or the problem."""
    port = browser.free_port()
    process = subprocess.Popen([sys.executable, str(WGF), 'serve', '--port', str(port)] + (['--hot'] if hot else []),
                               cwd=game, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    served = []
    threading.Thread(target=lambda: served.extend(process.stdout), daemon=True).start()
    breakable = sum(1 for a in assets if a['broken'] is not None)
    kinds = ', '.join(a['file'] for a in assets)
    try:
        if not wait(lambda: any(' at http://' in line for line in served), 600):
            return problem('wgf serve: never said where it serves', ''.join(served))
        # the steps, each begun by a line of the page's: code edited, assets saved, broken ones saved
        state = {'step': 'code', 'before': 0, 'assets_at': 0, 'broken_at': 0, 'errors': 0, 'done': False, 'first': {},
                 'last': {}}
        said = re.compile(r'(hello|reloaded), frame (\d+); (.*)')

        def on_line(line):
            if "didn't load again" in line:
                state['errors'] += 1
            m = said.search(line)
            if not m:
                return False
            word, frame = m.group(1), int(m.group(2))
            values = {name: int(value) for name, value in re.findall(r'(\w+) (-?\d+)', m.group(3))}
            if any(a['word'] not in values for a in assets):
                return False
            state['last'] = values
            first = all(a['was'](values[a['word']]) for a in assets)
            changed = bool(state['first']) and all(a['now'](values[a['word']], state['first'][a['word']])
                                                   for a in assets)
            if state['step'] == 'code' and word == 'hello' and frame >= 120 and first:
                state['first'] = values
                state['before'] = frame
                state['step'] = 'reloaded'
                # code, not a static's first value: a reload keeps every static's value
                main.write_text(main.read_text().replace("'hello, frame $frames", "'reloaded, frame $frames"))
            elif state['step'] == 'reloaded' and word == 'reloaded':
                state['after'] = frame
                state['step'] = 'assets'
                state['assets_at'] = frame
                write_assets(game, assets, 'changed')
            elif state['step'] == 'assets' and changed:
                state['shown_at'] = frame
                if not hot:  # loaded again with them: all a page that isn't hot is asked
                    state['step'] = 'shown'
                    state['done'] = True
                    return True
                state['step'] = 'broken'
                write_assets(game, assets, 'broken')
            elif state['step'] == 'broken' and state['errors'] >= breakable:
                if 'broken_at' not in state or state['broken_at'] == 0:
                    state['broken_at'] = frame  # the first line after the errors: one more to be sure
                elif frame > state['broken_at']:
                    state['kept'] = changed
                    state['step'] = 'whole'
                    state['whole_from'] = frame
                    (game / 'assets' / 'notes.txt').write_text('nothing is made from this\n')
            elif state['step'] == 'whole' and frame < state['whole_from']:  # counting from the start: a new page
                state['done'] = True
                return True
            return False

        lines = games.run_page_url(f'http://127.0.0.1:{port}/index.html', until=(), timeout=360, on_line=on_line,
                                   echo=False)
        log = '\n'.join(lines) + ''.join(served)
        if state['step'] == 'code':
            return problem(f'wgf serve: the page never said hello with its first assets ({kinds}); last, '
                           f'{state["last"]}', log)
        if state['step'] == 'reloaded':
            return problem('wgf serve: the edit never reached the page', log)
        if hot and state['after'] <= state['before']:
            return problem(f'wgf serve --hot: the reloaded page counted from {state["after"]}, not on from '
                           f'{state["before"]}: its state was lost', log)
        if not hot and state['after'] >= state['before']:
            return problem(f'wgf serve: the page went on counting, to {state["after"]} from {state["before"]}, with '
                           'the new code: it took it on without loading again', log)
        if state['step'] == 'assets':
            return problem(f'wgf serve: the saved assets never all showed in the page; first {state["first"]}, last '
                           f'{state["last"]}', log)
        if not hot:
            print(f'check_cli: serve: edited at frame {state["before"]}; the page loaded again and the new code said '
                  f'so at frame {state["after"]}; {len(assets)} assets saved ({kinds}), and it loaded again with them')
            return True
        if state['step'] == 'broken':
            return problem(f'wgf serve: {breakable} assets saved broken: {state["errors"]} error(s) logged (one each '
                           'wanted), or no line after them', log)
        if not state['done']:
            return problem('wgf serve --hot: a file nothing was made from was saved, and the page never loaded '
                           'again', log)
        if not state['kept'] or state['errors'] != breakable or state['shown_at'] <= state['assets_at']:
            return problem(f'wgf serve: {breakable} assets saved broken: kept {state["kept"]}, {state["errors"]} '
                           f'error(s) logged (one each wanted); last, {state["last"]}', log)
        print(f'check_cli: serve --hot: edited at frame {state["before"]}; the new code said so at frame {state["after"]}, '
              f'its state kept; {len(assets)} assets saved ({kinds}), shown by frame {state["shown_at"]}; '
              f'{breakable} saved broken, the old ones kept and one error logged for each; a file nothing was made '
              'from saved, and the page loaded again')
        return True
    finally:
        process.terminate()
        try:
            process.wait(20)
        except subprocess.TimeoutExpired:
            process.kill()


def step_export(game):
    """The web export (a trimmed host, smoke-tested in a browser) and the desktop one
    (smoke-tested in a window), each where it can be."""
    native, web = needs(native=True), needs(web=True, browser_too=True)
    if native and web:
        return f'{native}; {web}'
    args = ['export', '--autopilot', 'smoke.autopilot']
    args += ['--web'] if native else ['--desktop'] if web else []
    code, out = wgf(game, *args, timeout=1800)
    said = []
    if not web:
        site = game / 'export' / 'web'
        if 'web export: smoke.autopilot PASS' not in out or not (site / 'wgf-host.wasm').exists():
            return problem('wgf export: the web export failed', out)
        trimmed = json.loads((game / 'build' / 'export-web' / 'exports.json').read_text())['exports']
        full = json.loads((ROOT / 'hosts' / 'web' / 'exports.json').read_text())['exports']
        if not len(trimmed) < len(full) / 2:
            return problem(f'wgf export: the host wasn\'t trimmed ({len(trimmed)} of {len(full)} calls)', out)
        if not (site / 'assets' / 'manifest.json').exists() or (game / 'assets' / 'manifest.json').exists():
            return problem('wgf export: the web export\'s assets have no manifest, or the game\'s were written', out)
        sizes = [line for line in out.splitlines() if 'KB gzipped in all' in line]
        said.append(f'web: {len(trimmed)} of {len(full)} calls in the host, '
                    f'{sizes[0].split("; ")[-1] if sizes else ""}, its manifests written, flown by --autopilot')
    if not native:
        if 'desktop export: smoke.autopilot PASS' not in out and 'desktop export: SKIPPING' not in out:
            return problem('wgf export: the desktop export failed', out)
        said.append('desktop: ' + ('flown by --autopilot' if 'desktop export: smoke.autopilot PASS' in out
                                   else 'not smoke-tested here'))
        left = [p.name for p in (game / 'export' / 'desktop').rglob('*.autopilot')]
        if left:
            return problem(f'wgf export: the smoke run left {", ".join(left)} in the desktop export')
    if code != 0:
        return problem('wgf export: failed', out)
    if any(path.is_symlink() for path in game.joinpath('export').rglob('*')):
        return problem('wgf export: a link in the export, which a copy elsewhere would break')
    skipped = '; '.join(f'{t} skipped ({why})' for t, why in (('desktop', native), ('web', web)) if why)
    print(f'check_cli: export: {"; ".join(said)}' + (f'; {skipped}' if skipped else ''))
    return True


def wait(predicate, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return True
        time.sleep(0.2)
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--only', help='comma-separated steps: ' + ', '.join(STEPS) + ' (new always runs first)')
    ap.add_argument('--keep', action='store_true', help='keep the game made, to look at')
    args = ap.parse_args()
    chosen = args.only.split(',') if args.only else list(STEPS)
    for name in chosen:
        if name not in STEPS:
            print(f'check_cli: no step "{name}" ({", ".join(STEPS)})', file=sys.stderr)
            return 2
    base = Path(variants.work(variants.native('debug-headless'))) / 'check_cli'
    if base.exists():
        shutil.rmtree(base)
    base.mkdir(parents=True)
    game = base / 'clitest'
    skipped, failed = [], []
    for name in ['new'] + [s for s in chosen if s != 'new']:
        print(f'== {name}', flush=True)
        if name == 'new':
            outcome = step_new(base, game)
        else:
            outcome = globals()[f'step_{name}'](game)
        if isinstance(outcome, str):
            print(f'check_cli: SKIPPING {name} ({outcome})', flush=True)
            skipped.append(f'{name} ({outcome})')
        elif not outcome:
            failed.append(name)
            if name == 'new':
                break
    if not args.keep and not failed:
        shutil.rmtree(base, ignore_errors=True)
    note = f'; skipped: {", ".join(skipped)}' if skipped else ''
    if failed:
        print(f'check_cli: FAIL ({", ".join(failed)}){note}')
        return 1
    print(f'check_cli: PASS{note}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
