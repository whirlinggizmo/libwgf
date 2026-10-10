"""How libwgf/audio/tests/data/markers*.mp3 were made, kept for remaking them; not a tool (it
runs ffmpeg, which a tool may not assume) and not run by any build.

Two seconds of mono silence at 44.1 kHz with a marker (10 ms of a 1 kHz tone, which the
encoder's low-pass keeps where a click of single samples it would remove) at 0.5, 1.0,
and 1.5 s, encoded by ffmpeg's libmp3lame (ffmpeg 6.1.1, 128 kbps CBR):

  markers_gapless.mp3  as ffmpeg writes it: an Info frame with a LAME extension saying
                       the encoder's delay (576 frames) and padding, as LAME's own
                       command line writes it: a decoder that reads it plays the markers
                       on time
  markers.mp3          the same with -write_xing 0: no Info frame, so nothing says how
                       much the encoder delayed the audio, and every decoder plays the
                       markers late, by LAME's 576 frames plus the decoder's 529

    python3 libwgf/audio/tests/data/make_markers.py [DIR]
"""
import math
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

RATE, SECONDS, MARKERS = 44100, 2, (0.5, 1.0, 1.5)


def write_wav(path):
    data = bytearray(RATE * SECONDS * 2)
    for at in MARKERS:
        start = int(at * RATE)
        for i in range(RATE // 100):  # 10 ms of 1 kHz: sharp at its start, and kept by the encoder
            struct.pack_into('<h', data, (start + i) * 2, int(26000 * math.sin(2 * math.pi * 1000 * i / RATE)))
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(bytes(data))


def encode(wav, mp3, *extra):
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(wav), '-c:a', 'libmp3lame',
                    '-b:a', '128k', *extra, str(mp3)], check=True)


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent
    with tempfile.TemporaryDirectory() as work:
        wav = Path(work) / 'markers.wav'
        write_wav(wav)
        encode(wav, out / 'markers_gapless.mp3')
        encode(wav, out / 'markers.mp3', '-write_xing', '0')
    for name in ('markers_gapless.mp3', 'markers.mp3'):
        print(f'{name}: {(out / name).stat().st_size} bytes')


if __name__ == '__main__':
    main()
