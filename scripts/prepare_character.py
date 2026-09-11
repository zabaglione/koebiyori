#!/usr/bin/env python3
"""Pack the generated expression atlas into RGB565 assets for the display."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/source/sara-expressions.png"
OUTPUT = ROOT / "assets/generated"
# The artwork is authored by imagegen. FFmpeg prepares its display-sized assets.
PARTS = [
    ("portrait", 0, 0, None),
    ("mouth_small", 768, 0, (140, 162, 48, 36)),
    ("mouth_open", 0, 512, (140, 162, 48, 36)),
    ("eye_left", 768, 512, (88, 110, 67, 59)),
    ("eye_right", 768, 512, (175, 107, 76, 62)),
]


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    payload = bytearray()
    manifest = {}
    for name, px, py, patch in PARTS:
        filters = f"crop=680:512:{px + 56}:{py},scale=328:248:flags=lanczos"
        width, height = 328, 248
        if patch:
            x, y, width, height = patch
            filters += f",crop={width}:{height}:{x}:{y}"
        command = ["ffmpeg", "-v", "error", "-i", str(SOURCE), "-vf", filters, "-frames:v", "1"]
        raw = subprocess.check_output(command + ["-f", "rawvideo", "-pix_fmt", "rgb565le", "-"])
        if len(raw) != width * height * 2:
            raise RuntimeError("Invalid RGB565 asset size")
        manifest[name] = {"offset": len(payload), "width": width, "height": height, "position": patch[:2] if patch else [0, 0]}
        payload.extend(raw)
        subprocess.run(command + ["-y", str(OUTPUT / f"{name}.png")], check=True)
    (OUTPUT / "sara.rgb").write_bytes(payload)
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"bytes": len(payload), "parts": manifest}))


if __name__ == "__main__":
    main()
