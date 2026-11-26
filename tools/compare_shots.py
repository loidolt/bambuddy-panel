#!/usr/bin/env python3
"""Checks that the README's screenshots show the same pixels as fresh ones from the simulator.

Compares decoded pixels, not file bytes: the same picture can be packed differently on another
machine, or by a tool that makes PNGs smaller. Fails when there is nothing to compare, and,
given the README, when it shows a screenshot that isn't there.

Usage: python3 tools/compare_shots.py docs/screenshots shots [README.md]
"""
import re
import struct
import sys
import zlib
from pathlib import Path

CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}  # per PNG colour type


def unfilter(raw, width, height, bpp):
    """Undoes the PNG row filters: the bytes as they were before packing."""
    stride = width * bpp
    out = bytearray()
    prev = bytes(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                row[i] = (row[i] + a) & 255
            elif kind == 2:
                row[i] = (row[i] + b) & 255
            elif kind == 3:
                row[i] = (row[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif kind != 0:
                raise ValueError(f"unknown row filter {kind}")
        out += row
        prev = bytes(row)
    return bytes(out)


def pixels(path):
    """(width, height, RGB bytes) of an 8-bit, non-interlaced PNG."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    i, idat, head, palette = 8, b"", None, b""
    while i < len(data):
        length, = struct.unpack(">I", data[i:i + 4])
        kind, body = data[i + 4:i + 8], data[i + 8:i + 8 + length]
        if kind == b"IHDR":
            head = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = body
        elif kind == b"IDAT":
            idat += body
        i += 12 + length
    width, height, depth, colour, _, _, interlace = head
    if depth != 8 or interlace or colour not in CHANNELS:
        raise ValueError(f"{path}: only 8-bit, non-interlaced PNGs are compared")
    n = CHANNELS[colour]
    raw = unfilter(zlib.decompress(idat), width, height, n)
    if colour == 2:
        rgb = raw
    elif colour == 6:  # the screenshots have no see-through parts; alpha is left out
        rgb = bytes(b for j in range(0, len(raw), 4) for b in raw[j:j + 3])
    elif colour == 3:
        rgb = b"".join(palette[3 * v:3 * v + 3] for v in raw)
    else:  # grey, with or without alpha
        rgb = bytes(b for j in range(0, len(raw), n) for b in (raw[j],) * 3)
    return width, height, rgb


def main():
    docs, fresh = Path(sys.argv[1]), Path(sys.argv[2])
    problems = []
    names = sorted(p.name for p in docs.glob("*.png"))
    if not names:
        problems.append(f"no screenshots in {docs}")
    if len(sys.argv) > 3:
        text = Path(sys.argv[3]).read_text()
        for shown in sorted(set(re.findall(re.escape(docs.as_posix()) + r"/([\w.-]+\.png)", text))):
            if shown not in names:
                problems.append(f"{sys.argv[3]} shows {docs / shown}, which doesn't exist")
    for name in names:
        if not (fresh / name).exists():
            problems.append(f"{docs / name} has no fresh counterpart in {fresh}")
        elif pixels(docs / name) != pixels(fresh / name):
            problems.append(f"{docs / name} differs from {fresh / name}: copy the new one over")
    for line in problems:
        print(line)
    print(f"compared {len(names)} screenshots, {len(problems)} problems")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
