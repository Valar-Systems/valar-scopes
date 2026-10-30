"""WD5e/WD5f's analysis (docs/wx-device.md, WD5 addenda 4 and 5), committed before any capture: grades /diag/fb
frames of the radar, with the weather layer on (WD5e) or off (WD5f's marker clause).

    python -I scripts/wx-fb-check.py --look A|B [--drop-untrustworthy] <frame.raw> [...]   layer on: WD5e(1), (2)
    python -I scripts/wx-fb-check.py --off [--drop-untrustworthy] <frame.raw> [...]         layer off: WD5f, R5
    python -I scripts/wx-fb-check.py --selftest                                             synthetic cases below
    raw = the /diag/fb body: RGB565, big-endian, 240 x 240

Exit: 0 PASS, 1 FAIL, 2 UNTRUSTWORTHY (a frame's rings are not where the source draws them: a torn or mid-draw
frame, or the wrong geometry), 3 BLIND (no frame put the thing being checked where it can be seen), 4 rig.
--drop-untrustworthy (for captures taken before #480's finished-frame /diag/fb): an UNTRUSTWORTHY frame is dropped,
counted and reported, and the rest are graded together; with none left the verdict is UNTRUSTWORTHY.

Every colour and the ring geometry are READ FROM THE SOURCE, never transcribed: the rain table, its rounding and
the dither colours from src/weatherscope/WxPalette.h, the looks' percentages from include/WxS2.h, the planes'
colours, the labels' colour and the rings from src/AircraftManager.cpp. The rings are rasterised with LovyanGFX's
own drawCircle (LGFXBase.cpp:230-262 in this env's libdeps), ported line for line.

THE CHECKS, as ruled (R3-R5, 2026-10-26; addendum 5 records the definitions):
  A MARKER is an 8-connected run of one plane colour (scaleColor of a plane colour, f = 0.30-1.0, RGB565) of at
  least 8 px that contains a 2 x 2 block of it. A ring pixel (ported drawCircle) that shows its ring's colour is
  never part of a marker: the outer ring's colour is also a plane colour. AMBIGUOUS, counted and not graded: plane
  colours that are also weather colours of the look (R4), and plane colours that are also label colours
  (DrawAircraftInfo's colour at 0.30-1.0), since the glcd glyphs * @ g p q pass the marker filter on their own.
  (1) rings over weather (R3). A ring is UNGRADABLE in a look when its colour is a weather colour of that look; it
      is left out and named. On a gradable ring, a pixel is an EXCEPTION when its colour is a weather colour of the
      look, not the ring's, and some 3 x 3 block containing it is entirely that colour. EXERCISED: a gradable ring
      pixel with a weather colour within 2 px (its 5 x 5 block). BLIND when none is exercised.
  (2) outlines, look A (R4). A marker FAILS when any of its 8-neighbours has a weather colour of the look.
      EXERCISED: a marker with a weather pixel 2-3 px out. BLIND when none is.
  (3) the layer off (R5). A marker is EXERCISED when a ring pixel is among its 8-neighbours, and FAILS when any such
      ring pixel is black (0x0000): an outline drawn while no layer is shown. BLIND when none is exercised.

THE WEDGE MASK (ruled 2026-10-28), for the layer-on arms only. Look 4's sweep draws colours over the weather that are
not weather colours, so (1) and (2) are graded OUTSIDE its wedge; the wedge's interior is W3's, on the host. The beam
is located from each frame: exactly one blob of the head colour at the disc's edge (117-123 px out), and >= 70 % of
the beam line's 101 ray samples in its colour; anything else makes the frame UNTRUSTWORTHY, never a pass. Masked: the
centre (12 px) and the 1.0 rad tail behind the beam, plus 0.06 rad each side. Outside the mask, a ring pixel showing a
wedge-only colour (a lit colour or a wedge green that is no other colour the frame can hold) is an exception.

TWO THINGS THE RADAR DRAWS ON PURPOSE, set apart as ruled (2026-10-30; docs/wx-device.md, "The checker's two gaps"):
  THE NEAR/HIGH/FAST HIGHLIGHT is drawn in 0xF81F, which is also the magenta rain level lit to full, a wedge-only
      colour. In (1) a 0xF81F ring pixel outside the mask is counted as HIGHLIGHT, not an exception, when it lies on a
      highlight (7 +- 0.75 px from a centre with >= 20 of the frame's 0xF81F pixels at that distance and a plane
      colour on or beside it) or inside that highlight's tag box. By geometry, never by colour: any other 0xF81F
      outside the mask, a real lit magenta cell included, is still an exception.
  THE SCREEN DOTS (DrawScreenIndicator) sit at fixed positions inside the disc. A pixel inside a dot's fill_circle
      raster, while the whole raster shows that dot's colour, is no weather test's business, and a marker that lies
      wholly in such dots is not a marker. A pixel of the same colour outside a raster is never set apart.
  A MARKER TOUCHED ONLY BY LABEL-AND-WEATHER COLOURS (2): when every weather-coloured 8-neighbour of a marker is also
      a label colour (a later contact's label stroke, drawn over this one's outline), the marker is COUNTED APART and
      listed, never failed and never passed silently. THE COST, accepted and named: a real weather pixel in one of
      those label greens beside a dart is not seen as one.
"""
import glob
import json
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W = H = 240
# The screen dots as MEASURED on 10-29's real frames (docs/wx-device.md, F1), for the selftest's frames only. Never the
# source's positions: a selftest drawn from the same positions the checker grades against cannot see them move.
DOT_FIXTURE = os.path.join(ROOT, "test", "host", "fixtures", "wx_screen_dots.json")


def fixture_dots():
    d = json.load(open(DOT_FIXTURE, encoding="utf-8"))
    return [(e["x"], e["y"], e["r"], int(e["colour"], 16)) for e in d["dots"]]


def read(rel):
    return open(os.path.join(ROOT, rel), encoding="utf-8", newline="").read()


def s565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def triples(text):
    return [tuple(int(v) for v in m) for m in re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", text)]


def faded(c):
    return {s565(int(c[0] * f), int(c[1] * f), int(c[2] * f)) for f in (i / 10000.0 for i in range(3000, 10001))}


def load_source():
    pal = read("src/weatherscope/WxPalette.h")
    m = re.search(r"kRainTable\[12\]\s*=\s*\{(.*?)\};", pal, re.S)
    table = triples(m.group(1))
    one = lambda name: triples(re.search(name + r"\s*=\s*(\{[^}]*\})", pal).group(1))[0]
    rain75, nocov, hatch = one("kRain75"), one("kNoCoverage"), one("kMissingHatch")
    teal = one("kRain1")   # code 1, the ruled teal (A10.16), read like every other colour
    assert re.search(r"Dim\(uint8_t v, int pct\) \{ return \(uint8_t\)\(\(v \* pct \+ 50\) / 100\); \}", pal), \
        "wxpalette::Dim is not (v * pct + 50) / 100 any more: this script's rounding must follow it"
    s2 = read("include/WxS2.h")
    pct = {k: int(re.search(rf"kPercent{k} = (\d+);", s2).group(1)) for k in "AB"}
    # Light rain's own percent (WD7): the look's x kCode1Num / kCode1Den, half up. Read, and the rule's shape asserted,
    # so a change to WxS2.h cannot leave this script grading against the old colour.
    num = int(re.search(r"kCode1Num = (\d+);", s2).group(1))
    den = int(re.search(r"kCode1Den = (\d+);", s2).group(1))
    assert "return code == 1 ? (pct * kCode1Num + kCode1Den / 2) / kCode1Den : pct;" in s2, \
        "wxs2::CodePercent is not (pct * num + den / 2) / den any more: this script's rule must follow it"
    assert "return wxpalette::Dim(wxpalette::Rain(code), pct);" in s2, "wxs2::EchoAt no longer dims Rain(code): re-derive"
    c1pct = {k: (pct[k] * num + den // 2) // den for k in "AB"}
    am = read("src/AircraftManager.cpp")
    a = am.index("uint32_t altitudeColor(float altMeters)")
    planes = triples(am[a:am.index("\n}", a)].replace("color888(", "{").replace(");", "};"))
    b = am.index("noData ? lgfx::color888(")
    planes += triples(am[b:am.index(";", b)].replace("color888(", "{").replace(")", "}"))
    assert len(table) == 12 and len(planes) == 7, (len(table), len(planes))
    d = am.index("void AircraftManager::DrawAircraftInfo(")
    lab = re.search(r"setTextColor\(scaleColor\(lgfx::color888\((\d+), (\d+), (\d+)\), brightness\)\)",
                    am[d:am.index("\n}", d)])
    assert lab, "DrawAircraftInfo's label colour changed: re-derive"
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
    weather = {L: {s565(*dim(teal, c1pct[L]))} | {s565(*dim(c, pct[L])) for c in table[1:] + [rain75]} |
                  {s565(*nocov), s565(*hatch)} for L in "AB"}
    marker = set()
    for p in planes:
        marker |= faded(p)
    labels = faded(tuple(int(v) for v in lab.groups()))
    # Look 4's wedge (WD6, WD7; the wedge mask, ruled 2026-10-28), read from the source: what it can draw, and the
    # beam and head that locate it in a frame.
    sw = read("include/WxSweep.h")
    peak = int(re.search(r"constexpr int kPeak = (\d+);", sw).group(1))
    tail = float(re.search(r"constexpr float kTail = ([\d.]+)f;", sw).group(1))
    assert "return pct + ((100 - pct) * lvl + kPeak / 2) / kPeak;" in sw, "wxsweep::LitPercent changed: re-derive"
    assert "return (uint16_t)((lvl & 0xFC) << 3);" in sw, "wxsweep::Green565 changed: re-derive"
    assert "LitPercent(wxs2::CodePercent(code, pct), lvl)" in sw, "wxsweep::Lit565 changed: re-derive"
    dh = read("include/DrawHelpers.h")
    bm = re.search(r"drawLine\(cx, cy, xLead, yLead, lgfx::color888\((\d+), (\d+), (\d+)\)\)", dh)
    hd = re.search(r"fillCircle\(xLead, yLead, 2, lgfx::color888\((\d+), (\d+), (\d+)\)\)", dh)
    assert bm and hd, "DrawRadarSweepBeam's beam or head colour changed: re-derive"
    beam_c, head_c = s565(*map(int, bm.groups())), s565(*map(int, hd.groups()))
    base = {1: teal, **{c: table[c - 1] for c in range(2, 13)}, 13: rain75}
    greens = {(lvl & 0xFC) << 3 for lvl in range(1, peak + 1)}
    ring_cols = {col for _, col in rings}
    wedge_only = {}
    for L in "AB":
        lit = set()
        for c, b in base.items():
            p0 = c1pct[L] if c == 1 else pct[L]
            for lvl in range(1, peak + 1):
                lit.add(s565(*dim(b, p0 + ((100 - p0) * lvl + peak // 2) // peak)))
        wedge_only[L] = (lit | greens) - weather[L] - marker - labels - ring_cols - {beam_c, head_c, 0}
    # The NEAR/HIGH/FAST highlight (DrawAircraftInfo): its colour and geometry, asserted, so (1) sets it apart by shape.
    hlm = re.search(r"const uint32_t HL = lgfx::color888\((\d+), (\d+), (\d+)\);", am)
    assert hlm, "the highlight's colour changed: re-derive"
    for s in ("int tagY = y - 4;", "backbuffer.drawCircle(x, y, 7, HL);", "tagY -= 9;",
              "badge(tag.c_str(), x - (int)backbuffer.textWidth(tag) - 9, tagY, HL);",
              'highlight("NEAR");', 'highlight("HIGH");', 'highlight("FAST");'):
        assert s in am, f"the highlight's geometry changed ({s}): re-derive"
    # The tag box: three 4-letter tags of 6 px glcd glyphs (8 px tall) at x - width - 9, stacked up from y - 4 by 9.
    hl = {"colour": s565(*map(int, hlm.groups())), "r": 7, "x0": -(4 * 6 + 9), "x1": -9, "y0": -4 - 2 * 9, "y1": -4 + 8}
    # The screen dots (DrawScreenIndicator): one per screen a swipe reaches, Radar's (slot 0 here) the active one.
    hdr = read("src/AircraftManager.h")
    screens = [s.strip() for s in re.search(r"enum class Screen \{([^}]*)\};", hdr).group(1).split(",")]
    count = int(re.search(r"static constexpr int SCREEN_COUNT = (\d+);", hdr).group(1))
    assert len(screens) == count and screens[0] == "Radar", (screens, count)
    d = am.index("void AircraftManager::DrawScreenIndicator(")
    body = am[d:am.index("\n}", d)]
    for s in ("constexpr int cx = SCREEN_SIZE_DIV_2;", "const int y = SCREEN_SIZE - 16;",
              "const int shown = SCREEN_COUNT - 1;", "if ((Screen)i == Screen::Follow)",
              "backbuffer.fillCircle(cx - (shown - 1) * 6 + slot * 12, y, active ? 3 : 2,"):
        assert s in body, f"DrawScreenIndicator changed ({s}): re-derive"
    dc = re.search(r"active \? lgfx::color888\((\d+), (\d+), (\d+)\) : lgfx::color888\((\d+), (\d+), (\d+)\)\);", body)
    assert dc, "the screen dots' colours changed: re-derive"
    on_c, off_c = s565(*map(int, dc.groups()[:3])), s565(*map(int, dc.groups()[3:]))
    shown, dots = count - 1, []
    for i, name in enumerate(screens):
        if name == "Follow": continue
        x = W // 2 - (shown - 1) * 6 + len(dots) * 12
        dots.append((x, H - 16, 3, on_c) if name == "Radar" else (x, H - 16, 2, off_c))
    return {"centre": centre, "rings": rings, "weather": weather, "marker": marker, "label": labels,
            "tail": tail, "peak": peak, "beam": beam_c, "head": head_c, "wedge_only": wedge_only,
            "base": base, "pct": pct, "c1pct": c1pct, "hl": hl, "dots": dots}


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


def ring_geometry(src):
    cx = src["centre"]
    out = []
    for r, col in src["rings"]:
        s = set(); draw_circle(s, cx, cx, r); out.append((r, col, s))
    own = {}
    for r, col, s in out:
        for p in s: own.setdefault(p, col)
    return out, own


def at(f, x, y):
    return f[y * W + x] if 0 <= x < W and 0 <= y < H else None


def trustworthy(f, rings, n, out):
    for r, col, pts in rings:
        on = sum(at(f, x, y) == col for x, y in pts)
        if on < 0.80 * len(pts):
            out.append(f"frame {n}: ring r={r}: only {on}/{len(pts)} pixels have the ring's colour -- "
                       "not where the source draws it (UNTRUSTWORTHY)")
            return False
    return True


# The wedge mask (ruled 2026-10-28; docs/wx-device.md "Rulings on the wedge"), frozen before this code.
MASK_MARGIN = 0.06    # rad, each side of the wedge's 1.0 rad
MASK_CORE = 12        # px around the centre, where angles mean nothing
HEAD_RANGE = (117.0, 123.0)   # the head's centroid distance from the centre: the disc's edge
BEAM_MIN = 0.70       # the share of the beam's 101 ray samples (r = 10..110) that must show its colour
DISC_R = 120


def wedge_mask(f, src, n, out):
    """Look 4's wedge in frame f, located from the frame itself. (in_mask, share), or None: UNTRUSTWORTHY."""
    cx = src["centre"]
    hs = {(i % W, i // W) for i, c in enumerate(f) if c == src["head"]}
    comps, seen = [], set()
    for p in hs:
        if p in seen: continue
        comp, stack = set(), [p]
        while stack:
            q = stack.pop()
            if q in comp: continue
            comp.add(q)
            stack.extend((q[0] + dx, q[1] + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)
                         if (q[0] + dx, q[1] + dy) in hs and (q[0] + dx, q[1] + dy) not in comp)
        seen |= comp
        comps.append(comp)
    if len(comps) != 1:
        out.append(f"frame {n}: the beam's head: {len(comps)} blobs, not exactly one (UNTRUSTWORTHY)")
        return None
    hx = sum(p[0] for p in comps[0]) / len(comps[0])
    hy = sum(p[1] for p in comps[0]) / len(comps[0])
    d = math.hypot(hx - cx, hy - cx)
    if not HEAD_RANGE[0] <= d <= HEAD_RANGE[1]:
        out.append(f"frame {n}: the beam's head is {d:.1f} px from the centre, not at the disc's edge (UNTRUSTWORTHY)")
        return None
    th = math.atan2(hy - cx, hx - cx)
    hits = sum(any(at(f, round(cx + math.cos(th) * r) + i, round(cx + math.sin(th) * r) + k) == src["beam"]
                   for i in (-1, 0, 1) for k in (-1, 0, 1)) for r in range(10, 111))
    if hits < BEAM_MIN * 101:
        out.append(f"frame {n}: the beam line agrees with its head on {hits}/101 samples, under 70 % (UNTRUSTWORTHY)")
        return None
    lo, span = th - src["tail"] - MASK_MARGIN, src["tail"] + 2 * MASK_MARGIN

    def in_mask(x, y):
        dx, dy = x - cx, y - cx
        if dx * dx + dy * dy <= MASK_CORE * MASK_CORE: return True
        return (math.atan2(dy, dx) - lo) % (2 * math.pi) <= span

    disc = [(x, y) for y in range(H) for x in range(W) if (x - cx) ** 2 + (y - cx) ** 2 <= DISC_R * DISC_R]
    share = sum(in_mask(x, y) for x, y in disc) / len(disc)
    out.append(f"frame {n}: the beam at {th:+.3f} rad, its head {d:.1f} px out, beam hits {hits}/101, "
               f"masked share {share * 100:.2f} %")
    return in_mask, share


def markers(f, colours, own, ring_exclusion=True):
    """(colour, pixels) of every marker in frame f. A ring pixel showing its ring's colour is never part of one."""
    skip = (lambda p, c: own.get(p) == c) if ring_exclusion else (lambda p, c: False)
    seen = set()
    for y in range(H):
        for x in range(W):
            c = f[y * W + x]
            if (x, y) in seen or c not in colours or skip((x, y), c): continue
            comp, stack = set(), [(x, y)]
            while stack:
                p = stack.pop()
                if p in comp: continue
                comp.add(p)
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        q = (p[0] + dx, p[1] + dy)
                        if q not in comp and at(f, *q) == c and not skip(q, c): stack.append(q)
            seen |= comp
            if len(comp) >= 8 and any({(a, b), (a + 1, b), (a, b + 1), (a + 1, b + 1)} <= comp for a, b in comp):
                yield c, comp


def neighbours(comp, k=1):
    return {(a + dx, b + dy) for a, b in comp for dx in range(-k, k + 1) for dy in range(-k, k + 1)} - comp


def dot_pixels(f, src):
    """The pixels of every COMPLETE screen dot in frame f: its fill_circle raster, only while all of it is its colour."""
    out = set()
    for x, y, r, c in src["dots"]:
        s = set(); fill_circle(s, x, y, r)
        if all(at(f, *p) == c for p in s): out |= s
    return out


def highlight_centres(f, src):
    """The highlights' centres in frame f, found as rp6_cause1.py finds them: >= 20 of the frame's highlight-coloured
    pixels at 7 +- 0.75 px, and a plane colour on or beside the centre."""
    hc, r = src["hl"]["colour"], src["hl"]["r"]
    mag = [(i % W, i // W) for i, c in enumerate(f) if c == hc]
    if not mag: return []
    ring = [(i, j) for i in range(-r - 1, r + 2) for j in range(-r - 1, r + 2) if abs(math.hypot(i, j) - r) <= 0.75]
    votes = {}
    for px, py in mag:
        for i, j in ring:
            votes[(px + i, py + j)] = votes.get((px + i, py + j), 0) + 1
    return [q for q, k in votes.items() if k >= 20 and
            any(at(f, q[0] + i, q[1] + j) in src["marker"] for i in (-1, 0, 1) for j in (-1, 0, 1))]


def in_highlight(p, centres, src):
    """On a highlight's circle, or inside its tag box."""
    h = src["hl"]
    return any(abs(math.hypot(p[0] - q[0], p[1] - q[1]) - h["r"]) <= 0.75 or
               (q[0] + h["x0"] <= p[0] < q[0] + h["x1"] and q[1] + h["y0"] <= p[1] < q[1] + h["y1"]) for q in centres)


def grade(frames, look, src, drop=False):
    """Layer on: WD5e(1) and (2). frames: lists of W*H RGB565 values. Returns (verdict, report lines)."""
    Wc = src["weather"][look]
    amb_set = (src["marker"] & Wc) | (src["marker"] & src["label"])
    rings, own = ring_geometry(src)
    ungradable = [(r, col) for r, col, _ in rings if col in Wc]
    out = [f"look {look}: ungradable rings (the ring's colour is a weather colour of the look): "
           + (", ".join(f"r={r} {col:#06x}" for r, col in ungradable) or "none")]
    WO = src["wedge_only"][look]
    tot = {"exc": 0, "ex_r": 0, "markers": 0, "fail": 0, "ex_m": 0, "ambig": 0, "graded": 0, "dropped": 0,
           "masked_r": 0, "masked_m": 0, "wedge_out": 0, "hl": 0, "apart": 0, "dotm": 0,
           "a_dot": 0, "a_label": 0, "a_hl": 0}
    for n, f in enumerate(frames):
        wm = wedge_mask(f, src, n, out) if trustworthy(f, rings, n, out) else None
        if wm is None:
            if not drop: return "UNTRUSTWORTHY", out
            tot["dropped"] += 1
            continue
        in_mask, _share = wm
        tot["graded"] += 1
        dots = dot_pixels(f, src)
        centres = None   # the highlights, found only if a ring pixel needs them
        exc = ex = mr = wout = hl = 0
        for r, col, pts in rings:
            if col in Wc: continue
            for x, y in pts:
                if in_mask(x, y):          # the wedge's interior is W3's, on the host, not the captures'
                    mr += 1; continue
                c = at(f, x, y)
                if any(at(f, x + i, y + k) in Wc for i in range(-2, 3) for k in range(-2, 3) if i or k):
                    ex += 1
                if c in Wc and c != col and any(all(at(f, bx + i, by + k) == c for i in range(3) for k in range(3))
                                                 for bx in range(x - 2, x + 1) for by in range(y - 2, y + 1)):
                    exc += 1
                elif c in WO and c != col:  # a wedge colour outside the wedge: the mask is wrong, or a ring is
                    if c == src["hl"]["colour"]:
                        if centres is None: centres = highlight_centres(f, src)
                        if in_highlight((x, y), centres, src):
                            hl += 1; continue   # the NEAR/HIGH/FAST highlight, by its geometry (ruled 2026-10-30)
                    exc += 1; wout += 1
        mk = fail = exm = amb = mm = apart = dotm = 0
        by = {"screen dot": 0, "label": 0, "highlight": 0}   # highlight: 0xF81F is no weather colour, so 0 by construction
        for c, comp in markers(f, src["marker"], own):
            if comp <= dots:              # a screen dot, not a contact
                dotm += 1; continue
            if any(in_mask(*p) for p in comp | neighbours(comp, 3)):
                mm += 1; continue
            if c in amb_set:
                amb += 1; continue
            mk += 1
            whole = neighbours(comp)
            border = whole - dots
            dot_wet = [p for p in whole & dots if at(f, *p) in Wc]   # weather-coloured, but a complete screen dot's
            wet = [p for p in border if at(f, *p) in Wc]
            if any(at(f, *p) in Wc for p in neighbours(comp, 3) - whole - dots): exm += 1
            xs, ys = [p[0] for p in comp], [p[1] for p in comp]
            where = f"marker at x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)} ({len(comp)} px)"
            if wet and not all(at(f, *p) in src["label"] for p in wet):
                fail += 1
                out.append(f"frame {n}: {where}: {len(wet)} of {len(border)} neighbours are weather")
                continue
            # Passed only because an exclusion took its weather-coloured neighbours: counted apart, by cause, and
            # listed, so "0 without an outline" never hides a marker an exclusion passed (ruled 2026-10-30).
            causes = []
            if dot_wet: causes.append("screen dot")
            if wet: causes.append("label")
            if causes:
                apart += 1
                for k in causes: by[k] += 1
                out.append(f"frame {n}: COUNTED APART ({' + '.join(causes)}), {where}: "
                           f"{len(dot_wet)} screen-dot and {len(wet)} label-and-weather neighbours")
        out.append(f"frame {n}: rings exceptions {exc} (a wedge colour outside the wedge {wout}), highlight {hl}, "
                   f"exercised {ex}, masked {mr}; markers {mk} (ambiguous {amb}, masked {mm}, screen dots {dotm}), "
                   f"over weather {exm}, without an outline {fail}, counted apart {apart} (screen dot "
                   f"{by['screen dot']}, label {by['label']}, highlight {by['highlight']})")
        for k, v in zip(("exc", "ex_r", "markers", "fail", "ex_m", "ambig", "masked_r", "masked_m", "wedge_out",
                         "hl", "apart", "dotm", "a_dot", "a_label", "a_hl"),
                        (exc, ex, mk, fail, exm, amb, mr, mm, wout, hl, apart, dotm,
                         by["screen dot"], by["label"], by["highlight"])):
            tot[k] += v
    out.append(f"TOTAL look {look}: frames graded {tot['graded']}, dropped as UNTRUSTWORTHY {tot['dropped']}; "
               f"(1) ring exceptions {tot['exc']} (a wedge colour outside the wedge {tot['wedge_out']}), "
               f"highlight {tot['hl']}, exercised {tot['ex_r']}, masked {tot['masked_r']}; "
               f"(2) markers {tot['markers']}, over weather {tot['ex_m']}, without an outline {tot['fail']}, "
               f"counted apart {tot['apart']} (screen dot {tot['a_dot']}, label {tot['a_label']}, highlight "
               f"{tot['a_hl']}), ambiguous {tot['ambig']}, masked {tot['masked_m']}, screen dots {tot['dotm']}")
    if tot["graded"] == 0:
        out.append("no trustworthy frame"); return "UNTRUSTWORTHY", out
    r1 = "FAIL" if tot["exc"] else ("BLIND" if tot["ex_r"] == 0 else "PASS")
    r2 = "n/a" if look != "A" else ("FAIL" if tot["fail"] else ("BLIND" if tot["ex_m"] == 0 else "PASS"))
    out.append(f"WD5e(1) {r1}   WD5e(2) {r2}")
    if "FAIL" in (r1, r2): return "FAIL", out
    if "BLIND" in (r1, r2): return "BLIND", out
    return "PASS", out


def grade_off(frames, src, drop=False, ring_exclusion=True):
    """Layer off: WD5f's marker clause, R5. Returns (verdict, report lines)."""
    amb_set = src["marker"] & src["label"]
    rings, own = ring_geometry(src)
    ring_all = set(own)
    out = []
    tot = {"markers": 0, "ex": 0, "fail": 0, "ambig": 0, "graded": 0, "dropped": 0}
    for n, f in enumerate(frames):
        if not trustworthy(f, rings, n, out):
            if not drop: return "UNTRUSTWORTHY", out
            tot["dropped"] += 1
            continue
        tot["graded"] += 1
        mk = ex = fail = amb = 0
        for c, comp in markers(f, src["marker"], own, ring_exclusion):
            if c in amb_set:
                amb += 1; continue
            mk += 1
            on_ring = neighbours(comp) & ring_all
            if not on_ring: continue
            ex += 1
            black = [p for p in on_ring if at(f, *p) == 0]
            if black:
                fail += 1
                xs, ys = [p[0] for p in comp], [p[1] for p in comp]
                out.append(f"frame {n}: marker at x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)} ({len(comp)} px): "
                           f"{len(black)} of its {len(on_ring)} ring neighbours are black")
        out.append(f"frame {n}: markers {mk} (ambiguous {amb}), touching a ring {ex}, with black ring neighbours {fail}")
        for k, v in zip(("markers", "ex", "fail", "ambig"), (mk, ex, fail, amb)):
            tot[k] += v
    out.append(f"TOTAL layer off: frames graded {tot['graded']}, dropped as UNTRUSTWORTHY {tot['dropped']}; markers "
               f"{tot['markers']} (ambiguous {tot['ambig']}), touching a ring {tot['ex']}, with black ring neighbours {tot['fail']}")
    if tot["graded"] == 0:
        out.append("no trustworthy frame"); return "UNTRUSTWORTHY", out
    v = "FAIL" if tot["fail"] else ("BLIND" if tot["ex"] == 0 else "PASS")
    out.append(f"WD5f(markers) {v}")
    return v, out


def glcd_glyph(ch):
    """A glyph of LovyanGFX's glcd font (the radar's labels), from this env's libdeps: {(x, y)} of its set pixels."""
    hits = sorted(glob.glob(os.path.join(ROOT, ".pio", "libdeps", "*", "LovyanGFX", "src", "lgfx", "Fonts", "glcdfont.h")))
    if not hits:
        raise OSError("no LovyanGFX glcdfont.h under .pio/libdeps (run pio run once)")
    t = open(hits[0], encoding="utf-8", errors="replace").read()
    vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})",
                                           re.search(r"font\s*\[\s*\]\s*(?:PROGMEM\s*)?=\s*\{(.*?)\};", t, re.S).group(1))]
    return {(x, y) for x, c in enumerate(vals[ord(ch) * 5: ord(ch) * 5 + 5]) for y in range(8) if (c >> y) & 1}


def put(f, pts, c):
    for x, y in pts:
        if 0 <= x < W and 0 <= y < H: f[y * W + x] = c


def draw_wedge(f, src, look, th, head="edge", beam_dtheta=0.0):
    """Look 4's wedge over the layer, then the beam and its head, as the device draws them (WxSweep.h,
    DrawRadarSweepBeam): each disc pixel up to kTail behind the beam takes its slice's level, a dry pixel the wedge
    green and an echo pixel (the synthetic rain, code 8) its colour raised toward 100 %. By angle, not by the
    device's triangles: the mask's margin is what covers the difference."""
    cx, tail, peak, pct = src["centre"], src["tail"], src["peak"], src["pct"][look]
    for y in range(H):
        for x in range(W):
            dx, dy = x - cx, y - cx
            if dx * dx + dy * dy > DISC_R * DISC_R or (dx == 0 and dy == 0): continue
            back = (th - math.atan2(dy, dx)) % (2 * math.pi)
            if back > tail: continue
            i = max(1, math.ceil(back / tail * 18))
            lvl = int((1.0 - i / 18.0) ** 2 * peak)
            if f[y * W + x] == 0:
                f[y * W + x] = (lvl & 0xFC) << 3
            else:
                p = pct + ((100 - pct) * lvl + peak // 2) // peak
                f[y * W + x] = s565(*((v * p + 50) // 100 for v in src["base"][8]))
    bx = cx + int(math.cos(th + beam_dtheta) * DISC_R)
    by = cx + int(math.sin(th + beam_dtheta) * DISC_R)
    steps = max(abs(bx - cx), abs(by - cx))
    put(f, {(round(cx + (bx - cx) * k / steps), round(cx + (by - cx) * k / steps)) for k in range(steps + 1)}, src["beam"])
    heads = {"edge": [(th, DISC_R)], "none": [], "two": [(th, DISC_R), (th + 1.5, DISC_R)], "r80": [(th, 80)]}[head]
    for a, r in heads:
        s = set(); fill_circle(s, cx + int(math.cos(a) * r), cx + int(math.sin(a) * r), 2); put(f, s, src["head"])


def synth(src, look, weather_over_rings=False, outline="offsets", rings=True, weather=True, trail=False,
          blob_ring=1, blob_r=30, blob_dx=0, glyph=None, beam=True, theta=math.pi, head="edge", beam_dtheta=0.0,
          plant=None, highlight=None, dots=None, stroke=None):
    """A layer-on frame drawn in the device's order: weather, the sweep (look 4's wedge, the beam and its head), rings,
    trail and label, outline, marker. The beam points west by default, away from every case's subject (east)."""
    f = [0] * (W * H)
    pct = 40 if look == "A" else 25
    rain = s565(*(((255 * pct + 50) // 100), 0, 0))           # code 8, red: far from every green
    assert rain in src["weather"][look]
    cx = src["centre"]
    blob = set(); fill_circle(blob, cx + src["rings"][blob_ring][0] + blob_dx, cx, blob_r)
    ring_pts = []
    for r, col in src["rings"]:
        s = set(); draw_circle(s, cx, cx, r); ring_pts.append((s, col))
    if weather and not weather_over_rings: put(f, blob, rain)
    if beam: draw_wedge(f, src, look, theta, head, beam_dtheta)
    if rings:
        for s, col in ring_pts: put(f, s, col)
    if weather and weather_over_rings: put(f, blob, rain)
    if plant:
        # A missing ring pixel showing a wedge-only colour: on the middle ring, outside the mask (north) or inside it.
        a = -math.pi / 2 if plant == "outside" else theta - 0.5
        r1 = src["rings"][1][0]
        p = min(ring_pts[1][0], key=lambda q: (q[0] - (cx + math.cos(a) * r1)) ** 2 + (q[1] - (cx + math.sin(a) * r1)) ** 2)
        put(f, [p], min(src["wedge_only"][look]))
    mx, my = cx + src["rings"][1][0] + 8, cx + 6                # 8 px outside the middle ring, inside the blob
    colour = next(c for c in sorted(src["marker"] - src["weather"][look] - src["label"]) if c > 0x8000)
    if trail: put(f, [(mx - 20 + k, my + 12) for k in range(18)], colour)
    if glyph:
        lc = min(src["label"] & src["marker"] - src["weather"][look])
        put(f, {(cx + src["rings"][1][0] - 10 + x, cx - 15 + y) for x, y in glcd_glyph(glyph)}, lc)
    if outline == "offsets":
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                if ox or oy:
                    s = set(); fill_circle(s, mx + ox, my + oy, 3); put(f, s, 0)
    elif outline == "disc4":
        s = set(); fill_circle(s, mx, my, 4); put(f, s, 0)
    s = set(); fill_circle(s, mx, my, 3); put(f, s, colour)
    r1, hc = src["rings"][1][0], src["hl"]["colour"]
    if stroke:
        # A later contact's label stroke across this marker's border: a label-and-weather green, or plain weather.
        sc = 0x0320 if stroke == "label" else rain
        assert sc in src["weather"][look] and (sc in src["label"]) == (stroke == "label"), "the stroke means nothing"
        put(f, [(mx + 4, my), (mx + 5, my + 1), (mx + 6, my + 2)], sc)
    if highlight == "lone":
        # One 0xF81F on the middle ring's top, outside the mask, with no highlight about it.
        put(f, [min(ring_pts[1][0], key=lambda q: (q[0] - cx) ** 2 + (q[1] - (cx - r1)) ** 2)], hc)
    elif highlight == "near":
        # An outlined dart on the middle ring's top, then its NEAR highlight (circle and tag), as DrawAircraftInfo does.
        qx, qy = cx + 10, cx - r1 + 2
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                if ox or oy:
                    s = set(); fill_circle(s, qx + ox, qy + oy, 3); put(f, s, 0)
        s = set(); fill_circle(s, qx, qy, 3); put(f, s, colour)
        circ = set(); draw_circle(circ, qx, qy, src["hl"]["r"])
        tag = {(qx + src["hl"]["x0"] + 6 * k + gx, qy - 4 + gy) for k, ch in enumerate("NEAR") for gx, gy in glcd_glyph(ch)}
        assert circ & ring_pts[1][0] and tag & ring_pts[1][0], "the highlight does not cross the ring: the case means nothing"
        put(f, circ, hc); put(f, tag, hc)
    if dots:
        # A dart whose border the slot-2 screen dot covers (the dots are drawn after the radar), then the dots: all
        # complete, or slot 2 as one pixel of its colour where the dot would be. Drawn from the measured fixture.
        fdots = fixture_dots()
        x2, y2, _r2, c2 = fdots[2]
        dx_, dy_ = x2, y2 - 6
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                if ox or oy:
                    s = set(); fill_circle(s, dx_ + ox, dy_ + oy, 3); put(f, s, 0)
        s = set(); fill_circle(s, dx_, dy_, 3); put(f, s, colour)
        for k, (x, y, r, c) in enumerate(fdots):
            if k == 2 and dots == "pixel":
                put(f, [(x, y - 2)], c); continue
            s = set(); fill_circle(s, x, y, r); put(f, s, c)
    return f


def synth_off(src, outline=False, rings=True, on_ring=True, label=False, outer_colour_far_black=False):
    """A layer-off frame in the device's order: black, rings, outline (only if drawn wrongly), marker, later draws."""
    f = [0] * (W * H)
    cx = src["centre"]
    geo, own = ring_geometry(src)
    if rings:
        for r, col, s in geo: put(f, s, col)
    if outer_colour_far_black:
        r0, col0, s0 = geo[0]
        assert col0 in src["marker"], "the outer ring's colour is no longer a plane colour: this case means nothing"
        p = min(s0, key=lambda q: (q[0] - (cx + 84)) ** 2 + (q[1] - (cx + 84)) ** 2)   # on the ring at ~45 degrees
        far = min(s0, key=lambda q: abs(math.hypot(q[0] - p[0], q[1] - p[1]) - 40))   # ~40 px along it
        s = set(); fill_circle(s, p[0], p[1], 3); put(f, s, col0)
        put(f, [far], 0)
        return f
    mx, my = (cx + src["rings"][1][0], cx) if on_ring else (cx + src["rings"][1][0] + 20, cx)
    colour = 0x07E0
    assert colour in src["marker"] and colour not in src["label"]
    if outline:
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                if ox or oy:
                    s = set(); fill_circle(s, mx + ox, my + oy, 3); put(f, s, 0)
    s = set(); fill_circle(s, mx, my, 3); put(f, s, colour)
    if label:
        other = next(c for c in range(0xF800, 0x10000) if c not in src["marker"] and all(c != col for _, col in src["rings"]))
        border = neighbours(s) & set(own)
        put(f, [min(border)], other)
    return f


def selftest(src):
    on = lambda kw, look: grade([synth(src, look=look, **kw)], look, src)
    off = lambda kw, excl=True: grade_off([synth_off(src, **kw)], src, ring_exclusion=excl)

    def share_case(look):
        out = []
        f = synth(src, look=look)
        wm = wedge_mask(f, src, 0, out)
        if wm is None: return "UNTRUSTWORTHY", out
        verdict, lines = grade([f], look, src)
        ok = 0.184 <= wm[1] <= 0.189 and verdict == "PASS"
        return ("PASS" if ok else "FAIL"), out + lines

    def apart_case(kw, want):
        verdict, lines = grade([synth(src, look="A", **kw)], "A", src)
        m = re.search(r"counted apart \d+ \(screen dot (\d+), label (\d+), highlight (\d+)\)",
                      next(l for l in lines if l.startswith("TOTAL")))
        got = {"screen dot": int(m.group(1)), "label": int(m.group(2)), "highlight": int(m.group(3))}
        full = {"screen dot": 0, "label": 0, "highlight": 0, **want}
        return (verdict if got == full else f"{verdict} with counted apart {got}"), lines
    cases = [
        ("as drawn, look A",                          lambda: on({}, "A"),                               "PASS"),
        ("weather drawn over the rings",              lambda: on(dict(weather_over_rings=True), "A"),    "FAIL"),
        ("no outline, look A",                        lambda: on(dict(outline="none"), "A"),             "FAIL"),
        ("the old r=4 disc outline",                  lambda: on(dict(outline="disc4"), "A"),            "FAIL"),
        ("a 1 px trail in the marker colour",         lambda: on(dict(trail=True), "A"),                 "PASS"),
        ("no rings (a mid-draw frame)",               lambda: on(dict(rings=False), "A"),                "UNTRUSTWORTHY"),
        ("no weather on the screen",                  lambda: on(dict(weather=False), "A"),              "BLIND"),
        ("look B, no outline (weather on the middle ring, ungradable in B)",
                                                      lambda: on(dict(outline="none"), "B"),             "BLIND"),
        ("look B, weather across the outer ring",     lambda: on(dict(outline="none", blob_ring=0), "B"), "PASS"),
        ("look A, weather on one side of a ring only", lambda: on(dict(blob_r=12, blob_dx=13), "A"),     "PASS"),
        ("look A, a label 'p' in a colour also a plane colour, over weather",
                                                      lambda: on(dict(glyph="p"), "A"),                  "PASS"),
        ("off: a marker on the middle ring, no outline", lambda: off({}),                                "PASS"),
        ("off: the same marker with an outline (R5's defect)", lambda: off(dict(outline=True)),          "FAIL"),
        ("off: no marker touches a ring",             lambda: off(dict(on_ring=False)),                  "BLIND"),
        ("off: a non-black label over a ring neighbour", lambda: off(dict(label=True)),                  "PASS"),
        ("off: no rings",                             lambda: off(dict(rings=False)),                    "UNTRUSTWORTHY"),
        ("off: outer-ring-coloured marker on the outer ring, a black ring pixel ~40 px away",
                                                      lambda: off(dict(outer_colour_far_black=True)),    "PASS"),
        ("off: CONTROL, the same frame with ring pixels allowed into markers",
                                                      lambda: off(dict(outer_colour_far_black=True), excl=False), "FAIL"),
        # The wedge mask (ruled 2026-10-28). M2: located, and its share inside the predicted band.
        ("M2 look 4's wedge drawn: the mask is found, its share 18.4-18.9 %", lambda: share_case("A"),   "PASS"),
        ("M3 S-m1: a wedge-only colour on a ring pixel outside the mask", lambda: on(dict(plant="outside"), "A"), "FAIL"),
        ("M4 S-m2: the head erased",                  lambda: on(dict(head="none"), "A"),                "UNTRUSTWORTHY"),
        ("M5 two heads",                              lambda: on(dict(head="two"), "A"),                 "UNTRUSTWORTHY"),
        ("M5 the head 80 px out, not at the edge",    lambda: on(dict(head="r80"), "A"),                 "UNTRUSTWORTHY"),
        ("M5 the beam line 0.3 rad from its head",    lambda: on(dict(beam_dtheta=0.3), "A"),            "UNTRUSTWORTHY"),
        ("M6 CONTROL: the same wedge-only pixel inside the mask", lambda: on(dict(plant="inside"), "A"), "PASS"),
        # The checker's two gaps (ruled 2026-10-30): the highlight by its geometry; the screen dots; label strokes.
        ("H1 a NEAR highlight (circle and tag) across the middle ring outside the mask",
                                                      lambda: on(dict(highlight="near"), "A"),           "PASS"),
        ("H2 CONTROL: a lone 0xF81F on that ring",    lambda: on(dict(highlight="lone"), "A"),           "FAIL"),
        ("D1 a dart whose border the slot-2 screen dot covers (counted apart: screen dot 1)",
                                                      lambda: apart_case(dict(dots="full"), {"screen dot": 1}), "PASS"),
        ("D2 the same with one dot-coloured pixel and no dot (counted apart: label 1)",
                                                      lambda: apart_case(dict(dots="pixel"), {"label": 1}), "PASS"),
        ("L1 a label-and-weather stroke across an outlined dart (counted apart: label 1)",
                                                      lambda: apart_case(dict(stroke="label"), {"label": 1}), "PASS"),
        ("L2 CONTROL: the same stroke in a weather colour that is no label colour",
                                                      lambda: on(dict(stroke="weather"), "A"),           "FAIL"),
    ]
    bad = 0
    for label, run, want in cases:
        got, lines = run()
        ok = got == want
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {label}: {got} (want {want}) -- {lines[-2] if len(lines) > 1 else lines[-1]}")
    print("WX FB CHECK SELFTEST PASSED" if not bad else f"WX FB CHECK SELFTEST: {bad} NOT AS PREDICTED")
    return 0 if not bad else 1


def main(argv):
    try:
        src = load_source()
    except (AssertionError, AttributeError, ValueError, OSError) as e:
        print(f"RIG: could not read the source: {e}"); return 4
    if argv[1:2] == ["--selftest"]:
        try:
            return selftest(src)
        except OSError as e:
            print(f"RIG: {e}"); return 4
    args = argv[1:]
    drop = "--drop-untrustworthy" in args
    args = [a for a in args if a != "--drop-untrustworthy"]
    if args[:1] == ["--off"]:
        mode, paths = "off", args[1:]
    elif len(args) >= 2 and args[0] == "--look" and args[1] in ("A", "B"):
        mode, paths = args[1], args[2:]
    else:
        print(__doc__); return 4
    if not paths:
        print(__doc__); return 4
    frames = []
    for p in paths:
        b = open(p, "rb").read()
        if len(b) != W * H * 2:
            print(f"RIG: {p} is {len(b)} bytes, not a {W} x {H} RGB565 frame"); return 4
        frames.append([(b[2 * i] << 8) | b[2 * i + 1] for i in range(W * H)])
    verdict, lines = grade_off(frames, src, drop) if mode == "off" else grade(frames, mode, src, drop)
    print("\n".join(lines))
    print(verdict)
    return {"PASS": 0, "FAIL": 1, "UNTRUSTWORTHY": 2, "BLIND": 3}[verdict]


if __name__ == "__main__":
    sys.exit(main(sys.argv))
