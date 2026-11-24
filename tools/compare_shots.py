#!/usr/bin/env python3
"""Checks that the README's screenshots show the same pixels as fresh ones from the simulator.

Compares decoded pixels, not file bytes, since zlib may pack the same picture differently on
another machine. Usage: python3 tools/compare_shots.py docs/screenshots shots
"""
import struct
import sys
import zlib
from pathlib import Path


def pixels(path):
    data = path.read_bytes()
    i, idat, size = 8, b"", None
    while i < len(data):
        length, = struct.unpack(">I", data[i:i + 4])
        kind = data[i + 4:i + 8]
        if kind == b"IHDR":
            size = struct.unpack(">II", data[i + 8:i + 16])
        elif kind == b"IDAT":
            idat += data[i + 8:i + 8 + length]
        i += 12 + length
    return size, zlib.decompress(idat)


docs, fresh = Path(sys.argv[1]), Path(sys.argv[2])
stale = [p.name for p in sorted(docs.glob("*.png"))
         if not (fresh / p.name).exists() or pixels(p) != pixels(fresh / p.name)]
for name in stale:
    print(f"{docs / name} differs from {fresh / name}: copy the new one over")
sys.exit(1 if stale else 0)
