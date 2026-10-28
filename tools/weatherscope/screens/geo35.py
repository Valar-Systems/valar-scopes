"""3.5b's geography for the independent screen reference (docs/weatherscope-stage3.md A10.2, A10.5, A12.16).

RX1  ref35's own BMPK decoder, written from docs/basemap-package-format.md §3-§4 (not from validate.py, not from any
     C++), checked item for item against validate.py on a package.
R0b  bendmock.py's three screens from its own inputs, in two modes:
       G  its rules (the corner disc, PIL's polygon fill, round() in the line DDA, PIL's triangle): must equal each
          PNG at every pixel (R0a')
       P  the spec (CodeAt's centre disc, the centre fill rule, floor(v + 0.5), the frozen glyph): every pixel that
          differs from G must be named by one of A10.5's four causes, each judged from the INPUTS, never from the
          difference (R0b'):
            1 the disc   the pixel's corner test and centre test disagree
            2 the fill   an exact rational even-odd test at the pixel centre disagrees with PIL's fill of a polygon
            3 the DDA    the rounding argument was exactly k + 0.5 (ref35's Rounder)
            4 the glyph  the pixel lies in a placed peak's 7 x 6 glyph box
Prints no coordinate: bendmock.py's centre is read at run time and used, never shown.

    python -I tools/weatherscope/screens/geo35.py --rx1 <package.bmpk>
    python -I tools/weatherscope/screens/geo35.py --r0b <mockup dir> --sources <dir> [--out <dir>]
"""
import ast, csv, glob, importlib.util, io, json, math, os, re, struct, sys, zipfile
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.dont_write_bytecode = True


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))
    spec.loader.exec_module(m)
    return m


ref35 = _load("ref35", os.path.join(HERE, "ref35.py"))
BASEMAP = os.path.join(ROOT, "tools", "weatherscope", "basemap")


# ------------------------------------------------------------------------------------------------ RX1: the decoder
class _R:
    def __init__(self, b):
        self.b, self.i = b, 0

    def u8(self):
        v = self.b[self.i]
        self.i += 1
        return v

    def var(self):
        v, s = 0, 0
        while True:
            c = self.u8()
            v |= (c & 0x7F) << s
            s += 7
            if not c & 0x80:
                return v

    def svar(self):
        z = self.var()
        return (z >> 1) ^ -(z & 1)

    def text(self):
        n = self.u8()
        t = bytes(self.b[self.i:self.i + n]).decode("utf-8")
        self.i += n
        return t


def _verts(r, n, origin=(0, 0)):
    """n vertices: the first absolute (added to `origin`, which is (0, 0) for every item of a valid reading), then
    n - 1 deltas (format §4)."""
    x, y = origin[0] + r.svar(), origin[1] + r.svar()
    out = [(x, y)]
    for _ in range(n - 1):
        x += r.svar()
        y += r.svar()
        out.append((x, y))
    return out


def decode_bmpk(b):
    """{'header': (...), 'sections': [(type, items)]} for a package already known valid. Written from §3-§4."""
    magic, ver, nsec, flags, lat0, lon0, rmm, tol, total, crc = struct.unpack_from("<4sBBHiiIIII", b, 0)
    out = []
    for k in range(nsec):
        typ, _, _, count, off, length = struct.unpack_from("<BBHIII", b, 32 + 16 * k)
        r = _R(b[off:off + length])
        if typ == 1:
            items = [_verts(r, r.var()) for _ in range(count)]
        elif typ == 2:
            names = [r.text() for _ in range(r.var())]
            lines = []
            for _ in range(count):
                idx = r.var()
                lines.append((idx, _verts(r, r.var(), (0, 0))))
            items = (names, lines)
        elif typ == 3:
            items = []
            for _ in range(count):
                cls = r.u8()
                items.append((cls, _verts(r, r.var(), (0, 0))))
        elif typ == 4:
            items = [(r.svar(), r.svar(), r.svar(), r.text()) for _ in range(count)]
        elif typ == 5:
            items = [(r.svar(), r.svar(), r.var(), r.text()) for _ in range(count)]
        elif typ == 6:
            rl, n, _ = struct.unpack_from("<IHH", b, off)
            items = (rl, n, bytes(b[off + 8:off + length]))
        elif typ == 7:
            items = bytes(b[off:off + length])
        else:
            items = None
        out.append((typ, items))
    return {"header": (lat0, lon0, rmm, tol), "sections": out}


def _canon(v):
    if isinstance(v, (list, tuple)):
        return [_canon(x) for x in v]
    return v


def rx1(path):
    b = open(path, "rb").read()
    validate = _load("validate", os.path.join(BASEMAP, "validate.py"))
    st, info = validate.validate(b)
    if st != "kOk":
        print(f"RX1: REFUSED by validate.py ({st}); RX1 needs a valid package (the rig)")
        return 2
    mine = decode_bmpk(b)["sections"]
    theirs = info["sections"]
    names = {1: "water", 2: "rivers", 3: "roads", 4: "peaks", 5: "places", 6: "terrain", 7: "provenance"}
    bad = 0
    print(f"RX1: {os.path.basename(path)}, {len(mine)} sections, ref35's decoder against validate.py")
    for (t1, a), (t2, c) in zip(mine, theirs):
        same = t1 == t2 and _canon(a) == _canon(c)
        n = len(a[1]) if t1 == 2 else (1 if t1 in (6, 7) else len(a))
        print(f"  {'ok  ' if same else 'FAIL'}  {names.get(t1, t1)}: {n} item(s) {'equal' if same else 'DIFFER'}")
        bad += not same
    ok = bad == 0 and len(mine) == len(theirs)
    print("RX1: " + ("0 differences" if ok else f"{bad} section(s) differ"))
    return 0 if ok else 1


# ------------------------------------------------------------------------------------------------ R0b: the inputs
WATER_FILL = (10, 34, 72)
WATER_LINE = (26, 70, 128)
ROAD = (62, 62, 62)
PEAK = (176, 176, 176)


def mockup_inputs(mock_dir, root):
    """Everything bendmock.py selects, read from its own source and the same public files. Never printed."""
    src = open(os.path.join(mock_dir, "bendmock.py"), encoding="utf-8").read().replace("\r\n", "\n")
    m = re.search(r"^HOME = \((-?\d+\.\d+), (-?\d+\.\d+)\)", src, re.M)
    home = (float(m.group(1)), float(m.group(2)))
    rivers = ast.literal_eval(re.search(r'r\["FULLNAME"\] in (\("[^)]*"\))', src).group(1))
    want = ast.literal_eval(re.search(r"^want = (\{.*?\})\n", src, re.S | re.M).group(1))
    bend_pop = ast.literal_eval(re.search(r"^BEND_POP = (\{.*?\})\n", src, re.S | re.M).group(1))
    min_area = int(re.search(r'load_polys\("tl_2024_\*_areawater\.shp", ([\d_]+)\)', src).group(1).replace("_", ""))
    sys.path.insert(0, BASEMAP)
    import shp, sources
    tiger = sources.TIGER
    lakes, rlines, roads = [], [], []
    for z in sorted(glob.glob(os.path.join(os.path.dirname(sources.local_path(root, tiger + "AREAWATER/x")), "*.zip"))):
        _, recs, tab = shp.read_layer(z)
        for rec, row in zip(recs, tab.rows):
            if rec is not None and (row.get("AWATER") or 0) >= min_area:
                for part in rec.parts:
                    lakes.append([(la, lo) for (lo, la) in part])          # every part, holes too, as bendmock.py
    for z in sorted(glob.glob(os.path.join(os.path.dirname(sources.local_path(root, tiger + "LINEARWATER/x")), "*.zip"))):
        _, recs, tab = shp.read_layer(z)
        for rec, row in zip(recs, tab.rows):
            if rec is not None and row.get("FULLNAME") in rivers:
                for part in rec.parts:
                    rlines.append([(la, lo) for (lo, la) in part])
    _, recs, tab = shp.read_layer(sources.local_path(root, sources.prisecroads("41")))
    for rec, row in zip(recs, tab.rows):
        if rec is not None and row.get("MTFCC") in ("S1100", "S1200") and "Bus" not in (row.get("FULLNAME") or ""):
            for part in rec.parts:
                roads.append([(la, lo) for (lo, la) in part])
    terr = Terrain(root, home)
    peaks = []
    with zipfile.ZipFile(sources.local_path(root, sources.gnis("41"))) as zf:
        text = zf.read("Text/DomesticNames_OR.txt").decode("utf-8-sig")
    for row in csv.DictReader(io.StringIO(text), delimiter="|"):
        if row["feature_class"] == "Summit" and row["feature_name"] in want:
            la, lo = float(row["prim_lat_dec"]), float(row["prim_long_dec"])
            peaks.append((row["feature_name"], la, lo, terr.at(la, lo)[1]))
    towns = []
    with zipfile.ZipFile(sources.local_path(root, sources.GAZETTEER)) as zf:
        text = zf.read("2025_Gaz_place_national.txt").decode("utf-8-sig")
    for row in csv.DictReader(io.StringIO(text), delimiter="|"):
        if row["USPS"] != "OR" or row["LSAD"].strip() != "25":
            continue
        la, lo = float(row["INTPTLAT"]), float(row["INTPTLONG"])
        d = math.hypot((la - home[0]) * 111, (lo - home[1]) * 111 * math.cos(math.radians(home[0]))) / ref35.KM_PER_MI
        if d <= 60:
            towns.append((row["NAME"].strip(), la, lo))
    pop = dict(ref35.generator_pop(mock_dir))
    pop.update(bend_pop)
    ranked_peaks = [(n, la, lo) for (n, la, lo, e) in sorted(peaks, key=lambda t: -t[3])]
    return dict(home=home, lakes=lakes, rivers=rlines, roads=roads, terr=terr, peaks=ranked_peaks, towns=towns,
                pop=pop, counts=dict(lakes=len(lakes), rivers=len(rlines), roads=len(roads), peaks=len(peaks),
                                     towns=len(towns)))


class Terrain:
    """bendmock.py's float32 hillshade over z9 tiles x 81-84, y 184-187, in its own operations and order."""

    def __init__(self, root, home):
        import numpy as np
        from PIL import Image
        sys.path.insert(0, BASEMAP)
        import sources
        self.np, self.home = np, home
        self.X0, self.Y0, Z = 81, 184, 9
        dem = np.zeros((4 * 256, 4 * 256), dtype=np.float32)
        for ty in range(4):
            for tx in range(4):
                b = sources.read_bytes(root, sources.terrarium(self.X0 + tx, self.Y0 + ty))
                a = np.asarray(Image.open(io.BytesIO(b)).convert("RGB"), dtype=np.float32)
                dem[ty * 256:(ty + 1) * 256, tx * 256:(tx + 1) * 256] = a[..., 0] * 256 + a[..., 1] + a[..., 2] / 256 - 32768
        gy, gx = np.gradient(dem)
        mpp = 156543.03 * math.cos(math.radians(home[0])) / (2 ** Z)
        slope = np.arctan(2.0 * np.hypot(gx, gy) / mpp)
        aspect = np.arctan2(-gx, gy)
        az, alt = math.radians(315), math.radians(45)
        shade = np.sin(alt) * np.cos(slope) + np.cos(alt) * np.sin(slope) * np.cos(az - aspect)
        shade = np.clip(shade, 0, 1)
        k = 4
        pad_ = np.pad(shade, k // 2, mode="edge")
        cs = np.cumsum(np.cumsum(pad_, 0), 1)
        cs = np.pad(cs, ((1, 0), (1, 0)))
        sm = (cs[k:, k:] - cs[:-k, k:] - cs[k:, :-k] + cs[:-k, :-k]) / (k * k)
        self.shade, self.dem = sm[:shade.shape[0], :shade.shape[1]], dem

    def at(self, lat, lon):
        n = 2 ** 9
        x = (lon + 180) / 360 * n
        y = (1 - math.log(math.tan(math.radians(lat)) + 1 / math.cos(math.radians(lat))) / math.pi) / 2 * n
        px, py = int((x - self.X0) * 256), int((y - self.Y0) * 256)
        if 0 <= px < self.dem.shape[1] and 0 <= py < self.dem.shape[0]:
            return float(self.shade[py, px]), float(self.dem[py, px])
        return 0.5, 0.0


class MockView(ref35.View):
    """bendmock.py's BendView: its centre, 60 mi."""

    def __init__(self, home, radius_mi):
        self.h, self.home, self.R = None, home, radius_mi
        rkm = radius_mi * ref35.KM_PER_MI
        self.rLat = rkm / ref35.KM_PER_DEG
        self.rLon = rkm / (ref35.KM_PER_DEG * max(math.cos(math.radians(home[0])), 0.01))


def _onscreen(pts):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return max(xs) >= -1 and min(xs) <= ref35.SIZE and max(ys) >= -1 and min(ys) <= ref35.SIZE


# ------------------------------------------------------------------------------------------------ the two basemaps
def base_g(inp, view, variant, rnd, fills):
    """G: bendmock.py's base_map + draw_polygon + draw_polyline, in its order. fills[i] = polygon i's PIL set."""
    from PIL import Image, ImageDraw
    img = [[ref35.BLACK] * ref35.SIZE for _ in range(ref35.SIZE)]
    if variant == "C":
        for y in range(ref35.TOP_BAND, ref35.BOT_BAND):
            lat = view.home[0] + (ref35.C - (y + 0.5)) / ref35.C * view.rLat
            for x in range(ref35.SIZE):
                if not ref35.in_map(x, y):
                    continue
                lon = view.home[1] + ((x + 0.5) - ref35.C) / ref35.C * view.rLon
                s, _ = inp["terr"].at(lat, lon)
                g = int(10 + 36 * s)
                img[y][x] = (g, g, g + 2)
    if variant in ("B", "C"):
        for i, poly in enumerate(inp["lakes"]):
            pts = [view.project(la, lo) for (la, lo) in poly]
            if not _onscreen(pts):
                continue
            layer = Image.new("L", (ref35.SIZE, ref35.SIZE), 0)
            ImageDraw.Draw(layer).polygon(pts, fill=255)
            lp = layer.load()
            fills[i] = {(x, y) for y in range(ref35.SIZE) for x in range(ref35.SIZE) if lp[x, y]}
            for (x, y) in fills[i]:
                if ref35.TOP_BAND <= y < ref35.BOT_BAND and ref35.in_map(x, y):
                    img[y][x] = WATER_FILL
        for j, line in enumerate(inp["rivers"]):
            _polyline(img, view, line, WATER_LINE, rnd, f"river {j}")
        for j, line in enumerate(inp["roads"]):
            _polyline(img, view, line, ROAD, rnd, f"road {j}")
    return img


def base_p(inp, view, variant, rnd):
    """P: the spec (A10.2): terrain at every pixel centre, water by the centre rule, lines by wxraster::Line."""
    img = [[ref35.BLACK] * ref35.SIZE for _ in range(ref35.SIZE)]
    if variant == "C":
        for y in range(ref35.SIZE):
            lat = view.home[0] + (ref35.C - (y + 0.5)) / ref35.C * view.rLat
            for x in range(ref35.SIZE):
                lon = view.home[1] + ((x + 0.5) - ref35.C) / ref35.C * view.rLon
                s, _ = inp["terr"].at(lat, lon)
                g = int(10 + 36 * s)
                img[y][x] = (g, g, g + 2)
    if variant in ("B", "C"):
        for poly in inp["lakes"]:
            pts = [view.project(la, lo) for (la, lo) in poly]
            if not _onscreen(pts):
                continue
            for (x, y) in centre_fill(pts):
                img[y][x] = WATER_FILL
        for j, line in enumerate(inp["rivers"]):
            _polyline(img, view, line, WATER_LINE, rnd, f"river {j}")
        for j, line in enumerate(inp["roads"]):
            _polyline(img, view, line, ROAD, rnd, f"road {j}")
    return img


def _polyline(img, view, line, col, rnd, site):
    """The DDA both modes share (bendmock.py's draw_polyline is wxraster::Line): only rnd's rounding differs."""
    prev = None
    for k, (la, lo) in enumerate(line):
        x, y = view.project(la, lo)
        if prev is not None and _onscreen([prev, (x, y)]):
            n = int(max(abs(x - prev[0]), abs(y - prev[1]))) + 1
            for t in range(n + 1):
                xx, yy = rnd.point(prev[0] + (x - prev[0]) * t / n, prev[1] + (y - prev[1]) * t / n, f"{site} seg {k} k={t}")
                if ref35.in_map(xx, yy):
                    img[yy][xx] = col
        prev = (x, y)


def centre_fill(pts):
    """A10.2's fill, in double: a pixel is filled when its centre is inside the ring by even-odd; an edge counts at a
    scanline when (y1 > py) != (y2 > py); the centre must lie in [x_a, x_b) between paired crossings."""
    out = []
    n = len(pts)
    ys = [p[1] for p in pts]
    for y in range(max(0, int(math.floor(min(ys))) - 1), min(ref35.SIZE, int(math.ceil(max(ys))) + 2)):
        py = y + 0.5
        xs = []
        for i in range(n):
            (x1, y1), (x2, y2) = pts[i], pts[(i + 1) % n]
            if (y1 > py) != (y2 > py):
                xs.append(x1 + (py - y1) * (x2 - x1) / (y2 - y1))
        xs.sort()
        for a, b in zip(xs[0::2], xs[1::2]):
            for x in range(max(0, int(math.ceil(a - 0.5))), min(ref35.SIZE, int(math.ceil(b - 0.5)))):
                out.append((x, y))
    return out


def oracle_inside(pts, x, y):
    """Cause 2's oracle: the same rule at (x + 1/2, y + 1/2) in exact rational arithmetic, independent of centre_fill."""
    px, py = Fraction(2 * x + 1, 2), Fraction(2 * y + 1, 2)
    inside = False
    n = len(pts)
    for i in range(n):
        x1, y1 = Fraction(pts[i][0]), Fraction(pts[i][1])
        x2, y2 = Fraction(pts[(i + 1) % n][0]), Fraction(pts[(i + 1) % n][1])
        if (y1 > py) != (y2 > py):
            xi = x1 + (py - y1) * (x2 - x1) / (y2 - y1)
            if xi <= px:
                inside = not inside
    return inside


# ------------------------------------------------------------------------------------------------ the glyphs
def glyph_g(scr, x, y):
    """bendmock.py's triangle: PIL's polygon outline at the float anchor, unmasked."""
    from PIL import Image, ImageDraw
    layer = Image.new("L", (ref35.SIZE, ref35.SIZE), 0)
    ImageDraw.Draw(layer).polygon([(x, y - 3), (x - 3, y + 2), (x + 3, y + 2)], outline=255)
    lp = layer.load()
    for yy in range(ref35.SIZE):
        for xx in range(ref35.SIZE):
            if lp[xx, yy]:
                scr.put(xx, yy, PEAK)


def glyph_p(scr, x, y):
    """The frozen glyph (Q32), anchored at (int(x), int(y)): ref35.PEAK_GLYPH's offsets."""
    ix, iy = int(x), int(y)
    for dx, dy in ref35.PEAK_GLYPH:
        scr.put(ix + dx, iy + dy, PEAK)


def glyph_box(x, y):
    ix, iy = int(x), int(y)
    return {(ix + dx, iy + dy) for dx in range(-3, 4) for dy in range(-3, 3)}


# ------------------------------------------------------------------------------------------------ R0a', R0b'
def r0b(mock_dir, root, out_dir=None, g_only=False):
    inp = mockup_inputs(mock_dir, root)
    c = inp["counts"]
    print(f"R0b inputs (bendmock.py's selections, the same public files): {c['lakes']} lake rings, {c['rivers']} river "
          f"parts, {c['roads']} road parts, {c['peaks']} summits, {c['towns']} towns; POP {len(inp['pop'])} entries; "
          f"font {ref35.FONT_SHA[:12]}")
    view = MockView(inp["home"], 60)
    pop = lambda t: inp["pop"].get(ref35.label_name(t[0]), 1000)
    state = dict(line="NO RAIN 60 MI", colour=ref35.DIMTXT)
    shots = {}
    for variant in ("A", "B", "C"):
        for mode in (("G",) if g_only else ("G", "P")):
            rnd = ref35.Rounder(mode)
            fills = {}
            base = base_g(inp, view, variant, rnd, fills) if mode == "G" else base_p(inp, view, variant, rnd)
            plog = []
            scr, _ = ref35.compose(None, None, view, state, inp["towns"], pop, "2:40 PM", rnd, base=base,
                                   corner=(mode == "G"), peaks=inp["peaks"] if variant != "A" else None,
                                   glyph=glyph_g if mode == "G" else glyph_p, peak_log=plog)
            shots[(variant, mode)] = dict(scr=scr, log=rnd.log, fills=fills, peaks=plog)
    ok, results = True, []
    print("R0a': G against each PNG (RGB888, 57,600 px)")
    for variant in ("A", "B", "C"):
        want = ref35.png_rgb(os.path.join(mock_dir, f"screen-bend-{variant}.png"))
        got = shots[(variant, "G")]["scr"].rgb888()
        diff = [i for i in range(ref35.SIZE * ref35.SIZE) if got[3 * i:3 * i + 3] != want[3 * i:3 * i + 3]]
        good = not diff
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'} screen-bend-{variant}.png: {57600 - len(diff)} of 57600 equal" +
              ("" if good else f"; UNEXPLAINED (first 10, x,y): {[(i % 240, i // 240) for i in diff[:10]]}"))
        results.append(dict(check="R0a'", screen=variant, differing=len(diff), sample=[(i % 240, i // 240) for i in diff[:50]]))
    if g_only:
        return shots, inp, view, ok
    print("R0b': P against G; every differing pixel named by a cause judged from the inputs")
    for variant in ("A", "B", "C"):
        g, p = shots[(variant, "G")], shots[(variant, "P")]
        gb, pb = g["scr"].rgb888(), p["scr"].rgb888()
        diff = [(i % 240, i // 240) for i in range(57600) if gb[3 * i:3 * i + 3] != pb[3 * i:3 * i + 3]]
        n1 = {(x, y) for y in range(ref35.TOP_BAND, ref35.BOT_BAND) for x in range(240)
              if ref35.in_map(x, y) != ((x + 0.5 - 120.0) ** 2 + (y + 0.5 - 120.0) ** 2 <= 120.0 * 120.0)}
        n3 = {tuple(e["G"]) for e in p["log"]} | {tuple(e["P"]) for e in p["log"]}
        n4 = set()
        for (_, x, y) in g["peaks"] + p["peaks"]:
            n4 |= glyph_box(x, y)
        # Every cause that names a pixel is counted (a pixel can be named by more than one: a lake's fill edge under a
        # glyph box is named by both). A pixel passes when at least one names it.
        causes = {1: 0, 2: 0, 3: 0, 4: 0}
        multi, unnamed = 0, []
        lakes_on = {i: [view.project(la, lo) for (la, lo) in inp["lakes"][i]] for i in g["fills"]}
        for px in diff:
            hit = {1: px in n1, 3: px in n3, 4: px in n4,
                   2: any((px in g["fills"][i]) != oracle_inside(lakes_on[i], *px) for i in g["fills"])}
            for k, v in hit.items():
                causes[k] += v
            if not any(hit.values()):
                unnamed.append(px)
            multi += sum(hit.values()) > 1
        good = not unnamed
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'} screen-bend-{variant}: {len(diff)} pixels differ; named by: disc {causes[1]}, "
              f"fill {causes[2]}, DDA {causes[3]}, glyph box {causes[4]} ({multi} by more than one)" +
              ("" if good else f"; UNNAMED {len(unnamed)}: {unnamed[:10]}"))
        results.append(dict(check="R0b'", screen=variant, differing=len(diff), causes=causes, unnamed=unnamed[:50]))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
        json.dump(dict(results=results), open(os.path.join(out_dir, "r0b.json"), "w"), indent=1)
    print("R0b: " + ("PASSES" if ok else "STOPS -- an unexplained or unnamed pixel is a finding about the spec"))
    return 0 if ok else 1


def main(argv):
    if "--rx1" in argv:
        return rx1(argv[argv.index("--rx1") + 1])
    if "--r0b" in argv:
        out = argv[argv.index("--out") + 1] if "--out" in argv else None
        return r0b(argv[argv.index("--r0b") + 1], argv[argv.index("--sources") + 1], out)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
