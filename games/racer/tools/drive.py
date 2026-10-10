#!/usr/bin/env python3
"""Drive a lap of the racer by keyboard, for `wgf autopilot --record`, when no hand is at
the keys: run under an X display (xvfb-run), it starts the recording, reads the record
build's telemetry from its log (Main.telemetry: the track's centerline once, then the car
each frame), and presses and releases the arrow keys in the game's window with xdotool,
as a player would. The recorder writes what reached the game; this only types.

    xvfb-run -a -s "-screen 0 1280x720x24" python3 tools/drive.py autopilot/lap.autopilot

It steers by pure pursuit toward a point ahead on the centerline (the turn rate it asks for
against the car's own), holds the throttle,
brakes before corners too tight for its speed, and quits the game (Escape) a second into
lap 2.
"""
import math
import os
import subprocess
import sys
import time

LAPS = int(os.environ.get('RACER_LAPS', '1'))  # 3 drives the whole race, to its results (no expectations then)
WGF = os.path.expanduser('~/projects/github/whirlinggizmo/libwgf/wgf')

# what the car can do, for the speed a corner allows: its tires' sideways grip and its
# brakes, in m/s² (the vehicle's, felt out by driving; RACER_GRIP and RACER_BRAKE override)
GRIP = float(os.environ.get('RACER_GRIP', '10.0'))
BRAKE = float(os.environ.get('RACER_BRAKE', '5.5'))
TOP = 80.0


def wrap(a):
    while a > math.pi:
        a -= 2 * math.pi
    while a < -math.pi:
        a += 2 * math.pi
    return a


def corner_speed(curvature):
    """The fastest speed the tires hold through `curvature` (1/m)."""
    if curvature < 1e-4:
        return TOP
    return math.sqrt(GRIP / curvature)


class Keys:
    def __init__(self, window):
        self.window = window
        self.down = set()

    def set(self, key, down):
        if down == (key in self.down):
            return
        subprocess.run(['xdotool', 'keydown' if down else 'keyup', key], check=False)
        (self.down.add if down else self.down.discard)(key)

    def tap(self, key):
        subprocess.run(['xdotool', 'key', key], check=False)

    def release(self):
        for key in list(self.down):
            self.set(key, False)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'autopilot/lap.autopilot'
    run = subprocess.Popen([sys.executable, WGF, 'autopilot', '--record', out], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True, bufsize=1)
    track = []
    keys = None
    hint = 0
    last_heading = None
    ready_frames = 0
    pressed_at = None
    finished_at = None
    last_passed = 0
    trace = open(os.environ['RACER_TRACE'], 'w') if os.environ.get('RACER_TRACE') else None  # the telemetry and keys, a frame a line
    passes = []  # (checkpoints passed, the autopilot frame it happened in)
    for line in run.stdout:
        if 'racer.track ' in line:
            for pair in line.split('racer.track ', 1)[1].split():
                x, z = pair.split(',')
                track.append((float(x), float(z)))
            continue
        if 'racer.tel ' not in line:
            if 'wgf' in line and 'racer.tel' not in line:
                sys.stdout.write(line)
            continue
        if keys is None:
            wid = subprocess.run(['xdotool', 'search', '--sync', '--name', 'Racer'], capture_output=True,
                                 text=True).stdout.split()[0]
            subprocess.run(['xdotool', 'windowfocus', '--sync', wid], check=False)
            keys = Keys(wid)
        f = line.split('racer.tel ', 1)[1].split()
        if trace and trace.tell() == 0:
            trace.write('track ' + ' '.join(f'{a},{b}' for a, b in track) + '\n')
        if trace:
            trace.write(' '.join(f) + ' ' + ' '.join(sorted(keys.down)) + '\n')
        frame, state = int(f[0]), int(f[1])
        x, z, heading, speed, passed, lap = float(f[2]), float(f[3]), float(f[4]), float(f[5]), int(f[6]), int(f[7])
        if passed != last_passed:
            print(f'drive: passed {passed} lap {lap} at telemetry frame {frame}', flush=True)
            passes.append((passed, frame))
            last_passed = passed
        if state == 1:  # ready: the throttle starts the countdown, and stays down through it
            # not on the grid's first frames: the recorder's frame 0 is where core's loads
            # ended, and the grid can come a frame later in a replay than it did here (the
            # sky, loading last, ends the loads), so a press at once could land before it
            ready_frames += 1
            if ready_frames >= 10:
                if 'Up' not in keys.down:
                    pressed_at = frame  # the anchor between the telemetry's frames and the recording's
                keys.set('Up', True)
        if state == 4 and finished_at is None:  # the results: the race is over
            finished_at = frame
        if state not in (3, 4):
            continue
        if lap >= LAPS + 1 and finished_at is None:
            finished_at = frame
            print(f'drive: lap {lap - 1} done at frame {frame}', flush=True)
        if frame > int(os.environ.get('RACER_GIVE_UP', '100000')):
            finished_at = finished_at or frame - 61
        if finished_at is not None and frame > finished_at + 60:
            keys.release()
            keys.tap('Escape')
            break

        n = len(track)
        best, best_d = hint, 1e18
        for k in range(-15, 16):
            i = (hint + k) % n
            d = (track[i][0] - x) ** 2 + (track[i][1] - z) ** 2
            if d < best_d:
                best, best_d = i, d
        if best_d > 15 * 15:  # lost (far off the track): look along all of it
            best = min(range(n), key=lambda i: (track[i][0] - x) ** 2 + (track[i][1] - z) ** 2)
        hint = best

        # pure pursuit, a point ahead by more as it goes faster (a sample every 2 m)
        ahead = int((5 + 0.3 * max(speed, 0)) / 2)
        tx, tz = track[(best + ahead) % n]
        err = wrap(math.atan2(tx - x, tz - z) - heading)
        # pure pursuit's turn rate (the arc through the point ahead), and the car's own from
        # its last heading: the keys push the one toward the other, so it doesn't overshoot
        dist = max(math.hypot(tx - x, tz - z), 1.0)
        want_rate = 2 * max(speed, 3) * math.sin(err) / dist
        rate = wrap(heading - last_heading) * 60 if last_heading is not None else 0.0
        last_heading = heading
        keys.set('Left', want_rate - rate > 0.04)
        keys.set('Right', want_rate - rate < -0.04)

        # the tightest curvature in the distance it takes to brake to it
        limit = TOP
        reach = int(max(speed, 5) * 1.6 / 2) + 2
        for k in range(1, reach):
            a, b = (best + k) % n, (best + k + 3) % n
            ha = math.atan2(track[(a + 1) % n][0] - track[a][0], track[(a + 1) % n][1] - track[a][1])
            hb = math.atan2(track[(b + 1) % n][0] - track[b][0], track[(b + 1) % n][1] - track[b][1])
            k_here = abs(wrap(hb - ha)) / 6.0
            v = corner_speed(k_here)
            # what it can still slow to by there, braking at BRAKE
            v_allowed = math.sqrt(v * v + 2 * BRAKE * 2 * k)
            limit = min(limit, v_allowed)
        braking = speed > limit + 0.5
        keys.set('Up', not braking)
        keys.set('Down', braking)
    if keys:
        keys.release()
    for line in run.stdout:
        sys.stdout.write(line)
    code = run.wait()
    if code == 0 and passes and LAPS == 1:
        expect(out, passes, pressed_at)
    return code


WINDOW = 6  # frames either side of a pass the expectations allow: the press's own latency


def expect(path, passes, pressed_at):
    """The recording made a lap autopilot: a wait for the load at frame 1 (loading takes
    real time, and the inputs count frames), the driver's Escape taken out (the replay
    would quit before its end), and expectations from what the driver saw -- the start,
    each checkpoint passed in order at its frame, the lap's time in a window -- in frame
    order among the inputs."""
    lines = open(path, encoding='utf-8').read().splitlines()
    head, ats, tail = [], [], []
    for line in lines:
        if line.startswith('at '):
            if 'escape' not in line:
                ats.append(line)
        elif line.startswith('#   ') or 'the probes as the recording ended' in line:
            tail.append(line)
        elif line.startswith('# recorded by hand'):
            head += ['# One lap, recorded by `wgf autopilot --record`: the keys typed into the record',
                     "# build's window by tools/drive.py (under Xvfb), every input as it reached the game.",
                     '# Then (drive.py, expect): a wait for the load, the driver\'s Escape taken out, and',
                     '# the expectations. Pad lines are the machine\'s own pad, as the recorder found it.']
        else:
            head.append(line)
    end = int(next(l for l in ats if l.endswith(' end')).split()[1])
    ats = [l for l in ats if not l.endswith(' end')]
    ats.append('at 1 wait racer.state >= 1')
    ats.append('at 1 expect racer.state == 1')
    first_key = min(int(l.split()[1]) for l in ats if ' key ' in l)
    # the telemetry counts the program's frames from its first; the recording counts from
    # where core's loads ended: the start's press, in both, puts one on the other
    shift = first_key - pressed_at
    passes = [(n, f + shift) for n, f in passes]
    ats.append(f'at {first_key + 1} expect racer.state == 2')
    ats.append(f'at {first_key + 200} expect racer.state == 3')
    count = 8
    for n, f in passes:
        ats.append(f'at {f - WINDOW} expect racer.passed == {n - 1}')
        ats.append(f'at {f + WINDOW} expect racer.passed == {n}')
        ats.append(f'at {f + WINDOW} expect racer.checkpoint == {n % count}')
    line_frame = passes[-1][1]
    # the lap as the game timed it (from the start, standing): the recorder's last probe values
    best = [float(l.split('==')[1]) for l in tail if 'racer.best ==' in l]
    lap = best[0] if best else (passes[-1][1] - passes[0][1]) / 60
    ats.append(f'at {line_frame - WINDOW} expect racer.lap == 1')
    ats.append(f'at {line_frame + WINDOW} expect racer.lap == 2')
    ats.append(f'at {line_frame + WINDOW} expect racer.best > {lap - 1.5:.0f}')
    ats.append(f'at {line_frame + WINDOW} expect racer.best < {lap + 1.5:.0f}')
    ats.append(f'at {end} expect racer.grass == 0')
    ats.append(f'at {end} end')
    ats.sort(key=lambda l: int(l.split()[1]))  # stable: the inputs before the checks at a frame
    open(path, 'w', encoding='utf-8').write('\n'.join(head + ats + tail) + '\n')
    print(f'drive: {path}: {len(passes)} checkpoints expected, a lap of {lap:.2f}s', flush=True)
    if os.path.basename(path) == 'lap.autopilot':
        bench(path)


BENCH_FRAMES = 600


def bench(lap_path):
    """The benchmark autopilot beside the lap: its first BENCH_FRAMES frames (the start, the
    first straight and corners), what tools/bench/measure_frames.py flies for frame timing."""
    lines = open(lap_path, encoding='utf-8').read().splitlines()
    out = ['wgf-autopilot 1',
           '# The benchmark run for frame timing (tools/bench/measure_frames.py flies it): the lap',
           f'# (lap.autopilot) cut at frame {BENCH_FRAMES}, the start and the first two checkpoints. Of the lap\'s',
           '# stretches the start is within 15% of the busiest (the one at checkpoints 4 and 5, 1,300',
           "# frames in), which a software renderer can't reach within the frames step's time.",
           '# Cut again from lap.autopilot when the lap is re-recorded (tools/drive.py does, as it records).']
    for line in lines:
        if line.startswith('seed '):
            out.append(line)
        elif line.startswith('at ') and int(line.split()[1]) < BENCH_FRAMES and not line.endswith(' end'):
            out.append(line)
    out.append(f'at {BENCH_FRAMES} end')
    dest = os.path.join(os.path.dirname(lap_path), 'bench.autopilot')
    open(dest, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    print(f'drive: {dest}: the lap\'s first {BENCH_FRAMES} frames', flush=True)


def race(recorded, dest):
    """A RACER_LAPS=3 recording made the race autopilot: the driver's Escape out, a wait for
    the grid, the results expected (4 laps begun, 25 checkpoints, the best lap in a window
    about the recorder's own), a screenshot of them, and Race again by Enter."""
    lines = open(recorded, encoding='utf-8').read().splitlines()
    head = ['wgf-autopilot 1',
            '# The whole race, three laps, recorded by `wgf autopilot --record` (tools/drive.py',
            "# typing into the record build's window: RACER_LAPS=3), then the results and Race",
            '# again by Enter (`drive.py --race RECORDING autopilot/race.autopilot`). Long: in a',
            '# browser its default timeout grows with its frames.']
    ats, tail, end = [], [], None
    for line in lines:
        if line.startswith('wgf-autopilot') or line.startswith('# recorded by hand'):
            continue
        if line.startswith('at '):
            if 'escape' in line:
                continue
            if line.endswith(' end'):
                end = int(line.split()[1])
                continue
            ats.append(line)
        elif line.startswith('#   ') or 'the probes as the recording ended' in line:
            tail.append(line)
        else:
            head.append(line)
    best = [float(l.split('==')[1]) for l in tail if 'racer.best ==' in l][0]
    ats += ['at 1 wait racer.state >= 1', f'at {end} expect racer.state == 4', f'at {end} expect racer.lap == 4',
            f'at {end} expect racer.passed == 25', f'at {end} expect racer.best > {best - 1.5:.1f}',
            f'at {end} expect racer.best < {best + 1.5:.1f}', f'at {end + 30} screenshot results',
            f'at {end + 40} key tap enter', f'at {end + 42} expect racer.state == 2', f'at {end + 42} expect racer.lap == 1',
            f'at {end + 42} expect racer.passed == 0', f'at {end + 50} end']
    ats.sort(key=lambda l: int(l.split()[1]))
    open(dest, 'w', encoding='utf-8').write('\n'.join(head + ats + tail) + '\n')
    print(f'drive: {dest}: the race, its best lap {best:.2f}s', flush=True)


if __name__ == '__main__':
    if sys.argv[1:2] == ['--race']:  # a 3-lap recording made the race autopilot
        race(sys.argv[2], sys.argv[3])
        sys.exit(0)
    if sys.argv[1:2] == ['--bench']:  # the benchmark cut again from a lap already recorded
        bench(sys.argv[2])
        sys.exit(0)
    sys.exit(main())
