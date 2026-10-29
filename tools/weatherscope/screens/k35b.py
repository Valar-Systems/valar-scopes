"""3.5b's Bend K rows (docs/weatherscope-stage3.md A10.6, A12.41), re-derived here -- never from the C++: 21 Bend dry
rows (60, 30 and 15 mi x towns only, + water, + rivers, + roads, + peaks, all (B), all + terrain (C)) and 30 Bend loop
rows (15 frames at 60 mi, B and C), rendered through ref35.compose from look35's package path (bend.bmpk by the format
doc's section 9). Written in k35.ref.txt's form as test/fixtures/wx/k35b.ref.txt, which test_wx_k35b.cpp reproduces
through the device's path. Prints no coordinate.

    python -I tools/weatherscope/screens/k35b.py --write | --check | --diff <dir> [--dump <dir>]

  ROW <name> <sha256 of the 240 x 240 RGB565 big-endian frame>
  TIME <name> <text> <ink>
  BAND <name> S <text> <ink> | BAND <name> L <age> <index>
  LABEL <name> <label> <R|L> <x0> <y0> <x1> <y1>        each placed town label, in placement order (peaks are pixels)
"""
import hashlib, os, sys
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))
    spec.loader.exec_module(m)
    return m


look = _load("look35", os.path.join(HERE, "look35.py"))
r, geo35 = look.r, look.geo35
K_REF = os.path.join(r.WX, "k35b.ref.txt")
CLEAR_B = os.path.join(look.BEND, "synthetic-clear.wxc")
OFFSET_PT = -420   # Pacific (PDT) on 2024-06-02 (A10.4)
VARIANTS = [("towns", 0, False), ("water", 1, False), ("rivers", 2, False), ("roads", 4, False), ("peaks", 8, False),
            ("B", 15, False), ("C", 15, True)]
CAP = {60: 8, 30: 6, 15: 5}


def level_mm(mi):
    """The device's wxbase::LevelMm: the view's radius x 246/240, in mm."""
    return mi * 1609344 * 41 // 40


def dark():
    return look.THEMES["dark"]


def row(name, h, codes, mi, mask, terrain, loop=None):
    th = dark()
    view = r.View(h, mi)
    img, peaks, places = look.package_scene(view, level_mm(mi), mask, terrain)
    base = look.colours(img, th)
    clock = r.clock_text(h["epoch"], OFFSET_PT)
    line, col = r.state_rule(h, codes, 0)
    state = dict(line=line, colour=col, timeText=clock, timeInk=r.DIMTXT)
    scr, placed = r.compose(h, codes, view, state, places, lambda t: t[3], clock, r.Rounder("P"), zoomed=(mi != 60),
                            cap=CAP[mi], loop=loop, base=base, peaks=peaks if mask & 8 else None, glyph=geo35.glyph_p,
                            peak_log=[], pal=th["pal"], ink=th["ink"], terrain=terrain)
    lines = ["ROW %s %s" % (name, hashlib.sha256(scr.rgb565()).hexdigest()),
             "TIME %s %s %s" % (name, r.hx(clock), r.INKS[r.DIMTXT])]
    if loop is not None:
        lines.append("BAND %s L %s %d" % (name, r.hx(loop["age"]), loop["index"]))
    else:
        lines.append("BAND %s S %s %s" % (name, r.hx(line), r.INKS[col]))
    lines += ["LABEL %s %s %s %d %d %d %d" % (name, r.hx(lab), side, *box) for lab, side, box in placed]
    return name, scr, lines


def rows():
    out = []
    hC, cC = r.decode(CLEAR_B)
    if set(cC) != {0}:
        sys.exit("K35b: Bend's synthetic-clear.wxc is not all dry")
    for mi in (60, 30, 15):
        for vname, mask, terrain in VARIANTS:
            out.append(row("bdry-%d-%s" % (mi, vname), hC, cC, mi, mask, terrain))
    names = sorted(n for n in os.listdir(look.BEND) if n.startswith("shsr-") and n.endswith(".wxc"))
    if len(names) != 15:
        sys.exit(f"K35b: {len(names)} Bend loop frames, not 15")
    frames = [r.decode(os.path.join(look.BEND, n)) for n in names]
    newest = frames[-1][0]["epoch"]
    for i, (h, c) in enumerate(frames):
        mins = (newest - h["epoch"]) // 60
        loop = dict(age="NOW" if mins <= 0 else "%d MIN AGO" % mins, index=i)
        for vname, mask, terrain in VARIANTS[5:]:
            out.append(row("bloop-%02d-%s" % (i, vname), h, c, 60, mask, terrain, loop))
    return out


def text(rs):
    head = ["# Weatherscope 3.5b Bend K rows (docs/weatherscope-stage3.md A10.6, A12.41): written by",
            "# tools/weatherscope/screens/k35b.py --write, checked by its --check; read by test/host/test_wx_k35b.cpp.",
            "# Texts: printable ASCII, else %XX; '-' empty.",
            "# font %s" % r.FONT_SHA,
            "# bend.bmpk %s" % hashlib.sha256(open(look.PKG, "rb").read()).hexdigest(),
            "# bend-loop MANIFEST.json %s (LF-normalised)" % r.sha_lf(os.path.join(look.BEND, "MANIFEST.json")),
            "# bend synthetic-clear.wxc %s" % hashlib.sha256(open(CLEAR_B, "rb").read()).hexdigest()]
    return "\n".join(head + [l for _, _, lines in rs for l in lines]) + "\n"


def main(argv):
    rs = rows()
    t = text(rs)
    if "--dump" in argv:
        d = argv[argv.index("--dump") + 1]
        os.makedirs(d, exist_ok=True)
        for name, scr, _ in rs:
            open(os.path.join(d, name + ".rgb565"), "wb").write(scr.rgb565())
        print(f"dumped {len(rs)} frames to {d}")
    if "--write" in argv:
        open(K_REF, "w", encoding="utf-8", newline="\n").write(t)
        print(f"wrote {os.path.relpath(K_REF, r.ROOT)}: {len(rs)} rows")
        return 0
    if "--diff" in argv:   # diagnosis: per-row differing pixels against the device's dumps
        d = argv[argv.index("--diff") + 1]
        bad = 0
        for name, scr, _ in rs:
            want, got = scr.rgb565(), open(os.path.join(d, name + ".rgb565"), "rb").read()
            diff = [(i % r.SIZE, i // r.SIZE) for i in range(r.SIZE * r.SIZE) if want[2 * i:2 * i + 2] != got[2 * i:2 * i + 2]]
            bad += bool(diff)
            print(f"  {name}: {r.SIZE * r.SIZE - len(diff)} of {r.SIZE * r.SIZE} equal" + (f"; first {diff[:12]}" if diff else ""))
        return 1 if bad else 0
    if "--check" in argv:
        committed = open(K_REF, encoding="utf-8").read().replace("\r\n", "\n")
        same = committed == t
        print(f"{os.path.relpath(K_REF, r.ROOT)}: {'identical' if same else 'DIFFERS'} on regeneration")
        return 0 if same else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
