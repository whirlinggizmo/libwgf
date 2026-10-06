#!/usr/bin/env python3
"""Generate Asteroids' sounds: games/asteroids/assets/sounds/*.wav.

    tools/gen_sounds.py [--check] [--out DIR]

Each sound is made from a few lines of synthesis -- tones swept between two pitches,
noise through a low-pass filter, envelopes -- with a fixed seed, so the same files come
out every time; they are committed, and --check fails when one differs from what this
would write. 16-bit mono WAV at 22,050 Hz: the mixer and the browser both decode it, and
none is long enough for compression to matter. Standard library only.
"""
import argparse
import io
import math
import random
import struct
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'games' / 'asteroids' / 'assets' / 'sounds'
RATE = 22050


def envelope(i, n, attack=0.005, release=0.6):
    """A rise over `attack` seconds, then a fall over the last `release` of the sound."""
    t = i / RATE
    rise = min(1.0, t / attack) if attack > 0 else 1.0
    fall_from = n * (1 - release)
    fall = 1.0 if i < fall_from else max(0.0, 1 - (i - fall_from) / (n - fall_from))
    return rise * fall


def sweep(seconds, start, end, shape='square', volume=0.5, release=0.8):
    """A tone gliding from `start` Hz to `end` Hz."""
    n = int(seconds * RATE)
    out, phase = [], 0.0
    for i in range(n):
        f = start * (end / start) ** (i / n)
        phase += f / RATE
        p = phase % 1.0
        v = (1.0 if p < 0.5 else -1.0) if shape == 'square' else math.sin(2 * math.pi * p)
        out.append(v * volume * envelope(i, n, release=release))
    return out


def noise(seconds, cutoff_start, cutoff_end, seed, volume=0.8, release=0.95, attack=0.002):
    """White noise through a one-pole low-pass whose cutoff glides from start to end."""
    rng = random.Random(seed)
    n = int(seconds * RATE)
    out, y = [], 0.0
    for i in range(n):
        cutoff = cutoff_start * (cutoff_end / cutoff_start) ** (i / n)
        a = 1 - math.exp(-2 * math.pi * cutoff / RATE)
        y += a * (rng.uniform(-1, 1) - y)
        out.append(y * volume * envelope(i, n, attack=attack, release=release))
    return out


def notes(pitches, each, shape='square', volume=0.4):
    out = []
    for f in pitches:
        out += sweep(each, f, f, shape, volume, release=0.5)
    return out


def normalized(samples, peak=0.9):
    top = max(abs(s) for s in samples) or 1.0
    return [s * peak / top for s in samples]


def looped(samples, fade=0.02):
    """A loop's ends faded together, so it repeats without a click."""
    n = int(fade * RATE)
    head, body = samples[:n], samples[n:]
    for i in range(n):
        t = i / n
        body[len(body) - n + i] = body[len(body) - n + i] * (1 - t) + head[i] * t
    return body


def sounds():
    return {
        'fire': sweep(0.14, 1400, 260, 'square', 0.5, release=0.9),
        'bang_large': noise(1.0, 1800, 120, seed=1, volume=1.0),
        'bang_medium': noise(0.65, 2600, 180, seed=2, volume=0.9),
        'bang_small': noise(0.4, 4000, 300, seed=3, volume=0.8),
        'thrust': looped(noise(1.2, 380, 380, seed=4, volume=0.7, release=0.0, attack=0.0)),
        'extra_life': notes([660, 880, 1100, 1320], 0.09),
        'start': notes([440, 660, 880], 0.08),
        'game_over': notes([660, 520, 400, 260], 0.22, 'sine', 0.6),
    }


def wav_bytes(samples):
    buffer = io.BytesIO()
    with wave.open(buffer, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(max(-1, min(1, s)) * 32767)) for s in normalized(samples)))
    return buffer.getvalue()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='write nothing; fail when a sound differs')
    ap.add_argument('--out', type=Path, default=OUT, help='where to write them (default: the game\'s assets)')
    args = ap.parse_args()
    stale = []
    for name, samples in sounds().items():
        path = args.out / f'{name}.wav'
        data = wav_bytes(samples)
        if not path.exists() or path.read_bytes() != data:
            stale.append(path)
            if not args.check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
    if args.check:
        for path in stale:
            print(f'gen_sounds: stale: {path}', file=sys.stderr)
        print(f'gen_sounds: {"stale" if stale else "up to date"}')
        return 1 if stale else 0
    print(f'gen_sounds: {len(stale)} of {len(sounds())} sound(s) written to {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
