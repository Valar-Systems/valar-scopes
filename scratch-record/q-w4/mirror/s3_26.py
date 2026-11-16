# SCRATCH (map-server): step8_26.pred Amendment 5b, S-3 (I3) on tree B after the coverage step. READ-ONLY.
# The coverage step's store/coverage.bin must equal the dry run's in-memory coverage, whose sha256 the dry run recorded as
# dry-run.json's seaPieces.coverageSha256; and its first 12 hex must equal 5cf7169484c2, the prototype store's (basemap-dense-code.md
# :3899), because S-1 found the states file unchanged (:4115).
# CONTROL: the same bytes with the last byte changed must read unequal to the recorded sha. Prints booleans and a 12-hex prefix only.
# Exit 0 all true; 1 any false; 3 an input missing.
#   python -X utf8 -I mirror/s3_26.py
import hashlib, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.join(os.path.dirname(os.path.dirname(HERE)), "bm-cache-qw4", "dry-run")


def main():
    try:
        cov = open(os.path.join(TREE, "store", "coverage.bin"), "rb").read()
        rec = json.load(open(os.path.join(TREE, "dry-run.json"), encoding="utf-8"))["seaPieces"]["coverageSha256"]
    except (OSError, KeyError) as e:
        print(f"INSTRUMENT: an input is missing ({e.__class__.__name__})"); return 3
    h = hashlib.sha256(cov).hexdigest()
    planted = cov[:-1] + bytes([cov[-1] ^ 1])
    rows = {
        "coverage.bin equals the dry run's in-memory coverage": h == rec,
        "its first 12 hex are 5cf7169484c2": h[:12] == "5cf7169484c2",
        "CONTROL: one byte changed reads unequal": hashlib.sha256(planted).hexdigest() != rec,
    }
    print(f"S-3: coverage.bin {len(cov)} B, sha256 {h[:12]}; recorded {rec[:12]}")
    for k, v in rows.items():
        print(f"  {k}: {v}")
    ok = all(rows.values())
    print(f"S-3: {'AS PREDICTED' if ok else 'OFF'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
