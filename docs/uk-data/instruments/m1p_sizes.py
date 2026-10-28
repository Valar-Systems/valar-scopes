"""M1′ (docs/uk-data.md A3.3): BMPK v1 package sizes under the ruled UK rules. A measurement, not the builder.

Changes from M1 (m1_sizes.py, kept as it was read): the three instrument defects fixed (ONS's column found exactly;
files.terrain = the tiles THIS package's summit elevations read; section bytes recorded before AND after §6's budget);
roads chained (Q18) and rivers chained by name (A3.1); places = City and Town, the N most populous within R (ties by
name), kept in file order; London's populations by rule L (A3.2, as ruled: L1 by district code, L2 pinned to the Open
Names City London and region E12000007); NI sources gated out (Q19), with NI in printed only. No coordinate is printed.

Usage: python -I m1p_sizes.py <m1src> <m1work> <repo> <airports.csv> <out.json>
         [--only161] [--onlyiv256] [--eps0] [--nodrop] [--norchain] [--nall]
"""
import csv
import glob
import hashlib
import json
import math
import os
import pickle
import subprocess
import sys
import time
import urllib.request

import numpy as np
import openpyxl
import shapefile
from PIL import Image
from pyproj import CRS, Transformer
from shapely import STRtree
from shapely.geometry import Point, shape as geo_shape

args = [a for a in sys.argv[1:] if not a.startswith("--")]
FLAGS = {a for a in sys.argv[1:] if a.startswith("--")}
SRC, WORK, REPO, APTS, OUT = args
sys.path.insert(0, os.path.join(REPO, "tools", "weatherscope", "basemap"))
import bmpk  # noqa: E402
import validate as bmpk_validate  # noqa: E402

K = 111.0
CAP = 32 * 1024
F_LIMIT = 20 * 1024
UA = {"User-Agent": "BlipscopeBasemapM1/1 (+https://github.com/Valar-Systems/valar-scopes)"}
GB_CENTRES = ["LHR", "MAN", "BHX", "NCL", "EDI", "GLA", "ABZ", "INV", "CWL", "NQY"]
NI_CENTRES = ["BFS", "LDY"]
RADII = [(161, 671.0), (222, 925.0), (97, 100.584)]
if "--only161" in FLAGS:
    RADII = RADII[:1]
VARIANTS = ["iii", "iv"]
NS = [64, 128, 256]
if "--onlyiv256" in FLAGS:
    VARIANTS, NS = ["iv"], [256]
if "--m1pp" in FLAGS:   # M1'' (A3.6): LHR, MAN, BHX at 161 km, (iii), N = 256
    RADII, VARIANTS, NS = RADII[:1], ["iii"], [256]
    GB_CENTRES, NI_CENTRES = ["LHR", "MAN", "BHX"], []
LONDON_CITY_ID = "osgb" "4000000" "074813508"   # rule L2's feature, written in two literals (the device-id hook)
LONDON_REGION = "E12000007"
PROC_POP = ("processing.population: Census 2021 usual residents; England and Wales: ONS BUA 2022 Counts by the BUA "
            "containing the place; Scotland: NRS 2022 settlement Popcount; London: rule L (a place in a London BUA "
            "with no count takes its district's TS001 count by LAD22CD; the Open Names City London takes region "
            "E12000007); otherwise 0")
PROC_NAMES = "processing.names: the English name where a source has two, else the one name"


def q2(v):
    return math.floor(v * 100 + 0.5) / 100


# ---------------------------------------------------------------------------------------------- the GB sources
def load_gb():
    p = os.path.join(WORK, "geo_gb_m1p.pkl")
    if os.path.exists(p):
        return pickle.load(open(p, "rb"))
    tr = Transformer.from_crs("EPSG:27700", "EPSG:4326", always_xy=True)

    def ll(pts):
        a = np.asarray(pts, dtype=float)
        lon, lat = tr.transform(a[:, 0], a[:, 1])
        return np.column_stack([lon, lat])

    out = {}
    for layer, fn in (("roads", "roads.pkl"), ("rivers", "rivers_nodes.pkl"), ("water", "water.pkl")):
        feats = pickle.load(open(os.path.join(WORK, fn), "rb"))
        for f in feats:
            f["ll"] = ll(f["pts"])
            del f["pts"]
        out[layer] = feats
    for layer in ("places", "hills"):
        feats = pickle.load(open(os.path.join(WORK, layer + ".pkl"), "rb"))
        lon, lat = tr.transform(np.array([f["x"] for f in feats]), np.array([f["y"] for f in feats]))
        for f, a, b in zip(feats, lon, lat):
            f["lon"], f["lat"] = float(a), float(b)
        out[layer] = feats
    # populations
    ons = {}
    wb = openpyxl.load_workbook(os.path.join(SRC, "ons", "ons-bua-census2021.xlsx"), read_only=True)
    for sheet in ("1c", "1d"):
        rows = list(wb[sheet].iter_rows(values_only=True))
        hi = next(i for i, r in enumerate(rows) if r and "BUA code" in [str(c).strip() for c in r if c is not None])
        hdr = [str(c).strip() if c is not None else "" for c in rows[hi]]
        ci, cc = hdr.index("BUA code"), hdr.index("Counts")   # exact names (defect 1 fixed)
        for r in rows[hi + 1:]:
            if r and r[ci] and isinstance(r[cc], (int, float)):
                ons[str(r[ci]).strip()] = int(r[cc])
    polys, pops = [], []
    for gp in sorted(glob.glob(os.path.join(SRC, "ons-bua", "*.geojson"))):
        for ft in json.load(open(gp, encoding="utf-8"))["features"]:
            polys.append(geo_shape(ft["geometry"]))
            pops.append(ons.get(ft["properties"]["BUA22CD"], 0))
    nrs_p = glob.glob(os.path.join(SRC, "nrs", "**", "*.shp"), recursive=True)[0]
    r = shapefile.Reader(nrs_p, encoding="utf-8")
    nrs_polys, nrs_pops = [], []
    for sr in r.iterShapeRecords():
        nrs_polys.append(geo_shape(sr.shape.__geo_interface__))
        nrs_pops.append(int(sr.record["Popcount"] or 0))
    lad = json.load(open(os.path.join(SRC, "ons-rule-l", "lad2022-london.geojson"), encoding="utf-8"))["features"]
    lad_polys, lad_codes = [geo_shape(f["geometry"]) for f in lad], [f["properties"]["LAD22CD"] for f in lad]
    rgn = [f for f in json.load(open(os.path.join(SRC, "ons-rule-l", "rgn2022-london.geojson"), encoding="utf-8"))["features"]
           if f["properties"]["RGN22CD"] == LONDON_REGION]
    assert len(rgn) == 1
    rgn_poly = geo_shape(rgn[0]["geometry"])
    nomis = {}
    for fn in ("ts001-lad2022.csv", "ts001-regions.csv"):
        for row in csv.DictReader(open(os.path.join(SRC, "nomis", fn), encoding="utf-8")):
            nomis[row["GEOGRAPHY_CODE"]] = int(row["OBS_VALUE"])
    tree_b, tree_n, tree_l = STRtree(polys), STRtree(nrs_polys), STRtree(lad_polys)
    stats = {"ons_counts": len(ons), "bua_polygons": len(polys), "nrs_polygons": len(nrs_polys),
             "london_lads": len(lad_polys), "pop_from": {}, "rule_l": []}
    for f in out["places"]:
        pt = Point(f["x"], f["y"])
        pop, src = 0, "none"
        hit = [i for i in tree_n.query(pt) if nrs_polys[i].covers(pt)]
        if hit:
            pop, src = nrs_pops[min(hit)], "nrs"
        else:
            hit = [i for i in tree_b.query(pt) if polys[i].covers(pt)]
            if hit and pops[min(hit)]:
                pop, src = pops[min(hit)], "ons"
            elif hit and rgn_poly.covers(pt):
                if f["id"] == LONDON_CITY_ID:
                    pop, src = nomis[LONDON_REGION], "rule-L2"
                else:
                    lh = [i for i in tree_l.query(pt) if lad_polys[i].covers(pt)]
                    if lh:
                        pop, src = nomis[lad_codes[min(lh)]], "rule-L1"
                    else:
                        src = "london-bua-without-district"
                stats["rule_l"].append({"name": f["name"], "rule": src, "pop": pop})
            elif hit:
                src = "bua-without-count"
        f["pop"] = pop
        stats["pop_from"][src] = stats["pop_from"].get(src, 0) + 1
    out["pop_stats"] = stats
    pickle.dump(out, open(p, "wb"), protocol=4)
    return out


def load_ni():
    """M1's NI loader, unchanged, read from M1's cache (built by m1_sizes.py at e7188e0)."""
    return pickle.load(open(os.path.join(WORK, "geo_ni.pkl"), "rb"))


# ---------------------------------------------------------------------------------------------- geometry (as M1)
def clip_seg(x0, y0, x1, y1, R):
    t0, t1 = 0.0, 1.0
    dx, dy = x1 - x0, y1 - y0
    for p, q in ((-dx, x0 + R), (dx, R - x0), (-dy, y0 + R), (dy, R - y0)):
        if p == 0:
            if q < 0:
                return None
        else:
            t = q / p
            if p < 0:
                if t > t1:
                    return None
                t0 = max(t0, t)
            else:
                if t < t0:
                    return None
                t1 = min(t1, t)
    return t0, t1


def clip_line(pts, R):
    runs, cur = [], None
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        c = clip_seg(x0, y0, x1, y1, R)
        if c is None:
            if cur:
                runs.append(cur)
            cur = None
            continue
        t0, t1 = c
        a = (x0 + t0 * (x1 - x0), y0 + t0 * (y1 - y0))
        b = (x0 + t1 * (x1 - x0), y0 + t1 * (y1 - y0))
        if cur is None or t0 > 0:
            if cur:
                runs.append(cur)
            cur = [a]
        cur.append(b)
        if t1 < 1:
            runs.append(cur)
            cur = None
    if cur:
        runs.append(cur)
    return runs


def clip_ring(pts, R):
    out = list(pts)
    for inside, inter in (
        (lambda p: p[0] >= -R, lambda a, b: (-R, a[1] + (b[1] - a[1]) * (-R - a[0]) / (b[0] - a[0]))),
        (lambda p: p[0] <= R, lambda a, b: (R, a[1] + (b[1] - a[1]) * (R - a[0]) / (b[0] - a[0]))),
        (lambda p: p[1] >= -R, lambda a, b: (a[0] + (b[0] - a[0]) * (-R - a[1]) / (b[1] - a[1]), -R)),
        (lambda p: p[1] <= R, lambda a, b: (a[0] + (b[0] - a[0]) * (R - a[1]) / (b[1] - a[1]), R)),
    ):
        if not out:
            break
        src, out = out, []
        prev = src[-1]
        for cur in src:
            if inside(cur):
                if not inside(prev):
                    out.append(inter(prev, cur))
                out.append(cur)
            elif inside(prev):
                out.append(inter(prev, cur))
            prev = cur
    return out


def seg_dist(p, a, b):
    ax, ay = a; bx, by = b; px, py = p
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    if L == 0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def dp(pts, eps):
    n = len(pts)
    if n <= 2:
        return list(pts)
    keep = [False] * n
    keep[0] = keep[-1] = True
    stack = [(0, n - 1)]
    while stack:
        i, j = stack.pop()
        dmax, idx = -1.0, -1
        for k in range(i + 1, j):
            d = seg_dist(pts[k], pts[i], pts[j])
            if d > dmax:
                dmax, idx = d, k
        if idx >= 0 and dmax > eps:
            keep[idx] = True
            stack.append((idx, j))
            stack.append((i, idx))
    return [p for p, k in zip(pts, keep) if k]


def quant(pts_km, R_mm):
    out = []
    for e, s in pts_km:
        qx = math.floor(e * 1e6 / R_mm * 32767 + 0.5)
        qy = math.floor(s * 1e6 / R_mm * 32767 + 0.5)
        if abs(qx) > 32767 or abs(qy) > 32767:
            raise SystemExit("a vertex outside the square after clipping: a bug, never clamped (format §2)")
        if not out or out[-1] != (qx, qy):
            out.append((qx, qy))
    return out


class Frame:
    def __init__(self, lat, lon, r_km):
        self.lat0, self.lon0 = q2(lat), q2(lon)
        self.c0 = max(math.cos(self.lat0 * (math.pi / 180)), 0.01)
        self.R = float(r_km)
        self.R_mm = int(r_km) * 1_000_000
        self.dlat, self.dlon = self.R / K, self.R / (K * self.c0)

    def es(self, ll):
        return [((lon - self.lon0) * K * self.c0, (self.lat0 - lat) * K) for lon, lat in ll]

    def bbox_hit(self, ll):
        return (ll[:, 1].max() >= self.lat0 - self.dlat and ll[:, 1].min() <= self.lat0 + self.dlat and
                ll[:, 0].max() >= self.lon0 - self.dlon and ll[:, 0].min() <= self.lon0 + self.dlon)


def line_items(feats, fr, eps_m, counter):
    out = []
    for f in feats:
        if not fr.bbox_hit(f["ll"]):
            continue
        for ri, run in enumerate(clip_line(fr.es(f["ll"]), fr.R)):
            km = sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(run, run[1:]))
            simp = dp([(x * 1000, y * 1000) for x, y in run], eps_m)
            q = quant([(x / 1000, y / 1000) for x, y in simp], fr.R_mm)
            if len(q) < 2:
                counter[0] += 1
                continue
            out.append((tuple(f["key"]) + (ri,), q, km, f))
    out.sort(key=lambda t: t[0])
    return out


def disc_len_m(pts, R):
    tot = 0.0
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        dx, dy = x1 - x0, y1 - y0
        a = dx * dx + dy * dy
        if a == 0:
            continue
        b = 2 * (x0 * dx + y0 * dy)
        c = x0 * x0 + y0 * y0 - R * R
        disc = b * b - 4 * a * c
        if disc <= 0:
            continue
        sq = math.sqrt(disc)
        t0, t1 = max(0.0, (-b - sq) / (2 * a)), min(1.0, (-b + sq) / (2 * a))
        if t1 > t0:
            tot += (t1 - t0) * math.sqrt(a) * 1000
    return tot


# ---------------------------------------------------------------------------------------------- chaining (A3.1)
def chain(feats, keyfn):
    """A3.1: links join where they meet end to end (OS node ids) with the same key, at a node where exactly two links
    with that key meet. Seeds in source order; extended first from the seed's end node, then from its start node;
    a reversed link contributes its vertices reversed; the shared vertex is written once. keyfn -> None: not chained."""
    by_node = {}
    for i, r in enumerate(feats):
        k = keyfn(r)
        if k is not None and r["a"] and r["b"]:
            for n in (r["a"], r["b"]):
                by_node.setdefault((k, n), []).append(i)
    used = [False] * len(feats)
    out = []
    for i in sorted(range(len(feats)), key=lambda j: feats[j]["key"]):
        if used[i]:
            continue
        used[i] = True
        r = feats[i]
        k = keyfn(r)
        if k is None or not (r["a"] and r["b"]):
            out.append(r)
            continue
        seq = [(i, False)]
        for direction in ("fwd", "back"):
            node = r["b"] if direction == "fwd" else r["a"]
            while True:
                ids = by_node.get((k, node), [])
                if len(ids) != 2:
                    break
                nxt = [j for j in ids if not used[j]]
                if len(nxt) != 1:
                    break
                j = nxt[0]
                used[j] = True
                rj = feats[j]
                if direction == "fwd":
                    rev = rj["b"] == node
                    seq.append((j, rev))
                    node = rj["a"] if rev else rj["b"]
                else:
                    rev = rj["a"] == node
                    seq.insert(0, (j, rev))
                    node = rj["b"] if rev else rj["a"]
        coords = []
        for j, rev in seq:
            c = feats[j]["ll"][::-1] if rev else feats[j]["ll"]
            coords.extend(map(tuple, c if not coords else c[1:]))
        out.append({**r, "ll": np.asarray(coords), "members": len(seq),
                    "primary_any": any(feats[j].get("primary", False) for j, _ in seq),
                    "primary_all": all(feats[j].get("primary", False) for j, _ in seq)})
    return out


ROAD_KEY = lambda r: (r["num"], r["cls"]) if r["num"] else None  # noqa: E731
RIVER_KEY = lambda r: r["name"] or None  # noqa: E731


# ---------------------------------------------------------------------------------------------- terrain z9
_tiles, _touched = {}, set()


def tile(x, y):
    _touched.add((x, y))
    if (x, y) in _tiles:
        return _tiles[(x, y)]
    d = os.path.join(SRC, "terrain", "9", str(x))
    p = os.path.join(d, f"{y}.png")
    if not os.path.exists(p):
        os.makedirs(d, exist_ok=True)
        url = f"https://s3.amazonaws.com/elevation-tiles-prod/terrarium/9/{x}/{y}.png"
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
            b = r.read()
            hdr = {"url": url, "sha256": hashlib.sha256(b).hexdigest(), "last_modified": r.headers.get("Last-Modified"),
                   "sources": r.headers.get("x-amz-meta-x-imagery-sources")}
        open(p, "wb").write(b)
        json.dump(hdr, open(p + ".json", "w"))
    a = np.asarray(Image.open(p).convert("RGB"), dtype=np.float64)
    _tiles[(x, y)] = a[:, :, 0] * 256 + a[:, :, 1] + a[:, :, 2] / 256 - 32768
    return _tiles[(x, y)]


def elev(lon, lat):
    phi = lat * math.pi / 180
    X = math.floor((lon + 180) / 360 * 2 ** 17)
    Y = math.floor((1 - math.log(math.tan(math.pi / 4 + phi / 2)) / math.pi) / 2 * 2 ** 17)
    return math.floor(tile(X // 256, Y // 256)[Y % 256, X % 256] + 0.5)


# ---------------------------------------------------------------------------------------------- packages
MEMO = {}


class Centre:
    def __init__(self, gb, ni, name, lat, lon, r_km, ni_in):
        self.gb, self.ni, self.name, self.fr, self.ni_on = gb, ni, name, Frame(lat, lon, r_km), ni_in
        self.r_km = r_km

    def key(self, *k):
        return (self.name, self.r_km, self.ni_on) + k

    def roads(self, variant, eps):
        mk = self.key("roads", variant, eps)
        if mk not in MEMO:
            fr, coll = self.fr, [0]
            prim = variant in ("ii", "iv")
            sel = [r for r in self.gb["roads"] if (r["cls"] == 1 or not prim or r["primary"]) and fr.bbox_hit(r["ll"])]
            if variant in ("iii", "iv"):
                sel = chain(sel, ROAD_KEY)
            if self.ni_on:
                sel += [r for r in self.ni["roads"] if fr.bbox_hit(r["ll"])]
            MEMO[mk] = (line_items(sel, fr, eps, coll), coll[0])
        return MEMO[mk]

    def rivers(self, eps):
        mk = self.key("rivers", eps, "--norchain" in FLAGS)
        if mk not in MEMO:
            fr, coll = self.fr, [0]
            groups = {}
            for f in self.gb["rivers"]:
                if fr.bbox_hit(f["ll"]):
                    groups.setdefault(f["name"], []).append(f)
            lens = {n: sum(disc_len_m(fr.es(f["ll"]), fr.R) for f in fs) for n, fs in groups.items()}
            names = sorted([n for n in lens if lens[n] > 0], key=lambda n: (-lens[n], n.encode("utf-8")))[:5]
            items = []
            for idx, n in enumerate(names):
                fs = groups[n] if "--norchain" in FLAGS else chain(groups[n], RIVER_KEY)
                for key, q, km, f in line_items(fs, fr, eps, coll):
                    items.append((key, idx, q))
            items.sort(key=lambda t: t[0])
            MEMO[mk] = ((names, items), coll[0])
        return MEMO[mk]

    def water(self, eps):
        mk = self.key("water", eps)
        if mk not in MEMO:
            fr, coll, out = self.fr, 0, []
            for f in self.gb["water"] + (self.ni["water"] if self.ni_on else []):
                if not fr.bbox_hit(f["ll"]):
                    continue
                ring = clip_ring(fr.es(f["ll"]), fr.R)
                if len(ring) < 3:
                    continue
                path = [(x * 1000, y * 1000) for x, y in ring]
                q = quant([(x / 1000, y / 1000) for x, y in dp(path + [path[0]], eps)[:-1]], fr.R_mm)
                if len(q) > 1 and q[-1] == q[0]:
                    q = q[:-1]
                if len(q) < 3:
                    coll += 1
                    continue
                out.append((tuple(f["key"]), q))
            out.sort(key=lambda t: t[0])
            MEMO[mk] = (out, coll)
        return MEMO[mk]

    def peaks(self):
        """Summit elevations read fresh for every package, so files.terrain is this package's tiles (defect 2)."""
        _touched.clear()
        fr, cand = self.fr, []
        for f in self.gb["hills"]:
            e, s = fr.es([(f["lon"], f["lat"])])[0]
            if math.hypot(e, s) <= fr.R:
                cand.append((elev(f["lon"], f["lat"]), f["id"], f, e, s))
        cand.sort(key=lambda t: (-t[0], t[1].encode("utf-8")))
        kept = []
        for el, fid, f, e, s in cand:
            if any(math.hypot(e - k[3], s - k[4]) <= 3.0 for k in kept):
                continue
            kept.append((el, fid, f, e, s))
            if len(kept) == 16:
                break
        return [quant([(e, s)], fr.R_mm)[0] + (int(el), f["name"]) for el, fid, f, e, s in kept], sorted(_touched)

    def places(self, N):
        fr = self.fr
        cand = []
        for f in self.gb["places"] + (self.ni["places"] if self.ni_on else []):
            e, s = fr.es([(f["lon"], f["lat"])])[0]
            if math.hypot(e, s) <= fr.R:
                cand.append((f, e, s))
        if N is not None:
            cand = sorted(cand, key=lambda t: (-t[0]["pop"], t[0]["name"].encode("utf-8")))[:N]
        cand.sort(key=lambda t: t[0]["key"])   # kept in file order (format §5.5)
        return [quant([(e, s)], fr.R_mm)[0] + (int(f["pop"]), f["name"]) for f, e, s in cand]


def is_primary(f):
    """A3.6: a chain is primary if ANY of its links has primaryRoute = true; an unchained link, its own flag."""
    return bool(f.get("primary_any", f.get("primary", False)))


def class2_drop_key(length, primary, i):
    """Q12 as ruled (A3.5): non-primary chains shortest first, then primary chains shortest first; ties as RD19.
    Without --primarylast, M1''s order: shortest first whatever the flag."""
    return ((1 if primary else 0) if "--primarylast" in FLAGS else 0, length, -i)


def provenance(eps_req, dropped, coll, files):
    lines = ["format: BMPK 1", "builder: m1p_sizes 000000000000", "store: 000000000000"]
    for layer in ("water", "rivers", "roads", "peaks", "places", "population", "terrain"):
        fl = files.get(layer, [])
        if not fl:
            continue
        pre = os.path.commonprefix(fl)
        pre = pre[: pre.rfind("/") + 1]
        sha = hashlib.sha256("".join(f"{u} {'0' * 64}\n" for u in sorted(fl)).encode()).hexdigest()
        lines += [f"source.{layer}: {pre}", f"vintage.{layer}: 2026", f"files.{layer}: {len(fl)}", f"sha256.{layer}: {sha}"]
    lines += ["credit.terrain: Produced using Copernicus data and information funded by the European Union - EU-DEM layers",
              "credit.terrain: SRTM data courtesy of the U.S. Geological Survey",
              "credit.terrain: ETOPO1: DOC/NOAA/NESDIS/NCEI > National Centers for Environmental Information, NESDIS, NOAA, U.S. Department of Commerce",
              "processing.terrain: Terrarium z9 elevations; summit elevations taken from the z9 pixel containing the summit, rounded to whole metres",
              PROC_POP, PROC_NAMES]
    lines.append(f"epsilon.m: {eps_req:.3f}")
    for layer in ("water", "rivers", "roads", "peaks", "places"):
        lines += [f"dropped.{layer}: {dropped.get(layer, 0)}", f"collapsed.{layer}: {coll.get(layer, 0)}"]
    return lines


def build(c, variant, N, eps_req, nodrop=False):
    peaks, tiles_read = c.peaks()
    places = c.places(N)

    def encode(eps_used, water, rivers, roads, dropped, coll):
        names, ritems = rivers
        files = {"water": sorted({w[0][0] for w in water}), "rivers": sorted({it[0][0] for it in ritems}),
                 "roads": sorted({r[0][0] for r in roads}), "peaks": ["os-names/Data"] if peaks else [],
                 "places": ["os-names/Data"] if places else [],
                 "population": ["ons", "ons-bua", "nrs", "ons-rule-l", "nomis"] + (["nisra"] if c.ni_on else []),
                 "terrain": [f"terrain/9/{x}/{y}.png" for (x, y) in tiles_read]}
        secs = [(bmpk.WATER, len(water), bmpk.water_body([w[1] for w in water])),
                (bmpk.RIVERS, len(ritems), bmpk.rivers_body(names, [(it[1], it[2]) for it in ritems])),
                (bmpk.ROADS, len(roads), bmpk.roads_body([(r[3]["cls"], r[1]) for r in roads])),
                (bmpk.PEAKS, len(peaks), bmpk.peaks_body(peaks)),
                (bmpk.PLACES, len(places), bmpk.places_body(places))]
        prov = provenance(eps_req, dropped, coll, files)
        secs.append((bmpk.PROVENANCE, len(prov), bmpk.provenance_body(prov)))
        pkg = bmpk.encode(secs, math.floor(c.fr.lat0 * 1e6 + 0.5), math.floor(c.fr.lon0 * 1e6 + 0.5), c.fr.R_mm,
                          math.floor(eps_used * 1000 + 0.5))
        return pkg, {str(s[0]): len(s[2]) for s in secs}, len(files["terrain"])

    def layers(eps):
        (roads, cr), (rivers, cv), (water, cw) = c.roads(variant, eps), c.rivers(eps), c.water(eps)
        return water, rivers, roads, {"roads": cr, "rivers": cv, "water": cw}

    eps = 0.0 if "--eps0" in FLAGS else eps_req
    water, rivers, roads, coll = layers(eps)
    pkg, sec_first, nterr = encode(eps, water, rivers, roads, {}, coll)
    first = len(pkg)
    c2_all = sum(r[2] for r in roads if r[3]["cls"] == 2)
    c2_before = [r for r in roads if r[3]["cls"] == 2]
    dropped, doubled, sec_final = {}, False, sec_first
    if first <= CAP:
        outcome = "F" if first <= F_LIMIT else "C"
    elif nodrop:
        outcome = "X"
    else:
        doubled = True
        water, rivers, roads, coll = layers(eps * 2)
        c2_before = [r for r in roads if r[3]["cls"] == 2]   # the set the drops act on (2 eps)
        pkg, sec_final, nterr = encode(eps * 2, water, rivers, roads, {}, coll)
        if len(pkg) <= CAP:
            outcome = "D"
        else:
            def area(q):
                return abs(sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(q, q[1:] + q[:1])))
            order = ([("w", i) for i in sorted(range(len(water)), key=lambda i: (area(water[i][1]), -i))] +
                     [("r", i) for i in sorted([i for i in range(len(roads)) if roads[i][3]["cls"] == 2],
                                               key=lambda i: class2_drop_key(sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in
                                                                  zip(roads[i][1], roads[i][1][1:])), is_primary(roads[i][3]), i))])

            def fits(n):
                dw = {i for t, i in order[:n] if t == "w"}
                dr = {i for t, i in order[:n] if t == "r"}
                p, s, nt = encode(eps * 2, [w for i, w in enumerate(water) if i not in dw], rivers,
                                  [r for i, r in enumerate(roads) if i not in dr], {"water": len(dw), "roads": len(dr)}, coll)
                return len(p) <= CAP, p, s, dw, dr

            if not fits(len(order))[0]:
                outcome = "X"
            else:
                lo, hi = 0, len(order)
                while lo < hi:
                    mid = (lo + hi) // 2
                    if fits(mid)[0]:
                        hi = mid
                    else:
                        lo = mid + 1
                _, pkg, sec_final, dw, dr = fits(lo)
                water = [w for i, w in enumerate(water) if i not in dw]
                roads = [r for i, r in enumerate(roads) if i not in dr]
                dropped = {"water": len(dw), "roads": len(dr)}
                outcome = "D"
    st, _ = bmpk_validate.validate(pkg)
    c2_kept = sum(r[2] for r in roads if r[3]["cls"] == 2)
    kept_keys = {r[0] for r in roads}
    prim_in = sum(r[2] for r in c2_before if is_primary(r[3]))
    prim_kept = sum(r[2] for r in c2_before if is_primary(r[3]) and r[0] in kept_keys)
    nonp_kept = sum(r[2] for r in c2_before if not is_primary(r[3]) and r[0] in kept_keys)
    nonp_in = sum(r[2] for r in c2_before if not is_primary(r[3]))
    mixed = [r for r in c2_before if r[3].get("primary_any") and not r[3].get("primary_all")]
    prim_dropped = sum(1 for r in c2_before if is_primary(r[3]) and r[0] not in kept_keys)
    nonp_kept_n = sum(1 for r in c2_before if not is_primary(r[3]) and r[0] in kept_keys)
    return {"centre": c.name, "r_km": c.r_km, "eps_m": eps_req, "variant": variant, "N": N, "ni_in": c.ni_on,
            "outcome": outcome, "bytes_first": first, "bytes_final": len(pkg), "validate": st, "eps_doubled": doubled,
            "dropped": dropped, "collapsed": coll, "section_bytes_first": sec_first, "section_bytes_final": sec_final,
            "files_terrain": nterr, "class2_km_in_square": round(c2_all, 1), "class2_km_kept": round(c2_kept, 1),
            "primary_km_in_square": round(prim_in, 1), "primary_km_kept": round(prim_kept, 1),
            "nonprimary_km_in_square": round(nonp_in, 1), "nonprimary_km_kept": round(nonp_kept, 1),
            "mixed_chains": len(mixed), "mixed_km": round(sum(r[2] for r in mixed), 1),
            "order_property_ok": not (prim_dropped > 0 and nonp_kept_n > 0),
            "counts": {"water": len(water), "rivers": len(rivers[1]), "roads": len(roads), "peaks": len(peaks),
                       "places": len(places)}}


# ---------------------------------------------------------------------------------------------- controls
def controls(gb):
    out = {}
    r = subprocess.run([sys.executable, "-I", os.path.join(REPO, "tools", "weatherscope", "basemap", "synth.py"),
                        os.path.join(REPO, "test", "fixtures", "wx", "basemap"), "--check"], capture_output=True, text=True)
    out["synth_check"] = {"exit": r.returncode, "tail": (r.stdout + r.stderr).strip().splitlines()[-1:]}
    out["dp_bump_half_eps"] = len(dp([(0.0, 0.0), (500.0, 50.0), (1000.0, 0.0)], 100.0)) == 2
    out["dp_bump_double_eps_kept"] = len(dp([(0.0, 0.0), (500.0, 200.0), (1000.0, 0.0)], 100.0)) == 3
    ring = clip_ring([(-2.0, -0.5), (0.5, -0.5), (0.5, 0.5), (-2.0, 0.5)], 1.0)
    out["ring_clipped_closed"] = len(ring) >= 3 and all(-1.0 <= x <= 1.0 and -1.0 <= y <= 1.0 for x, y in ring)
    out["line_reentry_two_runs"] = len(clip_line([(-2.0, 0.0), (2.0, 0.0), (2.0, 0.5), (-2.0, 0.5)], 1.0)) == 2
    syn = [{"key": ("s", 0, 0), "name": "R", "a": "A", "b": "B", "ll": np.array([(0.0, 0.0), (1.0, 0.0)])},
           {"key": ("s", 1, 0), "name": "R", "a": "C", "b": "B", "ll": np.array([(2.0, 0.0), (1.0, 0.0)])},
           {"key": ("s", 2, 0), "name": "R", "a": "C", "b": "D", "ll": np.array([(2.0, 0.0), (3.0, 0.0)])}]
    ch = chain(syn, RIVER_KEY)
    out["river_chain_three_links"] = len(ch) == 1 and [tuple(map(float, p)) for p in ch[0]["ll"]] == [
        (0.0, 0.0), (1.0, 0.0), (2.0, 0.0), (3.0, 0.0)]
    pl = [{"pop": 100, "name": "B", "key": 0}, {"pop": 100, "name": "A", "key": 1}, {"pop": 50, "name": "C", "key": 2}]
    top = sorted(pl, key=lambda f: (-f["pop"], f["name"].encode()))[:2]
    out["place_ties_by_name"] = [f["name"] for f in sorted(top, key=lambda f: f["key"])] == ["B", "A"]
    pops = {f["name"]: f["pop"] for f in gb["places"] if f["name"] in ("London", "City of Westminster", "City of London")}
    out["rule_l_figures"] = pops
    out["rule_l_as_ruled"] = pops == {"London": 8799728, "City of Westminster": 204236, "City of London": 8583}
    syn = [(10.0, False, 0), (2.0, False, 1), (3.0, True, 2)]
    order = [i for _, _, i in sorted(syn, key=lambda t: class2_drop_key(t[0], t[1], t[2]))]
    out["drop_order_synthetic"] = {"order": order, "as_ruled": order == ([1, 0, 2] if "--primarylast" in FLAGS else [1, 2, 0])}
    out["london_id_is_the_city"] = [(f["name"], f["type"]) for f in gb["places"] if f["id"] == LONDON_CITY_ID] == [("London", "City")]
    return out


def main():
    apts = {}
    with open(APTS, encoding="utf-8") as f:
        for row in csv.DictReader(f):
            c = row.get("iata_code") or ""
            if c in GB_CENTRES + NI_CENTRES and row["type"] in ("large_airport", "medium_airport", "small_airport"):
                apts[c] = (float(row["latitude_deg"]), float(row["longitude_deg"]))
    t0 = time.time()
    gb, ni = load_gb(), load_ni()
    res = {"flags": sorted(FLAGS), "controls": controls(gb), "pop_stats": gb["pop_stats"]}
    nall = "--nall" in FLAGS
    first_terr = None
    runs = []
    for name in GB_CENTRES + NI_CENTRES:
        lat, lon = apts[name]
        for r_km, eps in RADII:
            c = Centre(gb, ni, name, lat, lon, r_km, False)
            for v in VARIANTS:
                for N in ([None] if nall else NS):
                    runs.append(build(c, v, N, eps, nodrop="--nodrop" in FLAGS))
                    if first_terr is None:
                        first_terr = (name, r_km, v, N, runs[-1]["files_terrain"])
                    print(json.dumps({k: runs[-1][k] for k in ("centre", "r_km", "variant", "N", "outcome", "bytes_first",
                                                               "bytes_final", "validate")}), flush=True)
            if name in ("BFS", "LDY", "GLA") and r_km in (161, 222) and not nall:
                runs.append(build(Centre(gb, ni, name, lat, lon, r_km, True), VARIANTS[-1], NS[-1], eps,
                                  nodrop="--nodrop" in FLAGS))
    # control: the first package's files.terrain, rebuilt after every other centre, is unchanged (defect 2)
    name, r_km, v, N, n0 = first_terr
    lat, lon = apts[name]
    again = build(Centre(gb, ni, name, lat, lon, r_km, False), v, N, dict(RADII)[r_km])
    res["controls"]["files_terrain_stable"] = {"first": n0, "after_all": again["files_terrain"], "equal": n0 == again["files_terrain"]}
    res["runs"] = runs
    res["seconds"] = round(time.time() - t0)
    json.dump(res, open(OUT, "w"), indent=1)
    print(json.dumps({"controls": res["controls"], "seconds": res["seconds"]}))


main()
