"""PJ3 (docs/weatherscope-stage3.md A9.10): every population stored in WxTowns.h is re-derived here, independently of
make_towns.py's join, and must be equal.

    python -I tools/weatherscope/towns/check_pop.py <2025_Gaz_place_national.zip> <sub-est2024.csv> [<WxTowns.h>]

How it re-derives (its own code path, not make_towns.py's): each stored town is found in the Gazetteer by its EXACT
NAME, INTPTLAT and INTPTLONG text, which gives its GEOID; the figure is POPESTIMATE2024 from the CSV's SUMLEV 162 row
whose STATE + PLACE equals that GEOID, or 0 when there is none. Also prints PJ2 (the top two by population) and PJ4
(the count with no figure). Exit 0 only when every stored figure equals the re-derived one. Prints no coordinate.
"""
import csv, io, os, re, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
ENTRY = re.compile(r'^    \{"([^"]*)", (-?\d+\.\d+), (-?\d+\.\d+), (\d+)\},$', re.M)


def main(argv):
    if len(argv) not in (3, 4):
        print(__doc__)
        return 2
    header = argv[3] if len(argv) == 4 else os.path.join(ROOT, "src", "weatherscope", "WxTowns.h")
    stored = ENTRY.findall(open(header, encoding="utf-8").read().replace("\r\n", "\n"))
    if not stored:
        print("REFUSED: no town entries with a population in the header (the rig)")
        return 2
    lines = zipfile.ZipFile(argv[1]).read("2025_Gaz_place_national.txt").decode("utf-8").splitlines()
    cols = [c.strip() for c in lines[0].split("|")]
    at = {c: cols.index(c) for c in ("GEOID", "NAME", "INTPTLAT", "INTPTLONG")}
    geoid = {}
    for line in lines[1:]:
        f = [x.strip() for x in line.split("|")]
        geoid[(f[at["NAME"]], f[at["INTPTLAT"]], f[at["INTPTLONG"]])] = f[at["GEOID"]]
    figure = {}
    with open(argv[2], encoding="latin-1", newline="") as fh:
        for r in csv.reader(fh):
            if r and r[0] == "162":
                figure[r[1] + r[3]] = int(r[15])   # STATE, PLACE, POPESTIMATE2024 by position, read independently
    head = next(csv.reader(open(argv[2], encoding="latin-1")))
    if head[0] != "SUMLEV" or head[1] != "STATE" or head[3] != "PLACE" or head[15] != "POPESTIMATE2024":
        print(f"REFUSED: the CSV's columns are not the expected ones: {head[:4]} ... {head[15]} (the rig)")
        return 2
    bad, missing_geoid = [], []
    for name, lat, lon, pop in stored:
        g = geoid.get((name, lat, lon))
        if g is None:
            missing_geoid.append(name)
            continue
        want = figure.get(g, 0)
        if int(pop) != want:
            bad.append((name, int(pop), want))
    ok = not bad and not missing_geoid
    print(f"PJ3: {len(stored)} towns; {len(stored) - len(bad) - len(missing_geoid)} figures equal the re-derived "
          f"POPESTIMATE2024 -> {'AS PREDICTED' if ok else 'DIFFER: ' + str(bad[:5]) + ' ' + str(missing_geoid[:5])}")
    ranked = sorted(stored, key=lambda t: -(int(t[3]) or 1000))
    print(f"PJ2: first {ranked[0][0]!r}, second {ranked[1][0]!r}")
    print(f"PJ4: {sum(1 for t in stored if int(t[3]) == 0)} with no figure")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
