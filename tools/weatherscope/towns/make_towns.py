"""The town labels (docs/weatherscope-stage3.md A8.10, A8.14, A9.10 PJ, A9.13): the 2025 Census Gazetteer Places
subset around the loop, with each place's 2024 population joined from Census SUB-EST2024 (Q12), written to
src/weatherscope/WxTowns.h.

    python -I tools/weatherscope/towns/make_towns.py <2025_Gaz_place_national.zip> <sub-est2024.csv>
    python -I tools/weatherscope/towns/make_towns.py <zip> <csv> --check [<WxTowns.h>]   D6(a)/PJ1: exit 0 only if identical

THE FILTER, as frozen: LSAD 25 only, within 60.0 mi (inclusive) of the loop crop's centre in the map's metric
(planar, 111 km per degree, longitude scaled by the cosine of the centre's latitude). The centre is read from a loop
crop's header (D3: all 15 share it). NAME, INTPTLAT and INTPTLONG are stored as the file's own text, never
re-formatted through a float; rows stay in the file's order.

THE JOIN (Q12): keyed on GEOID. A place's figure is POPESTIMATE2024 from SUB-EST2024's SUMLEV 162 row whose STATE + PLACE
equals the Gazetteer's GEOID; a place with no such row stores 0, "no figure", which ranks as 1,000 (A9.10 LB).

STOPS, never substitutes: the zip not holding the 2025 places file; no LSAD, INTPTLAT, INTPTLONG or GEOID column; the
CSV without SUMLEV, STATE, PLACE or POPESTIMATE2024; a GEOID with two SUMLEV 162 rows; a kept row whose name cannot be
written into a C string literal as-is; zero rows kept.

Both inputs are downloaded outside every repo and never committed. Prints counts and names -- never a coordinate.
--check compares with the committed file's line endings normalised to LF (a Windows checkout writes CRLF).
"""
import csv
import hashlib
import io
import math
import os
import struct
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
OUT = os.path.join(ROOT, "src", "weatherscope", "WxTowns.h")
CROP = os.path.join(ROOT, "test", "fixtures", "wx", "loop", "shsr-20240521-220000.wxc")
URL = "https://www2.census.gov/geo/docs/maps-data/data/gazetteer/2025_Gazetteer/2025_Gaz_place_national.zip"
POP_URL = "https://www2.census.gov/programs-surveys/popest/datasets/2020-2024/cities/totals/sub-est2024.csv"
MEMBER = "2025_Gaz_place_national.txt"
RADIUS_MI = 60.0
KM_PER_DEG, KM_PER_MI = 111.0, 1.609344


def centre():
    raw = open(CROP, "rb").read()
    _, ver, _, _, _, _, lat_nw, lon_nw, cell_lat, cell_lon, rows, cols = struct.unpack_from("<4sBBBBIiiIIHH", raw, 0)
    if raw[:4] != b"WXC1" or ver != 1:
        sys.exit("STOP: the loop crop is not a WXC1 v1 crop")
    return (lat_nw / 1e6 - (rows - 1) / 2 * cell_lat / 1e6, lon_nw / 1e6 + (cols - 1) / 2 * cell_lon / 1e6)


def populations(csv_path):
    raw = open(csv_path, "rb").read()
    psha = hashlib.sha256(raw).hexdigest()
    rows = list(csv.DictReader(io.StringIO(raw.decode("latin-1"))))
    for need in ("SUMLEV", "STATE", "PLACE", "POPESTIMATE2024"):
        if need not in rows[0]:
            sys.exit(f"STOP: no {need} column in the population file")
    pop = {}
    for r in rows:
        if r["SUMLEV"] != "162":
            continue
        g = r["STATE"] + r["PLACE"]
        if g in pop:
            sys.exit(f"STOP: GEOID {g} has two SUMLEV 162 rows")
        pop[g] = int(r["POPESTIMATE2024"])
    return pop, psha


def build(zip_path, csv_path):
    zbytes = open(zip_path, "rb").read()
    zsha = hashlib.sha256(zbytes).hexdigest()
    z = zipfile.ZipFile(io.BytesIO(zbytes))
    if MEMBER not in z.namelist():
        sys.exit(f"STOP: {MEMBER} is not in the zip ({z.namelist()})")
    lines = z.read(MEMBER).decode("utf-8").splitlines()
    cols = [c.strip() for c in lines[0].split("|")]
    for need in ("NAME", "LSAD", "INTPTLAT", "INTPTLONG", "GEOID"):
        if need not in cols:
            sys.exit(f"STOP: no {need} column in {MEMBER}")
    iname, ilsad, ilat, ilon, igeo = (cols.index(c) for c in ("NAME", "LSAD", "INTPTLAT", "INTPTLONG", "GEOID"))
    pop, psha = populations(csv_path)
    clat, clon = centre()
    kx, ky = KM_PER_DEG * math.cos(math.radians(clat)), KM_PER_DEG
    kept, lsad25 = [], 0
    for line in lines[1:]:
        f = [x.strip() for x in line.split("|")]
        if f[ilsad] != "25":
            continue
        lsad25 += 1
        lat_t, lon_t = f[ilat], f[ilon]
        dx, dy = (float(lon_t) - clon) * kx, (float(lat_t) - clat) * ky
        if math.sqrt(dx * dx + dy * dy) / KM_PER_MI <= RADIUS_MI:
            name = f[iname]
            if '"' in name or "\\" in name or any(ord(ch) < 0x20 for ch in name):
                sys.exit(f"STOP: the name {name!r} cannot be written into a C string literal as-is")
            kept.append((name, lat_t, lon_t, pop.get(f[igeo], 0)))
    if not kept:
        sys.exit("STOP: the filter kept zero rows")
    with_figure = sum(1 for k in kept if k[3] > 0)
    body = "".join(f'    {{"{n}", {la}, {lo}, {p}}},\n' for n, la, lo, p in kept)
    bsha = hashlib.sha256(body.encode("utf-8")).hexdigest()
    head = (
        "// Weatherscope's town labels: the 2025 Census Gazetteer Places subset around 3.3's loop, with each place's 2024\n"
        "// population (docs/weatherscope-stage3.md A8.10, A8.14, A9.10 PJ). GENERATED by tools/weatherscope/towns/make_towns.py\n"
        "// -- do not edit; regenerate it, and check it with --check (D6(a), PJ1).\n"
        "//\n"
        f"//   source:          {URL}\n"
        "//   vintage:         2025\n"
        f"//   source sha256:   {zsha}\n"
        f"//   population:      {POP_URL}\n"
        f"//   population sha:  {psha}\n"
        "//   join:            on GEOID: POPESTIMATE2024 of the SUMLEV 162 row whose STATE + PLACE equals it; 0 = no figure\n"
        f"//   filter:          LSAD 25 only, within {RADIUS_MI:.1f} mi (inclusive) of the loop crop's centre, in the map's metric\n"
        "//                    (planar, 111 km per degree, longitude scaled by the cosine of the centre's latitude)\n"
        "//   fields:          NAME, INTPTLAT, INTPTLONG, as the file's own text, and the population; rows in the file's order\n"
        f"//   towns:           {len(kept)} ({with_figure} with a population figure)\n"
        f"//   content sha256:  {bsha}  (of the array's body lines)\n"
        "//\n"
        "// Public-domain place data, stored by ruling (A8.10, Q6(b)(ii)). A tool that picks fixture centres skips this\n"
        "// file by path and content hash, or every town would read as 'too close'.\n"
        "#pragma once\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
        "namespace wxtowns {\n"
        "\n"
        "struct Town {\n"
        "    const char* name;  // NAME as the Gazetteer writes it, \"Arlington city\"; wxlogic::LabelText makes the label\n"
        "    double lat, lon;   // INTPTLAT, INTPTLONG: the place's internal point\n"
        "    uint32_t pop;      // POPESTIMATE2024 (SUB-EST2024); 0 = no figure, which ranks as 1,000\n"
        "};\n"
        "\n"
        "constexpr Town kTowns[] = {\n"
    )
    tail = "};\nconstexpr unsigned kCount = sizeof(kTowns) / sizeof(kTowns[0]);\n\n}  // namespace wxtowns\n"
    return head + body + tail, kept, lsad25, zsha, psha, bsha, with_figure


def main(argv):
    if len(argv) < 3 or (len(argv) > 3 and argv[3] != "--check") or len(argv) > 5:
        print(__doc__)
        return 2
    text, kept, lsad25, zsha, psha, bsha, with_figure = build(argv[1], argv[2])
    print(f"zip sha256 {zsha[:12]}; population sha256 {psha[:12]}; LSAD 25 rows in the file: {lsad25}; "
          f"kept within {RADIUS_MI:.0f} mi: {len(kept)}, {with_figure} with a figure (PJ4)")
    ranked = sorted(kept, key=lambda k: -(k[3] or 1000))
    print("by population: " + "; ".join(f"{n} {p}" for n, _, _, p in ranked[:5]))
    print(f"content sha256 {bsha[:12]}")
    if len(argv) >= 4:
        path = argv[4] if len(argv) == 5 else OUT
        committed = open(path, "rb").read().replace(b"\r\n", b"\n") if os.path.exists(path) else b""
        same = committed == text.encode("utf-8")
        print(f"{os.path.basename(path)}: {'identical' if same else 'DIFFERS'}")
        return 0 if same else 1
    open(OUT, "w", encoding="utf-8", newline="\n").write(text)
    print(f"wrote {os.path.relpath(OUT, ROOT)} ({len(text.encode('utf-8'))} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
