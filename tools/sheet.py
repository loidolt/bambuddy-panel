#!/usr/bin/env python3
"""Builds shots/index.html: every simulator screenshot at every size side by side, with what
the simulator's checks found under each, and the settings web page at phone and desktop width.

Written by `tools/shots.sh all`. Usage: python3 tools/sheet.py shots
"""
import html
import re
import sys
from pathlib import Path

# The order the page shows them in, by the start of the file name. What matches none is "Status".
GROUPS = [
    ("Status screen", ""),
    ("Maintenance", "maintenance"),
    ("Pressed and held", "pressed"),
    ("Toasts", "toast"),
    ("Start-up and setup messages", "msg-"),
    ("Offline", "offline-"),
    ("Touch calibration", "calibrate"),
]


def group_of(name):
    best = 0
    for i, (_, prefix) in enumerate(GROUPS):
        if prefix and name.startswith(prefix):
            best = i
    # "offline" and "toast" on their own are status-screen scenarios; "offline-..." are not.
    return best


def read_checks(path):
    """checks.txt -> (heading line, distinct problems, {shot: [(is_problem, text)]})."""
    if not path.exists():
        return "", [], {}
    lines = path.read_text().splitlines()
    distinct, by_shot, part, shot = [], {}, None, None
    for line in lines[1:]:
        if line.startswith("== "):
            part = line[3:]
        elif part == "distinct problems" and line.strip():
            distinct.append(line.strip())
        elif part == "by screenshot" and line.strip():
            if not line.startswith(" "):
                shot = line.strip()
                by_shot[shot] = []
            else:
                by_shot[shot].append((line[2] == "!", line[4:]))
    return lines[0], distinct, by_shot


def main():
    root = Path(sys.argv[1])
    sizes = {}  # "320x240" -> folder, relative to root
    if list(root.glob("*.png")):
        sizes["320x240"] = "."
    for d in sorted(root.iterdir()):
        if d.is_dir() and re.fullmatch(r"\d+x\d+", d.name) and list(d.glob("*.png")):
            sizes[d.name] = d.name
    if not sizes:
        sys.exit(f"no screenshots under {root}")

    checks = {size: read_checks(root / folder / "checks.txt") for size, folder in sizes.items()}
    names = []
    for folder in sizes.values():
        for p in sorted((root / folder).glob("*.png")):
            if p.stem not in names:
                names.append(p.stem)

    out = ["<!doctype html><meta charset='utf-8'><title>Bambuddy Panel: every screen</title><style>",
           "body{font:14px system-ui,sans-serif;margin:1.5em;background:#f4f4f2;color:#222}",
           "h2{margin-top:2em;border-bottom:1px solid #ccc;padding-bottom:.2em}",
           "table{border-collapse:collapse}td,th{vertical-align:top;padding:.5em .8em .9em 0;text-align:left}",
           "img{display:block;image-rendering:pixelated;border:1px solid #999}",
           ".n{font-weight:600;white-space:nowrap}ul{margin:.3em 0 0;padding-left:1.1em;max-width:34em}",
           "li.p{color:#b42318}li{color:#666;font-size:12px}pre{background:#fff;padding:.8em;overflow:auto}",
           "nav{position:sticky;top:0;background:#f4f4f2;padding:.5em 0;border-bottom:1px solid #ccc}",
           "nav a{margin-right:1em}iframe{border:1px solid #999;background:#fff}",
           ".web{display:flex;gap:1.5em;flex-wrap:wrap;margin-bottom:2em}",
           "</style><h1>Bambuddy Panel: every screen</h1>",
           "<nav><label><input type='checkbox' id='t'> touch targets</label> "
           "<label><input type='checkbox' id='z' checked> 2x</label> "
           "<label><input type='checkbox' id='q'> sizes of fine targets</label> &nbsp; "]
    present = sorted({group_of(n) for n in names})
    out += [f"<a href='#g{g}'>{html.escape(GROUPS[g][0])}</a>" for g in present]
    out.append("<a href='#web'>Settings web page</a><a href='#problems'>Problems</a></nav>")

    out.append("<h2 id='problems'>What the checks found</h2>")
    for size in sizes:
        heading, distinct, _ = checks[size]
        out.append(f"<h3>{html.escape(heading or size)}</h3><pre>"
                   + html.escape("\n".join(distinct) or "no checks.txt") + "</pre>")

    for g in present:
        out.append(f"<h2 id='g{g}'>{html.escape(GROUPS[g][0])}</h2><table><tr><th></th>"
                   + "".join(f"<th>{s}</th>" for s in sizes) + "</tr>")
        for name in [n for n in names if group_of(n) == g]:
            out.append(f"<tr><td class='n'>{html.escape(name)}</td>")
            for size, folder in sizes.items():
                png = Path(folder) / f"{name}.png"
                if not (root / png).exists():
                    out.append("<td>-</td>")
                    continue
                w, h = (int(v) for v in size.split("x"))
                target = Path(folder) / "targets" / f"{name}.png"
                items = "".join(f"<li class='{'p' if bad else 'f'}'>{html.escape(text)}</li>"
                                for bad, text in checks[size][2].get(name, []))
                out.append(f"<td><img src='{png.as_posix()}' data-plain='{png.as_posix()}' "
                           f"data-targets='{target.as_posix()}' data-w='{w}' width='{w * 2}' height='{h * 2}' "
                           f"loading='lazy' alt='{html.escape(name)} at {size}'><ul>{items}</ul></td>")
            out.append("</tr>")
        out.append("</table>")

    web = sorted((root / "web").glob("*.html")) if (root / "web").is_dir() else []
    out.append("<h2 id='web'>Settings web page</h2>")
    if not web:
        out.append("<p>Not written; run <code>tools/shots.sh all</code>.</p>")
    for page in web:
        src = f"web/{page.name}"
        out.append(f"<h3>{html.escape(page.stem)}</h3><div class='web'>"
                   f"<iframe src='{src}' width='375' height='812' title='{html.escape(page.stem)}, phone'></iframe>"
                   f"<iframe src='{src}' width='1024' height='812' title='{html.escape(page.stem)}, desktop'></iframe></div>")
    out.append("<p>The WiFi page of the setup network comes from the WiFiManager library and is "
               "only served by a real panel, so it is not shown here.</p>")

    out.append("<script>var t=document.getElementById('t'),z=document.getElementById('z'),q=document.getElementById('q');"
               "function u(){document.querySelectorAll('img[data-plain]').forEach(function(i){"
               "i.src=t.checked?i.dataset.targets:i.dataset.plain;var w=+i.dataset.w,k=z.checked?2:1;"
               "i.width=w*k;i.height=i.naturalHeight?i.naturalHeight*k:i.height});"
               "document.querySelectorAll('li.f').forEach(function(l){l.style.display=q.checked?'':'none'})}"
               "t.onchange=z.onchange=q.onchange=u;u()</script>")
    (root / "index.html").write_text("\n".join(out))
    shots = sum(len(list((root / f).glob("*.png"))) for f in sizes.values())
    print(f"wrote {root}/index.html: {shots} screenshots at {len(sizes)} sizes, {len(web)} web pages")


if __name__ == "__main__":
    main()
