"""Weatherscope 3.5a's independent screen reference (docs/weatherscope-stage3.md A9.2, A9.4, A9.10).

Renders a whole 240 x 240 screen from the rules of the reviewer's generator (wxmock.py), re-implemented here in
Python -- never from the device's C++. Two rounding modes (A9.10, R0):
  P  pinned: floor(v + 0.5) at every pixel rounding, an echo counts only within 60.0 mi (Q16) -- the device's rule
  G  the generator's: Python round() (half to even) at its two rounding sites (ring dots, polygon DDA points), and
     its corner-counting nearest echo -- used only by R0, to prove this file reproduces the mockups
Every rounding-site call whose argument is exactly k + 0.5 is logged, so R0 can name each pixel where P and G differ.

    python -I tools/weatherscope/screens/ref35.py --r0 <reviewer mockup dir> [--out <dir>]
    python -I tools/weatherscope/screens/ref35.py --k --write | --check | --diff <dir> [--dump <dir>]
        the 25 K rows (P mode, real populations) as test/fixtures/wx/k35.ref.txt, which test_wx_k35.cpp reproduces
        through the device's path; --dump writes each frame's RGB565, --diff compares another renderer's dumps

The font is the vendored Adafruit-GFX glcdfont.c, pinned by sha256 (FT1); colours come from legend_check.py's
pinned-table derivation, never from WxPalette.h. Prints no coordinate.
"""
import ast, hashlib, importlib.util, json, math, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
WX = os.path.join(ROOT, "test", "fixtures", "wx")
LOOP = os.path.join(WX, "loop")
FONT_PATH = os.path.join(WX, "glcdfont.c")
FONT_SHA = "bd6d60ecb9cc350a83f209d7c62de2fe5f6ce3402d5598cde2988b5b44e83e02"   # Adafruit-GFX-Library @6abf693
sys.dont_write_bytecode = True

C = 120.0
SIZE = 240
TOP_BAND, BOT_BAND = 24, 194
KM_PER_DEG, KM_PER_MI = 111.0, 1.609344
BLACK, WHITE = (0, 0, 0), (255, 255, 255)
DIMTXT, LABEL, ORANGE, RING, RINGTXT = (120, 120, 120), (176, 176, 176), (255, 140, 0), (64, 64, 64), (96, 96, 96)
MIDDOT, DEGREE = chr(0xFA), chr(0xF8)
# the home marker, read off the lossless screen-proposed-dry.png (A9.2): an 8 x 8 ring and a 2 x 2 centre
HOME = [(x, 116) for x in range(118, 122)] + [(117, 117), (122, 117)] + \
       [(x, y) for y in range(118, 122) for x in (116, 123)] + [(119, 119), (120, 119), (119, 120), (120, 120)] + \
       [(117, 122), (122, 122)] + [(x, 123) for x in range(118, 122)]

# 3.5b's peak glyph (A10.2, Q32), read off the lossless screen-bend-B.png at its first-ranked placed peak (Mount
# Jefferson), as offsets from the anchor (int(x), int(y)). All four placed peaks there carry this same pattern.
PEAK_GLYPH = [(-3, 2), (-2, 0), (-2, 1), (-2, 2), (-1, -2), (-1, -1), (-1, 2), (0, -3), (0, 2), (1, -2), (1, -1),
              (1, 2), (2, 0), (2, 1), (2, 2), (3, 2)]


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))
    spec.loader.exec_module(m)
    return m


legend = _load("legend_check", os.path.join(WX, "legend_check.py"))
ring_ref = _load("ring_ref", os.path.join(WX, "ring_ref.py"))


# ------------------------------------------------------------------------------------------------ the font (FT1)
def load_font():
    raw = open(FONT_PATH, "rb").read().replace(b"\r\n", b"\n")   # the blob's bytes on any checkout (autocrlf)
    got = hashlib.sha256(raw).hexdigest()
    if got != FONT_SHA:
        sys.exit(f"FT1 FAILED: glcdfont.c is {got[:12]}, pinned {FONT_SHA[:12]}")
    txt = raw.decode("utf-8", errors="replace")
    body = txt[txt.index("{") + 1: txt.rindex("}")]
    vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
    if len(vals) != 1280:
        sys.exit(f"FT2 FAILED: {len(vals)} bytes, not 1280")
    return [vals[i * 5:(i + 1) * 5] for i in range(256)]


FONT = load_font()


def text_w(s, size=1):
    return len(s) * 6 * size - size


# ------------------------------------------------------------------------------------------------ rounding
class Rounder:
    """'P': floor(v + 0.5). 'G': Python round(). point() rounds a point's x and y in BOTH modes and, where the two
    pixels differ (only an exact k + 0.5 argument can do that), logs the site with both pixels."""
    def __init__(self, mode):
        self.mode, self.log = mode, []

    def point(self, xv, yv, site):
        g = (round(xv), round(yv))
        p = (int(math.floor(xv + 0.5)), int(math.floor(yv + 0.5)))
        # The log holds ONLY calls whose argument is exactly k + 0.5 (A9.10's R0b), judged from the ARGUMENT, never
        # from whether the two results differ -- otherwise a broken pinned rounding would log, and so "name", its own
        # wrong pixels (found by R0's sabotage b, 2026-10-23).
        if xv - math.floor(xv) == 0.5 or yv - math.floor(yv) == 0.5:
            self.log.append({"site": site, "x": xv, "y": yv, "G": list(g), "P": list(p)})
        return p if self.mode == "P" else g


# ------------------------------------------------------------------------------------------------ the image
# The ink roles a screen is drawn in (3.5t, A12.22). DARK_INK is exactly the constants above, so a caller that passes
# no ink draws today's screens, byte for byte.
DARK_INK = dict(bg=BLACK, band=BLACK, dim=DIMTXT, white=WHITE, label=LABEL, halo=BLACK, ring=RING, ringtext=RINGTXT,
                home=WHITE, peak=LABEL, orange=ORANGE)


def inkmap(ink, col):
    """A state's ink (WHITE, DIMTXT or ORANGE, as the scenarios name them) in a theme's roles."""
    return {WHITE: ink["white"], DIMTXT: ink["dim"], ORANGE: ink["orange"]}.get(col, col)


class Screen:
    def __init__(self, bg=BLACK):
        self.px = [[bg] * SIZE for _ in range(SIZE)]

    def put(self, x, y, col):
        if 0 <= x < SIZE and 0 <= y < SIZE:
            self.px[y][x] = col

    def fill(self, x0, y0, x1, y1, col):   # inclusive, like PIL's rectangle
        for y in range(max(0, y0), min(SIZE - 1, y1) + 1):
            for x in range(max(0, x0), min(SIZE - 1, x1) + 1):
                self.px[y][x] = col

    def text(self, x, y, s, size=1, fill=WHITE, halo=None):
        cells = []
        for i, ch in enumerate(s):
            g = FONT[ord(ch) & 0xFF]
            for col in range(5):
                for row in range(7):
                    if g[col] & (1 << row):
                        cells.append((x + (i * 6 + col) * size, y + row * size))
        if halo is not None:
            for (cx, cy) in cells:
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        for sx in range(size):
                            for sy in range(size):
                                self.put(cx + sx + dx, cy + sy + dy, halo)
        for (cx, cy) in cells:
            for sx in range(size):
                for sy in range(size):
                    self.put(cx + sx, cy + sy, fill)

    def rgb888(self):
        return bytes(c for row in self.px for p in row for c in p)

    def rgb565(self):
        out = bytearray()
        for row in self.px:
            for r, g, b in row:
                v = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3
                out += bytes((v >> 8, v & 0xFF))
        return bytes(out)


# ------------------------------------------------------------------------------------------------ data
def decode(path):
    lat_nw, lon_nw, cell_lat, cell_lon, rows, cols, codes = ring_ref.decode(open(path, "rb").read())
    epoch = struct.unpack_from("<I", open(path, "rb").read(), 8)[0]
    return dict(latNw=lat_nw, lonNw=lon_nw, cellLat=cell_lat, cellLon=cell_lon, rows=rows, cols=cols, epoch=epoch), codes


def centre(h):
    return (h["latNw"] - (h["rows"] - 1) / 2.0 * h["cellLat"], h["lonNw"] + (h["cols"] - 1) / 2.0 * h["cellLon"])


class View:
    def __init__(self, h, radius_mi):
        self.h, self.home, self.R = h, centre(h), radius_mi
        rkm = radius_mi * KM_PER_MI
        self.rLat = rkm / KM_PER_DEG
        self.rLon = rkm / (KM_PER_DEG * max(math.cos(math.radians(self.home[0])), 0.01))

    def project(self, lat, lon):
        return C + (lon - self.home[1]) / self.rLon * C, C - (lat - self.home[0]) / self.rLat * C

    def ppm(self):
        return C / self.R


PAL = {0: legend.DRY, 14: legend.NOCOV}
PAL.update(legend.RAIN)


# R0's palette (A12.18.2): the generator's own code 1, the pinned table's 15 dBZ entry dimmed to 55 %, as the reviewer's
# 3.5a mockups were drawn before #488's teal. R0 only, in both its modes; every other path keeps PAL's teal.
GEN_PAL = dict(PAL)
GEN_PAL[1] = tuple((v * 55 + 50) // 100 for v in legend.TBL[2])


def colour(code, x, y, pal=None):
    pal = pal or PAL
    if code == 15:   # the hatch, a line on its own ground (A12.29): pal[15] and pal[0] theme it; PAL holds no 15
        return pal.get(15, legend.HATCH) if (x + y) % 4 == 0 else pal.get(0, legend.DRY)
    return pal[code]


def nearest_echo(h, codes, home, bound_mi):
    """(distance mi, 8-point direction, bearing deg) of the nearest echo cell; bound_mi None = the generator's
    corner-counting search over the whole crop."""
    kx, ky = KM_PER_DEG * math.cos(math.radians(home[0])), KM_PER_DEG
    best, bl = None, None
    for r in range(h["rows"]):
        dy = (h["latNw"] - r * h["cellLat"] - home[0]) * ky
        row = r * h["cols"]
        for c in range(h["cols"]):
            if 1 <= codes[row + c] <= 13:
                dx = (h["lonNw"] + c * h["cellLon"] - home[1]) * kx
                d2 = dx * dx + dy * dy
                if bound_mi is not None and math.sqrt(d2) / KM_PER_MI > bound_mi:
                    continue
                if best is None or d2 < best:
                    best, bl = d2, (dx, dy)
    if best is None:
        return None
    ang = (math.degrees(math.atan2(bl[0], bl[1])) + 360) % 360
    return math.sqrt(best) / KM_PER_MI, ["N", "NE", "E", "SE", "S", "SW", "W", "NW"][int((ang + 22.5) // 45) % 8], ang


def label_name(name):
    return re.sub(r"\s+(city|town|village|CDP)$", "", name)


# ------------------------------------------------------------------------------------------------ the rules
def inside_circle(x0, y0, x1, y1, r=116):
    return all((x - C) ** 2 + (y - C) ** 2 <= r * r for (x, y) in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)))


def box_clears_centre(box, inner):
    nx = min(max(C, box[0]), box[2]); ny = min(max(C, box[1]), box[3])
    return (nx - C) ** 2 + (ny - C) ** 2 >= inner * inner


def overlaps(a, b, pad=4):
    return not (a[2] + pad < b[0] or b[2] + pad < a[0] or a[3] + pad < b[1] or b[3] + pad < a[1])


def in_map(x, y):
    return 0 <= x < SIZE and TOP_BAND <= y < BOT_BAND and (x - C) ** 2 + (y - C) ** 2 <= C * C


def render_map(scr, h, codes, view, base=None, corner=False, pal=None, terrain=False):
    """The map pass. 3.5b's fold (A10.2): a dry pixel (code 0) shows `base`'s colour where there is a base. With no crop
    (h None) every pixel is dry. corner=True is bendmock.py's own mask (in_map, integer coordinates), for R0b's G mode
    only; the device and P use CodeAt's pixel-centre disc."""
    for y in range(SIZE):
        lat = view.home[0] + (C - (y + 0.5)) / C * view.rLat
        r = int(math.floor((h["latNw"] - lat) / h["cellLat"] + 0.5)) if h is not None else 0
        for x in range(SIZE):
            if corner:
                if not in_map(x, y):
                    continue
            elif (x + 0.5 - C) ** 2 + (y + 0.5 - C) ** 2 > C * C:
                continue
            if h is None:
                code = 0
            else:
                lon = view.home[1] + ((x + 0.5) - C) / C * view.rLon
                c = int(math.floor((lon - h["lonNw"]) / h["cellLon"] + 0.5))
                code = codes[r * h["cols"] + c] if 0 <= r < h["rows"] and 0 <= c < h["cols"] else 14
            if terrain and code == 14:   # with terrain on, no coverage is drawn as the hatch (A10.2, Q31)
                code = 15
            scr.px[y][x] = base[y][x] if (code == 0 and base is not None) else colour(code, x, y, pal)


def draw_ring(scr, r_px, rnd, col=RING):
    n = int(2 * math.pi * r_px)
    for k in range(0, n, 2):
        a = 2 * math.pi * k / n
        x, y = rnd.point(C + r_px * math.sin(a), C - r_px * math.cos(a), f"ring r={r_px:g} k={k}")
        if in_map(x, y):
            scr.px[y][x] = col


def place_ring_labels(scr, rings, obstacles, ink=None):
    ink = ink or DARK_INK
    for r_px, label in rings:
        w = text_w(label)
        for ang in (45, 135, 315, 225, 90, 270, 0, 180):
            tx = int(C + r_px * math.sin(math.radians(ang))) + (2 if ang < 180 else -w - 2)
            ty = int(C - r_px * math.cos(math.radians(ang))) - (9 if ang in (45, 315, 0) else (1 if ang in (135, 225, 180) else -4))
            box = (tx - 1, ty - 1, tx + w + 1, ty + 8)
            if box[1] < TOP_BAND + 1 or box[3] > BOT_BAND - 1 or not inside_circle(*box):
                continue
            if any(overlaps(box, o, 2) for o in obstacles):
                continue
            scr.text(tx, ty, label, 1, ink["ringtext"], halo=ink["halo"])
            obstacles.append(box)
            break


def draw_dda(scr, x0, y0, x1, y1, col, rnd, site):
    """A9.2's edge: n = int(max(|dx|, |dy|)) + 1, points k = 0..n, each rounded, clipped to the map area."""
    n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for k in range(n + 1):
        x, y = rnd.point(x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n, f"{site} k={k}")
        if in_map(x, y):
            scr.px[y][x] = col


def arrow_segments(sx, sy, from_deg):
    """Q17: a 12 px shaft from the storm along its travel (from + 180), and two 4 px head strokes at +-150 degrees
    from the travel, at the tip."""
    travel = (from_deg + 180) % 360
    t = math.radians(travel)
    tip = (sx + 12 * math.sin(t), sy - 12 * math.cos(t))
    heads = [(tip[0] + 4 * math.sin(math.radians(travel + d)), tip[1] - 4 * math.cos(math.radians(travel + d)))
             for d in (150, -150)]
    return [((sx, sy), tip), (tip, heads[0]), (tip, heads[1])]


def compose(h, codes, view, state, towns, pop, frame_clock, rnd, zoomed=False, warning=None, cap=8, loop=None,
            base=None, corner=False, peaks=None, glyph=None, peak_log=None, pal=None, ink=None, terrain=False):
    """One screen, in the generator's order (A9.2). Returns the Screen and its placed labels (for the board's own).
    3.5a's K rows add (A9.10, A9.14, Q17, Q18): state["timeText"]/["timeInk"] for the top strip; warning["arrow"] =
    (lat, lon, fromDeg), drawn after the title and before the labels, its box a fixed overlay; and `loop` =
    dict(age=, index=) for the loop's band in place of the state line. Towns are (name, lat, lon[, ...])."""
    ink = ink or DARK_INK
    scr = Screen(ink["bg"])
    render_map(scr, h, codes, view, base, corner, pal, terrain)
    scr.fill(0, 0, SIZE - 1, TOP_BAND - 1, ink["band"])
    scr.fill(0, BOT_BAND, SIZE - 1, SIZE - 1, ink["band"])
    fixed = [(0, 0, SIZE - 1, TOP_BAND - 1), (0, BOT_BAND, SIZE - 1, SIZE - 1)]
    ppm = view.ppm()
    rings = []
    for mi in (5, 15, 30):
        if 0.15 * view.R <= mi <= 0.85 * view.R:
            draw_ring(scr, mi * ppm, rnd, ink["ring"])
            rings.append((mi * ppm, "%d" % mi))
    scr.text(117, 3, "N", 1, ink["dim"])
    time_text = state.get("timeText", frame_clock)
    if not zoomed:
        scr.text(int(C - text_w(time_text) / 2), 13, time_text, 1, inkmap(ink, state.get("timeInk", DIMTXT)))
    else:
        tag = "ZOOM %d MI" % view.R
        x0 = int(C - text_w(time_text + "  " + tag) / 2)
        scr.text(x0, 13, time_text, 1, inkmap(ink, state.get("timeInk", DIMTXT)))
        scr.text(x0 + text_w(time_text + "  ") + 1, 13, tag, 1, ink["white"])
    if warning:
        pts = [view.project(la, lo) for (la, lo) in warning["polygon"]]
        for i in range(len(pts)):
            x0, y0 = pts[i]; x1, y1 = pts[(i + 1) % len(pts)]
            n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
            for k in range(n + 1):
                x, y = rnd.point(x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n, f"edge {i} k={k}")
                if in_map(x, y):
                    scr.px[y][x] = ORANGE
            fixed.append((int(min(x0, x1)), int(min(y0, y1)), int(max(x0, x1)), int(max(y0, y1))))
        title = warning["title"]
        tx = int(C - text_w(title, 2) / 2)
        scr.text(tx, 36, title, 2, ORANGE, halo=BLACK)
        sub = warning["sub"]
        sx = int(C - text_w(sub) / 2)
        scr.text(sx, 54, sub, 1, ORANGE, halo=BLACK)
        fixed.append((tx - 2, 34, tx + text_w(title, 2) + 2, 36 + 14 + 2))
        fixed.append((sx - 2, 52, sx + text_w(sub) + 2, 54 + 8 + 2))
        if warning.get("arrow"):
            alat, alon, from_deg = warning["arrow"]
            ax, ay = view.project(alat, alon)
            segs = arrow_segments(ax, ay, from_deg)
            for j, (p0, p1) in enumerate(segs):
                draw_dda(scr, p0[0], p0[1], p1[0], p1[1], ORANGE, rnd, f"arrow {j}")
            xs = [p[0] for s in segs for p in s]
            ys = [p[1] for s in segs for p in s]
            fixed.append((int(min(xs)), int(min(ys)), int(max(xs)), int(max(ys))))
    placed, placed_info = [], []
    inner = 0.17 * view.R * ppm + 4
    if peaks:   # 3.5b (A10.2): peaks before towns, the first 4 PLACED, in rank order; placed peaks are obstacles
        n = 0
        for name, lat, lon in peaks:
            if n >= 4:
                break
            x, y = view.project(lat, lon)
            if (x - C) ** 2 + (y - C) ** 2 < inner * inner:
                continue
            label = name.replace("Mount ", "Mt ")
            w = text_w(label)
            cands = [(int(x) - 3, int(y) - 4, int(x) + 5 + w, int(y) + 4, int(x) + 5),
                     (int(x) - 5 - w - 1, int(y) - 4, int(x) + 4, int(y) + 4, int(x) - 5 - w)]
            if x < C:
                cands.reverse()
            for (x0, y0, x1, y1, tx) in cands:
                box = (x0, y0, x1, y1)
                if y0 < TOP_BAND + 2 or y1 > BOT_BAND - 2 or not inside_circle(x0, y0, x1, y1):
                    continue
                if not box_clears_centre(box, inner):
                    continue
                if any(overlaps(box, f) for f in fixed) or any(overlaps(box, p) for p in placed):
                    continue
                glyph(scr, x, y, ink["peak"])
                scr.text(tx, int(y) - 3, label, 1, ink["peak"], halo=ink["halo"])
                placed.append(box)
                if peak_log is not None:
                    peak_log.append((name, x, y))
                n += 1
                break
    ranked = sorted(towns, key=lambda t: -(pop(t) or 1000))
    for t in ranked:
        name, lat, lon = t[0], t[1], t[2]
        if len(placed) >= cap:
            break
        x, y = view.project(lat, lon)
        if (x - C) ** 2 + (y - C) ** 2 < inner * inner:
            continue
        label = label_name(name)
        w = text_w(label)
        cands = [(int(x) - 1, int(y) - 4, int(x) + 4 + w, int(y) + 4, int(x) + 4, "R"),
                 (int(x) - 4 - w - 1, int(y) - 4, int(x) + 2, int(y) + 4, int(x) - 4 - w, "L")]
        if x < C:
            cands.reverse()
        for (x0, y0, x1, y1, tx, side) in cands:
            box = (x0, y0, x1, y1)
            if y0 < TOP_BAND + 2 or y1 > BOT_BAND - 2:
                continue
            if not inside_circle(x0, y0, x1, y1):
                continue
            if not box_clears_centre(box, inner):
                continue
            if any(overlaps(box, f) for f in fixed) or any(overlaps(box, p) for p in placed):
                continue
            scr.fill(int(x) - 1, int(y) - 1, int(x) + 1, int(y) + 1, ink["label"])
            scr.text(tx, int(y) - 3, label, 1, ink["label"], halo=ink["halo"])
            placed.append(box)
            placed_info.append((label, side, box))
            break
    place_ring_labels(scr, rings, fixed + placed, ink)
    for (x, y) in HOME:
        scr.px[y][x] = ink["home"]
    if warning:
        scr.text(int(C - text_w(warning["band1"]) / 2), 197, warning["band1"], 1, WHITE)
        scr.text(int(C - text_w(warning["band2"]) / 2), 207, warning["band2"], 1, WHITE)
    elif loop is not None:   # A9.10's loop band: the age, a 120 x 2 track, a 4 x 6 knob
        scr.text(int(C - text_w(loop["age"]) / 2), 198, loop["age"], 1, DIMTXT)
        scr.fill(60, 212, 179, 213, RINGTXT)
        kx = 60 + (loop["index"] * 116) // 14
        scr.fill(kx, 210, kx + 3, 215, WHITE)
    else:
        s, col = state["line"], state["colour"]
        scr.text(int(C - text_w(s, 2) / 2), 198, s, 2, inkmap(ink, col))
    return scr, placed_info


# ------------------------------------------------------------------------------------------------ R0
def load_towns_pre_join():
    """The 171 towns as WxTowns.h held them before 3.5a's population join (name, lat, lon), the generator's input:
    main's a7f21fb (#467), byte-identical to b202ff3's, which the squash left reachable only through the PR."""
    import subprocess
    t = subprocess.run(["git", "-C", ROOT, "show", "a7f21fb:src/weatherscope/WxTowns.h"], capture_output=True,
                       text=True, check=True).stdout
    return [(n, float(a), float(b)) for n, a, b in
            re.findall(r'\{\s*"([^"]+)",\s*(-?[0-9.]+),\s*(-?[0-9.]+)\s*\}', t)]


def generator_pop(mock_dir):
    """wxmock.py's hand-typed POP table, parsed from its source -- for R0 only, never a data source (ruling 4)."""
    src = open(os.path.join(mock_dir, "wxmock.py"), encoding="utf-8").read()
    m = re.search(r"^POP = (\{.*?\})\n", src, re.S | re.M)
    return ast.literal_eval(m.group(1))


def png_rgb(path):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    assert im.size == (SIZE, SIZE), im.size
    return im.tobytes()


def r0_fixture_pins(fx_dir):
    """A12.18.3: every file r0/MANIFEST.json lists must hash to its pin, or R0 does not run (exit 2, the rig)."""
    man = json.load(open(os.path.join(fx_dir, "MANIFEST.json"), encoding="utf-8"))
    bad = [n for n, want in man["files"].items()
           if hashlib.sha256(open(os.path.join(fx_dir, n), "rb").read()).hexdigest() != want]
    if bad:
        print(f"R0 REFUSED: {bad} do not hash to MANIFEST.json's pins (the rig)")
        sys.exit(2)
    return man


def load_towns_fixture(fx_dir):
    t = open(os.path.join(fx_dir, "towns-a7f21fb.txt"), encoding="utf-8").read()
    return [(n, float(a), float(b)) for n, a, b in
            (l.split("|") for l in t.splitlines() if l and not l.startswith("#"))]


def r0(mock_dir, out_dir):
    fixture = os.path.exists(os.path.join(mock_dir, "MANIFEST.json"))
    if fixture:
        r0_fixture_pins(mock_dir)
        towns = load_towns_fixture(mock_dir)
        POP = json.load(open(os.path.join(mock_dir, "generator-pop.json"), encoding="utf-8"))["pop"]
    else:
        towns = load_towns_pre_join()
        POP = generator_pop(mock_dir)
    pop = lambda t: POP.get(label_name(t[0]), 1000)
    hA, cA = decode(os.path.join(LOOP, "shsr-20240521-212400.wxc"))
    hD, cD = decode(os.path.join(LOOP, "shsr-20240521-220000.wxc"))
    hZ, cZ = decode(os.path.join(LOOP, "shsr-20240521-210800.wxc"))
    print(f"R0 inputs ({'fixtures, pinned' if fixture else 'the reviewer dir'}): {len(towns)} towns (a7f21fb, = b202ff3); POP table {len(POP)} entries (R0 only); code 1 {GEN_PAL[1]} (the generator's, A12.18.2); font {FONT_SHA[:12]}")
    results, ok = [], True
    images = {}
    for mode in ("G", "P"):
        bound = None if mode == "G" else 60.0
        home = centre(hA)
        nA, nD = nearest_echo(hA, cA, home, bound), nearest_echo(hD, cD, home, bound)
        nZ = nearest_echo(hZ, cZ, home, bound)
        R = (lambda v: round(v)) if mode == "G" else (lambda v: int(math.floor(v + 0.5)))
        warn = dict(polygon=[(home[0] - .25, home[1] - .25), (home[0] - .25, home[1] + .25),
                             (home[0] + .25, home[1] + .25), (home[0] + .25, home[1] - .25)],
                    title="SEVERE T-STORM", sub="WARNING " + MIDDOT + " UNTIL 5:09 PM",
                    band1="HAIL 1 IN " + MIDDOT + " WIND 60 MPH",
                    band2="STORM %d MI %s " % (R(nA[0]), nA[1]) + MIDDOT + " NE 35 MPH")
        v60, v60d = View(hA, 60), View(hD, 60)
        jobs = {
            "screen-proposed-dry.png": lambda rnd: compose(hD, cD, v60d, dict(line="RAIN %d MI %s" % (R(nD[0]), nD[1]), colour=DIMTXT), towns, pop, "5:00 PM", rnd, pal=GEN_PAL),
            "screen-proposed-approaching.png": lambda rnd: compose(hA, cA, v60, dict(line="RAIN %d MI %s" % (R(nA[0]), nA[1]), colour=WHITE), towns, pop, "4:24 PM", rnd, pal=GEN_PAL),
            "screen-proposed-herenow.png": lambda rnd: compose(hA, cA, v60, dict(line="RAIN NOW", colour=WHITE), towns, pop, "4:24 PM", rnd, pal=GEN_PAL),
            "screen-proposed-warning.png": lambda rnd: compose(hA, cA, v60, {}, towns, pop, "4:24 PM", rnd, warning=warn, pal=GEN_PAL),
        }
        for Rz in (60, 30, 15):
            jobs["screen-zoom-%d.png" % Rz] = (lambda Rz: lambda rnd: compose(
                hZ, cZ, View(hZ, Rz), dict(line="RAIN %d MI %s" % (R(nZ[0]), nZ[1]), colour=WHITE), towns, pop, "4:08 PM", rnd,
                zoomed=(Rz != 60), cap={60: 8, 30: 6, 15: 5}[Rz], pal=GEN_PAL))(Rz)
        shots, logs = {}, {}
        for name, job in jobs.items():
            rnd = Rounder(mode)
            shots[name] = job(rnd)[0]
            logs[name] = rnd.log
        images[mode] = (shots, None, logs)
    print("R0a: the generator's mode against each mockup PNG (RGB888, 57,600 px)")
    for name, scr in images["G"][0].items():
        want = png_rgb(os.path.join(mock_dir, name))
        got = scr.rgb888()
        diff = [i for i in range(SIZE * SIZE) if got[3 * i:3 * i + 3] != want[3 * i:3 * i + 3]]
        good = not diff
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'} {name}: {SIZE * SIZE - len(diff)} of {SIZE * SIZE} equal" +
              ("" if good else f"; UNEXPLAINED pixels (first 10, x,y): {[(i % SIZE, i // SIZE) for i in diff[:10]]}"))
        results.append(dict(check="R0a", screen=name, differing=len(diff), sample=[(i % SIZE, i // SIZE) for i in diff[:50]]))
    print("R0b: the pinned mode against the generator's mode; every differing pixel must be named by the rounding log")
    for name in images["G"][0]:
        g, p = images["G"][0][name].rgb888(), images["P"][0][name].rgb888()
        diff = [(i % SIZE, i // SIZE) for i in range(SIZE * SIZE) if g[3 * i:3 * i + 3] != p[3 * i:3 * i + 3]]
        named = {tuple(e["G"]) for e in images["P"][2][name]} | {tuple(e["P"]) for e in images["P"][2][name]}
        unnamed = [px for px in diff if px not in named]
        good = not unnamed
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'} {name}: {len(diff)} pixels differ, {len(diff) - len(unnamed)} named by the "
              f"rounding log ({len(images['P'][2][name])} logged points)" + ("" if good else f"; UNNAMED: {unnamed[:10]}"))
        results.append(dict(check="R0b", screen=name, differing=len(diff), named=len(diff) - len(unnamed),
                            unnamed=unnamed[:50], pixels=diff, log=images["P"][2][name]))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
        json.dump(dict(results=results), open(os.path.join(out_dir, "r0.json"), "w"), indent=1)
    print("R0: " + ("PASSES" if ok else "STOPS -- an unexplained pixel is a finding about the spec"))
    return 0 if ok else 1


# ------------------------------------------------------------------------------------------------ K (A9.10)
# The 25 K rows, re-derived here from the scenario definitions (A8.10, Q15) and A9.2's rules -- never from the C++.
# Each row is written as lines a C++ host test (test/host/test_wx_k35.cpp) reproduces through the device's path:
#   ROW <name> <sha256 of the 240 x 240 RGB565 big-endian frame>     the 57,600 pixels, color565-truncated
#   TIME <name> <text> <ink>                                         the top strip's time
#   BAND <name> S <text> <ink> | BAND <name> 1|2 <text> | BAND <name> L <age> <index>
#   LABEL <name> <label> <R|L> <x0> <y0> <x1> <y1>                   each placed town label, in placement order
# (each text one token: printable ASCII as itself, other bytes and '%' as %XX, '-' when empty -- see hx())
#   AR1 <x,y> ...                                                    A9.14: AR1's drawn pixel set, sorted
K_REF = os.path.join(WX, "k35.ref.txt")
CLEAR = os.path.join(LOOP, "synthetic-clear.wxc")
OFFSET_MIN = -300            # the scenarios' clock, CDT (A8.1)
STALE_LATE_S = 31 * 60       # Q15
CLEAR_TEMP_F = 54.0          # Q15
EXPIRES_AFTER_S = 45 * 60    # A8.10
FROM_DEG, SPEED_KT = 240, 30
MPH_PER_KT = 1.150779448
COMPASS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
INKS = {WHITE: "white", DIMTXT: "dim", ORANGE: "orange"}


def half_up(v):
    return int(math.floor(v + 0.5))


def compass(deg):
    return COMPASS[int((deg + 22.5) // 45) % 8]


def clock_text(epoch, off_min=OFFSET_MIN):
    day = (epoch + off_min * 60) % 86400
    hh, mm = day // 3600, day // 60 % 60
    return "%d:%02d %s" % (12 if hh % 12 == 0 else hh % 12, mm, "AM" if hh < 12 else "PM")


def planar(frm, to):
    """(miles, bearing degrees) in the map's metric: 111 km per degree, longitude scaled at `frm`'s latitude."""
    dx = (to[1] - frm[1]) * KM_PER_DEG * math.cos(math.radians(frm[0]))
    dy = (to[0] - frm[0]) * KM_PER_DEG
    return math.sqrt(dx * dx + dy * dy) / KM_PER_MI, (math.degrees(math.atan2(dx, dy)) + 360) % 360


def storm_cell(h, codes, home):
    """A8.10's storm: the nearest echo cell's centre over the whole crop (row-major, the first minimum), carried by
    the warning's motion text at six decimals, as the scenario writes it."""
    kx, ky = KM_PER_DEG * math.cos(math.radians(home[0])), KM_PER_DEG
    best, at = None, None
    for r in range(h["rows"]):
        lat = h["latNw"] - r * h["cellLat"]
        dy = (lat - home[0]) * ky
        for c in range(h["cols"]):
            if 1 <= codes[r * h["cols"] + c] <= 13:
                lon = h["lonNw"] + c * h["cellLon"]
                dx = (lon - home[1]) * kx
                d2 = dx * dx + dy * dy
                if best is None or d2 < best:
                    best, at = d2, (lat, lon)
    return float("%.6f" % at[0]), float("%.6f" % at[1])


def state_rule(h, codes, age_s, here_now=None, temp_f=None):
    """A9.2 / Q15 / Q16 / Q20: RADAR STALE past 30 min; RAIN|SNOW NOW; the nearest echo within 60.0 mi (white within
    15 mi); a temperature; else NO RAIN 60 MI."""
    if age_s > 1800:
        return "RADAR STALE", DIMTXT
    if here_now:
        return here_now + " NOW", WHITE
    e = nearest_echo(h, codes, centre(h), 60.0)
    if e:
        return "RAIN %d MI %s" % (half_up(e[0]), e[1]), WHITE if e[0] <= 15.0 else DIMTXT
    if temp_f is not None:
        return "%d" % half_up(temp_f) + DEGREE + " " + MIDDOT + " NO RAIN", DIMTXT
    return "NO RAIN 60 MI", DIMTXT


def load_towns_now():
    """WxTowns.h as this tree holds it, with SUB-EST2024's populations (Q12): (name, lat, lon, pop)."""
    t = open(os.path.join(ROOT, "src", "weatherscope", "WxTowns.h"), encoding="utf-8").read()
    rows = re.findall(r'\{\s*"([^"]+)",\s*(-?[0-9.]+),\s*(-?[0-9.]+),\s*([0-9]+)\s*\}', t)
    return [(n, float(a), float(b), int(p)) for n, a, b, p in rows]


def sha_lf(path):
    return hashlib.sha256(open(path, "rb").read().replace(b"\r\n", b"\n")).hexdigest()


def k_rows():
    """[(row name, Screen, lines)] for the 25 rows, and the AR1 line."""
    names = sorted(n for n in os.listdir(LOOP) if n.startswith("shsr-") and n.endswith(".wxc"))
    if len(names) != 15:
        sys.exit(f"K: {len(names)} loop frames, not 15")
    frames = [decode(os.path.join(LOOP, n)) for n in names]
    # A8.10's picks, re-derived: approaching = the latest frame with an echo within 15 mi; dry = the latest without
    near = [nearest_echo(h, c, centre(h), None) for h, c in frames]
    ia = max(i for i, e in enumerate(near) if e and e[0] <= 15.0)
    idry = max(i for i, e in enumerate(near) if not (e and e[0] <= 15.0))
    print(f"K picks: approaching {names[ia]}, dry {names[idry]}")
    (hA, cA), (hD, cD) = frames[ia], frames[idry]
    hC, cC = decode(CLEAR)
    if set(cC) != {0} or len(cC) != 38520:
        sys.exit("K: synthetic-clear.wxc is not 38,520 dry cells (SC1)")
    towns = load_towns_now()
    pop = lambda t: t[3]
    out = []

    def row(name, h, codes, R, state, warning=None, loop=None, clock_epoch=None):
        rnd = Rounder("P")
        clock = clock_text(clock_epoch if clock_epoch is not None else h["epoch"])
        state = dict(state)
        state.setdefault("timeText", clock)
        state.setdefault("timeInk", DIMTXT)
        scr, placed = compose(h, codes, View(h, R), state, towns, pop, clock, rnd, zoomed=(R != 60), warning=warning,
                              cap={60: 8, 30: 6, 15: 5}[R], loop=loop)
        lines = ["ROW %s %s" % (name, hashlib.sha256(scr.rgb565()).hexdigest()),
                 "TIME %s %s %s" % (name, hx(state["timeText"]), INKS[state["timeInk"]])]
        if warning:
            lines += ["BAND %s 1 %s" % (name, hx(warning["band1"])), "BAND %s 2 %s" % (name, hx(warning["band2"]))]
        elif loop is not None:
            lines.append("BAND %s L %s %d" % (name, hx(loop["age"]), loop["index"]))
        else:
            lines.append("BAND %s S %s %s" % (name, hx(state["line"]), INKS[state["colour"]]))
        lines += ["LABEL %s %s %s %d %d %d %d" % (name, hx(lab), side, *box) for lab, side, box in placed]
        out.append((name, scr, lines))

    def plain(h, codes, age_s=0, here_now=None, temp_f=None):
        line, col = state_rule(h, codes, age_s, here_now, temp_f)
        return dict(line=line, colour=col, timeInk=ORANGE if age_s > 600 else DIMTXT)

    home = centre(hA)
    storm = storm_cell(hA, cA, home)
    d, b = planar(home, storm)
    warn = dict(polygon=[(home[0] - .25, home[1] - .25), (home[0] - .25, home[1] + .25),
                         (home[0] + .25, home[1] + .25), (home[0] + .25, home[1] - .25)],
                title="SEVERE T-STORM", sub="WARNING " + MIDDOT + " UNTIL " + clock_text(hA["epoch"] + EXPIRES_AFTER_S),
                band1="HAIL 1 IN " + MIDDOT + " WIND 60 MPH",
                band2="STORM %d MI %s " % (half_up(d), compass(b)) + MIDDOT + " %s %d MPH" % (
                    compass((FROM_DEG + 180) % 360), half_up(SPEED_KT * MPH_PER_KT)),
                arrow=(storm[0], storm[1], FROM_DEG) if d <= 60.0 else None)
    row("dry-60", hD, cD, 60, plain(hD, cD))
    row("approaching-60", hA, cA, 60, plain(hA, cA))
    row("herenow-60", hA, cA, 60, plain(hA, cA, here_now="RAIN"))
    row("warning-60", hA, cA, 60, {}, warning=warn)
    row("stale-60", hA, cA, 60, plain(hA, cA, age_s=STALE_LATE_S))
    row("clear-60", hC, cC, 60, plain(hC, cC, temp_f=CLEAR_TEMP_F))
    row("approaching-30", hA, cA, 30, plain(hA, cA))
    row("approaching-15", hA, cA, 15, plain(hA, cA))
    row("dry-30", hD, cD, 30, plain(hD, cD))
    row("dry-15", hD, cD, 15, plain(hD, cD))
    newest = frames[-1][0]["epoch"]
    for i, (h, c) in enumerate(frames):
        mins = (newest - h["epoch"]) // 60
        row("loop-%02d" % i, h, c, 60, {}, loop=dict(age="NOW" if mins <= 0 else "%d MIN AGO" % mins, index=i))
    # AR1's drawn set (A9.14): the arrow from (150.0, 100.0), from 240 degrees, on its own
    pts = set()
    rnd = Rounder("P")
    for j, (p0, p1) in enumerate(arrow_segments(150.0, 100.0, 240)):
        n = int(max(abs(p1[0] - p0[0]), abs(p1[1] - p0[1]))) + 1
        for k in range(n + 1):
            x, y = rnd.point(p0[0] + (p1[0] - p0[0]) * k / n, p0[1] + (p1[1] - p0[1]) * k / n, f"AR1 {j} k={k}")
            if in_map(x, y):
                pts.add((x, y))
    ar1 = "AR1 " + " ".join("%d,%d" % p for p in sorted(pts))
    return out, ar1


def hx(s):
    """A text as one token: printable ASCII as itself, every other byte (and '%') as %XX; '-' when empty. Not hex:
    an 8-character label hex-encodes to 16 hex digits, the shape the device-id guard refuses."""
    if not s:
        return "-"
    return "".join(c if 0x21 <= ord(c) <= 0x7E and c != "%" else "%%%02X" % ord(c) for c in s)


def k_text():
    rows, ar1 = k_rows()
    head = ["# Weatherscope 3.5a K rows (docs/weatherscope-stage3.md A9.10): written by tools/weatherscope/screens/ref35.py --k",
            "# --write, checked by its --check; read by test/host/test_wx_k35.cpp. Texts: printable ASCII, else %XX; '-' empty.",
            "# font %s" % FONT_SHA,
            "# WxTowns.h %s (LF-normalised)" % sha_lf(os.path.join(ROOT, "src", "weatherscope", "WxTowns.h")),
            "# loop MANIFEST.json %s (LF-normalised)" % sha_lf(os.path.join(LOOP, "MANIFEST.json")),
            "# synthetic-clear.wxc %s" % hashlib.sha256(open(CLEAR, "rb").read()).hexdigest()]
    body = [l for _, _, lines in rows for l in lines] + [ar1]
    return "\n".join(head + body) + "\n", rows


def k_main(argv):
    text, rows = k_text()
    if "--dump" in argv:
        d = argv[argv.index("--dump") + 1]
        os.makedirs(d, exist_ok=True)
        for name, scr, _ in rows:
            open(os.path.join(d, name + ".rgb565"), "wb").write(scr.rgb565())
        print(f"dumped {len(rows)} frames to {d}")
    if "--write" in argv:
        open(K_REF, "w", encoding="utf-8", newline="\n").write(text)
        print(f"wrote {os.path.relpath(K_REF, ROOT)}: {len(rows)} rows")
        return 0
    if "--diff" in argv:   # diagnosis: per-row differing pixels against another renderer's dumps
        d = argv[argv.index("--diff") + 1]
        bad = 0
        for name, scr, _ in rows:
            want, got = scr.rgb565(), open(os.path.join(d, name + ".rgb565"), "rb").read()
            diff = [(i % SIZE, i // SIZE) for i in range(SIZE * SIZE) if want[2 * i:2 * i + 2] != got[2 * i:2 * i + 2]]
            bad += bool(diff)
            print(f"  {name}: {SIZE * SIZE - len(diff)} of {SIZE * SIZE} equal" + (f"; first {diff[:12]}" if diff else ""))
        return 1 if bad else 0
    if "--check" in argv:
        committed = open(K_REF, encoding="utf-8").read().replace("\r\n", "\n")
        same = committed == text
        print(f"{os.path.relpath(K_REF, ROOT)}: {'identical' if same else 'DIFFERS'} on regeneration")
        if not same:
            a, b = committed.splitlines(), text.splitlines()
            for i, (x, y) in enumerate(zip(a, b)):
                if x != y:
                    print(f"  first difference at line {i + 1}:\n    committed {x[:120]}\n    now       {y[:120]}")
                    break
        return 0 if same else 1
    print("ref35.py --k needs --write, --check, --diff <dir> or --dump <dir>")
    return 2


if __name__ == "__main__":
    a = sys.argv
    if len(a) >= 3 and a[1] == "--r0":
        out = a[a.index("--out") + 1] if "--out" in a else None
        sys.exit(r0(a[2], out))
    if len(a) >= 2 and a[1] == "--k":
        sys.exit(k_main(a))
    print(__doc__)
    sys.exit(2)
