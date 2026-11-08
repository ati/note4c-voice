#!/usr/bin/env python3
"""Generate the original Note4C cue WAVs. Run explicitly to restore defaults."""
import math
from pathlib import Path
import struct
import wave

ROOT = Path(__file__).resolve().parents[1]
RATE = 16000


def tone(hz, ms):
    count = RATE * ms // 1000
    return [round(6000 * min(1, i / 80, (count - 1 - i) / 80)
                  * math.sin(2 * math.pi * hz * i / RATE)) for i in range(count)]


def main():
    directory = ROOT / 'assets/sounds'
    directory.mkdir(parents=True, exist_ok=True)
    sounds = {'record-start.wav': tone(1200, 120),
              'record-stop.wav': tone(800, 80) + [0] * (RATE * 80 // 1000) + tone(800, 80)}
    for name, pcm in sounds.items():
        with wave.open(str(directory / name), 'wb') as file:
            file.setparams((1, 2, RATE, 0, 'NONE', 'not compressed'))
            file.writeframes(struct.pack('<' + 'h' * len(pcm), *pcm))
        print(name, len(pcm) * 1000 // RATE, 'ms')


if __name__ == '__main__':
    main()
