#!/usr/bin/env python3
"""Build or package all physical board profiles using the same checks in CI and releases."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

from collect_notices import CORE, ROOT, collect

VERSION = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?")
FRAMEWORK = CORE / "packages/framework-arduinoespressif32"


def rebuild_bundle(dest, version):
    override = """[esp32]
platform_packages = framework-arduinoespressif32 @ symlink://framework
lib_deps =
[panel_tft_espi]
lib_deps =
[touch_xpt2046]
lib_deps =
"""
    readme = f"""Bambuddy Panel {version}

Extract into an empty directory. Install PlatformIO Core 6.2.0. From this directory run
pio run -e cyd-st7789
Replace cyd-st7789 with the profile matching your board. The first build downloads the
pinned Espressif platform, tools and compiler. The framework/ directory contains the
editable Arduino core and the ESP32 SDK used by this release. lib/ contains the panel's
Arduino libraries. Edit framework/cores/esp32/ to change the core, run
pio run -e cyd-st7789 -t clean
then repeat the build to relink. The resulting application is
.pio/build/cyd-st7789/firmware.bin. Use the panel settings page to install it.
The build defaults to the dev version label. This archive accompanies the notices ZIP.
"""
    def keep(item):
        parts = Path(item.name).parts
        if any(part.startswith(".") or part == "__pycache__" for part in parts):
            return None
        if parts[0] in ("lib", "framework") and any(p.lower() in ("examples", "docs", "tests", "demos", "scripts") for p in parts[2:]):
            return None
        if item.issym() or item.islnk() or item.name.endswith(".pyc"):
            return None
        if parts[:3] == ("framework", "tools", "sdk") and len(parts) > 3 and parts[3] != "esp32":
            return None
        return item

    with tempfile.TemporaryDirectory() as work, tarfile.open(dest, "w:gz", compresslevel=3) as tar:
        for name in ("src", "boards", "platformio.ini", "LICENSE", "THIRD_PARTY.md",
                     "tools/icons/LICENSE.txt", "tools/notice-sources.json"):
            tar.add(ROOT / name, arcname=name, filter=keep)
        for name in ("cores", "libraries", "variants", "tools", "package.json"):
            tar.add(FRAMEWORK / name, arcname="framework/" + name, filter=keep)
        for name in ("lvgl", "ArduinoJson", "WiFiManager", "TFT_eSPI", "XPT2046_Touchscreen"):
            tar.add(ROOT / ".pio/libdeps/cyd-st7789" / name, arcname="lib/" + name, filter=keep)
        for name, value in (("platformio_override.ini", override), ("REBUILD.txt", readme)):
            path = Path(work) / name
            path.write_text(value)
            tar.add(path, arcname=name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", help="release tag, e.g. v0.1.0 (otherwise a CI dev build)")
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--out", type=Path, default=Path("dist"))
    args = parser.parse_args()
    version = "dev"
    if args.tag is not None:
        if not args.tag.startswith("v") or not VERSION.fullmatch(args.tag[1:]):
            parser.error("invalid release tag")
        version = args.tag[1:]
    boards = sorted(e for path in (ROOT / "boards").glob("*.ini")
                    for e in re.findall(r"^\[env:([\w-]+)\]", path.read_text(), re.M))
    documented = (ROOT / "docs/boards.md").read_text()
    if len(boards) < 10 or len(set(boards)) != len(boards) or any(f"`{e}`" not in documented for e in boards):
        parser.error("missing, duplicate or undocumented board profile")
    if args.build:
        env = os.environ.copy()
        env["PLATFORMIO_BUILD_FLAGS"] = r'-DFW_VERSION=\"' + version + r'\"'
        subprocess.run(["pio", "run", *(v for e in boards for v in ("-e", e))], env=env, cwd=ROOT, check=True)

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if any(out.iterdir()):
        parser.error(f"choose an empty output directory: {out}")
    with tempfile.TemporaryDirectory(dir=out) as work:
        stage = Path(work)
        for e in boards:
            build = Path(os.environ.get("PLATFORMIO_BUILD_DIR", ROOT / ".pio/build")) / e
            app = (build / "firmware.bin").read_bytes()
            if (f"bambuddy-panel/{version}\0".encode() not in app or
                    f"bambuddy-panel-board:{e};".encode() not in app or
                    any(m in app for m in (b"bambuddy-panel-%d", b"ci-only-password"))):
                raise ValueError(f"{e} has an unexpected version, board or OTA configuration")
            prefix = f"bambuddy-panel-{version}-{e}"
            full = stage / (prefix + "-full.bin")
            subprocess.run(["pio", "pkg", "exec", "-p", "tool-esptoolpy", "--", "esptool.py", "--chip", "esp32",
                            "merge_bin", "-o", str(full), "--flash_mode", "dio", "--flash_freq", "40m",
                            "--flash_size", "4MB", "0x1000", str(build / "bootloader.bin"), "0x8000",
                            str(build / "partitions.bin"), "0xe000", str(FRAMEWORK / "tools/partitions/boot_app0.bin"),
                            "0x10000", str(build / "firmware.bin")], cwd=ROOT, check=True)
            if full.read_bytes()[0x10000:] != app:
                raise ValueError(f"{e} merged application differs")
            (stage / (prefix + "-app.bin")).write_bytes(app)
        with tempfile.TemporaryDirectory() as notices:
            collect(Path(notices))
            with zipfile.ZipFile(stage / f"bambuddy-panel-{version}-notices.zip", "w", zipfile.ZIP_DEFLATED) as z:
                for f in sorted(Path(notices).rglob("*")):
                    if f.is_file():
                        z.write(f, f.relative_to(notices))
        rebuild_bundle(stage / f"bambuddy-panel-{version}-source.tar.gz", version)
        checks = "".join(f"{hashlib.sha256(f.read_bytes()).hexdigest()}  {f.name}\n" for f in sorted(stage.iterdir()))
        (stage / "SHA256SUMS.txt").write_text(checks)
        for f in stage.iterdir():
            shutil.move(f, out / f.name)


if __name__ == "__main__":
    main()
