#!/usr/bin/env python3
"""Turns the SVG icons in tools/icons/ into src/icons.c.

Each icon becomes an 8-bit alpha mask (the UI picks the colour). Plain Python, no packages:
it reads the filled <path> elements, flattens their curves and fills them with smoothing.
Handles the path commands these icons use (M L H V C S Z, either case); not a general SVG reader.

The icons are Google's Material Icons, Apache License 2.0 (see tools/icons/LICENSE.txt).

Run from anywhere:  python3 tools/make_icons.py
"""
import re
from pathlib import Path

SAMPLES = 4    # supersampling per axis, for smooth edges
CURVE_STEPS = 16

ROOT = Path(__file__).resolve().parent.parent
# Each icon is drawn once per screen scale a board has, so the panel never stretches one:
# stretched bitmaps come out soft and stepped. Suffix in the code: pixels per unit.
SCALES = {"": 1.0, "_x133": 4 / 3}  # 320x240, and 480x320 (boards/README.md)

# name in the code: (file in tools/icons, size in units, square)
ICONS = {
    "icon_thumb_up": ("thumb_up.svg", 84),
    "icon_thumb_down": ("thumb_down.svg", 84),
    "icon_wrench": ("build.svg", 20),
    "icon_spool": ("spool.svg", 20),
}

TOKEN = re.compile(r"[MmLlHhVvCcSsZz]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")
ARGS = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6, "S": 4, "Z": 0}


def cubic(p0, p1, p2, p3):
    for i in range(1, CURVE_STEPS + 1):
        t = i / CURVE_STEPS
        u = 1 - t
        yield tuple(u**3 * a + 3 * u * u * t * b + 3 * u * t * t * c + t**3 * d
                    for a, b, c, d in zip(p0, p1, p2, p3))


def flatten(d):
    """SVG path data -> list of closed polygons (lists of (x, y))."""
    tokens = TOKEN.findall(d)
    polys, poly = [], []
    pos = start = (0.0, 0.0)
    prev_ctrl = None
    cmd = None
    i = 0
    while i < len(tokens):
        if tokens[i].isalpha():
            cmd = tokens[i]
            i += 1
        elif cmd in ("M", "m"):  # further pairs after a move are lines
            cmd = "L" if cmd == "M" else "l"
        up = cmd.upper()
        rel = cmd.islower()
        n = ARGS[up]
        a = [float(t) for t in tokens[i:i + n]]
        i += n
        ox, oy = pos if rel else (0.0, 0.0)
        ctrl = None
        if up == "M":
            if poly:
                polys.append(poly)
            pos = start = (a[0] + ox, a[1] + oy)
            poly = [pos]
        elif up == "L":
            pos = (a[0] + ox, a[1] + oy)
            poly.append(pos)
        elif up == "H":
            pos = (a[0] + ox, pos[1])
            poly.append(pos)
        elif up == "V":
            pos = (pos[0], a[0] + oy)
            poly.append(pos)
        elif up in ("C", "S"):
            if up == "C":
                c1 = (a[0] + ox, a[1] + oy)
                a = a[2:]
            else:  # smooth: first control point mirrors the previous one
                c1 = (2 * pos[0] - prev_ctrl[0], 2 * pos[1] - prev_ctrl[1]) if prev_ctrl else pos
            c2 = (a[0] + ox, a[1] + oy)
            end = (a[2] + ox, a[3] + oy)
            poly.extend(cubic(pos, c1, c2, end))
            pos, ctrl = end, c2
        elif up == "Z":
            if poly:
                polys.append(poly)
            poly = []
            pos = start
        prev_ctrl = ctrl
    if poly:
        polys.append(poly)
    return polys


def read_svg(path):
    """Returns (viewbox size, polygons) for the filled paths of a square icon."""
    text = path.read_text()
    box = [float(v) for v in re.search(r'viewBox="([^"]+)"', text).group(1).split()]
    polys = []
    for tag in re.findall(r"<path\b[^>]*>", text):
        if 'fill="none"' in tag:
            continue
        polys += flatten(re.search(r'\bd="([^"]+)"', tag).group(1))
    return box[2], polys


def rasterise(view, polys, SIZE):
    """Non-zero fill, one scanline per sample row."""
    scale = SIZE / view
    edges = []
    for poly in polys:
        pts = [(x * scale, y * scale) for x, y in poly]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
            if y0 != y1:
                edges.append((x0, y0, x1, y1))

    rows = []
    for py in range(SIZE):
        hits = [0] * SIZE
        for sy in range(SAMPLES):
            y = py + (sy + 0.5) / SAMPLES
            crossings = sorted(
                (x0 + (y - y0) * (x1 - x0) / (y1 - y0), 1 if y1 > y0 else -1)
                for x0, y0, x1, y1 in edges
                if min(y0, y1) <= y < max(y0, y1)
            )
            for px in range(SIZE):
                for sx in range(SAMPLES):
                    x = px + (sx + 0.5) / SAMPLES
                    winding = sum(w for cx, w in crossings if cx <= x)
                    hits[px] += winding != 0
        rows.append([round(255 * h / SAMPLES**2) for h in hits])
    return rows


def c_array(name, rows):
    SIZE = len(rows)
    body = ",\n".join("  " + ", ".join(f"0x{v:02x}" for v in row) for row in rows)
    return (
        f"static const uint8_t {name}_map[] = {{\n{body}\n}};\n\n"
        f"const lv_image_dsc_t {name} = {{\n"
        f"  .header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, "
        f".w = {SIZE}, .h = {SIZE}, .stride = {SIZE}}},\n"
        f"  .data_size = sizeof({name}_map),\n"
        f"  .data = {name}_map,\n"
        f"}};\n"
    )


def main():
    parts = [
        "// Generated by tools/make_icons.py from tools/icons/*.svg. Do not edit.\n"
        "// Icons: Google Material Icons (thumb_up, thumb_down, build), Apache License 2.0, see\n"
        "// tools/icons/LICENSE.txt. Modified: the SVG paths are rasterised into 8-bit alpha masks.\n"
        "// spool: drawn for this project.\n"
        '#include "icons.h"\n'
    ]
    for name, (file, size) in ICONS.items():
        view, polys = read_svg(ROOT / "tools" / "icons" / file)
        for suffix, scale in SCALES.items():
            parts.append(c_array(name + suffix, rasterise(view, polys, round(size * scale))))
    out = ROOT / "src" / "icons.c"
    out.write_text("\n".join(parts))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
