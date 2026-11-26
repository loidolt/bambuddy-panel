#!/usr/bin/env python3
"""Gathers the license texts that have to travel with a built firmware into one folder.

Takes them from where a build left them: this repository, and the libraries PlatformIO fetched
into .pio/libdeps. Build a board first. THIRD_PARTY.md says which texts this cannot find.

Usage: python3 tools/collect_notices.py notices [env]     (env defaults to cyd-st7789)
"""
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NOT_IN_FIRMWARE = {"Unity"}  # tests only
LICENSE_NAME = re.compile(r"^(licen[cs]e|copying|notice)(\.\w+)?$", re.I)


def main():
    out = Path(sys.argv[1])
    env = sys.argv[2] if len(sys.argv) > 2 else "cyd-st7789"
    libs = ROOT / ".pio" / "libdeps" / env
    if not libs.is_dir():
        sys.exit(f"{libs} is missing: build first (pio run -e {env})")
    out.mkdir(parents=True, exist_ok=True)

    shutil.copy(ROOT / "THIRD_PARTY.md", out / "THIRD_PARTY.md")
    shutil.copy(ROOT / "LICENSE", out / "bambuddy-panel-LICENSE.txt")
    shutil.copy(ROOT / "tools" / "icons" / "LICENSE.txt", out / "material-icons-LICENSE.txt")

    count = 0
    for lib in sorted(p for p in libs.iterdir() if p.is_dir() and p.name not in NOT_IN_FIRMWARE):
        found = [f for f in sorted(lib.iterdir()) if f.is_file() and LICENSE_NAME.match(f.name)]
        if not found:
            # No license file: the notice is at the top of the sources. Take those along whole.
            found = sorted(f for f in lib.iterdir() if f.suffix in (".h", ".cpp", ".c"))
            if not found:
                sys.exit(f"{lib.name}: no license file and no source file to take its notice from")
        for f in found:
            shutil.copy(f, out / f"{lib.name}-{f.name}")
        count += 1
    if count == 0:
        sys.exit(f"no libraries in {libs}")
    print(f"notices for {count} libraries, the icons and the panel itself are in {out}/")


if __name__ == "__main__":
    main()
