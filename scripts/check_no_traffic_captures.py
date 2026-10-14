#!/usr/bin/env python3
"""Refuse recorded aircraft traffic anywhere in the tracked tree.

WHY. The bench traffic capture (docs/v18-label-declutter.md, docs/v18-next-overhead.md)
records aircraft positions around a home, so a capture locates that home. Captures are
WORKSTATION-ONLY (review decision, 2026-10-08): never committed, never attached to a PR.
.gitignore keeps the expected names out; this is the check that RUNS, because a capture
saved under any other name, or pasted into a fixture, is the case .gitignore cannot see.

TWO TESTS, because they fail differently:
  1. NAME: tracked files matching the capture patterns below.
  2. CONTENT: a line in the capture's own format -- `[traffic] t=<epoch> hex=<6 hex>
     lat=<deg> lon=<deg> ...`. The pattern needs real numbers, so prose that MENTIONS
     "[traffic]" (the specs do) never matches.

Usage: check_no_traffic_captures.py            # scan `git ls-files`
       check_no_traffic_captures.py --selftest # a planted capture must be caught
"""
import fnmatch
import re
import subprocess
import sys
import tempfile
from pathlib import Path

NAME_PATTERNS = ["*.traffic.jsonl", "*.traffic.log", "*.traffic.csv", "traffic-captures/*"]
LINE = re.compile(r"\[traffic\] t=\d{9,} hex=[0-9a-fA-F]{6} lat=-?\d{1,2}\.\d+ lon=-?\d{1,3}\.\d+")
BINARY_EXT = {".bin", ".png", ".jpg", ".jpeg", ".gif", ".ico", ".pdf", ".3mf", ".step", ".stp", ".woff", ".woff2", ".ttf", ".elf", ".zip"}


def scan(paths, root):
    hits = []
    for rel in paths:
        if any(fnmatch.fnmatch(rel, p) for p in NAME_PATTERNS):
            hits.append(f"{rel}: file name matches a traffic-capture pattern")
            continue
        p = Path(root) / rel
        if p.suffix.lower() in BINARY_EXT or not p.is_file():
            continue
        try:
            for n, line in enumerate(p.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
                if LINE.search(line):
                    hits.append(f"{rel}:{n}: a recorded [traffic] line (aircraft position near a home)")
                    break
        except OSError:
            continue
    return hits


def tracked(root):
    out = subprocess.run(["git", "-C", root, "ls-files"], capture_output=True, text=True, check=True).stdout
    return [l for l in out.splitlines() if l]


def selftest():
    ok = True
    with tempfile.TemporaryDirectory() as d:
        # EXAMPLE -- null island, not anyone's home. ASSEMBLED AT RUNTIME, never written out in
        # this file: the first run of this check in the tree refused its own selftest line, and
        # excluding this file by name would leave a capture pasted into it unseen.
        planted = " ".join(["12:00:00.000", "[traffic]", "t=" + "1791457200", "hex=" + "abc123",
                            "lat=" + "0.0000", "lon=" + "0.0000", "alt=1000 gs=100 trk=90"])
        Path(d, "fixture.log").write_text(planted + "\n")
        Path(d, "notes.md").write_text("The bench writes `[traffic]` lines; see the spec.\n")
        Path(d, "x.traffic.jsonl").write_text("{}\n")
        hits = scan(["fixture.log", "notes.md", "x.traffic.jsonl"], d)
        got = sorted(h.split(":")[0] for h in hits)
        for want, why in (("fixture.log", "a planted capture line is caught"), ("x.traffic.jsonl", "a capture file name is caught")):
            res = want in got
            ok &= res
            print(f"selftest {'ok' if res else 'FAIL'}: {why}")
        res = "notes.md" not in got
        ok &= res
        print(f"selftest {'ok' if res else 'FAIL'}: CONTROL -- prose mentioning [traffic] is not a capture")
    return ok


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(0 if selftest() else 1)
    root = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True).stdout.strip()
    files = tracked(root)
    hits = scan(files, root)
    if hits:
        print("REFUSED: recorded aircraft traffic is workstation-only (it locates a home):")
        for h in hits:
            print("  " + h)
        sys.exit(1)
    print(f"ok: no traffic capture in {len(files)} tracked files")
