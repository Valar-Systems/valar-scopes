#!/usr/bin/env python3
"""docs/speed-p5p6.md 15c, the reviewer's X addition: a branch's change touches NO line outside a FRAME_BENCH region or a
tools file -- checked on the DIFF, not stated.

A changed path passes when it is under tools/ or docs/; or it is include/FrameBench.h (wholly under #ifdef FRAME_BENCH:
check_frame_bench.py's FB5 row); or every ADDED line lies inside a `// THE FRAME BENCH BEGIN`..`END` region of the NEW
file and every REMOVED line inside one of the OLD file. Anything else is a shipping line, named with its file and line.

    python -I check_bench_diff.py <repo> <base> [<head>]    head defaults to the working tree; exit 0 clean, 1 a
                                                           shipping line, 2 usage / git error
    python -I check_bench_diff.py --selftest <repo> <base>  the real change reads clean; a planted copy of src/main.cpp
                                                           with one line added OUTSIDE a region reads 1, naming it; and
                                                           include/Layout.h deleted reads 1, naming exactly its lines
                                                           as REMOVED (16f, ruled (a))
"""
import os
import re
import subprocess
import sys
import tempfile

BEGIN, END = "// THE FRAME BENCH BEGIN", "// THE FRAME BENCH END"
FREE = ("tools/", "docs/")
WHOLE = ("include/FrameBench.h",)


def git(repo, *a):
    r = subprocess.run(["git", "-C", repo, *a], capture_output=True)
    if r.returncode != 0:
        raise SystemExit(f"git {' '.join(a)} failed: {r.stderr.decode('utf-8', 'replace')[:200]}")
    return r.stdout.decode("utf-8", "replace")


def regions(text):
    """The set of 1-based line numbers inside a BEGIN..END region (markers included)."""
    inside, on = set(), False
    for n, ln in enumerate(text.split("\n"), 1):
        if ln.strip() == BEGIN:
            on = True
        if on:
            inside.add(n)
        if ln.strip() == END:
            on = False
    return inside


def check(repo, base, head=None, override=None):
    """[(path, line, side, text)] of shipping lines. override: {path: new text} replaces the working tree's file; a
    None text means the file is deleted. A path absent at the head (or from the working tree) reads as "", exactly as
    a path absent at the base does (docs/speed-p5p6.md 16f, ruled (a)): a deleted file's lines are REMOVED lines."""
    args = ["diff", "-U0", base] + ([head] if head else [])
    names = [p for p in git(repo, *(["diff", "--name-only", base] + ([head] if head else []))).split("\n") if p]
    if override:
        names = sorted(set(names) | set(override))
    bad = []
    for path in names:
        if path.startswith(FREE) or path in WHOLE:
            continue
        old = git(repo, "show", f"{base}:{path}") if path in git(repo, "ls-tree", "-r", "--name-only", base, "--", path) else ""
        if override and path in override:
            new = override[path] if override[path] is not None else ""
        elif head:
            new = git(repo, "show", f"{head}:{path}") if path in git(repo, "ls-tree", "-r", "--name-only", head, "--", path) else ""
        else:
            fp = os.path.join(repo, *path.split("/"))
            if os.path.exists(fp):
                with open(fp, encoding="utf-8", newline="") as f:
                    new = f.read()
            else:
                new = ""
        ro, rn = regions(old), regions(new)
        with tempfile.TemporaryDirectory() as t:
            a, b = os.path.join(t, "a"), os.path.join(t, "b")
            open(a, "w", encoding="utf-8", newline="").write(old)
            open(b, "w", encoding="utf-8", newline="").write(new)
            d = subprocess.run(["git", "diff", "--no-index", "-U0", a, b], capture_output=True).stdout.decode("utf-8", "replace")
        o = n = 0
        for ln in d.split("\n"):
            h = re.match(r"^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@", ln)
            if h:
                o, n = int(h.group(1)), int(h.group(2))
                continue
            if ln.startswith(("---", "+++", "diff ", "index ")):
                continue
            if ln.startswith("-"):
                if o not in ro:
                    bad.append((path, o, "removed", ln[1:].strip()[:90]))
                o += 1
            elif ln.startswith("+"):
                if n not in rn:
                    bad.append((path, n, "added", ln[1:].strip()[:90]))
                n += 1
    return bad


def report(bad):
    for path, line, side, text in bad:
        print(f"  SHIPPING LINE {side}: {path}:{line}: {text}")
    print("BENCH-DIFF " + ("CLEAN: every changed line is in a FRAME_BENCH region, FrameBench.h or tools/docs" if not bad
                           else f"NOT CLEAN: {len(bad)} line(s) outside"))
    return 1 if bad else 0


def selftest(repo, base):
    print("the real change:")
    r1 = report(check(repo, base))
    with open(os.path.join(repo, "src", "main.cpp"), encoding="utf-8", newline="") as f:
        m = f.read()
    anchor = "void loop()\n{\n"
    assert m.count(anchor) == 1, "the plant's anchor is not exactly once"
    planted = m.replace(anchor, anchor + "  volatile int planted_shipping_line = 0;\n", 1)
    assert planted != m
    print("the plant (one line added outside any region in src/main.cpp):")
    bad = check(repo, base, override={"src/main.cpp": planted})
    r2 = report(bad)
    named = any("planted_shipping_line" in t for _, _, _, t in bad)
    # 16f, ruled (a): a DELETED shipping file must be named, every line of it outside a region, as REMOVED lines.
    victim = "include/Layout.h"
    with open(os.path.join(repo, *victim.split("/")), encoding="utf-8", newline="") as f:
        vt = f.read()
    want = sum(1 for n in range(1, len(vt.splitlines()) + 1) if n not in regions(vt))
    print(f"the plant ({victim} deleted; {want} line(s) outside any region):")
    badd = check(repo, base, override={victim: None})
    r3 = report(badd)
    deleted_ok = (r3 == 1 and want > 0 and len(badd) == want
                  and all(p == victim and side == "removed" for p, _, side, _ in badd))
    print(f"the deleted file: {len(badd)} line(s) named, all '{victim}' and removed: {deleted_ok}")
    ok = r1 == 0 and r2 == 1 and named and deleted_ok
    print("BENCH-DIFF SELFTEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == "--selftest":
        sys.exit(selftest(a[1], a[2]))
    if len(a) in (2, 3):
        sys.exit(report(check(*a)))
    print(__doc__)
    sys.exit(2)
