"""The OPERA backup fallback's three registered sabotages (docs/wx-opera-backup-fallback.md section 3), replayed.

  cd proxy && python -I -X utf8 scripts/sabotage-wx-opera-fallback.py     exit 0 every set as registered, 1 off, 2 BLIND

THE BASELINE FIRST: the grader (test/wx-opera-fallback.test.ts, through vitest) runs on the CLEAN tree and must read all
14 tests with 0 red, or the driver is BLIND and stops (exit 2) before any plant. Under a plant, a grader that reads fewer
than 14 tests is BLIND too, never OFF: "could not see" must never print as "saw a difference".

Instrument failure #1 (2026-11-14): the first draft gave vitest the 8.3 SHORT path (DANIEL~1) as its cwd, and workerd
could not resolve its own modules ("No such module @fs/..."), so every run read 0 tests and printed OFF. The cwd is now
the long path (Path.resolve(strict=True)), and the baseline would have stopped that draft before its first plant.

Each plant asserts its anchor appears EXACTLY once and that the file changed, then runs the grader, and restores the file
from a byte copy checked by sha256. Per-test sets, frozen 2026-11-14 ~09:04Z (FB7 is two tests; the registered sets
name the row):
  S-MIX      red {FB1, FB2, FB3, FB5, FB7}; FB7 (lagging) green (its check makes no read)
  S-FALLREAD red {FB5}
  S-SRC      red {FB1, FB2, FB3, FB6, FB7, FB7 (lagging)} (the lagging case asserts latest's source too)
"""
import hashlib
import pathlib
import re
import subprocess
import sys

P = pathlib.Path(__file__).resolve(strict=True).parent.parent   # proxy/, as a LONG path
SRC = P / "src" / "wx" / "ingest.ts"
TESTS = 14
READ = 'const b = await buildOperaFrame(p, key, epoch, doFetch, opts, attempts, x, bucket, OPERA_HOST[source ?? "primary"]);'
PLANTS = [
    ("S-MIX", READ, READ.replace('OPERA_HOST[source ?? "primary"]', 'OPERA_HOST[source === "primary" ? "backup" : "primary"]'),
     {"FB1", "FB2", "FB3", "FB5", "FB7"}),
    ("S-FALLREAD", READ,
     READ[:-1] + ".catch(() => buildOperaFrame(p, key, epoch, doFetch, opts, attempts, x, bucket, OPERA_BACKUP_BUCKET));",
     {"FB5"}),
    ("S-SRC", "customMetadata = { coveredShare: String(b.covered), ...(source ? { source } : {}) };",
     "customMetadata = { coveredShare: String(b.covered) };", {"FB1", "FB2", "FB3", "FB6", "FB7", "FB7 (lagging)"}),
]
ROW = re.compile(r"> (FB\d[a-z]?|HZ1|UA\d)\b")


def grade():
    """(ids read, ids red) from one run of the fallback test file."""
    r = subprocess.run(["node", "node_modules/vitest/vitest.mjs", "run", "test/wx-opera-fallback.test.ts", "--reporter=verbose"],
                       cwd=P, capture_output=True, text=True, encoding="utf-8", errors="replace")
    out = re.sub(r"\x1b\[[0-9;]*m", "", r.stdout + r.stderr)
    seen, red = set(), set()
    for line in out.splitlines():
        s = line.strip()
        if not s.startswith(("✓", "×")):
            continue
        m = ROW.search(s)
        if not m:
            continue
        tid = "FB7 (lagging)" if "FB7 (lagging)" in s else m.group(1)
        seen.add(tid)
        if s.startswith("×"):
            red.add(tid)
    return seen, red


seen, red = grade()
if len(seen) != TESTS or red:
    print(f"BASELINE: BLIND -- read {len(seen)} of {TESTS} tests, {len(red)} red. STOPPING: no plant was applied.")
    sys.exit(2)
print(f"BASELINE: {len(seen)} of {TESTS} tests, 0 red (cwd {'long' if '~' not in str(P) else 'SHORT'} path)")

off = 0
for name, a, b, want in PLANTS:
    clean = SRC.read_bytes()
    text = clean.decode("utf-8")
    n = text.count(a)
    if n != 1:
        print(f"{name}: PLANT DID NOT APPLY (anchor found {n} times) -- stopping")
        sys.exit(2)
    SRC.write_bytes(text.replace(a, b).encode("utf-8"))
    changed = hashlib.sha256(SRC.read_bytes()).hexdigest() != hashlib.sha256(clean).hexdigest()
    try:
        if not changed:
            print(f"{name}: THE PLANT DID NOT CHANGE THE FILE -- stopping")
            sys.exit(2)
        seen, red = grade()
    finally:
        SRC.write_bytes(clean)
    if hashlib.sha256(SRC.read_bytes()).hexdigest() != hashlib.sha256(clean).hexdigest():
        print(f"{name}: RESTORE FAILED -- stopping")
        sys.exit(2)
    if len(seen) != TESTS:
        print(f"{name}: BLIND under the plant -- read {len(seen)} of {TESTS} tests. Restored, sha equal. STOPPING")
        sys.exit(2)
    ok = red == want
    off += 0 if ok else 1
    print(f"{name}: {'as registered' if ok else 'OFF'} -- red {sorted(red)} (want {sorted(want)}); restored, sha equal")
print(f"SABOTAGE PASSED ({len(PLANTS)} plants, after a full baseline)" if off == 0 else f"SABOTAGE: {off} off")
sys.exit(1 if off else 0)
