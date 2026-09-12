#!/usr/bin/env python3
"""Synthesize a short shared startup chime; no API or external tools required."""
import array
import json
import math
from pathlib import Path
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
RATE = 16000
DURATION = 0.72
# Gentle rising notes, with overlapping decay and a short release to avoid clicks.
NOTES = [(0.00, 659.25, 0.30, 0.70), (0.12, 987.77, 0.36, 0.70), (0.26, 1318.51, 0.40, 0.50)]


def main():
    values = []
    for i in range(round(RATE * DURATION)):
        t = i / RATE
        value = 0.0
        for start, frequency, duration, gain in NOTES:
            age = t - start
            if 0 <= age < duration:
                attack = min(1.0, age / 0.008)
                release = min(1.0, (duration - age) / 0.040)
                envelope = attack * release * math.exp(-7.0 * age)
                phase = 2 * math.pi * frequency * age
                value += gain * envelope * (math.sin(phase) + 0.12 * math.sin(2 * phase))
        values.append(value)
    scale = 7800 / max(map(abs, values))
    pcm = array.array('h', (round(value * scale) for value in values))
    if sys.byteorder != 'little':
        pcm.byteswap()
    raw = pcm.tobytes()
    source = ROOT / 'assets/audio/startup.wav'
    target = ROOT / 'assets/generated/startup.pcm'
    source.parent.mkdir(parents=True, exist_ok=True)
    target.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(source), 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(raw)
    target.write_bytes(raw)
    print(json.dumps({'event': 'startup_chime_prepared', 'samples': len(values),
                      'duration_ms': round(DURATION * 1000), 'peak': 7800}))


if __name__ == '__main__':
    main()
