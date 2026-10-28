"""The BMPK host tool (docs/basemap-package-format.md; docs/weatherscope-stage3.md A10.2, A12): fetches a square's
sources, builds its package with a manifest and a licence file, and checks a package by rebuilding it.

    python -I make_package.py select --origin-from <bend.ts> --sources <dir>
    python -I make_package.py fetch  --origin-from <bend.ts> --sources <dir>
    python -I make_package.py build  --origin-from <bend.ts> --sources <dir> --out <bend.bmpk>
    python -I make_package.py check  <bend.bmpk> --sources <dir>

The origin is read from the site feed's own constant (`proxy/src/sitefeed/bend.ts`'s `BEND`), quantised to 2 dp as
the route quantises (crop.ts's quantise), and is stored ONLY in the package's binary header (A10.4). Nothing here
prints or records it: not the console, not the manifest, not the licence file.

--sources is a directory outside every repo; the files are fetched there once and re-checked by sha256 on every read.
"""
import argparse
import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bmpk      # noqa: E402
import layers    # noqa: E402
import shp       # noqa: E402
import sources   # noqa: E402

R_MM = 97_000_000            # A10.2: r = 97 km, an integer in mm (§2)
TOLERANCE_MM = 100_584       # §6: half a 15 mi pixel at Weatherscope's finest zoom


def origin_from(path):
    """(lat0Micro, lon0Micro) from bend.ts's `export const BEND = { lat: .., lon: .. }`, each floor(v * 100 + 0.5)
    hundredths (§1's rounding; equal to crop.ts's Math.round for every real input), times 10,000. Never printed."""
    text = open(path, encoding="utf-8").read()
    m = re.search(r"export const BEND = \{ lat: (-?\d+(?:\.\d+)?), lon: (-?\d+(?:\.\d+)?) \}", text)
    if not m:
        raise SystemExit("REFUSED: no `export const BEND = { lat: .., lon: .. }` in the origin file (the rig)")
    lat, lon = float(m.group(1)), float(m.group(2))
    return math.floor(lat * 100 + 0.5) * 10_000, math.floor(lon * 100 + 0.5) * 10_000


def select(lat0_micro, lon0_micro, root):
    """RD-P6: the states and counties whose TIGER box meets the square's latitude/longitude box, closed. Returns
    ([(statefp, name)], [(geoid, namelsad)]) in GEOID order."""
    box = sources.latlon_box(lat0_micro, lon0_micro, R_MM)
    out = {}
    for url, key, name in ((sources.STATES, "STATEFP", "NAME"), (sources.COUNTIES, "GEOID", "NAMELSAD")):
        sources.fetch(root, url)
        ftype, recs, tab = shp.read_layer(sources.local_path(root, url))
        hit = []
        for r, row, dead in zip(recs, tab.rows, tab.deleted):
            if r is None or dead:
                continue
            if sources.box_meets(r.box, box):
                hit.append((row[key], row[name]))
        out[url] = sorted(hit)
    return out[sources.STATES], out[sources.COUNTIES]


LEVELS_MM = (24_743_664, 49_487_328, 98_974_656)   # §4: the 15, 30 and 60 mi views x 246/240
LEVEL_N = 246


def level_tiles(lat0_micro, lon0_micro, level_mm):
    """§5.6: every z9 tile meeting the level's square, plus a one-tile margin on every side. [(x, y)] sorted."""
    latmin, lonmin, latmax, lonmax = sources.latlon_box(lat0_micro, lon0_micro, level_mm)
    x0, y0 = sources.z9_pixel(latmax, lonmin)
    x1, y1 = sources.z9_pixel(latmin, lonmax)
    return [(x, y) for x in range(x0 // 256 - 1, x1 // 256 + 2) for y in range(y0 // 256 - 1, y1 // 256 + 2)]


def plan(lat0_micro, lon0_micro, root):
    """{layer: [url]} for every file the build reads except the summit tiles (which need GNIS read first). Each list
    in URL order (RD2)."""
    states, counties = select(lat0_micro, lon0_micro, root)
    tiles = sorted({t for lv in LEVELS_MM for t in level_tiles(lat0_micro, lon0_micro, lv)})
    return {
        "water": sorted(sources.areawater(g) for g, _ in counties),
        "rivers": sorted(sources.linearwater(g) for g, _ in counties),
        "roads": sorted(sources.prisecroads(s) for s, _ in states),
        "peaks": sorted(sources.gnis(s) for s, _ in states),
        "places": [sources.GAZETTEER],
        "population": [sources.SUBEST],
        "terrain": sorted(sources.terrarium(x, y) for x, y in tiles),
        "states": [sources.STATES],
        "counties": [sources.COUNTIES],
    }


def fetch_all(files, root):
    """Fetches every file; returns {url: meta}. For each TIGER layer file, prints A12.3's RF1-RF3."""
    metas = {}
    for layer, urls in files.items():
        for url in urls:
            metas[url] = sources.fetch(root, url)
            if layer in ("water", "rivers", "roads", "states", "counties"):
                ftype, recs, tab = shp.read_layer(sources.local_path(root, url))
                n, nulls, eq, parts, closed = shp.real_file_rows(ftype, recs)
                ok = eq == n - nulls and closed == parts
                print(f"  RF {url.rsplit('/', 1)[1]}: type {ftype}, {n} records, {nulls} null; RF1 boxes {eq}/{n - nulls}; "
                      f"RF2 closed {closed}/{parts}{'' if ok else '  <-- OFF-PREDICTION'}")
                if not ok:
                    raise SystemExit("STOP (A12.3): a real file breaks RF1 or RF2")
    return metas


LAYERS = ("water", "rivers", "roads", "peaks", "places", "population", "terrain")
BUILDER_NAME = "weatherscope-basemap"
PREDICATES = [
    "water: AWATER >= 1500000, outer rings only",
    "rivers: FULLNAME ends with \" Riv\", the 5 longest inside the disc",
    "roads: MTFCC in {S1100, S1200}, \"Bus\" as a whole word in FULLNAME dropped",
    "peaks: feature_class = Summit, inside the disc, ranked by z9 elevation, 3.0 km apart, top 16",
    "places: LSAD = 25, inside the disc",
]
CREDIT_SRTM = "credit.terrain: SRTM data courtesy of the U.S. Geological Survey"
PROCESSING_BOTH = ("processing.terrain: Terrarium z9 elevations; hillshade from 315 deg at 45 deg with 2x vertical "
                   "exaggeration; 4x4 mean of z9 pixels; each level pixel takes the z9 pixel containing its centre; shade "
                   "quantised to 8 bits; summit elevations taken from the z9 pixel containing the summit, rounded to whole "
                   "metres")
NOTICE = "© COPYRIGHT 2000 BY THE UNITED STATES GOVERNMENT. NO COPYRIGHT CLAIMED UNDER TITLE 17 U.S.C."
NOTICE_NOTE = "2000 is the SRTM release joerd's attribution.md names; joerd records several updates, including in 2014."
CAP, TARGET = 32 * 1024, 20 * 1024


def builder_commit():
    """F1: the commit of the code that writes the package and its manifest. Refuses a dirty tool directory, so the
    commit named is the code that ran."""
    dirty = subprocess.run(["git", "status", "--porcelain", "--", HERE], capture_output=True, text=True, cwd=HERE)
    if dirty.returncode != 0:
        raise SystemExit(f"REFUSED: git status failed in {HERE} (the rig)")
    if dirty.stdout.strip():
        raise SystemExit("REFUSED: the tool directory has uncommitted changes; commit them so `builder:` names the code "
                         "that ran:\n" + dirty.stdout)
    head = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=HERE).stdout.strip()
    if not re.fullmatch(r"[0-9a-f]{40}", head):
        raise SystemExit("REFUSED: no HEAD commit (the rig)")
    return head


def digest(urls, metas):
    """§7: the sha256 over the `<url> <sha256>\\n` lines, in byte order."""
    lines = sorted(f"{u} {metas[u]['sha256']}\n".encode("utf-8") for u in urls)
    return hashlib.sha256(b"".join(lines)).hexdigest()


def common_dir(urls):
    pre = os.path.commonprefix(urls)
    return pre[:pre.rfind("/") + 1]


def manifest_bytes(files, metas, commit):
    entries = []
    for layer in list(LAYERS) + ["states", "counties"]:
        for u in files.get(layer, []):
            m = metas[u]
            entries.append({"bytes": m["bytes"], "layer": layer, "sha256": m["sha256"], "url": u,
                            "vintage": sources.vintage(u, m["last_modified"])})
    entries.sort(key=lambda e: e["url"])   # RD30 (MI4): the files by URL alone
    doc = {"builder": commit, "files": entries, "predicates": PREDICATES}
    return (json.dumps(doc, indent=1, sort_keys=True, ensure_ascii=False) + "\n").encode("utf-8")


def provenance(files, metas, commit, store, counts):
    lines = ["format: BMPK 1", f"builder: {BUILDER_NAME} {commit}", f"store: {store}"]
    for layer in LAYERS:
        urls = files[layer]
        vint = sorted({sources.vintage(u, metas[u]["last_modified"]) for u in urls})
        lines += [f"source.{layer}: {common_dir(urls)}", f"vintage.{layer}: {' '.join(vint)}",
                  f"files.{layer}: {len(urls)}", f"sha256.{layer}: {digest(urls, metas)}"]
    lines += [CREDIT_SRTM, PROCESSING_BOTH, f"epsilon.m: {TOLERANCE_MM // 1000}.{TOLERANCE_MM % 1000:03d}"]
    for layer in LAYERS:
        lines += [f"dropped.{layer}: 0", f"collapsed.{layer}: {counts.get(layer, 0)}"]
    return lines


def licence_text():
    return "\n".join([
        "bend.bmpk -- sources and terms (docs/basemap-package-format.md §7; docs/weatherscope-stage3.md A10.14, A10.18)",
        "",
        "Public domain (works of the United States government): TIGER/Line 2024 (AREAWATER, LINEARWATER, PRISECROADS, "
        "and the state and county files used to choose them), the 2025 Census Gazetteer, SUB-EST2024 (Census Bureau "
        "population estimates) and USGS GNIS Domestic Names.",
        "",
        "Terrain: AWS Terrain Tiles (Terrarium, zoom 9). This package's tiles contain SRTM only (inland US land below "
        "60 N), per tilezen/joerd's docs/data-sources.md zoom-9 row.",
        CREDIT_SRTM.split(": ", 1)[1],
        NOTICE,
        NOTICE_NOTE,
        "",
        "What was done to the elevations: " + PROCESSING_BOTH.split(": ", 1)[1],
        "",
    ])


def build(lat0, lon0, root, dry_run=False):
    """(package bytes, manifest bytes, licence text, report lines). Prints nothing."""
    fr = layers.Frame(lat0, lon0, R_MM, TOLERANCE_MM)
    files = plan(lat0, lon0, root)
    cands = layers.summits(fr, root, files["peaks"])
    extra = [sources.terrarium(x, y) for x, y in layers.summit_tiles(cands)]
    missing = sorted(set(extra) - set(files["terrain"]))
    if missing:
        raise SystemExit(f"REFUSED: {len(missing)} summit tile(s) outside the levels' mosaic (the rig)")
    metas = {u: sources.read_meta(root, u) for us in files.values() for u in us}
    mosaic = layers.Mosaic(root, [(int(u.rsplit("/", 2)[-2]), int(u.rsplit("/", 1)[-1][:-4])) for u in files["terrain"]])
    mosaic.shade(fr.lat0)

    counts, report = {}, []
    rings, counts["water"] = layers.water(fr, root, files["water"])
    names, rlines, counts["rivers"], rlen = layers.rivers(fr, root, files["rivers"])
    roads, counts["roads"], bus = layers.roads(fr, root, files["roads"])
    peaks = layers.peaks(fr, mosaic, cands)
    places = layers.places(fr, root, files["places"][0], files["population"][0])
    terr = [(lv, layers.terrain_level(fr, mosaic, lv, LEVEL_N)) for lv in LEVELS_MM]

    commit = "0" * 40 if dry_run else builder_commit()   # a dry run can never pass as a real build
    man = manifest_bytes(files, metas, commit)
    store = hashlib.sha256(man).hexdigest()[:12]
    prov = provenance(files, metas, commit, store, counts)
    sections = [
        (bmpk.WATER, len(rings), bmpk.water_body(rings)),
        (bmpk.RIVERS, len(rlines), bmpk.rivers_body(names, rlines)),
        (bmpk.ROADS, len(roads), bmpk.roads_body(roads)),
        (bmpk.PEAKS, len(peaks), bmpk.peaks_body(peaks)),
        (bmpk.PLACES, len(places), bmpk.places_body(places)),
    ] + [(bmpk.TERRAIN, 1, bmpk.terrain_body(lv, LEVEL_N, vals)) for lv, vals in terr] + [
        (bmpk.PROVENANCE, len(prov), bmpk.provenance_body(prov))]
    pkg = bmpk.encode(sections, lat0, lon0, R_MM, TOLERANCE_MM)
    non_terrain = len(pkg) - sum(len(b) for t, _, b in sections if t == bmpk.TERRAIN)   # RD20
    report += [
        f"sections: water {len(rings)} rings, rivers {len(rlines)} lines over {len(names)} names, roads {len(roads)} "
        f"lines ({bus} business-route records dropped), peaks {len(peaks)}, places {len(places)}, terrain {len(terr)} levels",
        "rivers kept: " + "; ".join(f"{n} {rlen[n] / 1000:.1f} km" for n in names),
        "peaks, rank order: " + "; ".join(f"{p[3]} {p[2]} m" for p in peaks),
        f"collapsed: {counts}",
        "bytes by section: " + ", ".join(f"{t}:{len(b)}" for t, _, b in sections),
        f"non-terrain (RD20): {non_terrain:,} B against the {TARGET:,} B target and the {CAP:,} B cap; total {len(pkg):,} B",
    ]
    if non_terrain > CAP:
        raise SystemExit("\n".join(report) + "\nSTOP (A10.2): over the 32 KB cap; no drop policy without its ruling")
    return pkg, man, licence_text(), report


def origin_from_package(path):
    b = open(path, "rb").read()
    return struct.unpack_from("<ii", b, 8)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["select", "fetch", "build", "check"])
    ap.add_argument("package", nargs="?")
    ap.add_argument("--origin-from")
    ap.add_argument("--sources", required=True)
    ap.add_argument("--out")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv[1:])
    if a.cmd == "select":
        lat0, lon0 = origin_from(a.origin_from)
        states, counties = select(lat0, lon0, a.sources)
        print(f"states read ({len(states)}): " + ", ".join(f"{fp} {n}" for fp, n in states))
        print(f"counties read ({len(counties)}): " + ", ".join(f"{g} {n}" for g, n in counties))
        return 0
    if a.cmd == "fetch":
        lat0, lon0 = origin_from(a.origin_from)
        files = plan(lat0, lon0, a.sources)
        metas = fetch_all(files, a.sources)
        for layer, urls in files.items():
            print(f"{layer}: {len(urls)} files, {sum(metas[u]['bytes'] for u in urls):,} B")
        return 0
    if a.cmd == "build":
        if a.origin_from:
            lat0, lon0 = origin_from(a.origin_from)
        elif a.package:
            lat0, lon0 = origin_from_package(a.package)   # check mode: the package's own header, never printed
        else:
            raise SystemExit("build needs --origin-from or a package to take the origin from")
        pkg, man, lic, report = build(lat0, lon0, a.sources, a.dry_run)
        stem = a.out[:-5] if a.out.endswith(".bmpk") else a.out
        for path, data in ((a.out, pkg), (stem + ".manifest.json", man), (stem + ".LICENSE.txt", lic.encode("utf-8"))):
            with open(path, "wb") as fh:
                fh.write(data)
        print("\n".join(report))
        print(f"wrote {os.path.basename(a.out)} ({len(pkg):,} B, sha256 {hashlib.sha256(pkg).hexdigest()[:12]}), "
              f"its manifest and its licence file")
        return 0
    if a.cmd == "check":
        return check(a.package, a.sources)
    raise SystemExit(f"{a.cmd}: unknown")


def check(pkg_path, root):
    """§7: rebuild with the `builder` commit recorded IN THE PACKAGE, in a temporary worktree, and compare bytes.
    0 identical, 1 any byte differs, 2 the rebuild could not run (BLIND, never 0)."""
    pkg = open(pkg_path, "rb").read()
    m = re.search(rb"builder: " + BUILDER_NAME.encode() + rb" ([0-9a-f]{40})", pkg)
    if not m:
        print("BLIND: no builder commit in the package's PROVENANCE")
        return 2
    commit = m.group(1).decode()
    tmp = tempfile.mkdtemp(prefix="bmpk-check-")
    wt = os.path.join(tmp, "wt")
    try:
        def git(*args):
            return subprocess.run(["git", *args], capture_output=True, text=True, cwd=HERE)
        r = git("worktree", "add", "--no-checkout", "--detach", wt, commit)
        if r.returncode != 0:
            print(f"BLIND: cannot check out {commit[:12]}: {r.stderr.strip()}")
            return 2
        for args in (["sparse-checkout", "set", "tools/weatherscope/basemap"], ["checkout", "-q", commit]):
            r = subprocess.run(["git", *args], capture_output=True, text=True, cwd=wt)
            if r.returncode != 0:
                print(f"BLIND: {' '.join(args)}: {r.stderr.strip()}")
                return 2
        out = os.path.join(tmp, "rebuilt.bmpk")
        tool = os.path.join(wt, "tools", "weatherscope", "basemap", "make_package.py")
        # -B: no __pycache__ in the worktree, so it removes cleanly without --force
        r = subprocess.run([sys.executable, "-I", "-B", tool, "build", pkg_path, "--sources", root, "--out", out],
                           capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            print(f"BLIND: the rebuild at {commit[:12]} failed:\n{r.stdout[-600:]}{r.stderr[-600:]}")
            return 2
        again = open(out, "rb").read()
        same = again == pkg
        first = next((i for i in range(min(len(pkg), len(again))) if pkg[i] != again[i]), None)
        print(f"check: rebuilt at {commit[:12]}: {len(again):,} B against {len(pkg):,} B -> "
              f"{'IDENTICAL' if same else f'DIFFERS (first at byte {first})'}")
        return 0 if same else 1
    finally:
        if os.path.isdir(wt):
            r = subprocess.run(["git", "worktree", "remove", wt], capture_output=True, text=True, cwd=HERE)
            if r.returncode != 0:
                print(f"note: the check's worktree was left at {wt} ({r.stderr.strip()}); remove it by hand")
            else:
                shutil.rmtree(tmp, ignore_errors=True)
        else:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
