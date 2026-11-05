"""Plan S's test sabotages (docs/wx-plan-s.md, PS1s, PS2s, PS3s, PS45s). Each one: plant an exact one-place edit
(refused unless the anchor matches exactly once), run test/wx-ingest-dev.test.ts alone (vitest JSON reporter), compare
the WHOLE list of failed tests with the predicted one, then restore the file from its bytes and check its sha256.
A sabotage whose failed list is empty did not apply, and fails the run.

  python scripts/sabotage-wx-plan-s.py          (from proxy/; exit 0 only if every sabotage reads as predicted)
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile

PROXY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST = "test/wx-ingest-dev.test.ts"
SABOTAGES = [
    ("PS1s", "src/wx/ingest-worker.ts",
     "products: readonly Product[] = PRODUCTS): Promise<void> {",
     "products: readonly Product[] = PRODUCTS.slice(0, 1)): Promise<void> {",
     ["PS1"]),
    ("PS2s", "src/wx/ingest-worker.ts",
     "await Promise.all(products.map(async (p) => {",
     "await Promise.all(PRODUCTS.map(async (p) => {",
     ["PS2", "PS6"]),
    ("PS3s", "src/wx/ingest-dev-worker.ts",
     "log(JSON.stringify({ evt: PLANT_MARKER, check: label, iter, result: plant(iter) }));",
     "await env.WX.head(\"plant-sabotage\"); log(JSON.stringify({ evt: PLANT_MARKER, check: label, iter, result: plant(iter) }));",
     ["PS3"]),
    ("PS45s", "src/wx/ingest-dev-worker.ts",
     "log(JSON.stringify({ evt: mode === \"off\" ? \"wx_dev_off\" : \"wx_dev_mode_error\", check: label, mode }));",
     "await checkAll(env, label, doFetch, log, opts);",
     ["PS4", "PS5"]),
]


def run_tests():
    out = os.path.join(tempfile.mkdtemp(prefix="plan-s-"), "r.json")
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
if base != ([], 6):
    print("FAIL: the unsabotaged file does not read 6 passed")
    ok = False
for name, rel, old, new, expect in SABOTAGES:
    path = os.path.join(PROXY, rel)
    orig = open(path, "rb").read()
    sha = hashlib.sha256(orig).hexdigest()
    text = orig.decode("utf-8")
    if text.count(old) != 1:
        print(f"{name}: FAIL -- the anchor matches {text.count(old)} times, not once")
        ok = False
        continue
    try:
        open(path, "wb").write(text.replace(old, new).encode("utf-8"))
        if open(path, "rb").read() == orig:
            print(f"{name}: FAIL -- the plant did not change the file")
            ok = False
            continue
        got = run_tests()
    finally:
        open(path, "wb").write(orig)
    restored = hashlib.sha256(open(path, "rb").read()).hexdigest() == sha
    verdict = got is not None and got[0] == expect and got[1] == 6 and restored
    print(f"{name}: failed {got[0] if got else None} of {got[1] if got else None}, predicted {expect}; restored by sha: {restored} -> {'AS PREDICTED' if verdict else 'OFF'}")
    ok = ok and verdict
print("ALL AS PREDICTED" if ok else "OFF")
sys.exit(0 if ok else 1)
