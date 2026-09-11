#!/usr/bin/env python3
"""Generate the fixed startup cue once; normal device use never calls TTS."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import urllib.error
import urllib.request

from dotenv import dotenv_values

ROOT = Path(__file__).resolve().parents[1]
TEXT = "あっ、来てくれたんだ。ちょっと待ってね。"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generate", action="store_true", help="Call the Speech API to replace the source WAV")
    args = parser.parse_args()
    source = ROOT / "assets/audio/startup.wav"
    target = ROOT / "assets/generated/startup.pcm"
    if args.generate:
        key = dotenv_values(ROOT / ".env.local").get("OPENAI_API_KEY") or os.environ.get("OPENAI_API_KEY")
        if not key:
            raise SystemExit("OPENAI_API_KEY is unavailable")
        request = urllib.request.Request(
            "https://api.openai.com/v1/audio/speech",
            data=json.dumps({"model": "gpt-4o-mini-tts", "voice": "marin", "input": TEXT,
                "instructions": "明るく親しみやすい日本語で、友達に気づいた時のように自然に話してください。少し軽快に、長い間を空けず、約3秒で。大げさな演技や効果音は不要です。",
                "response_format": "wav"}).encode(),
            headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=90) as response:
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_bytes(response.read())
        except urllib.error.HTTPError as error:
            raise SystemExit(f"Speech API HTTP {error.code}") from None
    if not source.exists():
        raise SystemExit("Source WAV missing; use --generate once")
    # Trim only the outside silence. Keep pauses within the sentence intact.
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(source),
        "-af", "silenceremove=start_periods=1:start_threshold=-46dB:start_silence=0.025,areverse,silenceremove=start_periods=1:start_threshold=-46dB:start_silence=0.08,areverse",
        "-ar", "16000", "-ac", "1", "-f", "s16le", str(target)], check=True)
    samples = target.stat().st_size // 2
    if not 16000 <= samples <= 80000:
        raise SystemExit("Startup cue must be between 1 and 5 seconds")
    print(json.dumps({"event": "startup_voice_prepared", "samples": samples, "duration_ms": samples / 16}))


if __name__ == "__main__":
    main()
