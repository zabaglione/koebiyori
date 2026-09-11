#!/usr/bin/env python3
"""Package the installed library sources, or restore a release source snapshot."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

from package_source import ROOT, check_public_content

COMPONENTS = {
    "Arduino-ESP32": (".pio-core/packages/framework-arduinoespressif32", [
        "cores", "libraries", "variants", "CMakeLists.txt", "Kconfig.projbuild",
        "boards.txt", "platform.txt", "programmers.txt", "package.json",
        "tools/*.py", "tools/partitions/*.csv", "tools/sdk/versions.txt"]),
    "PlatformIO-ESP32": (".pio-core/platforms/espressif32", [
        "builder", "boards", "platform.py", "platform.json", "LICENSE"]),
    **{name: (".pio/libdeps/cores3/" + name, [
        "src", "library.json", "library.properties", "CMakeLists.txt", "component.mk",
        "idf_component.yml", "LICENSE*", "keywords.txt"])
       for name in ["M5Unified", "M5GFX", "WebSockets", "ArduinoJson"]},
}
PREFIX = "koebiyori-dependencies/"


def collect():
    content = {}
    for name, (relative, patterns) in COMPONENTS.items():
        base = ROOT / relative
        if not base.is_dir():
            raise SystemExit("Install dependencies first: python -m platformio pkg install")
        for pattern in patterns:
            for match in base.glob(pattern):
                for path in match.rglob("*") if match.is_dir() else [match]:
                    rel = path.relative_to(base)
                    if any(p in {"__pycache__", "examples", "tests", ".git"} for p in rel.parts):
                        continue
                    if path.is_symlink(): raise SystemExit("Symbolic dependency file: " + name)
                    if path.is_file() and path.suffix != ".pyc":
                        content[name + "/" + rel.as_posix()] = path.read_bytes()
    content["Arduino-ESP32/LICENSE.md"] = (ROOT / "distribution/licenses/Arduino-ESP32-LGPL.txt").read_bytes()
    return content


def manifest_for(content):
    return {name: hashlib.sha256(data).hexdigest() for name, data in sorted(content.items())}


def restore(archive_path):
    with zipfile.ZipFile(archive_path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)): raise SystemExit("Duplicate archive entries")
        manifest = json.loads(archive.read(PREFIX + "MANIFEST.json"))
        if set(names) != {PREFIX + name for name in [*manifest, "MANIFEST.json"]}:
            raise SystemExit("Archive manifest mismatch")
        content = {name: archive.read(PREFIX + name) for name in manifest}
    if manifest_for(content) != manifest: raise SystemExit("Dependency checksum mismatch")
    pending = []
    for name, data in content.items():
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or len(path.parts) < 2:
            raise SystemExit("Invalid dependency path")
        component, *relative = path.parts
        if component not in COMPONENTS: raise SystemExit("Unknown component")
        base = ROOT / COMPONENTS[component][0]
        if not base.is_dir(): raise SystemExit("Run platformio pkg install before restoring sources")
        target = base.joinpath(*relative)
        if not target.resolve().is_relative_to(base.resolve()) or base.is_symlink():
            raise SystemExit("Unsafe restore destination")
        pending.append((target, data))
    for path, data in pending:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    print(json.dumps({"event": "dependency_sources_restored", "files": len(pending)}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--restore", type=Path, help="Restore sources into installed PlatformIO packages")
    args = parser.parse_args()
    if args.restore:
        restore(args.restore)
        return
    content = collect()
    check_public_content(content)
    content["MANIFEST.json"] = (json.dumps(manifest_for(content), indent=2) + "\n").encode()
    output = ROOT / "dist/dependencies.zip"
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(content.items()):
            info = zipfile.ZipInfo(PREFIX + name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    print(json.dumps({"event": "dependency_package_created", "files": len(content),
                      "bytes": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}))


if __name__ == "__main__": main()
