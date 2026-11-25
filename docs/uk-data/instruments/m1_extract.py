"""M1 stage A (docs/uk-data.md A2.4): keep only the GB features §2.2's rules read, in source order, with counts.

Usage: python -I m1_extract.py <m1src> <m1work>
Writes <m1work>/{roads,rivers,water,names}.pkl and extract.json (every count, every rule applied).
Coordinates stay in British National Grid metres; projection to a package's frame happens in stage B.
Source order (RD2): files by path ascending, then record, then part.
"""
import csv
import glob
import json
import os
import pickle
import sys

import shapefile

src, work = sys.argv[1], sys.argv[2]
os.makedirs(work, exist_ok=True)
counts = {}


def files(pat):
    return sorted(glob.glob(os.path.join(src, pat), recursive=True))


def dbf_encoding(p):
    """The DBF's own language-driver byte (offset 29) decides; only the one the OS files carry is accepted."""
    ldid = open(p[:-4] + ".dbf", "rb").read(32)[29]
    if ldid != 0x03:
        raise SystemExit(f"{p}: unexpected DBF language driver {ldid:#x}")
    return "cp1252"


def rel(p):
    return os.path.relpath(p, src).replace("\\", "/")


# roads: Motorway -> class 1, A Road -> class 2 (primary flag kept for variant (ii)); slip roads dropped
roads, seen, by_class = [], 0, {}
for fi, p in enumerate(files("os-roads/**/*_RoadLink.shp")):
    r = shapefile.Reader(p, encoding=dbf_encoding(p))
    names = [f[0] for f in r.fields[1:]]
    for ri, sr in enumerate(r.iterShapeRecords()):
        seen += 1
        a = dict(zip(names, sr.record))
        by_class[a["class"]] = by_class.get(a["class"], 0) + 1
        if a["class"] not in ("Motorway", "A Road") or a["formOfWay"] == "Slip Road":
            continue
        pts = sr.shape.points
        parts = list(sr.shape.parts) + [len(pts)]
        for pi in range(len(parts) - 1):
            roads.append({"key": (rel(p), ri, pi), "cls": 1 if a["class"] == "Motorway" else 2,
                          "primary": str(a["primary"]).lower() == "true", "num": a["roadNumber"] or "",
                          "fow": a["formOfWay"], "a": a["startNode"], "b": a["endNode"],
                          "pts": [tuple(x) for x in pts[parts[pi]:parts[pi + 1]]]})
counts["dbf_encoding"] = "cp1252, from every DBF's language-driver byte 0x03 (Windows ANSI)"
counts["roads"] = {"links_read": seen, "by_class": by_class, "kept_polylines": len(roads),
                   "kept_class1": sum(1 for x in roads if x["cls"] == 1), "kept_class2": sum(1 for x in roads if x["cls"] == 2),
                   "kept_class2_primary": sum(1 for x in roads if x["cls"] == 2 and x["primary"]),
                   "slip_dropped": "formOfWay == Slip Road"}

# rivers: inlandRiver or tidalRiver, named; the English name where there are two (Q10): name2, else name1
rivers, seen, forms, two = [], 0, {}, 0
for p in files("os-rivers/**/WatercourseLink.shp"):
    r = shapefile.Reader(p, encoding=dbf_encoding(p))
    names = [f[0] for f in r.fields[1:]]
    for ri, sr in enumerate(r.iterShapeRecords()):
        seen += 1
        a = dict(zip(names, sr.record))
        forms[a["form"]] = forms.get(a["form"], 0) + 1
        n1, n2 = (a["name1"] or "").strip(), (a["name2"] or "").strip()
        if a["form"] not in ("inlandRiver", "tidalRiver") or not (n1 or n2):
            continue
        if n1 and n2:
            two += 1
        pts = sr.shape.points
        parts = list(sr.shape.parts) + [len(pts)]
        for pi in range(len(parts) - 1):
            rivers.append({"key": (rel(p), ri, pi), "name": n2 or n1, "pts": [tuple(x) for x in pts[parts[pi]:parts[pi + 1]]]})
counts["rivers"] = {"links_read": seen, "forms": forms, "kept_polylines": len(rivers), "with_two_names": two,
                    "name_rule": "name2 (English alternative) if present, else name1"}


def shoelace(ring):
    s = 0.0
    for (x0, y0), (x1, y1) in zip(ring, ring[1:] + ring[:1]):
        s += x0 * y1 - x1 * y0
    return s / 2.0


# water: SurfaceWater_Area features >= 1.5 km2 (outer minus holes); outer rings kept, holes dropped (RD3)
water, seen, kept_f = [], 0, 0
for p in files("os-water/**/*_SurfaceWater_Area.shp"):
    r = shapefile.Reader(p, encoding=dbf_encoding(p))
    for ri, shp in enumerate(r.iterShapes()):
        seen += 1
        pts = shp.points
        parts = list(shp.parts) + [len(pts)]
        rings = []
        for pi in range(len(parts) - 1):
            ring = [tuple(x) for x in pts[parts[pi]:parts[pi + 1]]]
            if len(ring) > 1 and ring[0] == ring[-1]:
                ring = ring[:-1]
            rings.append((pi, ring, shoelace(ring)))
        # shapefile: outer rings clockwise (negative shoelace in x-east, y-north), holes counter-clockwise
        area = sum(-a for _, _, a in rings)
        if area < 1_500_000:
            continue
        kept_f += 1
        for pi, ring, a in rings:
            if a < 0 and len(ring) >= 3:
                water.append({"key": (rel(p), ri, pi), "pts": ring})
counts["water"] = {"features_read": seen, "features_kept": kept_f, "outer_rings_kept": len(water),
                   "rule": "feature area (outer - holes, BNG m2) >= 1,500,000; holes dropped"}

# names: populatedPlace City/Town, and landform Hill Or Mountain; the English name where there are two
hdr = open(files("os-names/**/*Header*.csv")[0], encoding="utf-8-sig").read().strip().split(",")
places, hills, rows, lang = [], [], 0, {}
for fi, p in enumerate(files("os-names/**/Data/*.csv") or [f for f in files("os-names/**/*.csv") if "Header" not in f]):
    with open(p, encoding="utf-8") as f:
        for ri, row in enumerate(csv.reader(f)):
            rows += 1
            a = dict(zip(hdr, row))
            t, lt = a["TYPE"], a["LOCAL_TYPE"]
            if not ((t == "populatedPlace" and lt in ("City", "Town")) or (t == "landform" and lt == "Hill Or Mountain")):
                continue
            n1, l1, n2, l2 = a["NAME1"], a["NAME1_LANG"], a["NAME2"], a["NAME2_LANG"]
            if n2 and l2 == "eng":
                name, why = n2, "name2 eng"
            elif n1 and l1 in ("eng", ""):
                name, why = n1, "name1 eng or untagged"
            else:
                name, why = n1, f"name1 only ({l1}/{l2 or '-'})"
            lang[why] = lang.get(why, 0) + 1
            rec = {"key": (rel(p), ri), "id": a["ID"], "name": name, "x": float(a["GEOMETRY_X"]), "y": float(a["GEOMETRY_Y"]),
                   "type": lt}
            (places if t == "populatedPlace" else hills).append(rec)
counts["names"] = {"rows_read": rows, "places_city_town": len(places), "hills": len(hills), "name_rule_counts": lang}

for k, v in (("roads", roads), ("rivers", rivers), ("water", water), ("places", places), ("hills", hills)):
    with open(os.path.join(work, k + ".pkl"), "wb") as f:
        pickle.dump(v, f, protocol=4)
json.dump(counts, open(os.path.join(work, "extract.json"), "w"), indent=1, sort_keys=True)
print(json.dumps(counts, indent=1, sort_keys=True))
