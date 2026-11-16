# SCRATCH, read-only: KB7-a and KB7-b, each with its (b) control (docs/basemap-dense-code.md, #652, part 2; the 03:56Z pick, design (a)).
# Areas are shapely's, in degrees squared, the same projection on both sides of every comparison; the 1.5 km2 threshold uses the dry run's
# own km2 (planar, at the geometry's centroid latitude). The cut is the dry run's: shapely.clip_by_rect per 1-degree cell, then a piece's
# holes under 1.5 km2 dropped (filled).
#   KB7-a (synthetic): US = box(10.5, 9, 12, 12); lake1 = box(10.2, 10.2, 10.8, 10.8) crosses its border at lon 10.5, inside cell (10, 10);
#         lake2 = box(10.6, 10.6, 10.7, 10.7) is wholly inside it. PASS: lake1 selected and its pieces sum to it within 1e-9 relative;
#         lake2 not selected. CONTROL: the (b) cut (lake1 minus US) must FAIL the same sum.
#   KB7-b (real, the dry run's sea-pieces.json and sea-extent-cells.json): for Lake Erie, Lake Champlain, Falcon Lake and Amistad Reservoir,
#         every cell the lake touches is in the extent (else the instrument stops); the pieces whose src is the lake sum to the lake's area
#         PLUS the islands the cut drops (under 1.5 km2 and wholly inside one cell), within 1e-9 relative. CONTROL: the (b) geometry, the lake
#         minus the US polygon, with the same allowance, must FAIL.
# Prints counts only; exits 1 on a stop.  geo-venv/Scripts/python kb7.py
import json, math, struct, sys, zipfile
import shapely
from shapely.geometry import Polygon, box
from shapely.ops import unary_union

MIN_KM2, TOL = 1.5, 1e-9
km2 = lambda g: g.area * 111.0 * 111.0 * max(math.cos(math.radians(g.centroid.y)), 0.01)
def stop(m): print(json.dumps({"STOP": m})); sys.exit(1)
def cut(g, cells):
    out = []
    for la0, lo0 in cells:
        for p in shapely.get_parts(shapely.clip_by_rect(g, lo0, la0, lo0 + 1, la0 + 1)):
            if p.geom_type != "Polygon" or p.is_empty or p.area <= 0: continue
            out.append(Polygon(p.exterior.coords, [h.coords for h in p.interiors if km2(Polygon(h)) >= MIN_KM2]))
    return out
def shp(zpath, want):
    z = zipfile.ZipFile(zpath)
    s = z.read([n for n in z.namelist() if n.endswith(".shp")][0]); d = z.read([n for n in z.namelist() if n.endswith(".dbf")][0])
    nrec, hlen, rlen = struct.unpack_from("<IHH", d, 4); fields, q = [], 32
    while d[q] != 0x0D: fields.append((d[q:q + 11].split(b"\0")[0].decode(), d[q + 16])); q += 32
    attrs = []
    for i in range(nrec):
        r, w, a = d[hlen + i * rlen + 1: hlen + (i + 1) * rlen], 0, {}
        for name, ln in fields: a[name] = r[w:w + ln].decode("latin-1").strip(); w += ln
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
    outers = [r for r in rings if sa(r) < 0]; holes = [r for r in rings if sa(r) >= 0]; pols = []
    for op in outers:
        P = Polygon(op); shapely.prepare(P)
        pols.append(Polygon(op, [h for h in holes if shapely.contains_xy(P, *Polygon(h).representative_point().coords[0])]))
    return pols
def dropped_islands(g):
    tot = 0.0
    for p in shapely.get_parts(g):
        for h in p.interiors:
            H = Polygon(h); x0, y0, x1, y1 = H.bounds
            if km2(H) < MIN_KM2 and math.floor(x0) == math.floor(x1) and math.floor(y0) == math.floor(y1): tot += H.area
    return tot
res = {}
# ---- KB7-a
US_s = box(10.5, 9, 12, 12); l1, l2 = box(10.2, 10.2, 10.8, 10.8), box(10.6, 10.6, 10.7, 10.7)
sel = [nm for nm, g in (("lake1", l1), ("lake2", l2)) if km2(g) >= MIN_KM2 and not US_s.contains(g)]
a_ok = abs(sum(p.area for p in cut(l1, [(10, 10)])) - l1.area) <= TOL * l1.area
a_ctl = abs(sum(p.area for p in cut(l1.difference(US_s), [(10, 10)])) - l1.area) <= TOL * l1.area
res["KB7-a"] = {"selected": sel, "lake1_sums": a_ok, "PASS": sel == ["lake1"] and a_ok, "control_b_sums": a_ctl, "control_FAILS": not a_ctl}
# ---- KB7-b
pieces = json.load(open("sea-pieces.json")); extent = {tuple(c) for c in json.load(open("sea-extent-cells.json"))}
if len(extent) != 2584: stop(f"the extent file holds {len(extent)} cells, not 2,584")
us = unary_union(polygonize(shp("ne-admin0-dl/ne_10m_admin_0_countries.zip", lambda a: a.get("ADM0_A3") == "USA")[0][1]))
for name in ("Lake Erie", "Lake Champlain", "Falcon Lake", "Amistad Reservoir"):
    recs = shp("ne-dl/ne_10m_lakes.zip", lambda a, n=name: a.get("name") == n)
    if not recs: stop(f"{name} is not in ne_10m_lakes")
    g = unary_union([unary_union(polygonize(r)) for _, r in recs])
    x0, y0, x1, y1 = g.bounds
    touched = {(la, lo) for la in range(math.floor(y0), math.floor(y1) + 1) for lo in range(math.floor(x0), math.floor(x1) + 1) if g.intersects(box(lo, la, lo + 1, la + 1))}
    if not touched <= extent: stop(f"{name} touches {len(touched - extent)} cells outside the extent")
    mine = [Polygon(p["outer"], p["holes"]) for v in pieces.values() for p in v if p["src"] == name]
    a_p, expect = sum(p.area for p in mine), g.area + dropped_islands(g)
    gb = g.difference(us); a_b = gb.area + dropped_islands(gb)
    res[name] = {"pieces": len(mine), "cells": len(touched), "rel_diff": abs(a_p - expect) / g.area, "PASS": abs(a_p - expect) <= TOL * g.area,
                 "control_b_rel_diff": abs(a_b - expect) / g.area, "control_FAILS": abs(a_b - expect) > TOL * g.area}
print(json.dumps(res))
