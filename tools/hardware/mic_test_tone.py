"""Generate the local three-tone acoustic stimulus used for microphone QA."""
import argparse
import math
from pathlib import Path
import struct
import wave

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=Path)
args = parser.parse_args()
rate = 16000
frames = []
for i in range(int(rate * 2.9)):
    t = i / rate
    frequency = (1000 if 0.35 <= t < 0.85 else 2000 if 1.25 <= t < 1.75
                 else 1500 if 2.15 <= t < 2.65 else 0)
    if frequency:
        local = (t - 0.35) % 0.9
        ramp = max(0, min(1.0, local / 0.01, (0.5 - local) / 0.01))
        value = int(6000 * ramp * math.sin(2 * math.pi * frequency * t))
    else:
        value = 0
    frames.append(value)
args.output.parent.mkdir(parents=True, exist_ok=True)
with wave.open(str(args.output), "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(rate)
    wav.writeframes(struct.pack("<" + "h" * len(frames), *frames))
print(args.output)
