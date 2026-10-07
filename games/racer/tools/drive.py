#!/usr/bin/env python3
"""Drive a lap of the racer by keyboard, for `wgf autopilot --record`, when no hand is at
the keys: run under an X display (xvfb-run), it starts the recording, reads the record
build's telemetry from its log (Main.telemetry: the track's centerline once, then the car
each frame), and presses and releases the arrow keys in the game's window with xdotool,
as a player would. The recorder writes what reached the game; this only types.

    xvfb-run -a -s "-screen 0 1280x720x24" python3 tools/drive.py autopilot/lap.autopilot

It steers by pure pursuit toward a point ahead on the centerline, holds the throttle,
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

# ArcadeDrive's numbers, for the speed a corner allows
TURN = 1.9
TOP = 44.0


def wrap(a):
    while a > math.pi:
        a -= 2 * math.pi
    while a < -math.pi:
        a += 2 * math.pi
    return a


def corner_speed(curvature):
    """The fastest speed whose turn rate (ArcadeDrive's, at full lock) follows `curvature`."""
    if curvature < 1e-4:
        return TOP
    # v * k = TURN * (1 - 0.4 v / TOP)  ->  v = TURN / (k + 0.4 TURN / TOP)
    return TURN / (curvature + 0.4 * TURN / TOP) * 0.92


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
    finished_at = None
    last_passed = 0
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
        frame, state = int(f[0]), int(f[1])
        x, z, heading, speed, passed, lap = float(f[2]), float(f[3]), float(f[4]), float(f[5]), int(f[6]), int(f[7])
        if passed != last_passed:
            print(f'drive: passed {passed} lap {lap} at frame {frame - 1}', flush=True)
            passes.append((passed, frame - 1))
            last_passed = passed
        if state == 1:  # ready: the throttle starts the countdown, and stays down through it
            keys.set('Up', True)
        if state == 4 and finished_at is None:  # the results: the race is over
            finished_at = frame
        if state not in (3, 4):
            continue
        if lap >= LAPS + 1 and finished_at is None:
            finished_at = frame
            print(f'drive: lap {lap - 1} done at frame {frame}', flush=True)
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
        hint = best

        # pure pursuit, a point ahead by more as it goes faster (a sample every 2 m)
        ahead = int((5 + 0.3 * max(speed, 0)) / 2)
        tx, tz = track[(best + ahead) % n]
        err = wrap(math.atan2(tx - x, tz - z) - heading)
        keys.set('Left', err > 0.03)
        keys.set('Right', err < -0.03)

        # the tightest curvature in the distance it takes to brake to it
        limit = TOP
        reach = int(max(speed, 5) * 1.6 / 2) + 2
        for k in range(1, reach):
            a, b = (best + k) % n, (best + k + 3) % n
            ha = math.atan2(track[(a + 1) % n][0] - track[a][0], track[(a + 1) % n][1] - track[a][1])
            hb = math.atan2(track[(b + 1) % n][0] - track[b][0], track[(b + 1) % n][1] - track[b][1])
            k_here = abs(wrap(hb - ha)) / 6.0
            v = corner_speed(k_here)
            # what it can still slow to by there, braking at 30 m/s²
            v_allowed = math.sqrt(v * v + 2 * 30 * 2 * k * 0.6)
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
        expect(out, passes)
    return code


def expect(path, passes):
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
    ats.append(f'at {first_key + 1} expect racer.state == 2')
    ats.append(f'at {first_key + 200} expect racer.state == 3')
    count = 8
    for n, f in passes:
        ats.append(f'at {f - 2} expect racer.passed == {n - 1}')
        ats.append(f'at {f + 2} expect racer.passed == {n}')
        ats.append(f'at {f + 2} expect racer.checkpoint == {n % count}')
    line_frame = passes[-1][1]
    lap = (passes[-1][1] - passes[0][1]) / 60
    ats.append(f'at {line_frame - 2} expect racer.lap == 1')
    ats.append(f'at {line_frame + 2} expect racer.lap == 2')
    ats.append(f'at {line_frame + 2} expect racer.best > {lap - 1.5:.0f}')
    ats.append(f'at {line_frame + 2} expect racer.best < {lap + 1.5:.0f}')
    ats.append(f'at {end} expect racer.grass == 0')
    ats.append(f'at {end} end')
    ats.sort(key=lambda l: int(l.split()[1]))  # stable: the inputs before the checks at a frame
    open(path, 'w', encoding='utf-8').write('\n'.join(head + ats + tail) + '\n')
    print(f'drive: {path}: {len(passes)} checkpoints expected, a lap of {lap:.2f}s', flush=True)


if __name__ == '__main__':
    sys.exit(main())
