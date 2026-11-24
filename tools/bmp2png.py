#!/usr/bin/env python3
"""Converts the simulator's 24-bit .bmp screenshots to .png and removes the .bmp.

Plain Python, so the screenshot script works the same on macOS, Linux and CI.
Usage: python3 tools/bmp2png.py shots/*.bmp
"""
import struct
import sys
import zlib
from pathlib import Path


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def convert(path):
    raw = path.read_bytes()
    offset, = struct.unpack_from("<I", raw, 10)
    width, height = struct.unpack_from("<ii", raw, 18)
    row = (width * 3 + 3) & ~3  # BMP rows are padded to 4 bytes and stored bottom-up, BGR
    lines = []
    for y in range(height - 1, -1, -1):
        bgr = raw[offset + y * row: offset + y * row + width * 3]
        rgb = bytearray(bgr)
        rgb[0::3], rgb[2::3] = bgr[2::3], bgr[0::3]
        lines.append(b"\x00" + bytes(rgb))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(b"".join(lines), 9))
           + chunk(b"IEND", b""))
    path.with_suffix(".png").write_bytes(png)
    path.unlink()


for name in sys.argv[1:]:
    convert(Path(name))
