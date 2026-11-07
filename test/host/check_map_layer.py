"""CM9 (docs/blipscope-map-client-steps.md, step M): the map layer's theme table is the ONLY place a colour lives in
include/MapLayer.h, and its Dark row is the plan's own table (docs/blipscope-basemap-client.md, section 4) -- read from
the plan, never transcribed here, so either side moving turns this red.

    python -I test/host/check_map_layer.py [--header PATH] [--plan PATH]

Two halves, both required:
  - outside the block between "THE THEME TABLE BEGIN" and "THE THEME TABLE END", no {r, g, b} triple, no 16- or
    24-bit hex colour and no color888() call (S-CMLIT plants a literal);
  - the Dark row's land, terrain gain, water, river, road and place name equal the plan's Dark column (S-CMPLAN moves
    one colour).
Exit 0 pass, 1 fail, 2 the rig (a file unreadable, or a table or row not found)."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TRIPLE = re.compile(r"\{\s*\d+\s*,\s*\d+\s*,\s*\d+\s*\}")
HEX = re.compile(r"\b0x[0-9A-Fa-f]{4}(?:[0-9A-Fa-f]{2})?\b")
C888 = re.compile(r"\bcolor888\s*\(")
BEGIN, END = "THE THEME TABLE BEGIN", "THE THEME TABLE END"
# The plan's section 4 rows, by their first cell, and the order the Dark row holds them in.
ROWS = [("land", "rgb"), ("terrain gain (S2)", "num"), ("water", "rgb"), ("river", "rgb"), ("road", "rgb"),
        ("place name", "rgb")]


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def read(path):
    try:
        return open(path, "rb").read().decode("utf-8").replace("\r\n", "\n")
    except OSError:
        return None


def rig(why):
    print(f"the RIG: {why}")
    sys.exit(2)


def main():
    hp = arg("--header", os.path.join(ROOT, "include", "MapLayer.h"))
    pp = arg("--plan", os.path.join(ROOT, "docs", "blipscope-basemap-client.md"))
    h, p = read(hp), read(pp)
    if h is None or p is None:
        rig("cannot read the header or the plan")
    if h.count(BEGIN) != 1 or h.count(END) != 1 or h.index(BEGIN) > h.index(END):
        rig("the theme table's markers are not found exactly once, in order")
    i, j = h.index(BEGIN), h.index(END)
    table, outside = h[i:j], h[:i] + h[j:]

    stray = [m.group(0) for rx in (TRIPLE, HEX, C888) for m in rx.finditer(outside)]

    # The Dark row: the line after the "// Dark" comment inside the table.
    m = re.search(r"//\s*Dark[^\n]*\n\s*(\{[^\n]*\}),", table)
    if not m:
        rig("the Dark row is not found in the table")
    row = m.group(1)
    got = [tuple(int(v) for v in t) for t in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", row)]
    gains = re.findall(r"\}\s*,\s*([0-9]*\.[0-9]+)\s*,", row)
    if len(got) != 5 or len(gains) != 1:
        rig(f"the Dark row does not read as 5 colours and a gain: {row}")
    mine = {"land": got[0], "terrain gain (S2)": float(gains[0]), "water": got[1], "river": got[2], "road": got[3],
            "place name": got[4]}

    # The plan's section 4: "| <entry> | <Dark> | <Light> |".
    sec = re.search(r"## 4\. The theme table\n(.*?)\n## ", p, re.S)
    if not sec:
        rig("the plan's section 4 is not found")
    plan = {}
    for name, kind in ROWS:
        r = re.search(r"^\|\s*" + re.escape(name) + r"\s*\|\s*([^|]+?)\s*\|", sec.group(1), re.M)
        if not r:
            rig(f"the plan's row '{name}' is not found")
        cell = r.group(1)
        if kind == "num":
            plan[name] = float(cell)
        else:
            t = re.fullmatch(r"\((\d+),\s*(\d+),\s*(\d+)\)", cell)
            if not t:
                rig(f"the plan's '{name}' does not read as (r, g, b): {cell}")
            plan[name] = tuple(int(v) for v in t.groups())
    differ = [f"{n}: header {mine[n]} plan {plan[n]}" for n, _ in ROWS if mine[n] != plan[n]]

    ok = not stray and not differ
    what = (f"{len(stray)} colour literal(s) outside the table{': ' + ', '.join(stray) if stray else ''}; "
            f"Dark row against the plan: {'equal' if not differ else '; '.join(differ)}")
    print(f"  {'ok  ' if ok else 'FAIL'} CM9 {what}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
