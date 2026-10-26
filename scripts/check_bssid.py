"""No committed file carries a BSSID (docs/bssid-guard.md; the reviewer's ruling of 2026-10-23).

Fails on a MAC-shaped value after "bssid" (or the join-diag "bss") in any tracked text file, except EXACTLY the 13 files
below: logs committed before the ruling, which stay as they are (no history rewrite). A named file that no longer
carries one is refused too, so the allowance cannot outlive its reason.

    python3 scripts/check_bssid.py              exit 0 clean, 1 a BSSID or a stale allowance, 2 rig
    python3 scripts/check_bssid.py --selftest   each plant must turn it red naming exactly the planted file
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATTERN = re.compile(r"\b(bssid|bss)[^0-9A-Fa-f]{0,20}?([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}", re.IGNORECASE)

# The 13 logs committed before 2026-10-23 that carry one (each the "[WiFi] fast join" line). By name, never by glob.
ALLOWED = {
    "bench-logs/bindctl-2026-08-07-2023.log",
    "bench-logs/bindfinal-2026-08-07-2024.log",
    "bench-logs/bindprobe-2026-08-07-2020.log",
    "bench-logs/board-01-fixed-2026-08-07-2008.log",
    "bench-logs/com118-handover.log",
    "bench-logs/com118-preflash-banner.log",
    "bench-logs/com119-reset-boot.log",
    "bench-logs/fresh-boot-acceptance-2026-08-23T032015Z.log",
    "bench-logs/fresh-boot-acceptance-2026-08-23T032416Z.log",
    "bench-logs/fresh-boot-acceptance-2026-08-24T163408Z.log",
    "bench-logs/fresh-boot-acceptance-2026-08-24T172307Z.log",
    "bench-logs/jxl-s3-128-soak-shipconfig-2026-07-10.log",
    "bench-logs/soak-s3-128-densesky-cloud-2026-08-07.log",
}
assert len(ALLOWED) == 13


def tracked(root):
    r = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True)
    if r.returncode != 0:
        return None
    return [p for p in r.stdout.decode("utf-8", "replace").split("\0") if p]


def scan(root, files):
    hits = set()
    for rel in files:
        p = os.path.join(root, rel)
        try:
            with open(p, encoding="utf-8", errors="strict") as fh:
                text = fh.read()
        except (UnicodeDecodeError, OSError):
            continue                                   # binaries carry no log lines
        if PATTERN.search(text):
            hits.add(rel.replace(os.sep, "/"))
    return hits


def problems(root, files, allowed=ALLOWED):
    hits = scan(root, files)
    new = sorted(hits - allowed)
    stale = sorted(f for f in allowed if f in set(files) and f not in hits)
    missing = sorted(f for f in allowed if f not in set(files))
    return new, stale, missing


def main(argv):
    if argv[1:2] == ["--selftest"]:
        return selftest()
    files = tracked(ROOT)
    if files is None:
        print("RIG: git ls-files failed"); return 2
    new, stale, missing = problems(ROOT, files)
    for f in new:
        print(f"FAIL {f}: a MAC-shaped value after 'bssid'. Redact it (scripts/redact-line.ps1, redact_line.py)")
    for f in stale + missing:
        print(f"FAIL {f}: allowed by name but carries no BSSID now (or is gone): drop it from ALLOWED")
    ok = not (new or stale or missing)
    print(f"bssid check: {len(files)} tracked files, {len(ALLOWED)} allowed by name; " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


def selftest():
    files = tracked(ROOT)
    if files is None:
        print("RIG: git ls-files failed"); return 2
    if any(problems(ROOT, files)):
        print("RIG: the real tree must pass before the selftest means anything"); return 2
    # The MACs are built here, not written out: a literal one in this file would trip the check on itself.
    m1, m2 = ":".join(["AA", "BB", "CC", "DD", "EE", "F1"]), "-".join(["aa", "bb", "cc", "dd", "ee", "f1"])
    plants = [
        ("a new file, the fast-join form", "bench-logs/plant-a.log", f"x [WiFi] fast join: ch=1 bssid={m1}\n"),
        ("another case and separator", "docs/plant-b.md", f"BSSID: {m2}\n"),
        ("the join-diag form", "bench-logs/plant-c.log", f"[jdiag]   bss {m1} ch=11\n"),
    ]
    fails = 0
    for label, rel, body in plants:
        tmp = tempfile.mkdtemp(prefix="bssid-")
        try:
            os.makedirs(os.path.join(tmp, os.path.dirname(rel)), exist_ok=True)
            open(os.path.join(tmp, rel), "w", encoding="utf-8").write(body)
            new, stale, _ = problems(tmp, [rel], allowed=set())
            ok = new == [rel] and not stale
            fails += not ok
            print(f"  {'ok  ' if ok else 'FAIL'} {label}: red {new}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    # (d) one of the 13 with its MAC removed: the allowance is stale.
    victim = sorted(ALLOWED)[0]
    tmp = tempfile.mkdtemp(prefix="bssid-")
    try:
        os.makedirs(os.path.join(tmp, os.path.dirname(victim)), exist_ok=True)
        text = open(os.path.join(ROOT, victim), encoding="utf-8").read()
        open(os.path.join(tmp, victim), "w", encoding="utf-8").write(PATTERN.sub("bssid=<bssid redacted>", text))
        _, stale, _ = problems(tmp, [victim], allowed={victim})
        ok = stale == [victim]
        fails += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} a named file with its MAC removed: stale {stale}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("bssid check SELFTEST PASSED" if not fails else f"bssid check SELFTEST: {fails} NOT AS PREDICTED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
