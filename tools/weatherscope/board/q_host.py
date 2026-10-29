"""Build Q's host side (docs/weatherscope-stage3.md A10.10 Q30, A12.25): the counts its predictions and B2-Bend's bound
are made from -- counted here, not estimated. Prints no coordinate.

    python -I tools/weatherscope/board/q_host.py [--json <file>]

  disc     the on-disc pixels in the map rows 24-193 by the pixel-centre test: the most dry pixels a frame can read
           (A10.3's 36,828; recounted here as the counter's control)
  reads    each Dallas loop frame at 60 mi, oldest first (the device's history order): its dry on-disc map pixels,
           the probe's reads in that frame
  bend     each embedded Bend crop, in WxBendFrames.S's order (sorted names, so the 15 frames then synthetic-clear):
           bytes, cells, nonzero cells, and the FNV-1a 32 of its decoded codes -- the board's decode must match
  peaks    the distinct pixels the 4 placed peaks write at Bend 60 mi (glyph, label and its halo), on the dry scene and
           the rain scene: an upper bound on what the peaks add to the layer's replay, since the layer keeps one entry
           a pixel and replays each once (WxScreens.h Replay)
"""
import importlib.util, json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))
    spec.loader.exec_module(m)
    return m


look = _load("look35", os.path.join(HERE, "..", "screens", "look35.py"))
r, geo35 = look.r, look.geo35
TOP, BOT = r.TOP_BAND, r.BOT_BAND


def fnv1a32(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def on_disc(x, y):
    return (x + 0.5 - r.C) ** 2 + (y + 0.5 - r.C) ** 2 <= r.C * r.C


def code_grid(h, codes, view):
    """ref35.render_map's code at each pixel (14 off the crop), None off the disc."""
    out = [[None] * r.SIZE for _ in range(r.SIZE)]
    for y in range(r.SIZE):
        lat = view.home[0] + (r.C - (y + 0.5)) / r.C * view.rLat
        rr = int(math.floor((h["latNw"] - lat) / h["cellLat"] + 0.5))
        for x in range(r.SIZE):
            if not on_disc(x, y):
                continue
            lon = view.home[1] + ((x + 0.5) - r.C) / r.C * view.rLon
            c = int(math.floor((lon - h["lonNw"]) / h["cellLon"] + 0.5))
            out[y][x] = codes[rr * h["cols"] + c] if 0 <= rr < h["rows"] and 0 <= c < h["cols"] else 14
    return out


def dry_reads(h, codes, view):
    g = code_grid(h, codes, view)
    return sum(1 for y in range(TOP, BOT) for x in range(r.SIZE) if g[y][x] == 0)


class _Rec:
    on = False
    armed = False
    got = set()


class _RecRow(list):
    def __init__(self, items, y):
        super().__init__(items)
        self.y = y

    def __setitem__(self, i, v):
        if _Rec.on:
            _Rec.got.add((i, self.y))
        list.__setitem__(self, i, v)


class _RecScreen(r.Screen):
    def __init__(self, bg=r.BLACK):
        super().__init__(bg)
        self.px = [_RecRow(row, y) for y, row in enumerate(self.px)]

    def text(self, *a, **k):
        if _Rec.armed:   # the text right after a peak's glyph is that peak's label (compose's order)
            _Rec.on = True
            try:
                return super().text(*a, **k)
            finally:
                _Rec.on = _Rec.armed = False
        return super().text(*a, **k)


def _glyph(scr, x, y, col):
    _Rec.on = True
    try:
        geo35.glyph_p(scr, x, y, col)
    finally:
        _Rec.on = False
        _Rec.armed = True


def peak_pixels(scene):
    """(distinct pixels the peaks write, the peaks' placed names, the control's verdict)."""
    _Rec.got, _Rec.on, _Rec.armed = set(), False, False
    saved = r.Screen
    r.Screen = _RecScreen
    try:
        h, codes = r.decode(os.path.join(look.BEND, "synthetic-clear.wxc" if scene == "dry" else "shsr-20240602-200600.wxc"))
        view = r.View(h, 60)
        img, peaks, places = look.package_scene(view)
        base = look.colours(img, look.THEMES["dark"])
        line, col = r.state_rule(h, codes, 0)
        clock = r.clock_text(h["epoch"], look.OFFSET_PT)
        state = dict(line=line, colour=col, timeText=clock, timeInk=r.DIMTXT)
        plog = []
        scr, _ = r.compose(h, codes, view, state, places, lambda t: t[3], clock, r.Rounder("P"), base=base, peaks=peaks,
                           glyph=_glyph, peak_log=plog, pal=r.PAL, ink=r.DARK_INK)
        got = set(_Rec.got)
    finally:
        r.Screen = saved
    # the control: the peaks' own surviving pixels, found independently by drawing them in a sentinel ink, must all be
    # among the recorded writes (else the recorder missed the peaks' writes and its count is no bound)
    ink2 = dict(r.DARK_INK, peak=(1, 2, 3))
    scr2, _ = r.compose(h, codes, view, state, places, lambda t: t[3], clock, r.Rounder("P"), base=base, peaks=peaks,
                        glyph=geo35.glyph_p, peak_log=[], pal=r.PAL, ink=ink2)
    sentinel = {(x, y) for y in range(r.SIZE) for x in range(r.SIZE) if scr2.px[y][x] == (1, 2, 3)}
    return got, [n for n, _, _ in plog], sentinel


def main(argv):
    out = {}
    disc = sum(1 for y in range(TOP, BOT) for x in range(r.SIZE) if on_disc(x, y))
    out["disc"] = disc
    print(f"disc: {disc} on-disc pixels in rows {TOP}-{BOT - 1}" + ("" if disc == 36828 else "  <-- NOT A10.3's 36,828"))

    names = sorted(n for n in os.listdir(r.LOOP) if n.startswith("shsr-") and n.endswith(".wxc"))
    reads = []
    for n in names:
        h, codes = r.decode(os.path.join(r.LOOP, n))
        reads.append(dry_reads(h, codes, r.View(h, 60)))
    out["reads"] = reads
    print(f"reads: Dallas loop at 60 mi, {len(reads)} frames: {reads} (min {min(reads)}, max {max(reads)})")

    bend = []
    for n in sorted(f for f in os.listdir(look.BEND) if f.endswith(".wxc")):
        p = os.path.join(look.BEND, n)
        h, codes = r.decode(p)
        bend.append(dict(name=n, bytes=os.path.getsize(p), cells=h["rows"] * h["cols"], nz=sum(1 for c in codes if c),
                         fnv="%08x" % fnv1a32(bytes(codes))))
    out["bend"] = bend
    for i, b in enumerate(bend):
        print(f"bend {i:2d} {b['name']:28s} bytes={b['bytes']:5d} cells={b['cells']} nz={b['nz']:5d} fnv={b['fnv']}")

    ok = disc == 36828
    pk = {}
    for scene in ("dry", "rain"):
        got, names_p, sentinel = peak_pixels(scene)
        ctl = bool(sentinel) and sentinel <= got
        ok &= ctl
        pk[scene] = dict(pixels=len(got), peaks=names_p, sentinel=len(sentinel), control=ctl)
        print(f"peaks {scene}: {len(got)} distinct pixels written by {names_p}; CONTROL: the {len(sentinel)} surviving "
              f"peak-ink pixels {'are all among them' if ctl else 'ARE NOT all among them -- the count is no bound'}")
    same = pk["dry"]["pixels"] == pk["rain"]["pixels"] and pk["dry"]["peaks"] == pk["rain"]["peaks"]
    ok &= same
    out["peaks"] = pk
    print(f"peaks: dry and rain {'agree' if same else 'DISAGREE'}")
    if "--json" in argv:
        json.dump(out, open(argv[argv.index("--json") + 1], "w"), indent=1)
    print("q_host: " + ("ok" if ok else "A CONTROL FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
