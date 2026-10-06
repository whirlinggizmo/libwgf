#!/usr/bin/env python3
"""Generate Asteroids' sounds: games/asteroids/assets/sounds/*.ogg.

    tools/gen_sounds.py [--check] [--out DIR]

Each sound is made from a few lines of synthesis -- tones swept between two pitches,
noise through a low-pass filter, envelopes -- with a fixed seed, so the same samples come
out every time (16-bit mono at 22,050 Hz), then encoded as Ogg Vorbis by ffmpeg's
libvorbis (quality 4), which libwgf's mixer decodes natively (Xiph's decoder) and the
browser on the web. The samples' SHA-256 for each sound is in sounds.json beside them,
committed with them: --check makes the samples again and fails when one differs from what
the manifest says its Ogg was made from, needing no encoder (an encoder's version may
change the bytes of the same sound). Standard library, and ffmpeg to write.
"""
import argparse
import hashlib
import io
import json
import shutil
import subprocess
import tempfile
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


def encode(wav, path):
    """`wav`'s bytes as Ogg Vorbis at `path`, by ffmpeg's libvorbis, bit-exact (no
    encoder version or date in the file)."""
    ffmpeg = shutil.which('ffmpeg')
    if ffmpeg is None:
        raise SystemExit('gen_sounds: writing the sounds needs ffmpeg (with libvorbis); --check needs none')
    with tempfile.TemporaryDirectory() as scratch:
        source = Path(scratch) / 'sound.wav'
        source.write_bytes(wav)
        subprocess.run([ffmpeg, '-loglevel', 'error', '-y', '-i', str(source), '-c:a', 'libvorbis', '-q:a', '4',
                        '-map_metadata', '-1', '-fflags', '+bitexact', '-flags:a', '+bitexact', str(path)], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='write nothing; fail when a sound isn\'t what its samples make')
    ap.add_argument('--out', type=Path, default=OUT, help='where to write them (default: the game\'s assets)')
    args = ap.parse_args()
    manifest_path = args.out / 'sounds.json'
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    stale, made = [], {}
    for name, samples in sounds().items():
        wav = wav_bytes(samples)
        digest = hashlib.sha256(wav).hexdigest()
        made[name] = digest
        path = args.out / f'{name}.ogg'
        if manifest.get(name) != digest or not path.exists():
            stale.append(path)
            if not args.check:
                path.parent.mkdir(parents=True, exist_ok=True)
                encode(wav, path)
    if args.check:
        gone = sorted(set(manifest) - set(made))
        for path in stale:
            print(f'gen_sounds: stale: {path}', file=sys.stderr)
        for name in gone:
            print(f'gen_sounds: in the manifest, but not made any more: {name}', file=sys.stderr)
        print(f'gen_sounds: {"stale" if stale or gone else "up to date"}')
        return 1 if stale or gone else 0
    manifest_path.write_text(json.dumps(made, indent=1, sort_keys=True) + '\n')
    for old in args.out.glob('*.wav'):
        old.unlink()  # the sounds are Ogg now
    print(f'gen_sounds: {len(stale)} of {len(made)} sound(s) written to {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
