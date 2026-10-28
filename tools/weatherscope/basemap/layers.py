"""The seven BMPK layers, built from local source files (docs/basemap-package-format.md §5-§7 and §13). Each builder
returns its section's items plus its `collapsed` count; nothing here prints a coordinate.

READINGS this module makes that the format doc does not pin are marked "READING".
"""
import csv
import io
import math
import re
import zipfile

import numpy as np
from PIL import Image

import geometry as g
import shp
import sources

BUS = re.compile(r"(^|[^A-Za-z0-9])Bus($|[^A-Za-z0-9])")


class Frame:
    def __init__(self, lat0_micro, lon0_micro, r_mm, tol_mm):
        self.lat0, self.lon0, self.c0 = sources.frame(lat0_micro, lon0_micro)
        self.r_mm, self.r_km, self.eps_m = r_mm, r_mm / 1e6, tol_mm / 1000.0

    def en(self, lon, lat):
        return g.project(lon, lat, self.lat0, self.lon0, self.c0)

    def q(self, p):
        return g.quantise(p, self.r_mm)

    def in_disc(self, p):
        return math.hypot(p[0], p[1]) <= self.r_km   # RD9: <=


def _layer(root, url):
    return shp.read_layer(sources.local_path(root, url))


def _line_items(fr, part_lonlat):
    """One source part -> its quantised in-square runs (RD5), and how many collapsed (§2)."""
    km = [fr.en(x, y) for x, y in part_lonlat]
    items, collapsed = [], 0
    for run in g.clip_line(km, fr.r_km):
        q = g.dedupe([fr.q(p) for p in g.simplify_line(run, fr.eps_m)])
        if len(q) < 2:
            collapsed += 1
        else:
            items.append(q)
    return items, collapsed


def water(fr, root, urls):
    """§5.1: AWATER >= 1,500,000; each OUTER part (clockwise in the file) a ring; holes dropped (RD3); RD2's order."""
    rings, collapsed = [], 0
    for url in urls:
        _, recs, tab = _layer(root, url)
        for r, row, dead in zip(recs, tab.rows, tab.deleted):
            if r is None or dead or row.get("AWATER") is None or row["AWATER"] < 1_500_000:
                continue
            for part in r.parts:
                ring = part[:-1] if len(part) > 1 and part[0] == part[-1] else part
                # clockwise (an outer ring) is a negative shoelace sum with x = lon, y = lat
                s = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(ring, ring[1:] + ring[:1]))
                if s >= 0:
                    continue
                clipped = g.clip_ring([fr.en(x, y) for x, y in ring], fr.r_km)
                if not clipped:
                    continue
                q = g.dedupe([fr.q(p) for p in g.simplify_ring(clipped, fr.eps_m)], ring=True)
                if len(q) < 3:
                    collapsed += 1
                else:
                    rings.append(q)
    return rings, collapsed


def rivers(fr, root, urls):
    """§5.2 + RD8: FULLNAME ending in " Riv"; groups by FULLNAME; the 5 longest by raw length inside the disc (ties
    by FULLNAME byte order), all their parts. Returns (names, [(name index, vertices)], collapsed)."""
    feats, length = [], {}
    for url in urls:
        _, recs, tab = _layer(root, url)
        for r, row, dead in zip(recs, tab.rows, tab.deleted):
            name = row.get("FULLNAME") or ""
            if r is None or dead or not name.endswith(" Riv"):
                continue
            for part in r.parts:
                km = [fr.en(x, y) for x, y in part]
                length[name] = length.get(name, 0.0) + g.in_disc_length_m(km, fr.r_km)
                feats.append((name, part))
    ranked = sorted((n for n, v in length.items() if v > 0.0), key=lambda n: (-length[n], n.encode("utf-8")))
    names = ranked[:5]
    index = {n: i for i, n in enumerate(names)}
    lines, collapsed = [], 0
    for name, part in feats:
        if name not in index:
            continue
        items, c = _line_items(fr, part)
        collapsed += c
        lines.extend((index[name], q) for q in items)
    return names, lines, collapsed, {n: length[n] for n in names}


def roads(fr, root, urls):
    """§5.3: MTFCC S1100 (class 1) or S1200 (class 2), minus a FULLNAME holding "Bus" as a whole word; RD4's order."""
    lines, collapsed, dropped_bus = [], 0, 0
    for url in urls:
        _, recs, tab = _layer(root, url)
        for r, row, dead in zip(recs, tab.rows, tab.deleted):
            cls = {"S1100": 1, "S1200": 2}.get(row.get("MTFCC"))
            if r is None or dead or cls is None:
                continue
            if BUS.search(row.get("FULLNAME") or ""):
                dropped_bus += 1
                continue
            for part in r.parts:
                items, c = _line_items(fr, part)
                collapsed += c
                lines.extend((cls, q) for q in items)
    return lines, collapsed, dropped_bus


# --------------------------------------------------------------------------------------------- terrain (§5.6)

class Mosaic:
    """z9 Terrarium tiles over a full rectangle of tile indices, as one elevation array (metres, double)."""

    def __init__(self, root, tiles):
        xs = sorted({t[0] for t in tiles})
        ys = sorted({t[1] for t in tiles})
        self.x0, self.y0 = xs[0], ys[0]
        w, h = xs[-1] - xs[0] + 1, ys[-1] - ys[0] + 1
        if set(tiles) != {(x, y) for x in xs for y in ys} or len(xs) != w or len(ys) != h:
            raise SystemExit("REFUSED: the terrain tiles are not a full rectangle (the rig)")
        self.dem = np.zeros((h * 256, w * 256), dtype=np.float64)
        for x, y in tiles:
            img = Image.open(io.BytesIO(sources.read_bytes(root, sources.terrarium(x, y)))).convert("RGB")
            a = np.asarray(img, dtype=np.float64)
            if a.shape != (256, 256, 3):
                raise SystemExit(f"REFUSED: tile {x}/{y} is {a.shape}, not 256 x 256 RGB")
            ty, tx = y - self.y0, x - self.x0
            self.dem[ty * 256:(ty + 1) * 256, tx * 256:(tx + 1) * 256] = a[..., 0] * 256 + a[..., 1] + a[..., 2] / 256 - 32768

    def at(self, X, Y):
        return self.dem[Y - self.y0 * 256, X - self.x0 * 256]

    def shade(self, lat0):
        """§5.6 in double, aligned as bendmock.py: np.gradient, the 2x slope, aspect, the 315/45 light, clipped, then
        the 4 x 4 mean over an edge-padded array. READING: each mean is the 16 values summed row by row, left to right,
        from 0.0, then / 16 (bendmock.py's cumulative sums are a summation method, not a value)."""
        gy, gx = np.gradient(self.dem)
        mpp = 156543.03 * math.cos(lat0 * math.pi / 180) / 2 ** 9
        slope = np.arctan(2.0 * np.hypot(gx, gy) / mpp)
        aspect = np.arctan2(-gx, gy)
        az, alt = 315 * math.pi / 180, 45 * math.pi / 180   # RD16
        s = np.sin(alt) * np.cos(slope) + np.cos(alt) * np.sin(slope) * np.cos(az - aspect)
        s = np.clip(s, 0, 1)
        p = np.pad(s, 2, mode="edge")
        h, w = s.shape
        acc = np.zeros_like(s)
        for a in range(4):
            for b in range(4):
                acc = acc + p[a:a + h, b:b + w]
        self.sm = acc / 16.0
        return self.sm


def terrain_level(fr, mosaic, level_mm, n):
    """§4/§5.6: N x N shade bytes, row 0 north; each pixel takes the z9 pixel containing its centre (RD21)."""
    out = bytearray()
    px_mm = 2 * level_mm / n
    for j in range(n):
        s_km = ((j + 0.5) - n / 2) * px_mm / 1e6
        lat = fr.lat0 - s_km / g.K
        for i in range(n):
            e_km = ((i + 0.5) - n / 2) * px_mm / 1e6
            lon = fr.lon0 + e_km / (g.K * fr.c0)
            X, Y = sources.z9_pixel(lat, lon)
            v = mosaic.sm[Y - mosaic.y0 * 256, X - mosaic.x0 * 256]
            out.append(int(math.floor(v * 255 + 0.5)))
    return bytes(out)


# ------------------------------------------------------------------------------------------------ peaks (§5.4)

def summits(fr, root, urls):
    """GNIS Summits inside the disc (RD9, RD12), one per feature_id (RD11): [(feature_id, name, (E, S) km, lat, lon)]
    in file order."""
    out, seen = [], set()
    for url in urls:
        with zipfile.ZipFile(sources.local_path(root, url)) as z:
            member = [n for n in z.namelist() if n.lower().endswith(".txt")]
            if len(member) != 1:
                raise SystemExit(f"REFUSED: {url} holds {len(member)} text files (the rig)")
            text = z.read(member[0]).decode("utf-8-sig")
        rows = text.splitlines()
        cols = rows[0].split("|")
        at = {c: cols.index(c) for c in ("feature_id", "feature_name", "feature_class", "prim_lat_dec", "prim_long_dec")}
        for line in rows[1:]:
            f = line.split("|")
            if len(f) != len(cols) or f[at["feature_class"]] != "Summit":
                continue
            fid = int(f[at["feature_id"]])
            if fid in seen:
                continue
            lat, lon = float(f[at["prim_lat_dec"]]), float(f[at["prim_long_dec"]])
            p = fr.en(lon, lat)
            if fr.in_disc(p):
                seen.add(fid)
                out.append((fid, f[at["feature_name"]], p, lat, lon))
    return out


def summit_tiles(cands):
    return sorted({(X // 256, Y // 256) for X, Y in (sources.z9_pixel(lat, lon) for _, _, _, lat, lon in cands)})


def peaks(fr, mosaic, cands, keep=16, sep_km=3.0):
    """§5.4: elevation from the z9 pixel (RD10), ranked by elevation descending then feature_id ascending; a summit
    within 3.0 km (<=) of a higher-ranked KEPT one is dropped; the top 16 survivors."""
    ranked = []
    for fid, name, p, lat, lon in cands:
        X, Y = sources.z9_pixel(lat, lon)
        elev = int(math.floor(mosaic.at(X, Y) + 0.5))
        ranked.append((-elev, fid, name, p, elev))
    ranked.sort(key=lambda t: (t[0], t[1]))
    kept = []
    for _, fid, name, p, elev in ranked:
        if any(math.hypot(p[0] - k[2][0], p[1] - k[2][1]) <= sep_km for k in kept):
            continue
        kept.append((fid, name, p, elev))
        if len(kept) == keep:
            break
    return [(fr.q(p)[0], fr.q(p)[1], elev, name) for fid, name, p, elev in kept]


# ----------------------------------------------------------------------------------------------- places (§5.5)

def places(fr, root, gaz_url, pop_url):
    """LSAD 25 within R (<=) of the origin, in file order; POPESTIMATE2024 from SUMLEV 162 on STATE + PLACE = GEOID."""
    with zipfile.ZipFile(sources.local_path(root, gaz_url)) as z:
        text = z.read("2025_Gaz_place_national.txt").decode("utf-8")
    rows = text.splitlines()
    cols = [c.strip() for c in rows[0].split("|")]
    at = {c: cols.index(c) for c in ("GEOID", "NAME", "LSAD", "INTPTLAT", "INTPTLONG")}
    figure = {}
    pop_text = sources.read_bytes(root, pop_url).decode("latin-1")
    rdr = csv.reader(io.StringIO(pop_text, newline=""))
    head = next(rdr)
    hi = {c: head.index(c) for c in ("SUMLEV", "STATE", "PLACE", "POPESTIMATE2024")}
    for r in rdr:
        if r and r[hi["SUMLEV"]] == "162":
            figure[r[hi["STATE"]] + r[hi["PLACE"]]] = int(r[hi["POPESTIMATE2024"]])
    out = []
    for line in rows[1:]:
        f = [x.strip() for x in line.split("|")]
        if len(f) != len(cols) or f[at["LSAD"]] != "25":
            continue
        p = fr.en(float(f[at["INTPTLONG"]]), float(f[at["INTPTLAT"]]))
        if fr.in_disc(p):
            qx, qy = fr.q(p)
            out.append((qx, qy, figure.get(f[at["GEOID"]], 0), f[at["NAME"]]))
    return out
