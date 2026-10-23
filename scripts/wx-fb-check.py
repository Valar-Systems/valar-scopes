"""WD5e's analysis (docs/wx-device.md, WD5 addendum 4), committed before any capture: grades /diag/fb frames of the
radar with the weather layer on.

    python -I scripts/wx-fb-check.py --look A|B <frame.raw> [...]    raw = the /diag/fb body: RGB565, big-endian
    python -I scripts/wx-fb-check.py --selftest                      the synthetic cases below, each with one verdict

Exit: 0 PASS, 1 FAIL, 2 UNTRUSTWORTHY (a frame's rings are not where the source draws them: a torn or mid-draw
frame, or the wrong geometry), 3 BLIND (no frame put the thing being checked over weather), 4 rig.

Every colour and the ring geometry are READ FROM THE SOURCE, never transcribed: the rain table, its rounding and
the dither colours from src/weatherscope/WxPalette.h, the looks' percentages from include/WxS2.h, the planes'
colours and the rings from src/AircraftManager.cpp. The rings are rasterised with LovyanGFX's own drawCircle
(LGFXBase.cpp:230-262 in this env's libdeps), ported line for line.

THE TWO CHECKS, as defined here (addendum 4 states why the definitions are these):
  (1) rings over weather. A ring pixel is an EXCEPTION when its colour is a weather colour of the look, is not the
      ring's colour, and some 3 x 3 block containing it is entirely that colour: weather drawn over the ring. A
      1 px trail or a text stroke crossing the ring never fills a 3 x 3 block, so it is not an exception.
      EXERCISED: ring pixels with weather 2 px inside AND 2 px outside them. BLIND: ring pixels whose ring colour
      IS a weather colour of the look, with that colour on both sides (look B's code 2 on the middle ring).
  (2) outlines (look A). A MARKER is an 8-connected run of one plane colour (scaleColor of a plane colour,
      f = 0.30-1.0, RGB565) of at least 8 px that contains a 2 x 2 block of it; plane colours that are also
      weather colours of the look are left out and counted as ambiguous. A marker FAILS when any of its
      8-neighbours has a weather colour of the look. EXERCISED: markers with a weather pixel 2-3 px out.
"""
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W = H = 240


def read(rel):
    return open(os.path.join(ROOT, rel), encoding="utf-8", newline="").read()


def s565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def triples(text):
    return [tuple(int(v) for v in m) for m in re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", text)]


def load_source():
    pal = read("src/weatherscope/WxPalette.h")
    m = re.search(r"kRainTable\[12\]\s*=\s*\{(.*?)\};", pal, re.S)
    table = triples(m.group(1))
    one = lambda name: triples(re.search(name + r"\s*=\s*(\{[^}]*\})", pal).group(1))[0]
    rain75, nocov, hatch = one("kRain75"), one("kNoCoverage"), one("kMissingHatch")
    assert re.search(r"Dim\(uint8_t v, int pct\) \{ return \(uint8_t\)\(\(v \* pct \+ 50\) / 100\); \}", pal), \
        "wxpalette::Dim is not (v * pct + 50) / 100 any more: this script's rounding must follow it"
    s2 = read("include/WxS2.h")
    pct = {k: int(re.search(rf"kPercent{k} = (\d+);", s2).group(1)) for k in "AB"}
    am = read("src/AircraftManager.cpp")
    a = am.index("uint32_t altitudeColor(float altMeters)")
    planes = triples(am[a:am.index("\n}", a)].replace("color888(", "{").replace(");", "};"))
    b = am.index("noData ? lgfx::color888(")
    planes += triples(am[b:am.index(";", b)].replace("color888(", "{").replace(")", "}"))
    assert len(table) == 12 and len(planes) == 7, (len(table), len(planes))
    d = am.index("void AircraftManager::DrawRadarCircles(")
    body = am[d:am.index("\n}", d)]
    centre = re.search(r"CENTRE = SCREEN_SIZE_DIV_2 - 1;", body) and W // 2 - 1
    outer = re.search(r"OUTER = SCREEN_SIZE_DIV_2 - 1;", body) and W // 2 - 1
    assert centre and outer, "DrawRadarCircles' CENTRE/OUTER changed: re-derive"
    rings = []
    for expr, r, g, bl in re.findall(r"drawCircle\(CENTRE, CENTRE, ([^,]+), lgfx::color888\((\d+), (\d+), (\d+)\)\)", body):
        e = expr.replace("OUTER", str(outer)).replace("/", "//")
        assert re.fullmatch(r"[\d\s()*/+-]+", e), expr
        rings.append((eval(e), s565(int(r), int(g), int(bl))))
    assert len(rings) == 3, rings
    dim = lambda c, p: tuple((v * p + 50) // 100 for v in c)
    weather = {L: {s565(*dim(c, pct[L])) for c in table + [rain75]} | {s565(*nocov), s565(*hatch)} for L in "AB"}
    marker = set()
    for p in planes:
        for i in range(3000, 10001):
            f = i / 10000.0
            marker.add(s565(int(p[0] * f), int(p[1] * f), int(p[2] * f)))
    return {"centre": centre, "rings": rings, "weather": weather, "marker": marker}


def draw_circle(px, x, y, r):
    """LGFXBase::drawCircle, ported line for line."""
    def h(x0, y0, w):
        for k in range(w): px.add((x0 + k, y0))
    def v(x0, y0, n):
        for k in range(n): px.add((x0, y0 + k))
    if r <= 0:
        px.add((x, y)); return
    f, ddF_y, ddF_x, i, j = 1 - r, -(r << 1), 1, 0, -1
    while True:
        while f < 0:
            i += 1; ddF_x += 2; f += ddF_x
        ddF_y += 2; f += ddF_y
        h(x - i, y + r, i - j); h(x - i, y - r, i - j); h(x + j + 1, y - r, i - j); h(x + j + 1, y + r, i - j)
        v(x + r, y + j + 1, i - j); v(x + r, y - i, i - j); v(x - r, y - i, i - j); v(x - r, y + j + 1, i - j)
        j = i
        r -= 1
        if not (i < r): break


def fill_circle(px, x, y, r):
    """LGFXBase::fillCircle (LGFXBase.cpp:302-339), ported line for line: the selftest's markers."""
    def h(x0, y0, w):
        for k in range(w): px.add((x0 + k, y0))
    h(x - r, y, (r << 1) + 1)
    if r <= 0: return
    f, ddF_y, ddF_x, i = 1 - r, -(r << 1), 1, 0
    while True:
        ln = 0
        while f < 0:
            ddF_x += 2; f += ddF_x; ln += 1
        i += ln
        ddF_y += 2; f += ddF_y
        for jj in range(ln): h(x - r, y + i - ln + 1 + jj, (r << 1) + 1)
        h(x - i, y + r, (i << 1) + 1); h(x - i, y - r, (i << 1) + 1)
        for jj in range(ln): h(x - r, y - i + jj, (r << 1) + 1)
        r -= 1
        if not (i < r): break


def grade(frames, look, src):
    """frames: lists of W*H RGB565 values. Returns (verdict, report lines)."""
    Wc, M = src["weather"][look], src["marker"] - src["weather"][look]
    cx = cy = src["centre"]
    ring_px = []
    for r, col in src["rings"]:
        s = set(); draw_circle(s, cx, cy, r); ring_px.append((r, col, sorted(s)))
    tot = {"exc": 0, "ex_r": 0, "blind_r": 0, "markers": 0, "fail": 0, "ex_m": 0, "ambig": 0}
    out = []
    at = lambda f, x, y: f[y * W + x] if 0 <= x < W and 0 <= y < H else None
    for n, f in enumerate(frames):
        for r, col, pts in ring_px:
            on = sum(at(f, x, y) == col for x, y in pts)
            if on < 0.80 * len(pts):
                out.append(f"frame {n}: ring r={r}: only {on}/{len(pts)} pixels have the ring's colour -- not where the source draws it")
                return "UNTRUSTWORTHY", out
        exc = ex = bl = 0
        for r, col, pts in ring_px:
            for x, y in pts:
                c = at(f, x, y)
                th = math.atan2(y - cy, x - cx)
                ins = at(f, round(cx + (r - 2) * math.cos(th)), round(cy + (r - 2) * math.sin(th)))
                outs = at(f, round(cx + (r + 2) * math.cos(th)), round(cy + (r + 2) * math.sin(th)))
                if c == col and col in Wc and ins == col and outs == col:
                    bl += 1
                if ins in Wc and outs in Wc and ins != col and outs != col:
                    ex += 1
                if c in Wc and c != col and any(all(at(f, bx + i, by + k) == c for i in range(3) for k in range(3))
                                                 for bx in range(x - 2, x + 1) for by in range(y - 2, y + 1)):
                    exc += 1
        seen, mk, fail, exm, amb = set(), 0, 0, 0, 0
        for y in range(H):
            for x in range(W):
                c = f[y * W + x]
                if (x, y) in seen or c not in src["marker"]: continue
                comp, stack = set(), [(x, y)]
                while stack:
                    p = stack.pop()
                    if p in comp: continue
                    comp.add(p)
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            q = (p[0] + dx, p[1] + dy)
                            if q not in comp and at(f, *q) == c: stack.append(q)
                seen |= comp
                if len(comp) < 8 or not any({(a, b), (a + 1, b), (a, b + 1), (a + 1, b + 1)} <= comp for a, b in comp):
                    continue
                if c not in M:
                    amb += 1; continue
                mk += 1
                border = {(a + dx, b + dy) for a, b in comp for dx in (-1, 0, 1) for dy in (-1, 0, 1)} - comp
                wb = sum(at(f, *p) in Wc for p in border)
                ring2 = {(a + dx, b + dy) for a, b in comp for dx in range(-3, 4) for dy in range(-3, 4)} - comp - border
                if any(at(f, *p) in Wc for p in ring2): exm += 1
                if wb:
                    fail += 1
                    xs, ys = [p[0] for p in comp], [p[1] for p in comp]
                    out.append(f"frame {n}: marker at x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)} ({len(comp)} px): "
                               f"{wb} of {len(border)} neighbours are weather")
        out.append(f"frame {n}: rings exceptions {exc}, exercised {ex}, blind {bl}; markers {mk} "
                   f"(ambiguous {amb}), over weather {exm}, without an outline {fail}")
        for k, v in zip(("exc", "ex_r", "blind_r", "markers", "fail", "ex_m", "ambig"), (exc, ex, bl, mk, fail, exm, amb)):
            tot[k] += v
    out.append(f"TOTAL look {look}: (1) ring exceptions {tot['exc']}, exercised {tot['ex_r']}, blind {tot['blind_r']}; "
               f"(2) markers {tot['markers']}, over weather {tot['ex_m']}, without an outline {tot['fail']}, ambiguous {tot['ambig']}")
    r1 = "FAIL" if tot["exc"] else ("BLIND" if tot["ex_r"] == 0 else "PASS")
    r2 = "n/a" if look != "A" else ("FAIL" if tot["fail"] else ("BLIND" if tot["ex_m"] == 0 else "PASS"))
    out.append(f"WD5e(1) {r1}   WD5e(2) {r2}")
    if "FAIL" in (r1, r2): return "FAIL", out
    if "BLIND" in (r1, r2): return "BLIND", out
    return "PASS", out


def synth(src, look, weather_over_rings=False, outline="offsets", rings=True, weather=True, trail=False):
    """A synthetic frame drawn in the device's order: weather, rings, trail, outline, marker."""
    f = [0] * (W * H)
    put = lambda pts, c: [f.__setitem__(y * W + x, c) for x, y in pts if 0 <= x < W and 0 <= y < H]
    pct = 40 if look == "A" else 25
    rain = s565(*(((255 * pct + 50) // 100), 0, 0))           # code 8, red: far from every green
    assert rain in src["weather"][look]
    cx = src["centre"]
    blob = set(); fill_circle(blob, cx + src["rings"][1][0], cx, 30)  # across the middle ring at angle 0
    ring_pts = []
    for r, col in src["rings"]:
        s = set(); draw_circle(s, cx, cx, r); ring_pts.append((s, col))
    if weather and not weather_over_rings: put(blob, rain)
    if rings:
        for s, col in ring_pts: put(s, col)
    if weather and weather_over_rings: put(blob, rain)
    mx, my = cx + src["rings"][1][0] + 8, cx + 6                # 8 px outside the middle ring, inside the blob
    colour = next(c for c in sorted(src["marker"] - src["weather"][look]) if c > 0x8000)
    if trail: put([(mx - 20 + k, my + 12) for k in range(18)], colour)
    if outline == "offsets":
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                if ox or oy:
                    s = set(); fill_circle(s, mx + ox, my + oy, 3); put(s, 0)
    elif outline == "disc4":
        s = set(); fill_circle(s, mx, my, 4); put(s, 0)
    s = set(); fill_circle(s, mx, my, 3); put(s, colour)
    return f


def selftest(src):
    cases = [
        ("as drawn, look A",                 dict(look="A"),                          "A", "PASS"),
        ("weather drawn over the rings",     dict(look="A", weather_over_rings=True), "A", "FAIL"),
        ("no outline, look A",               dict(look="A", outline="none"),          "A", "FAIL"),
        ("the old r=4 disc outline",         dict(look="A", outline="disc4"),         "A", "FAIL"),
        ("a 1 px trail in the marker colour", dict(look="A", trail=True),             "A", "PASS"),
        ("no rings (a mid-draw frame)",      dict(look="A", rings=False),             "A", "UNTRUSTWORTHY"),
        ("no weather on the screen",         dict(look="A", weather=False),           "A", "BLIND"),
        ("look B, no outline (as shipped B)", dict(look="B", outline="none"),         "B", "PASS"),
    ]
    bad = 0
    for label, kw, look, want in cases:
        got, lines = grade([synth(src, **kw)], look, src)
        ok = got == want
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {label}: {got} (want {want}) -- {lines[-1]}")
    print("WX FB CHECK SELFTEST PASSED" if not bad else f"WX FB CHECK SELFTEST: {bad} NOT AS PREDICTED")
    return 0 if not bad else 1


def main(argv):
    try:
        src = load_source()
    except (AssertionError, AttributeError, ValueError, OSError) as e:
        print(f"RIG: could not read the source: {e}"); return 4
    if argv[1:2] == ["--selftest"]:
        return selftest(src)
    if len(argv) < 4 or argv[1] != "--look" or argv[2] not in ("A", "B"):
        print(__doc__); return 4
    frames = []
    for p in argv[3:]:
        b = open(p, "rb").read()
        if len(b) != W * H * 2:
            print(f"RIG: {p} is {len(b)} bytes, not a {W} x {H} RGB565 frame"); return 4
        frames.append([(b[2 * i] << 8) | b[2 * i + 1] for i in range(W * H)])
    verdict, lines = grade(frames, argv[2], src)
    print("\n".join(lines))
    print(verdict)
    return {"PASS": 0, "FAIL": 1, "UNTRUSTWORTHY": 2, "BLIND": 3}[verdict]


if __name__ == "__main__":
    sys.exit(main(sys.argv))
