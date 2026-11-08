"""The region table's independent side (docs/wx-opera-code-stage.md §3): shapely on the same polygons, never the
generator's scanline. Read-only and offline; nothing here reaches a network or a device.

  fixture  writes D1's 40 label points and their products, D2's 15 left-out label points, and D3's 15 border crossings
           (the lattice point nearest the border on each side) as one JSON fixture for test/wx-region.test.ts.
  d4       every lattice point within 3 cells of a served-excluded border: a generated table against shapely. Prints
           the count of points and of disagreements, and the points inside each excluded neighbour.
  coast    Q-COAST, printed for a ruling: Natural Earth 1:10m populated places in the 40 whose quantised point the
           table does not serve.

  python -I eu_borders.py fixture <graded.json> <ne_admin0.geojson> <eu_rules.json> <out.json>
  python -I eu_borders.py d4      <graded.json> <ne_admin0.geojson> <eu_rules.json> <region.generated.ts>
  python -I eu_borders.py coast   <graded.json> <ne_admin0.geojson> <eu_rules.json> <region.generated.ts> <ne_places.geojson>

The three inputs are the plan's own (docs/wx-opera-europe.md §10) and their sha256s are asserted before reading. The
served region is built as eu_rules.py builds it; a point is quantised as the server does (JavaScript's Math.round, which
is floor(x + 0.5), not Python's round(): CLAUDE.md, "a cross-language port of Worker math must pin its rounding"), and
the UK box is closed, as Q-UKBOX's ruling and the generator have it. No output prints a coordinate.
"""
import base64
import hashlib
import json
import math
import re
import struct
import sys

import numpy as np
import shapely
from pyproj import Transformer
from shapely.geometry import Point, box, shape
from shapely.ops import nearest_points, unary_union

GRADED_SHA = "0b0d43ac4363093b840655aaa763a57b78946b856f13bc1c60156b6374c1a6bd"
NE_SHA = "239eec57ac17f100a11e2536cffc56752c318b50ae765b0918ff7aab4ce8f255"
RULES_SHA = "f4727ddef3744c36c0952e6d7bd4dfe526ef5dafa91de89bb2c8742fb8217680"
TINY = ["San Marino", "Monaco", "Gibraltar", "Guernsey"]
UK_BOX = (-870, 4980, 180, 6090)               # OPUK_LAND in hundredths: lonW, latS, lonE, latN, closed
IDS = {"isles": "opuk", "iceland": "opis", "iberia": "opib", "malta": "opmt", "west": "opwe", "central": "opce",
       "southeast": "opse", "baltic": "opba", "scandisw": "opsw", "scandin": "opsn"}
# D3: (crossing, served country, excluded country, a public seed near the crossing as (lat, lon)). The seed only picks
# which stretch of that pair's border is meant; the points are the border's own.
CROSSINGS = [
    ("Nova Gorica/Gorizia", "Slovenia", "Italy", (45.955, 13.64)),
    ("Menton/Ventimiglia", "France", "Italy", (43.787, 7.531)),
    ("Chiasso/Como", "Switzerland", "Italy", (45.83, 9.03)),
    ("Brenner", "Austria", "Italy", (47.004, 11.505)),
    ("San Marino/Italy", "San Marino", "Italy", None),            # None: San Marino's own label point
    ("Narva/Ivangorod", "Estonia", "Russia", (59.377, 28.20)),
    ("Storskog", "Norway", "Russia", (69.67, 30.12)),
    ("Vaalimaa", "Finland", "Russia", (60.58, 27.83)),
    ("Terespol/Brest", "Poland", "Belarus", (52.08, 23.65)),
    ("Kybartai/Chernyshevskoye", "Lithuania", "Russia", (54.64, 22.76)),
    ("Giurgiu/Ruse", "Romania", "Bulgaria", (43.88, 25.96)),
    ("Uzhhorod", "Slovakia", "Ukraine", (48.65, 22.22)),
    ("Ceuta", "Spain", "Morocco", (35.873, -5.336)),
    ("Sukobin", "Montenegro", "Albania", (42.02, 19.40)),
    ("Merdare", "Republic of Serbia", "Kosovo", (42.98, 21.25)),
]


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def js_round2_h(x):
    """The server's quantise(): Math.round(x * 100), as an integer number of hundredths."""
    return math.floor(x * 100 + 0.5)


def load(graded_p, ne_p, rules_p):
    assert sha(graded_p) == GRADED_SHA, "graded.json is not the survey's"
    assert sha(ne_p) == NE_SHA, "the admin-0 file is not Natural Earth v5.1.2's"
    assert sha(rules_p) == RULES_SHA, "eu_rules.json is not the plan's"
    g = json.load(open(graded_p, encoding="utf-8"))
    ne = json.load(open(ne_p, encoding="utf-8"))
    rules = [(IDS[n], round(a * 100), round(b * 100), round(c * 100), round(d * 100))
             for n, a, b, c, d in json.load(open(rules_p, encoding="utf-8"))["rules"]]
    grids = {IDS[p["product"]]: p for p in json.load(open(rules_p, encoding="utf-8"))["products"]}
    served = sorted([r["name"] for r in g["ranked"] if r["covered_median"] >= 90] + TINY)
    left = [r["name"] for r in g["ranked"] if r["covered_median"] < 90] + \
           [a["name"] for a in g["apart"] if a["in_domain_pct"] < 50]
    P, ras = g["raster"]["proj"], g["raster"]
    fwd = Transformer.from_crs("EPSG:4326", f"+proj=laea +lat_0={P['lat0']} +lon_0={P['lon0']} +x_0={P['x0']} "
                               f"+y_0={P['y0']} +a={P['a']} +rf={P['invF']} +units=m +no_defs", always_xy=True)
    feat = {f["properties"]["ADMIN"]: f for f in ne["features"]}
    parts = []
    for n in served:
        geo = shape(feat[n]["geometry"])
        for pt in (geo.geoms if geo.geom_type == "MultiPolygon" else [geo]):
            c = np.asarray(pt.exterior.coords)
            x, y = fwd.transform(c[:, 0], c[:, 1])
            if ((x >= 0) & (x <= ras["width"] * ras["xs"]) & (y <= 0) & (y >= -ras["height"] * ras["ys"])).any():
                parts.append(pt)
    land = unary_union(parts)
    shapely.prepare(land)
    def in_raster(lon, lat):
        x, y = fwd.transform(lon, lat)
        return 0 <= x <= ras["width"] * ras["xs"] and -ras["height"] * ras["ys"] <= y <= 0
    return {"served": served, "left": left, "feat": feat, "land": land, "rules": rules, "grids": grids, "ne": ne,
            "in_raster": in_raster}


def in_region(env, rh, ch):
    """Served at the lattice point (rh, ch) in hundredths: inside the kept parts, or in the closed UK box."""
    w, s, e, n = UK_BOX
    if s <= rh <= n and w <= ch <= e:
        return True
    return bool(shapely.contains_xy(env["land"], ch / 100, rh / 100))


def expected(env, rh, ch):
    if not in_region(env, rh, ch):
        return None
    return next((pid for pid, w, s, e, n in env["rules"] if s <= rh <= n and w <= ch <= e), "LEFTOVER")


def in_some_grid(env, lat, lon):
    for p in env["grids"].values():
        if p["la1"] - (p["nj"] - 1) * 0.01 <= lat <= p["la1"] and p["lo1"] <= lon <= p["lo1"] + (p["ni"] - 1) * 0.01:
            return True
    return False


def label(env, name):
    pr = env["feat"][name]["properties"]
    return pr["LABEL_Y"], pr["LABEL_X"]


def fixture(env, out_p):
    d1 = []
    for n in env["served"]:
        lat, lon = label(env, n)
        rh, ch = js_round2_h(lat), js_round2_h(lon)
        assert shape(env["feat"][n]["geometry"]).contains(Point(ch / 100, rh / 100)), f"{n}: quantised label not inside"
        p = expected(env, rh, ch)
        assert p not in (None, "LEFTOVER"), n
        d1.append({"name": n, "lat": lat, "lon": lon, "product": p})
    d2, d2_in_grid = [], []
    for n in env["left"]:
        lat, lon = label(env, n)
        rh, ch = js_round2_h(lat), js_round2_h(lon)
        assert expected(env, rh, ch) is None, f"{n}: a left-out label point is served"
        if in_some_grid(env, rh / 100, ch / 100):
            d2_in_grid.append(n)
        d2.append({"name": n, "lat": lat, "lon": lon})
    d3 = []
    for cname, a, b, seed in CROSSINGS:
        A, B = shape(env["feat"][a]["geometry"]), shape(env["feat"][b]["geometry"])
        border = A.boundary.intersection(B.buffer(1e-6))
        assert not border.is_empty, f"{cname}: {a} and {b} share no border"
        s = Point(label(env, a)[1], label(env, a)[0]) if seed is None else Point(seed[1], seed[0])
        bp = nearest_points(border, s)[0]
        assert bp.distance(s) < 0.2, f"{cname}: the seed is {bp.distance(s):.2f} deg from {a}/{b}'s border"
        r0, c0 = js_round2_h(bp.y), js_round2_h(bp.x)
        best = {"served": None, "excluded": None}
        for dr in range(-12, 13):
            for dc in range(-12, 13):
                rh, ch = r0 + dr, c0 + dc
                pt = Point(ch / 100, rh / 100)
                d = pt.distance(bp)
                if A.contains(pt) and expected(env, rh, ch) not in (None, "LEFTOVER"):
                    k = "served"
                elif B.contains(pt) and expected(env, rh, ch) is None:
                    k = "excluded"
                else:
                    continue
                if best[k] is None or d < best[k][0]:
                    best[k] = (d, rh, ch)
        assert best["served"] and best["excluded"], f"{cname}: no lattice point found on one side"
        _, rs, cs = best["served"]
        _, rx, cx = best["excluded"]
        d3.append({"crossing": cname,
                   "served": {"country": a, "lat": rs / 100, "lon": cs / 100, "product": expected(env, rs, cs)},
                   "excluded": {"country": b, "lat": rx / 100, "lon": cx / 100,
                                "insideAGrid": in_some_grid(env, rx / 100, cx / 100)}})
    out = {"about": "test/wx-region.test.ts's points, chosen by tools/wx/eu_borders.py (shapely) from Natural Earth "
                    "1:10m admin-0 v5.1.2. Public geography: country label points and border crossings.",
           "inputs": {"graded": GRADED_SHA, "ne": NE_SHA, "rules": RULES_SHA},
           "d1": d1, "d2": d2, "d3": d3}
    open(out_p, "w", encoding="utf-8", newline="\n").write(json.dumps(out, indent=1) + "\n")
    by = {}
    for e in d1:
        by[e["product"]] = by.get(e["product"], 0) + 1
    print(json.dumps({"d1": len(d1), "d1_by_product": by, "d2": len(d2), "d2_inside_a_grid": d2_in_grid,
                      "d3": len(d3), "d3_excluded_inside_a_grid": sum(e["excluded"]["insideAGrid"] for e in d3),
                      "d3_italian_sides": sum(e["excluded"]["country"] == "Italy" for e in d3)}))


def read_table(ts_p):
    s = open(ts_p, encoding="utf-8").read()
    h = json.loads(re.search(r"export const REGION_HEADER = (\{.*?\n\}) as const;", s, re.S).group(1))

    def arr(name):
        body = re.search(rf"export const {name}: string = \[\n(.*?)\n\]\.join", s, re.S).group(1)
        return base64.b64decode("".join(re.findall(r'"([^"]*)"', body)))
    ri = struct.unpack(f"<{h['rows'] + 1}I", arr("REGION_ROW_INDEX"))
    ru = struct.unpack(f"<{h['runs'] * 3}H", arr("REGION_RUNS"))
    ids = [r["id"] for r in h["rules"]]

    def at(rh, ch):
        i = h["row0"] - rh
        if not 0 <= i < h["rows"]:
            return None
        c = ch - h["col0"]
        for j in range(ri[i], ri[i + 1]):
            if ru[3 * j] <= c <= ru[3 * j + 1]:
                return ids[ru[3 * j + 2]]
        return None
    return h, at


def d4(env, ts_p):
    h, at = read_table(ts_p)
    land = env["land"]
    near = [shape(f["geometry"]) for n, f in env["feat"].items()
            if n not in env["served"] and shape(f["geometry"]).intersects(box(*land.bounds).buffer(1))]
    excl = unary_union(near)
    lines = land.boundary.intersection(excl.buffer(1e-6))
    shapely.prepare(lines)
    pts = set()
    for ln in getattr(lines, "geoms", [lines]):
        w, s, e, n = ln.bounds
        rr = np.arange(math.floor(s * 100) - 4, math.ceil(n * 100) + 5)
        cc = np.arange(math.floor(w * 100) - 4, math.ceil(e * 100) + 5)
        R, C = np.meshgrid(rr, cc, indexing="ij")
        R, C = R.ravel(), C.ravel()
        keep = shapely.dwithin(ln, shapely.points(C / 100, R / 100), 0.03)
        pts.update(zip(R[keep].tolist(), C[keep].tolist()))
    neighbours = {n: shape(env["feat"][n]["geometry"]) for n in env["feat"] if n not in env["served"]}
    dis, inside = 0, {}
    for rh, ch in sorted(pts):
        want, got = expected(env, rh, ch), at(rh, ch)
        if want == "LEFTOVER":
            raise SystemExit("a served point with no rule: the truth itself is incomplete")
        if want != got:
            dis += 1
        if want is None:
            for n, g in neighbours.items():
                if g.bounds[0] <= ch / 100 <= g.bounds[2] and g.bounds[1] <= rh / 100 <= g.bounds[3] and \
                        g.contains(Point(ch / 100, rh / 100)):
                    inside[n] = inside.get(n, 0) + 1
                    break
    print(json.dumps({"table": ts_p.replace("\\", "/").split("/")[-1], "sabotage": h.get("sabotage"),
                      "points_within_3_cells": len(pts), "disagreements": dis,
                      "points_inside_each_excluded_neighbour": dict(sorted(inside.items()))}))


def coast(env, ts_p, places_p):
    _, at = read_table(ts_p)
    a3 = {env["feat"][n]["properties"]["ADM0_A3"]: n for n in env["served"]}
    places = json.load(open(places_p, encoding="utf-8"))["features"]
    # Two kinds of "outside", kept apart: a place in a part outside the raster (an overseas part, not served by design,
    # docs/wx-opera-europe.md §1) and a place inside the raster whose quantised point the table does not serve -- the
    # coast question Q-COAST asks about.
    total, in_raster, kinds = 0, 0, {"outside_the_raster": {}, "inside_the_raster": {}}
    for f in places:
        n = a3.get(f["properties"].get("ADM0_A3"))
        if n is None:
            continue
        lon, lat = f["geometry"]["coordinates"][:2]
        total += 1
        inside = env["in_raster"](lon, lat)
        in_raster += inside
        if at(js_round2_h(lat), js_round2_h(lon)) is None:
            k = kinds["inside_the_raster" if inside else "outside_the_raster"]
            k[n] = k.get(n, 0) + 1
    coastal = sum(kinds["inside_the_raster"].values())
    print(json.dumps({"places_in_the_40": total, "inside_the_raster": in_raster,
                      "not_served_outside_the_raster": dict(sorted(kinds["outside_the_raster"].items(), key=lambda kv: -kv[1])),
                      "not_served_inside_the_raster": coastal,
                      "share_of_in_raster_pct": round(100 * coastal / in_raster, 2) if in_raster else None,
                      "inside_the_raster_by_country": dict(sorted(kinds["inside_the_raster"].items(), key=lambda kv: -kv[1])),
                      "places_sha256": sha(places_p)}))


if __name__ == "__main__":
    mode, rest = sys.argv[1], sys.argv[2:]
    env = load(*rest[:3])
    if mode == "fixture":
        fixture(env, rest[3])
    elif mode == "d4":
        d4(env, rest[3])
    elif mode == "coast":
        coast(env, rest[3], rest[4])
    else:
        raise SystemExit(f"unknown mode {mode}")
