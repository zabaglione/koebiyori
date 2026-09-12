#!/usr/bin/env python3
"""Create an allow-listed source ZIP; never copy local credentials or recordings."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import zipfile

from dotenv import dotenv_values

ROOT = Path(__file__).resolve().parents[1]
FILES = """
.env.example .gitignore .gitattributes LICENSE README.md CONTRIBUTING.md THIRD_PARTY_NOTICES.md platformio.ini
docs/setup.md docs/customization.md docs/hardware.md docs/build.md docs/launcher.md
distribution/m5burner-description.txt
distribution/licenses/Arduino-ESP32-LGPL.txt
distribution/licenses/ESP-Bluetooth.txt
distribution/licenses/ESP-IDF-Apache.txt
distribution/licenses/ESP-IDF-copyrights.txt
distribution/licenses/ESP-PHY.txt
distribution/licenses/ESP-WiFi.txt
distribution/licenses/FreeRTOS.txt
distribution/licenses/GCC-GPLv3.txt
distribution/licenses/GCC-runtime-exception.txt
distribution/licenses/MbedTLS.txt
distribution/licenses/SOURCES.json
distribution/licenses/cJSON.txt
distribution/licenses/lwIP.txt
distribution/licenses/newlib.txt
distribution/licenses/protobuf-c.txt
distribution/licenses/wpa_supplicant.txt
config/character.json
src/main.cpp src/assistant_config.h src/assistant_config.cpp
src/speech_options.h
src/burner_config.h src/burner_config.cpp src/device_log.h
src/launcher_support.h src/launcher_support.cpp
src/character_ui.h src/character_ui.cpp src/duplex_audio.h src/duplex_audio.cpp
src/proximity_gate.h src/proximity_sensor.h src/proximity_sensor.cpp src/delegation_tracker.h src/playback_continuity.h src/api_event_filter.h src/microphone_level.h
scripts/prepare_character.py scripts/prepare_startup_cue.py
scripts/package_source.py scripts/package_firmware.py scripts/package_dependencies.py scripts/requirements.txt
assets/README.md assets/LICENSE.md assets/source/sara-front.png
assets/source/sara-expressions.png
assets/generated/sara.rgb assets/generated/manifest.json assets/generated/portrait.png
assets/generated/mouth_small.png assets/generated/mouth_open.png
assets/generated/eye_left.png assets/generated/eye_right.png
assets/audio/startup.wav assets/generated/startup.pcm
certs/gts-root-r4.pem
lib/esp-aec/LICENSE lib/esp-aec/README.md lib/esp-aec/library.json lib/esp-aec/link.py
lib/esp-aec/include/esp_aec.h lib/esp-aec/lib/libesp_audio_processor.a
lib/esp-aec/lib/libdl_lib.a lib/esp-aec/lib/libc_speech_features.a
""".split()


def check_public_content(content, *, firmware_names=()):
    config = dotenv_values(ROOT / ".env.local")
    secrets = [config.get(k) for k in ("OPENAI_API_KEY", "WIFI_SSID", "WIFI_PASSWORD")]
    secrets.extend(value for key, value in os.environ.items()
                   if any(part in key for part in ("API_KEY", "TOKEN", "PASSWORD", "SECRET")))
    secrets = [s.encode() for s in secrets if s and len(s) >= 4]
    patterns = [rb"sk-[A-Za-z0-9_-]{20,}", rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----",
                rb"/" + rb"Users/[^/\s]+/", rb"/private/" + rb"var/folders/"]
    for relative, data in content.items():
        if any(secret in data for secret in secrets): raise SystemExit("Credential value detected in " + relative)
        selected = patterns
        if relative in firmware_names:
            # TLS parsers contain PEM labels and newlib contains upstream build paths.
            # Reject actual PEM payloads and this builder's paths in compiled images.
            selected = [patterns[0], rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----[\r\n]+[A-Za-z0-9+/=\r\n]{80,}",
                        re.escape(str(Path.home()).encode() + b"/"), re.escape(str(ROOT).encode() + b"/"), patterns[3]]
        if any(re.search(pattern, data) for pattern in selected): raise SystemExit("Sensitive pattern detected in " + relative)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="koebiyori-source")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/source.zip")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.name):
        raise SystemExit("Invalid archive name")
    content = {}
    for relative in sorted(FILES):
        path = ROOT / relative
        if path.is_symlink() or not path.is_file(): raise SystemExit("Missing or symbolic source: " + relative)
        data = path.read_bytes()
        content[relative] = data
    check_public_content(content)
    manifest = {name: hashlib.sha256(data).hexdigest() for name, data in content.items()}
    content["SOURCE_MANIFEST.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in content.items():
            info = zipfile.ZipInfo(args.name + "/" + name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    print(json.dumps({"event": "source_package_created", "files": len(content), "bytes": args.output.stat().st_size,
                      "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest()}))


if __name__ == "__main__": main()
