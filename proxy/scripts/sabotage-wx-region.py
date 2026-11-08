"""The region rows' plants (docs/wx-opera-code-stage.md §3 and §7.4): D1s and F2s (a table generated without Malta),
D3s-a (with Italy added, its left-over refusal off), D5s (without today's UK box), and A1s (opis's row without
deviceMaxAgeS). Each plant is applied to the real file, test/wx-region.test.ts runs alone, and the WHOLE list of failed
tests, and the RESULT lines the rows print, are compared with the prediction below, written before the first run. The
file is then restored from its bytes and its sha256 checked. A plant that does not change its file fails the run.

Local only: the tables come from the generator, which needs Natural Earth's 13 MB file (scripts/gen-wx-region.mjs).
Before each vitest run, the host's TIME_WAIT sockets must be under 2,000 (pool-workers runs exhaust ephemeral ports).

  python scripts/sabotage-wx-region.py --ne <ne_10m_admin_0_countries.geojson>   (from proxy/; exit 0 only as predicted)
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time

PROXY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST = "test/wx-region.test.ts"
TABLE = "src/wx/region.generated.ts"
PRODUCTS = "src/wx/products.ts"
TESTS = 12   # G1, D1, D2, D2s, D3, D3s-b, D5, A1, F2, R1, R1s, R2
ITALIAN = [f"{c}: excluded side" for c in ("Nova Gorica/Gorizia", "Menton/Ventimiglia", "Chiasso/Como", "Brenner", "San Marino/Italy")]
OPIS = 'opera("opis", box(-25.0, 62.0, -7.0, 72.0), { la1: 73.2, lo1: -26.56, ni: 2065, nj: 1183 }, { n: 242 }),'

# name -> (how to plant, the predicted failed tests, predicted RESULT values by row)
PLANTS = {
    "D1s+F2s": (("table", ["--drop", "Malta"]), ["D1", "F2", "G1"], {"D1": ["Malta"], "F2": ["Malta"]}),
    "D3s-a": (("table", ["--add", "Italy", "--allow-leftover"]), ["D2", "D3", "G1"], {"D2": ["Italy"], "D3": ITALIAN}),
    "D5s": (("table", ["--no-ukbox"]), ["D5", "G1"], {}),
    "A1s": (("edit", PRODUCTS, OPIS, "{ ...opera(\"opis\", box(-25.0, 62.0, -7.0, 72.0), { la1: 73.2, lo1: -26.56, ni: 2065, nj: 1183 }, { n: 242 }), deviceMaxAgeS: undefined },"),
            ["A1", "F2"], {"A1": ["opis: row"], "F2": ["Iceland"]}),
}


def drain():
    if os.name != "nt":
        return
    for _ in range(120):
        n = subprocess.run(["netstat", "-an"], capture_output=True, text=True, errors="replace").stdout.count("TIME_WAIT")
        if n < 2000:
            return
        time.sleep(5)
    raise SystemExit("TIME_WAIT stayed at 2,000 or more for 10 minutes: stop")


def run_tests():
    drain()
    out = os.path.join(tempfile.mkdtemp(prefix="region-"), "r.json")
    p = subprocess.run(["npx", "vitest", "run", TEST, "--reporter=default", "--reporter=json", f"--outputFile.json={out}"],
                       cwd=PROXY, shell=(os.name == "nt"), capture_output=True, text=True, errors="replace")
    if not os.path.exists(out):
        return None
    r = json.load(open(out, encoding="utf-8"))
    results = [a for t in r["testResults"] for a in t["assertionResults"]]
    failed = sorted(a["title"].split(" ")[0] for a in results if a["status"] != "passed")
    rows = {m.group(1): json.loads(m.group(2)) for m in re.finditer(r"RESULT (\S+) (.+)", p.stdout)}
    return failed, len(results), rows


def generate(ne, args, dest):
    p = subprocess.run(["npx", "tsx", "scripts/gen-wx-region.mjs", "--ne", ne, *args, "--out", dest], cwd=PROXY,
                       shell=(os.name == "nt"), capture_output=True, text=True, errors="replace")
    return p.returncode, p.stdout


def main():
    ne = sys.argv[sys.argv.index("--ne") + 1] if "--ne" in sys.argv else None
    if not ne:
        raise SystemExit("--ne <ne_10m_admin_0_countries.geojson> is required")
    ok = True
    base = run_tests()
    print("unsabotaged:", base and (base[0], base[1]), "D5", base and base[2].get("D5"), "R2", base and base[2].get("R2"))
    if not base or base[0] != [] or base[1] != TESTS:
        print(f"FAIL: the unsabotaged file does not read {TESTS} passed")
        ok = False
    for name, (how, expect, rows) in PLANTS.items():
        path = os.path.join(PROXY, TABLE if how[0] == "table" else how[1])
        orig = open(path, "rb").read()
        sha = hashlib.sha256(orig).hexdigest()
        try:
            if how[0] == "table":
                tmp = os.path.join(tempfile.mkdtemp(prefix="region-table-"), "t.ts")
                rc, log = generate(ne, how[1], tmp)
                if rc != 0:
                    print(f"{name}: FAIL -- the generator refused: {log[-300:]}")
                    ok = False
                    continue
                open(path, "wb").write(open(tmp, "rb").read())
            else:
                text = orig.decode("utf-8")
                if text.count(how[2]) != 1:
                    print(f"{name}: FAIL -- the anchor matches {text.count(how[2])} times, not once")
                    ok = False
                    continue
                open(path, "wb").write(text.replace(how[2], how[3]).encode("utf-8"))
            if open(path, "rb").read() == orig:
                print(f"{name}: FAIL -- the plant did not change the file")
                ok = False
                continue
            got = run_tests()
        finally:
            open(path, "wb").write(orig)
        restored = hashlib.sha256(open(path, "rb").read()).hexdigest() == sha
        rows_ok = got is not None and all(got[2].get(k) == v for k, v in rows.items())
        if name == "D5s" and got is not None:
            rows_ok = rows_ok and (got[2].get("D5") or {}).get("notOpuk", 0) > 0
        verdict = got is not None and got[0] == expect and got[1] == TESTS and rows_ok and restored
        print(f"{name}: failed {got[0] if got else None} of {got[1] if got else None}, predicted {expect}; "
              f"rows {json.dumps({k: got[2].get(k) for k in (list(rows) or ['D5'])}) if got else None}; "
              f"restored by sha: {restored} -> {'AS PREDICTED' if verdict else 'OFF'}")
        ok = ok and verdict
    print("ALL AS PREDICTED" if ok else "OFF")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
