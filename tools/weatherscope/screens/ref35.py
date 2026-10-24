"""Weatherscope 3.5a's independent screen reference (docs/weatherscope-stage3.md A9.2, A9.4, A9.10).

Renders a whole 240 x 240 screen from the rules of the reviewer's generator (wxmock.py), re-implemented here in
Python -- never from the device's C++. Two rounding modes (A9.10, R0):
  P  pinned: floor(v + 0.5) at every pixel rounding, an echo counts only within 60.0 mi (Q16) -- the device's rule
  G  the generator's: Python round() (half to even) at its two rounding sites (ring dots, polygon DDA points), and
     its corner-counting nearest echo -- used only by R0, to prove this file reproduces the mockups
Every rounding-site call whose argument is exactly k + 0.5 is logged, so R0 can name each pixel where P and G differ.

    python -I tools/weatherscope/screens/ref35.py --r0 <reviewer mockup dir> [--out <dir>]

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
class Screen:
    def __init__(self):
        self.px = [[BLACK] * SIZE for _ in range(SIZE)]

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


def colour(code, x, y):
    if code == 15:
        return legend.HATCH if (x + y) % 4 == 0 else legend.DRY
    return PAL[code]


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


def render_map(scr, h, codes, view):
    for y in range(SIZE):
        lat = view.home[0] + (C - (y + 0.5)) / C * view.rLat
        r = int(math.floor((h["latNw"] - lat) / h["cellLat"] + 0.5))
        for x in range(SIZE):
            if (x + 0.5 - C) ** 2 + (y + 0.5 - C) ** 2 > C * C:
                continue
            lon = view.home[1] + ((x + 0.5) - C) / C * view.rLon
            c = int(math.floor((lon - h["lonNw"]) / h["cellLon"] + 0.5))
            if 0 <= r < h["rows"] and 0 <= c < h["cols"]:
                scr.px[y][x] = colour(codes[r * h["cols"] + c], x, y)
            else:
                scr.px[y][x] = legend.NOCOV


def draw_ring(scr, r_px, rnd):
    n = int(2 * math.pi * r_px)
    for k in range(0, n, 2):
        a = 2 * math.pi * k / n
        x, y = rnd.point(C + r_px * math.sin(a), C - r_px * math.cos(a), f"ring r={r_px:g} k={k}")
        if in_map(x, y):
            scr.px[y][x] = RING


def place_ring_labels(scr, rings, obstacles):
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
            scr.text(tx, ty, label, 1, RINGTXT, halo=BLACK)
            obstacles.append(box)
            break


def compose(h, codes, view, state, towns, pop, frame_clock, rnd, zoomed=False, warning=None, cap=8):
    """One screen, in the generator's order (A9.2). Returns the Screen and its placed labels (for the board's own)."""
    scr = Screen()
    render_map(scr, h, codes, view)
    scr.fill(0, 0, SIZE - 1, TOP_BAND - 1, BLACK)
    scr.fill(0, BOT_BAND, SIZE - 1, SIZE - 1, BLACK)
    fixed = [(0, 0, SIZE - 1, TOP_BAND - 1), (0, BOT_BAND, SIZE - 1, SIZE - 1)]
    ppm = view.ppm()
    rings = []
    for mi in (5, 15, 30):
        if 0.15 * view.R <= mi <= 0.85 * view.R:
            draw_ring(scr, mi * ppm, rnd)
            rings.append((mi * ppm, "%d" % mi))
    scr.text(117, 3, "N", 1, DIMTXT)
    if not zoomed:
        scr.text(int(C - text_w(frame_clock) / 2), 13, frame_clock, 1, state.get("timeInk", DIMTXT))
    else:
        tag = "ZOOM %d MI" % view.R
        x0 = int(C - text_w(frame_clock + "  " + tag) / 2)
        scr.text(x0, 13, frame_clock, 1, state.get("timeInk", DIMTXT))
        scr.text(x0 + text_w(frame_clock + "  ") + 1, 13, tag, 1, WHITE)
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
    placed, placed_info = [], []
    inner = 0.17 * view.R * ppm + 4
    ranked = sorted(towns, key=lambda t: -(pop(t) or 1000))
    for name, lat, lon in ranked:
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
            scr.fill(int(x) - 1, int(y) - 1, int(x) + 1, int(y) + 1, LABEL)
            scr.text(tx, int(y) - 3, label, 1, LABEL, halo=BLACK)
            placed.append(box)
            placed_info.append((label, side, box))
            break
    place_ring_labels(scr, rings, fixed + placed)
    for (x, y) in HOME:
        scr.px[y][x] = WHITE
    if warning:
        scr.text(int(C - text_w(warning["band1"]) / 2), 197, warning["band1"], 1, WHITE)
        scr.text(int(C - text_w(warning["band2"]) / 2), 207, warning["band2"], 1, WHITE)
    else:
        s, col = state["line"], state["colour"]
        scr.text(int(C - text_w(s, 2) / 2), 198, s, 2, col)
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


def r0(mock_dir, out_dir):
    towns = load_towns_pre_join()
    POP = generator_pop(mock_dir)
    pop = lambda t: POP.get(label_name(t[0]), 1000)
    hA, cA = decode(os.path.join(LOOP, "shsr-20240521-212400.wxc"))
    hD, cD = decode(os.path.join(LOOP, "shsr-20240521-220000.wxc"))
    hZ, cZ = decode(os.path.join(LOOP, "shsr-20240521-210800.wxc"))
    print(f"R0 inputs: {len(towns)} towns (a7f21fb, = b202ff3); POP table {len(POP)} entries (wxmock.py, R0 only); font {FONT_SHA[:12]}")
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
            "screen-proposed-dry.png": lambda rnd: compose(hD, cD, v60d, dict(line="RAIN %d MI %s" % (R(nD[0]), nD[1]), colour=DIMTXT), towns, pop, "5:00 PM", rnd),
            "screen-proposed-approaching.png": lambda rnd: compose(hA, cA, v60, dict(line="RAIN %d MI %s" % (R(nA[0]), nA[1]), colour=WHITE), towns, pop, "4:24 PM", rnd),
            "screen-proposed-herenow.png": lambda rnd: compose(hA, cA, v60, dict(line="RAIN NOW", colour=WHITE), towns, pop, "4:24 PM", rnd),
            "screen-proposed-warning.png": lambda rnd: compose(hA, cA, v60, {}, towns, pop, "4:24 PM", rnd, warning=warn),
        }
        for Rz in (60, 30, 15):
            jobs["screen-zoom-%d.png" % Rz] = (lambda Rz: lambda rnd: compose(
                hZ, cZ, View(hZ, Rz), dict(line="RAIN %d MI %s" % (R(nZ[0]), nZ[1]), colour=WHITE), towns, pop, "4:08 PM", rnd,
                zoomed=(Rz != 60), cap={60: 8, 30: 6, 15: 5}[Rz]))(Rz)
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


if __name__ == "__main__":
    a = sys.argv
    if len(a) >= 3 and a[1] == "--r0":
        out = a[a.index("--out") + 1] if "--out" in a else None
        sys.exit(r0(a[2], out))
    print(__doc__)
    sys.exit(2)
