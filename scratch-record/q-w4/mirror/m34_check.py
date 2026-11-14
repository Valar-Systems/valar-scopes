# SCRATCH, read-only: switch_n6.pred's M-3 and M-4 graders, written before the dry runs.
#   keep <cache> <label>     after a dry run: copies dry-run/dry-run.json and dry-run/store/manifest.json to mirror/n6-runs/<label>-*
#   m3 <label>               M-3 on that manifest: 16,036 entries; the 8 entries equal bm-obj's (as JSON objects); keys
#                            builder,files,notes,predicates in that order; notes exactly the 6 expected lines in byte order,
#                            none on 27081 or 54093
#   m4 <labelA> <labelB> <treeA.json> <treeB.json>
#                            M-4: the store trees (seastore/tree.py hash) differ ONLY in store/manifest.json; the two manifests
#                            are equal once the notes key is removed; dry-run.json differs ONLY in storeId, manifestBytes and
#                            vectorStoreBytes
import json, os, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import S, bm_records, eight, mirror_of

K = os.path.join(S, "mirror", "n6-runs")
E = eight()
SIX = [u for u in E if not u.endswith(("27081_linearwater.zip", "54093_linearwater.zip"))]
BMID = json.load(open(os.path.join(S, "bm-obj", "current"), encoding="utf-8"))["storeId"]
note = lambda u: (f"{u}: fetched via the Census Bureau's mirror {mirror_of(u)} after www2.census.gov's firewall refused it; "
                  f"its bytes and sha256 equal this url's record in store {BMID}")
bad = 0


def row(name, ok, why):
    global bad
    bad += not ok
    print(f"  {name}: {'AS PREDICTED' if ok else 'OFF-PREDICTION'} ({why})")


cmd = sys.argv[1]
if cmd == "keep":
    cache, label = sys.argv[2], sys.argv[3]
    os.makedirs(K, exist_ok=True)
    for src, dst in (("dry-run.json", "dry-run.json"), (os.path.join("store", "manifest.json"), "manifest.json")):
        p = os.path.join(K, f"{label}-{dst}")
        if os.path.exists(p):
            sys.exit(f"STOP: {p} exists already")
        shutil.copyfile(os.path.join(cache, "dry-run", src), p)
    print(f"kept {label}-dry-run.json and {label}-manifest.json")
elif cmd == "m3":
    raw = open(os.path.join(K, f"{sys.argv[2]}-manifest.json"), "rb").read()
    m = json.loads(raw)
    BM = bm_records()
    files = {f["url"]: f for f in m["files"]}
    row("M-3 entries", len(m["files"]) == 16036, f"{len(m['files'])}")
    eq = [u for u in E if json.dumps(files.get(u), sort_keys=True) == json.dumps(BM.get(u), sort_keys=True)]
    row("M-3 the 8 equal bm-obj's entries", len(eq) == 8, f"{len(eq)} of 8 equal")
    row("M-3 keys", ",".join(m.keys()) == "builder,files,notes,predicates", ",".join(m.keys()))
    want = sorted((note(u) for u in SIX), key=lambda s: s.encode())
    got = m.get("notes", [])
    row("M-3 notes exactly the 6", got == want, f"{len(got)} notes; equal to the 6 expected: {got == want}")
    row("M-3 no note on 27081 or 54093", not any(n.split(": ", 1)[0] not in SIX for n in got), "")
    print(f"m3: {'ALL AS PREDICTED' if bad == 0 else f'{bad} OFF'}")
elif cmd == "m4":
    la, lb, ta, tb = sys.argv[2:6]
    A, B = (json.load(open(t, encoding="utf-8")) for t in (ta, tb))
    diff = sorted(k for k in set(A) | set(B) if A.get(k) != B.get(k))
    row("M-4 trees differ only in store/manifest.json", diff == ["store/manifest.json"], f"{len(A)}/{len(B)} files; differ {diff[:5]}")
    ma, mb = (json.load(open(os.path.join(K, f"{x}-manifest.json"), encoding="utf-8")) for x in (la, lb))
    strip = lambda d: {k: v for k, v in d.items() if k != "notes"}
    row("M-4 manifests equal once notes are removed", strip(ma) == strip(mb), f"notes {len(ma.get('notes', []))}/{len(mb.get('notes', []))}")
    da, db = (json.load(open(os.path.join(K, f"{x}-dry-run.json"), encoding="utf-8")) for x in (la, lb))
    dk = sorted(k for k in set(da) | set(db) if da.get(k) != db.get(k))
    row("M-4 dry-run.json differs only in storeId, manifestBytes, vectorStoreBytes", dk == ["manifestBytes", "storeId", "vectorStoreBytes"], f"{dk}")
    print(f"m4: {'ALL AS PREDICTED' if bad == 0 else f'{bad} OFF'}")
else:
    sys.exit("keep | m3 | m4")
sys.exit(1 if bad else 0)
