#!/usr/bin/env python3
"""The guard that runs (docs/token-file-python.md): no tracked Python file opens the token file directly. Every reader
goes through tools/token_file.py, which applies BLIPSCOPE_TOKEN_FILE and token-file.ts's refusals. A direct open (a
call to open or expanduser whose line also names the token file's path) bypasses all of that, so it is refused here.
This file is scanned like every other: its own planted example is built in two pieces so that no line of it matches.

  python3 scripts/check_token_file_readers.py [--tree <dir>]   exit 0 none found, 1 found (file:line, never content), 2 BLIND (0 tracked .py scanned)
  python3 scripts/check_token_file_readers.py --selftest      G2: a planted direct open is caught; a clean file is not; G2 scanned/blind: the scanned count
"""
import contextlib
import io
import os
import re
import subprocess
import sys
import tempfile

DIRECT = re.compile(r"(\bopen|\bexpanduser)\s*\([^\n]*blipscope[/\\]+tokens")


def scan_text(text):
    """Line numbers (1-based) of direct opens."""
    return [i + 1 for i, line in enumerate(text.splitlines()) if DIRECT.search(line)]


def tracked_py(tree):
    out = subprocess.run(["git", "-C", tree, "ls-files", "-z", "--", "*.py"], capture_output=True, check=True).stdout
    return [p for p in out.decode().split("\0") if p]


def scan_tree(tree, counts=None):
    # counts, when given, gets "scanned": the tracked .py files actually READ. The hits alone cannot tell a clean tree
    # from a scan that saw nothing (a git ls-files that returned no files reads exactly like a tree with no direct open).
    hits = []
    scanned = 0
    for rel in tracked_py(tree):
        try:
            text = open(os.path.join(tree, rel), encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        scanned += 1
        hits += [(rel, n) for n in scan_text(text)]
    if counts is not None:
        counts["scanned"] = scanned
    return hits


def selftest():
    fails = []
    # Built in two pieces: at runtime it is one matching line (a real plant), while no source line here matches.
    planted = 'tok = open(os.path.expanduser("~/.config/blipscope' + '/tokens"), encoding="utf-8").read()\n'
    clean = 'import importlib.util as _ilu\ntok = TOKEN_FILE.token("analytics-read")   # see ~/.config/blipscope/tokens in the doc\n'
    a = scan_text(planted)
    b = scan_text(clean)
    for ok, what in ((a == [1], f"G2: a planted direct open is caught (lines {a})"),
                     (b == [], f"G2 control: a reader through the helper, mentioning the path in a comment, is not (lines {b})")):
        print(("PASS " if ok else "FAIL ") + what)
        if not ok:
            fails.append(what)
    with tempfile.TemporaryDirectory() as d:
        subprocess.run(["git", "init", "-q", d], check=True)
        with open(os.path.join(d, "plant.py"), "w", encoding="utf-8") as fh:
            fh.write(planted)
        subprocess.run(["git", "-C", d, "add", "plant.py"], check=True)
        hits = scan_tree(d)
    ok = hits == [("plant.py", 1)]
    print(("PASS " if ok else "FAIL ") + f"G2 tree: a tracked planted file is found by the tree scan ({hits})")
    if not ok:
        fails.append("G2 tree")
    # G2 scanned: the scanned count. One tracked clean .py at the root, one tracked .py two folders down holding the plant, and
    # an UNTRACKED .py holding it too: the scan must read exactly the 2 tracked files and find only the subfolder plant.
    # It fails when the count is 0, which is the case the hits alone cannot show.
    with tempfile.TemporaryDirectory() as d:
        subprocess.run(["git", "init", "-q", d], check=True)
        os.makedirs(os.path.join(d, "sub", "deep"))
        for rel, body in (("root.py", "x = 1\n"), ("sub/deep/inner.py", planted), ("loose.py", planted)):
            with open(os.path.join(d, *rel.split("/")), "w", encoding="utf-8") as fh:
                fh.write(body)
        subprocess.run(["git", "-C", d, "add", "root.py", "sub/deep/inner.py"], check=True)
        counts = {}
        hits = scan_tree(d, counts)
    ok = counts.get("scanned") == 2 and hits == [("sub/deep/inner.py", 1)]
    print(("PASS " if ok else "FAIL ") + f"G2 scanned: the 2 tracked .py are read, one two folders down, and the untracked one is not "
          f"(scanned {counts.get('scanned')}, hits {hits})")
    if not ok:
        fails.append("G2 scanned")
    # G2 blind: a tree with no tracked .py is BLIND, not a pass: main() exits 2 and says it scanned 0.
    with tempfile.TemporaryDirectory() as d:
        subprocess.run(["git", "init", "-q", d], check=True)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = main(["--tree", d])
    said = out.getvalue()
    ok = rc == 2 and "0 tracked .py scanned" in said and "-- OK" not in said
    print(("PASS " if ok else "FAIL ") + f"G2 blind: a tree with no tracked .py exits 2, not 0 (exit {rc})")
    if not ok:
        fails.append("G2 blind")
    print("SELFTEST " + ("PASSED" if not fails else f"FAILED: {len(fails)}"))
    return 0 if not fails else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    tree = argv[argv.index("--tree") + 1] if "--tree" in argv else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    counts = {}
    hits = scan_tree(tree, counts)
    files = sorted({f for f, _ in hits})
    for f, n in hits:
        print(f"DIRECT OPEN: {f}:{n}")
    print(f"{counts['scanned']} tracked .py scanned")
    if counts["scanned"] == 0:
        print("token-file readers: BLIND -- 0 tracked .py scanned, so a pass would say nothing (is this a git tree?)")
        return 2
    print(f"token-file readers: {len(files)} file(s), {len(hits)} direct open(s) -- " +
          ("OK" if not hits else "move them to tools/token_file.py (docs/token-file-python.md)"))
    return 0 if not hits else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
