# SCRATCH, read-only: part 2's INGEST DRY RUN for the sea layer (the orchestrator's 04:12:32Z go; docs/basemap-dense-code.md, #652).
# Nothing uploads; no network. From the pinned, verified Natural Earth zips and the local store's index and coverage:
#   1. THE EXTENT: the 1-degree level-0 cells within 250 km of a covered point, recomputed with sea-extent.py's own definition (copied,
#      byte for byte in its logic) and required to be 2,584, the recorded figure (the control: a different count stops).
#   2. THE SOURCES: NE 10 m ocean (one record: outers with their holes, assigned by containment); NE 10 m lakes of design (a): area at
#      least 1.5 km2 (planar, km, at the lake's own centroid latitude) and NOT wholly inside the US (admin_0 ADM0_A3 = USA, `contains`).
#   3. THE CUT: each source, per cell, by a BOOLEAN rectangle clip (GEOS clip_by_rect, pre-tiled at 10 degrees for speed). A hole that
#      crosses the cell's edge becomes a notch in the piece's outer ring, not an edge-coincident ring (a change from the proposal's
#      Sutherland-Hodgman, flagged for ruling). Each resulting polygon is a PIECE: part 0 its outer, parts 1..n its holes of at least
#      1.5 km2 (smaller islands are dropped and counted).
#   4. THE BYTES: each part encoded as store.ts's encodeFeature would, kind sea (6): 1+2+4+2+1+1 header, 1 number (the piece's area)
#      x 8, 1 text (the source: "ocean" or the lake's name) as 2 + its UTF-8 length, 4 + 16 x vertices.
#   5. THE REPORT: cells holding sea, sea-only cells (absent from the index), pieces, islands kept and dropped, bytes; each cell's new
#      size and whether it crosses the 24 MB split; the new store's put count and bytes; and the ORDER both ways (the 04:12:32Z ruling).
# Writes sea-pieces.json (every piece, lon/lat, by cell) for the prototype store. Prints counts only.
#   geo-venv/Scripts/python sea-dryrun.py
import json, math, struct, sys, zipfile
from collections import defaultdict
import shapely
from shapely.geometry import Polygon, box
from shapely.ops import unary_union

R, LIM, MIN_KM2, SPLIT = 6371.0088, 250.0, 1.5, 24 * 1024 * 1024
def stop(m): print(f"STOP: {m}"); sys.exit(1)

# ---- 1. the extent (sea-extent.py's definition)
cov = open("bm-obj/coverage.bin", "rb").read()
if cov[:4] != b"BMCV": stop("not a coverage.bin")
n_rows = struct.unpack_from("<I", cov, 8)[0]; segs, o = [], 12
for _ in range(n_rows):
    h, n = struct.unpack_from("<iI", cov, o); o += 8
    for k in range(n):
        a, b = struct.unpack_from("<ii", cov, o + 8 * k); segs.append((h / 100.0, a / 100.0, b / 100.0))
    o += 8 * n
def hav(la1, lo1, la2, lo2):
    p1, p2, dl = math.radians(la1), math.radians(la2), math.radians(lo2 - lo1)
    x = math.sin((p2 - p1) / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * R * math.asin(min(1.0, math.sqrt(x)))
wrap = lambda lon: (lon + 180.0) % 360.0 - 180.0
def lon_gap(a, b, c0):
    best = 360.0
    for s in (-360.0, 0.0, 360.0):
        lo, hi = c0 + s, c0 + 1 + s
        best = min(best, 0.0 if (b >= lo and a <= hi) else min(abs(lo - b), abs(a - hi)))
    return best
cells = set()
for lat, a, b in segs:
    dlat = LIM / 111.0 + 0.01
    for la0 in range(math.floor(lat - dlat), math.floor(lat + dlat) + 1):
        latc = min(max(lat, la0), la0 + 1)
        cosm = max(math.cos(math.radians(max(abs(lat), abs(latc)) + 0.5)), 0.01)
        dlon = LIM / (111.0 * cosm) + 0.01
        for lo0 in range(math.floor(a - dlon), math.floor(b + dlon) + 1):
            key = (la0, int(wrap(lo0)))
            if key in cells: continue
            if hav(lat, 0.0, latc, lon_gap(a, b, lo0)) <= LIM: cells.add(key)
if len(cells) != 2584: stop(f"the extent is {len(cells)} cells, not the recorded 2,584")
idx = json.load(open("bm-obj/index.json"))
have = {tuple(int(x) for x in c["key"].split("/")[1:]): c["bytes"] for c in idx["cells"] if c["key"].startswith("0/")}
print(f"1. extent: {len(cells)} cells (the recorded 2,584: AS RECORDED); {len(set(have) & cells)} of the index's {len(have)} cells inside it")

# ---- 2. the sources
def shp(zpath, want):
    z = zipfile.ZipFile(zpath)
    s = z.read([n for n in z.namelist() if n.endswith(".shp")][0]); d = z.read([n for n in z.namelist() if n.endswith(".dbf")][0])
    nrec, hlen, rlen = struct.unpack_from("<IHH", d, 4); fields, p = [], 32
    while d[p] != 0x0D: fields.append((d[p:p + 11].split(b"\0")[0].decode(), d[p + 16])); p += 32
    attrs = []
    for i in range(nrec):
        r, q, a = d[hlen + i * rlen + 1: hlen + (i + 1) * rlen], 0, {}
        for name, ln in fields: a[name] = r[q:q + ln].decode("latin-1").strip(); q += ln
        attrs.append(a)
    p, i, out = 100, 0, []
    while p < len(s):
        _, clen = struct.unpack_from(">ii", s, p); p += 8
        st, = struct.unpack_from("<i", s, p)
        if st == 5 and want(attrs[i]):
            nparts, npts = struct.unpack_from("<ii", s, p + 36)
            parts = struct.unpack_from(f"<{nparts}i", s, p + 44); pts = struct.unpack_from(f"<{2 * npts}d", s, p + 44 + 4 * nparts)
            out.append((attrs[i], [[(pts[2 * k], pts[2 * k + 1]) for k in range(parts[j], parts[j + 1] if j + 1 < nparts else npts)] for j in range(nparts)]))
        p += 2 * clen; i += 1
    return out
def polygonize(rings):
    sa = lambda r: sum(r[k][0] * r[k + 1][1] - r[k + 1][0] * r[k][1] for k in range(len(r) - 1))
    outers = [r for r in rings if sa(r) < 0]; holes = [r for r in rings if sa(r) >= 0]
    pols = []
    for op in outers:
        Pp = Polygon(op); shapely.prepare(Pp)
        hs = [h for h in holes if shapely.contains_xy(Pp, *Polygon(h).representative_point().coords[0])]
        pols.append(Polygon(op, hs))
    return pols
ocean = polygonize(shp("ne-ocean-dl/ne_10m_ocean.zip", lambda a: True)[0][1])
km2 = lambda g: g.area * 111.0 * 111.0 * max(math.cos(math.radians(g.centroid.y)), 0.01)
us = unary_union(polygonize(shp("ne-admin0-dl/ne_10m_admin_0_countries.zip", lambda a: a.get("ADM0_A3") == "USA")[0][1])); shapely.prepare(us)
lakes_all = shp("ne-dl/ne_10m_lakes.zip", lambda a: True)
lakes, inside, small = [], 0, 0
for a, rings in lakes_all:
    g = unary_union(polygonize(rings))
    if g.is_empty: continue
    if km2(g) < MIN_KM2: small += 1; continue
    if us.contains(g): inside += 1; continue
    lakes.append((a.get("name") or a.get("name_en") or "lake", g))
print(f"2. sources: ocean {len(ocean)} outer polygon(s), {sum(len(p.interiors) for p in ocean)} holes; NE lakes {len(lakes_all)}: "
      f"{len(lakes)} selected (design a), {inside} wholly inside the US (not stored), {small} under {MIN_KM2} km2")

# ---- 3. the cut, pre-tiled at 10 degrees
srcs = [("ocean", p) for p in ocean] + lakes
tiles = defaultdict(list)
for la0, lo0 in cells: tiles[(math.floor(la0 / 10), math.floor(lo0 / 10))].append((la0, lo0))
pieces, kept_holes, dropped_holes, sea_bytes = defaultdict(list), 0, 0, defaultdict(int)
enc = lambda name, nv: 1 + 2 + 4 + 2 + 1 + 1 + 8 + 2 + len(name.encode("utf-8")) + 4 + 16 * nv
for (t_la, t_lo), tcells in tiles.items():
    tb = box(t_lo * 10, t_la * 10, t_lo * 10 + 10, t_la * 10 + 10)
    tsrc = [(nm, shapely.clip_by_rect(g, *tb.bounds)) for nm, g in srcs if g.intersects(tb)]
    tsrc = [(nm, g) for nm, g in tsrc if not g.is_empty]
    for la0, lo0 in tcells:
        for nm, g in tsrc:
            c = shapely.clip_by_rect(g, lo0, la0, lo0 + 1, la0 + 1)
            for p in shapely.get_parts(c):
                if p.geom_type != "Polygon" or p.is_empty or p.area <= 0: continue
                hs = [h for h in p.interiors if km2(Polygon(h)) >= MIN_KM2]
                dropped_holes += len(p.interiors) - len(hs); kept_holes += len(hs)
                ring = list(p.exterior.coords); holes = [list(h.coords) for h in hs]
                pieces[(la0, lo0)].append({"src": nm, "outer": ring, "holes": holes})
                sea_bytes[(la0, lo0)] += enc(nm, len(ring)) + sum(enc(nm, len(h)) for h in holes)
sea_cells = set(pieces); new_cells = sea_cells - set(have)
n_pieces = sum(len(v) for v in pieces.values())
over = [k for k in sea_cells if have.get(k, 12) + sea_bytes[k] > SPLIT and have.get(k, 0) <= SPLIT]
print(f"3. the cut: {len(sea_cells)} cells hold sea ({len(new_cells)} sea-only, new to the index); {n_pieces} pieces; "
      f"islands kept {kept_holes}, dropped (under {MIN_KM2} km2) {dropped_holes}; sea bytes {sum(sea_bytes.values()):,} "
      f"(largest cell {max(sea_bytes.values()):,}); cells newly over the 24 MB split: {len(over)}")

# ---- 4. the store, and the order
tiles_n = sum(1 for f in json.load(open("bm-obj/manifest.json"))["files"] if f["layer"] == "terrain")
cell_objs = len(set(have) | sea_cells)
puts = cell_objs + tiles_n + 5
cell_bytes = sum(have.values()) + sum(sea_bytes.values()) + 12 * len(new_cells)
print(f"4. the new store: {cell_objs} cell objects ({len(have)} existing + {len(new_cells)} sea-only; splits not counted: {len(over)} cells newly over 24 MB) "
      f"+ {tiles_n} terrain tiles + 5 small objects = {puts:,} puts; cell bytes {cell_bytes:,}")
print("   ORDER, FORWARD: 1. the builder that knows code 6 deploys (staging, then production); 2. the new store uploads (write, verify, "
      "repair, verify); 3. R2 bm/v1/current names it; 4. KV bm:store names it (publishStore, MI8)")
print("   ORDER, ROLLBACK (REVERSED): 1. KV bm:store back to the old store id; 2. R2 bm/v1/current back to the old id; 3. only then may "
      "the builder roll back (an old builder can't read code 6)")
json.dump({"/".join(map(str, k)): v for k, v in pieces.items()}, open("sea-pieces.json", "w"))
json.dump(sorted([list(k) for k in cells]), open("sea-extent-cells.json", "w"))  # KB7-b's extent (step 0's re-run; sea-pieces.json must not change)
print("   wrote sea-pieces.json and sea-extent-cells.json")
