#!/usr/bin/env python3
"""Build clean CoreS3 images for M5Burner and M5Launcher from build artifacts."""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import zipfile

from package_source import FILES, ROOT, check_public_content


def main():
    subprocess.run([sys.executable, "-m", "platformio", "run", "-e", "cores3"], cwd=ROOT, check=True)
    build = ROOT / ".pio/build/cores3"
    version = re.search(r'constexpr char BUILD\[\] = "koebiyori-([0-9.]+)";',
                        (ROOT / "src/main.cpp").read_text()).group(1)
    segments = {
        0x0000: (build / "bootloader.bin").read_bytes(),
        0x8000: (build / "partitions.bin").read_bytes(),
        0xe000: (ROOT / ".pio-core/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin").read_bytes(),
        0x10000: (build / "firmware.bin").read_bytes(),
    }
    partitions = {}
    for offset in range(0, len(segments[0x8000]), 32):
        magic, kind, subtype, address, size, label, flags = struct.unpack_from("<HBBII16sI", segments[0x8000], offset)
        if magic != 0x50aa: break
        partitions[label.rstrip(b"\0").decode("ascii")] = (kind, subtype, address, size, flags)
    if partitions.get("nvs") != (1, 2, 0x9000, 0x5000, 0) or partitions.get("app0") != (0, 0x10, 0x10000, 0x640000, 0):
        raise SystemExit("Unsupported partition layout")
    if len(segments[0x10000]) > partitions["app0"][3]: raise SystemExit("Application exceeds partition")
    # PlatformIO emits ESP32-S3 DIO / 80 MHz / 16 MB headers for qio_qspi.
    for offset in (0, 0x10000):
        if segments[offset][0] != 0xe9 or segments[offset][2:4] != b"\x02\x4f":
            raise SystemExit("Unexpected flash header")
    image = bytearray(b"\xff" * (0x10000 + len(segments[0x10000])))
    previous_end = 0
    for offset, data in sorted(segments.items()):
        if offset < previous_end: raise SystemExit("Overlapping flash segments")
        image[offset:offset + len(data)] = data
        previous_end = offset + len(data)
    if image[0x9000:0xe000] != b"\xff" * 0x5000: raise SystemExit("NVS is not blank")

    binary_name = f"koebiyori-{version}-cores3.bin"
    app_name = f"koebiyori-{version}-cores3-app.bin"
    content = {
        binary_name: bytes(image),
        app_name: segments[0x10000],
        "cover.png": (ROOT / "assets/generated/portrait.png").read_bytes(),
        "description.txt": (ROOT / "distribution/m5burner-description.txt").read_bytes(),
        "LICENSE": (ROOT / "LICENSE").read_bytes(),
        "assets/LICENSE.md": (ROOT / "assets/LICENSE.md").read_bytes(),
        "lib/esp-aec/LICENSE": (ROOT / "lib/esp-aec/LICENSE").read_bytes(),
        "THIRD_PARTY_NOTICES.md": (ROOT / "THIRD_PARTY_NOTICES.md").read_bytes(),
    }
    for component, relative in {
        "M5Unified": "M5Unified/LICENSE", "M5GFX": "M5GFX/LICENSE",
        "ArduinoJson": "ArduinoJson/LICENSE.txt", "WebSockets": "WebSockets/LICENSE",
        "libb64": "WebSockets/src/libb64/LICENSE",
    }.items():
        content[f"licenses/{component}.txt"] = (ROOT / ".pio/libdeps/cores3" / relative).read_bytes()
    for name in FILES:
        if name.startswith("distribution/licenses/"):
            content[name.removeprefix("distribution/")] = (ROOT / name).read_bytes()
    entry = {"name": "koebiyori", "version": version, "device_type": "CoreS3",
             "github": "https://github.com/zabaglione/koebiyori", "firmware": binary_name,
             "cover": "cover.png", "description": "description.txt", "flash_address": "0x0000",
             "flash_size": "16MB", "nvs_blank": True,
             "launcher": {"firmware": app_name, "format": "app-only", "device_type": "CoreS3",
                          "guide": "https://github.com/zabaglione/koebiyori/blob/main/docs/launcher.md"},
             "settings": ["wifi_ssid", "wifi_password", "openai_api_key", "voice", "voice_style", "status"]}
    content["entry.json"] = (json.dumps(entry, indent=2) + "\n").encode()
    content["README.txt"] = (
        f"koebiyori {version} - M5Stack CoreS3\n\n"
        "Firmware: " + binary_name + "\nFlash address: 0x0000; flash size: 16 MB.\n"
        "This clean image contains blank NVS. Burning it replaces the app and resets settings.\n"
        "It also replaces an installed Launcher when flashed directly over USB.\n\n"
        "M5Launcher: " + app_name + " (app-only; install through Launcher, not direct USB burn).\n"
        "Launcher installs the app in its own partition and keeps the existing NVS settings.\n"
        "Launcher guide: https://github.com/zabaglione/koebiyori/blob/main/docs/launcher.md\n\n"
        "Configure Wi-Fi and your own OpenAI API key with M5Burner > USER CUSTOM > BurnerNVS.\n"
        "Save each field, read status, close Burner NVS, and restart CoreS3.\n"
        "Setup guide: https://github.com/zabaglione/koebiyori/blob/main/docs/setup.md\n"
        "Application source and build instructions: source.zip.\n"
        "Library sources, including LGPL Arduino and WebSockets: dependencies.zip.\n"
        "See docs/build.md in source.zip to modify and relink these libraries.\n"
        f"Release: https://github.com/zabaglione/koebiyori/releases/tag/v{version}\n"
        "entry.json describes upload fields; it is not an M5Burner import format.\n\n"
        "Code: MIT. Sara artwork and derivatives: CC BY-NC 4.0, non-commercial use only.\n"
        "Artwork attribution: Sara by zabaglione, https://github.com/zabaglione/koebiyori\n"
        "Expression and color edits were made with AI; see assets/LICENSE.md.\n"
        "Startup chime is synthesized locally. Conversation speech is AI generated. See THIRD_PARTY_NOTICES.md and licenses/.\n"
    ).encode()
    check_public_content(content, firmware_names=[binary_name, app_name])
    subprocess.run([sys.executable, "scripts/package_source.py"], cwd=ROOT, check=True)
    content["source.zip"] = (ROOT / "dist/source.zip").read_bytes()
    subprocess.run([sys.executable, "scripts/package_dependencies.py"], cwd=ROOT, check=True)
    content["dependencies.zip"] = (ROOT / "dist/dependencies.zip").read_bytes()
    manifest = {name: hashlib.sha256(data).hexdigest() for name, data in sorted(content.items())}
    content["SHA256.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    output = ROOT / "dist/m5burner"
    output.mkdir(parents=True, exist_ok=True)
    for name, data in content.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    archive_path = ROOT / f"dist/koebiyori-{version}-m5burner.zip"
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(content.items()):
            info = zipfile.ZipInfo("koebiyori/" + name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    print(json.dumps({"event": "firmware_package_created", "version": version,
                      "firmware_bytes": len(image), "nvs_blank": True,
                      "launcher_app_bytes": len(segments[0x10000]),
                      "sha256": hashlib.sha256(archive_path.read_bytes()).hexdigest()}))


if __name__ == "__main__": main()
