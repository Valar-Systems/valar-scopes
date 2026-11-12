"""/healthz's every-product sabotages (docs/wx-healthz-products.md §4: H1s, H2s, H3s). Each one: plant an exact one-place
edit (refused unless the anchor matches exactly once), run test/wx-endpoint.test.ts alone (vitest JSON reporter),
compare the WHOLE list of failed tests with the predicted one, then restore the file from its bytes and check its
sha256. A sabotage whose failed list is empty did not apply, and fails the run. On Windows each vitest run waits for
the host's TIME_WAIT sockets to drain below 2,000 first (back-to-back pool-workers runs exhaust ephemeral ports).
Built like sabotage-wx-split.py. Not run by CI: run it locally before any change to these files or tests.

  python scripts/sabotage-wx-healthz.py          (from proxy/; exit 0 only if every sabotage reads as predicted)
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time

PROXY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST = "test/wx-endpoint.test.ts"
SABOTAGES = [
    # /healthz goes back to reporting ROUTED's products only: the nine unrouted ones vanish again.
    ("H1s", "src/index.ts",
     "wx: await wxHealth(env, PRODUCTS, ROUTED), basemap",
     "wx: await wxHealth(env, PRODUCTS.filter((p) => ROUTED.has(p.id)), ROUTED), basemap",
     ["H1", "H2", "H3"]),
    # Every product reads routed (H3 fails too: its fixture projection carries routed; corrected before any run).
    ("H2s", "src/wx/handler.ts",
     "    e.routed = routed.has(p.id);",
     "    e.routed = true;",
     ["H1", "H2", "H3"]),
    # Nothing is ever stale.
    ("H3s", "src/wx/handler.ts",
     "    e.stale = rec ? now - rec.epoch > p.maxAgeS : null;",
     "    e.stale = rec ? false : null;",
     ["H3"]),
]
TESTS = 17   # wx-endpoint.test.ts after docs/wx-healthz-products.md: 15 before, H1 replacing one, H2 and H3 new


def drain():
    if os.name != "nt":
        return
    for _ in range(120):
        n = subprocess.run(["netstat", "-an"], capture_output=True, text=True, errors="replace").stdout.count("TIME_WAIT")
        if n < 2000:
            return
        time.sleep(5)


def run_tests():
    drain()
    out = os.path.join(tempfile.mkdtemp(prefix="healthz-"), "r.json")
    subprocess.run(["npx", "vitest", "run", TEST, "--reporter=json", f"--outputFile={out}"], cwd=PROXY,
                   shell=(os.name == "nt"), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(out):
        return None
    r = json.load(open(out, encoding="utf-8"))
    results = [a for t in r["testResults"] for a in t["assertionResults"]]
    return sorted(a["title"].split(" ")[0] for a in results if a["status"] != "passed"), len(results)


ok = True
base = run_tests()
print("unsabotaged:", base)
if base != ([], TESTS):
    print(f"FAIL: the unsabotaged file does not read {TESTS} passed")
    ok = False
for name, rel, old, new, expect in SABOTAGES:
    path = os.path.join(PROXY, rel)
    orig = open(path, "rb").read()
    sha = hashlib.sha256(orig).hexdigest()
    text = orig.decode("utf-8")
    nl = "\r\n" if "\r\n" in text else "\n"
    o, n = old.replace("\n", nl), new.replace("\n", nl)
    if text.count(o) != 1:
        print(f"{name}: FAIL -- the anchor matches {text.count(o)} times, not once")
        ok = False
        continue
    try:
        open(path, "wb").write(text.replace(o, n).encode("utf-8"))
        if open(path, "rb").read() == orig:
            print(f"{name}: FAIL -- the plant did not change the file")
            ok = False
            continue
        got = run_tests()
    finally:
        open(path, "wb").write(orig)
    restored = hashlib.sha256(open(path, "rb").read()).hexdigest() == sha
    verdict = got is not None and got[0] == expect and got[1] == TESTS and restored
    print(f"{name}: failed {got[0] if got else None} of {got[1] if got else None}, predicted {expect}; restored by sha: {restored} -> {'AS PREDICTED' if verdict else 'OFF'}")
    ok = ok and verdict
print("ALL AS PREDICTED" if ok else "OFF")
sys.exit(0 if ok else 1)
