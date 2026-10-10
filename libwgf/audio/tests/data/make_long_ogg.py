"""How libwgf/audio/tests/data/long.ogg was made, kept for remaking it; not a tool (it runs
ffmpeg, which a tool may not assume) and not run by any build.

Forty seconds of mono at 44.1 kHz, a 440 Hz tone under quiet noise (so the encoder has
something to spend bits on, making a file of a few hundred KB: longer than the margin a
decoder keeps ahead of a file arriving, wgf_audio_mix.c), encoded by ffmpeg's libvorbis
(ffmpeg 6.1.1, -q:a 4). For a streamed sound read while its file arrives
(wgf_audio_arrive_test.c).

    python3 libwgf/audio/tests/data/make_long_ogg.py [DIR]
"""
import math
import random
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

RATE, SECONDS = 44100, 40


def write_wav(path):
    noise = random.Random(4)
    data = bytearray(RATE * SECONDS * 2)
    for i in range(RATE * SECONDS):
        sample = 9000 * math.sin(2 * math.pi * 440 * i / RATE) + noise.uniform(-3000, 3000)
        struct.pack_into('<h', data, i * 2, int(sample))
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(bytes(data))


def main():
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / 'long.wav'
        write_wav(source)
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', str(source), '-c:a', 'libvorbis', '-q:a', '4',
                        str(out_dir / 'long.ogg')], check=True)


if __name__ == '__main__':
    main()
