#!/usr/bin/env python3
"""Collect notices from installed packages and immutable upstream sources after a board build."""
import concurrent.futures
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CORE = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
LICENSE_NAME = re.compile(r"^(licen[cs]e|copying|notice)(\.\w+)?$", re.I)


def collect(out, env="cyd-st7789"):
    def copy(source, name):
        dest = out / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)

    libs = ROOT / ".pio/libdeps" / env
    for name in ("LICENSE", "THIRD_PARTY.md", "tools/icons/LICENSE.txt", "tools/notice-sources.json"):
        copy(ROOT / name, name)
    for name in ("ArduinoJson", "lvgl", "TFT_eSPI", "WiFiManager", "XPT2046_Touchscreen"):
        folder = libs / name
        files = sorted(f for f in folder.iterdir() if f.is_file() and LICENSE_NAME.match(f.name))
        if name == "XPT2046_Touchscreen" and not files:
            files = [folder / "XPT2046_Touchscreen.h", folder / "XPT2046_Touchscreen.cpp"]
        if not files:
            raise ValueError(f"Missing license for {name}")
        for f in files:
            copy(f, Path(name) / f.name)
    for name in ("src/libs/qrcode/qrcodegen.h", "src/stdlib/builtin/lv_tlsf.h"):
        copy(libs / "lvgl" / name, Path("lvgl") / name)
    packages = CORE / "packages"
    copy(packages / "framework-arduinoespressif32/cores/esp32/libb64/LICENSE", "libb64-LICENSE")
    gcc = packages / "toolchain-xtensa-esp32/share/licenses/gcc"
    for name in ("COPYING3", "COPYING.RUNTIME"):
        copy(gcc / name, Path("gcc") / name)

    def download(entry):
        name, url = entry
        dest = out / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["curl", "--retry", "3", "--max-time", "60", "-fsSL", url, "-o", str(dest)], check=True)
        if not dest.stat().st_size:
            raise ValueError(f"Empty notice {name}")

    sources = json.loads((ROOT / "tools/notice-sources.json").read_text())
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(download, sources.items()))


if __name__ == "__main__":
    collect(Path(sys.argv[1]), *sys.argv[2:])
