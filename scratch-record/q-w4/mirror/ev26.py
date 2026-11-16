# SCRATCH (map-server): step8_26.pred's EV, the cross-commit check. READ-ONLY. Compares two sets of a dry run's small JSONs: a
# directory A and a directory B, each holding dry-run.json, store/manifest.json, store/index.json and store/sea-regions.json (the
# M-4 evidence dir from 6a11b5a21d0a, and a keep's json/ dir). Prints key NAMES, byte-equality and counts, never a value.
#   python -X utf8 -I ev26.py <dirA> <dirB> [<B's files.json>]
# Lines: "dry-run.json: keys that differ [...]", "store/manifest.json: keys that differ [...]",
#        "store/index.json byte-equal <bool>", "store/sea-regions.json byte-equal <bool>",
#        "manifestBytes equal <bool>; vectorStoreBytes equal <bool>", and, with files.json, "B cell files <n>".
# Exit 0 when it could read all four in both; 3 when a file is missing (no reading given).
import json, os, sys

FILES = ("dry-run.json", "store/manifest.json", "store/index.json", "store/sea-regions.json")


def main():
    a, b = sys.argv[1], sys.argv[2]
    pa = {f: os.path.join(a, *f.split("/")) for f in FILES}
    pb = {f: os.path.join(b, *f.split("/")) for f in FILES}
    miss = [f for f in FILES if not os.path.isfile(pa[f]) or not os.path.isfile(pb[f])]
    if miss:
        print(f"INSTRUMENT: missing {miss}; no EV reading")
        return 3
    for f in ("dry-run.json", "store/manifest.json"):
        ja, jb = json.load(open(pa[f], encoding="utf-8")), json.load(open(pb[f], encoding="utf-8"))
        moved = sorted(k for k in set(ja) | set(jb) if ja.get(k, "<absent>") != jb.get(k, "<absent>"))
        print(f"{f}: keys that differ {moved}")
        if f == "dry-run.json":
            print(f"manifestBytes equal {ja.get('manifestBytes') == jb.get('manifestBytes')}; "
                  f"vectorStoreBytes equal {ja.get('vectorStoreBytes') == jb.get('vectorStoreBytes')}")
    for f in ("store/index.json", "store/sea-regions.json"):
        print(f"{f} byte-equal {open(pa[f], 'rb').read() == open(pb[f], 'rb').read()}")
    if len(sys.argv) > 3:
        m = json.load(open(sys.argv[3], encoding="utf-8"))["files"]
        print(f"B cell files {sum(1 for r in m if r.startswith('store/cells/'))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
