"""Decode and verify a local `mic_test dump` capture without cloud services."""
import argparse
import array
import base64
import json
import math
from pathlib import Path
import re
import sys
import wave


def decode(source: Path, output: Path):
    text = source.read_text(encoding="utf-8-sig")
    matches = list(re.finditer(
        r"MIC_PCM_BEGIN samples=(\d+) rate=(\d+) bytes=(\d+) fnv1a=([0-9a-fA-F]{8})"
        r"\r?\n(.*?)MIC_PCM_END", text, re.S))
    if len(matches) != 1:
        raise ValueError("Expected one complete MIC_PCM_BEGIN/END frame")
    match = matches[0]
    frames, rate, size = (int(match[i]) for i in (1, 2, 3))
    expected_hash = int(match[4], 16)
    if not (0 < size <= 1920000 and 8000 <= rate <= 48000 and size == frames * 2):
        raise ValueError("Invalid mono PCM16 sample metadata")
    lines = [line.strip() for line in match[5].splitlines()]
    encoded = "".join(line for line in lines if re.fullmatch(r"[A-Za-z0-9+/]{1,64}={0,2}", line))
    pcm = base64.b64decode(encoded, validate=True)
    if len(pcm) != size:
        raise ValueError(f"Truncated PCM: {len(pcm)} bytes, expected {size}")
    actual_hash = 2166136261
    for byte in pcm:
        actual_hash = ((actual_hash ^ byte) * 16777619) & 0xFFFFFFFF
    if actual_hash != expected_hash:
        raise ValueError("PCM checksum mismatch")
    values = array.array("h", pcm)
    if sys.byteorder != "little":
        values.byteswap()
    output.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(pcm)
    result = {
        "input": source.name, "wav": output.name,
        "sample_rate": rate, "channels": 1, "bits": 16,
        "frames": len(values), "seconds": len(values) / rate,
        "pcm_bytes": len(pcm), "fnv1a": f"{actual_hash:08x}",
        "checksum_verified": True,
        "peak": max(abs(v) for v in values),
        "rms": round(math.sqrt(sum(v * v for v in values) / len(values)), 3),
        "dc_mean": round(sum(values) / len(values), 3),
        "nonzero_samples": sum(v != 0 for v in values),
        "clipped_samples": sum(abs(v) >= 32760 for v in values),
        "half_second_blocks": [],
    }
    for start in range(0, len(values), rate // 2):
        block = values[start:start + rate // 2]
        result["half_second_blocks"].append({
            "peak": max(abs(v) for v in block),
            "rms": round(math.sqrt(sum(v * v for v in block) / len(block)), 3),
        })
    output.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    decode(args.input, args.output)
