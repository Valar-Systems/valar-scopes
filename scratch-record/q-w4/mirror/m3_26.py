# SCRATCH (map-server): step8_25.pred's M-3 row, with step8_26.pred's builder (e81e794908a6), as one checker. READ-ONLY.
#   python -X utf8 -I m3_26.py <manifest.json>
# Grades the manifest against: mirror/m3-26-expect.json (TIGER2025_NOTE and TIGER2025_URLS from the dry-clone's own sources.ts),
# record.json (the six's url / bytes / sha256), bm-obj's manifest (the two P1 entries) and the dry-clone's HEAD (the builder).
# Rows, each printed AS PREDICTED or OFF:
#   entries 16,036; keys exactly builder,files,notes,predicates in that order; builder == the dry-clone's full HEAD;
#   the 2 P1 entries (27081 and 54093 TIGER2024 linearwater) equal bm-obj's entries, as JSON objects;
#   the six: url, bytes and sha256 equal record.json's, vintage "2025", layers water x4 (AREAWATER) and rivers x2 (LINEARWATER);
#   notes exactly the six lines "<url>: <TIGER2025_NOTE>", in byte order, on exactly TIGER2025_URLS; none on 27081 or 54093;
#   no "ftp2.census.gov" anywhere in the manifest's bytes.
# Exit 0 all AS PREDICTED; 1 any OFF; 3 an input missing. Counts and row names only.
import json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCR = os.path.dirname(HERE)
RECORD = "C:/Github/_handoffs/map-server/census-2025/record.json"
P1 = ("tl_2024_27081_linearwater.zip", "tl_2024_54093_linearwater.zip")
bad = 0


def row(name, ok, why):
    global bad
    bad += not ok
    print(f"  {name}: {'AS PREDICTED' if ok else 'OFF'} ({why})")


def main():
    path = sys.argv[1]
    try:
        raw = open(path, "rb").read()
        exp = json.load(open(os.path.join(HERE, "m3-26-expect.json"), encoding="utf-8"))
        rec = json.load(open(RECORD, encoding="utf-8"))["files"]
        bm = {f["url"]: f for f in json.load(open(os.path.join(SCR, "bm-obj", "manifest.json"), encoding="utf-8"))["files"]}
    except OSError as e:
        print(f"INSTRUMENT: an input is missing ({e.__class__.__name__})"); return 3
    head = subprocess.run(["git", "-C", os.path.join(SCR, "dry-clone"), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    m = json.loads(raw)
    files = m.get("files", [])
    by = {f["url"]: f for f in files}
    print(f"M-3 on {os.path.basename(path)}:")
    row("entries", len(files) == 16036, f"{len(files)}")
    row("keys", ",".join(m.keys()) == "builder,files,notes,predicates", ",".join(m.keys()))
    row("builder", m.get("builder") == head and head.startswith("e81e794908a6"), f"{str(m.get('builder'))[:12]} vs the dry-clone's {head[:12]}")
    p1 = [u for u in bm if u.endswith(P1)]
    row("the 2 P1 entries equal bm-obj's", len(p1) == 2 and all(by.get(u) == bm[u] for u in p1), f"{sum(by.get(u) == bm[u] for u in p1)} of {len(p1)}")
    six_ok = len(rec) == 6
    layers = {"water": 0, "rivers": 0}
    for r in rec:
        f = by.get(r["url"])
        good = f is not None and f.get("bytes") == r["bytes"] and f.get("sha256") == r["sha256"] and f.get("vintage") == "2025"
        if good:
            layers[f.get("layer")] = layers.get(f.get("layer"), 0) + 1
            good = ("AREAWATER" in r["url"] and f["layer"] == "water") or ("LINEARWATER" in r["url"] and f["layer"] == "rivers")
        six_ok &= good
    row("the six equal record.json, vintage 2025, layers water x4 rivers x2", six_ok and layers.get("water") == 4 and layers.get("rivers") == 2,
        f"water {layers.get('water')}, rivers {layers.get('rivers')}")
    want = sorted(f"{u}: {exp['note']}" for u in exp["urls"])
    want = sorted(want, key=lambda s: s.encode("utf-8"))
    got = m.get("notes", [])
    row("notes exactly the six lines, byte order", got == want, f"{len(got)} notes; equal: {got == want}")
    row("no note on 27081 or 54093", not any(n.split(": ", 1)[0].endswith(P1) for n in got), f"{sum(n.split(': ', 1)[0].endswith(P1) for n in got)}")
    row("no ftp2.census.gov in the bytes", b"ftp2.census.gov" not in raw, f"{raw.count(b'ftp2.census.gov')} occurrence(s)")
    print(f"M-3: {'AS PREDICTED' if bad == 0 else f'{bad} row(s) OFF'}")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
