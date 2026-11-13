# SCRATCH: scope ruling u1 at the code PR's head (docs/basemap-dense-code.md, "The code PR's runs after SEA-STORE").
# (1) KB3/KB4/KB8's inputs beyond the package bytes, hashed on each side and compared: every piece's (file, record, fate) in order and
#     the sea set (ringIdx vs run 10's seaIdx), the code's from seafig/sf-out.json (SEA-FIG's builds), run 10's from p2-out/*-sea-p21.json;
#     plus the sources run 10 rendered against (sea-pieces.json, the sea-obj index) printed by sha.
# (2) The KSEA guard on the code PR's KSEA package (cp-out/KSEA-sea.bmpk, SEA-PKG's build): its water rings (pkg2json.mts) with the code's
#     own ringIdx, rendered by run 10's p2-render.py kb4 against sea-obj. Guard: frac >= 0.886. By identity it must read run 10's 0.886081.
#     Its rings are also compared with run 10's p21 rings. Counts and hashes only.
#   python -X utf8 -I seafig/u1-ksea.py
import hashlib, json, os, subprocess
D = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = ["KBOS", "KJFK", "KLGA", "KEWR", "MIDTOWN", "KMIA", "KSFO", "KSEA", "KORD", "KDTW", "KMSP", "KIND", "KJAC", "BUFFALO", "ROUSES", "ZAPATA"]
sha = lambda b: hashlib.sha256(b).hexdigest()
canon = lambda o: json.dumps(o, separators=(",", ":"), sort_keys=True).encode()
code = json.load(open(os.path.join(D, "seafig", "sf-out.json"), encoding="utf-8"))
mine = {h: {"fates": code[h]["sea"]["fates"], "sea": code[h]["sea"]["ringIdx"]} for h in H}
r10 = {}
for h in H:
    q = json.load(open(os.path.join(D, "p2-out", f"{h}-sea-p21.json"), encoding="utf-8"))
    r10[h] = {"fates": [[f["file"], f["record"], f["fate"]] for f in q["fates"]], "sea": q["seaIdx"]}
a, b = sha(canon(mine)), sha(canon(r10))
print(f"u1 inputs, all 16 (fates in order + sea set): code {a[:12]}, run 10 {b[:12]}: {'EQUAL' if a == b else 'DIFFER'}")
per = [h for h in H if canon(mine[h]) != canon(r10[h])]
print(f"  points differing: {len(per)} {per}")
print(f"  sea-pieces.json {sha(open(os.path.join(D, 'sea-pieces.json'), 'rb').read())[:12]}; sea-obj index files: "
      + ", ".join(f"{n} {sha(open(os.path.join(D, 'sea-obj', n), 'rb').read())[:12]}" for n in sorted(os.listdir(os.path.join(D, "sea-obj"))) if n.endswith(".json") and "index" in n))
# (2) the KSEA guard
pk = os.path.join(D, "cp-out", "KSEA-sea.bmpk"); print(f"code PR's KSEA package {sha(open(pk, 'rb').read())[:12]} (SEA-PKG froze 9bd5b34d6718)")
tmp = os.path.join(D, "seafig", "ksea-code.json")
x = subprocess.run(["node", "--import", "tsx", os.path.join(D, "i2", "pkg2json.mts"), os.path.join(D, "wt-code", "proxy"), pk, tmp],
                   cwd=os.path.join(D, "wt-main", "proxy"), capture_output=True, text=True, encoding="utf-8")
if x.returncode: raise SystemExit(f"OFF: stop -- pkg2json exit {x.returncode}, stderr {len(x.stderr)} B")
d = json.load(open(tmp, encoding="utf-8")); d["water"] = [[[p[0], p[1]] for p in r] for r in d["water"]]; d["seaIdx"] = code["KSEA"]["sea"]["ringIdx"]
q = json.load(open(os.path.join(D, "p2-out", "KSEA-sea-p21.json"), encoding="utf-8"))
print(f"  its water rings vs run 10's p21: {'EQUAL' if d['water'] == q['water'] else 'DIFFER'} ({len(d['water'])} vs {len(q['water'])}); sea set {len(d['seaIdx'])}; "
      f"origin/radius {'EQUAL' if (d['lat0Micro'], d['lon0Micro'], d['radiusMm']) == (q['lat0Micro'], q['lon0Micro'], q['radiusMm']) else 'DIFFER'}")
json.dump(d, open(tmp, "w", encoding="utf-8"))
y = subprocess.run(["python", "-X", "utf8", "-I", os.path.join(D, "p2-render.py"), "kb4", tmp, os.path.join(D, "sea-obj")], cwd=D, capture_output=True, text=True, encoding="utf-8")
ls = [l for l in y.stdout.splitlines() if l.startswith("{")]
if y.returncode or len(ls) != 1: raise SystemExit(f"OFF: stop -- kb4 render exit {y.returncode}, {len(ls)} JSON lines, stderr {len(y.stderr)} B")
k = json.loads(ls[0])
print(f"KSEA GUARD on the code PR's package: frac {k['frac_le0']:.6f} (guard 0.886, run 10 0.886081), cover {k['cover']:.4f}, sea px {k['sea_px']}: "
      + ("PASS" if k["frac_le0"] >= 0.886 else "RED -- stop") + ("; equals run 10's to 6 places" if f"{k['frac_le0']:.6f}" == "0.886081" else "; DIFFERS from run 10's"))
