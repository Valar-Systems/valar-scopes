#!/usr/bin/env python3
"""The guard that runs (docs/token-file-python.md): no tracked Python file opens the token file directly. Every reader
goes through tools/token_file.py, which applies BLIPSCOPE_TOKEN_FILE and token-file.ts's refusals. A direct open --
open( or expanduser( on the same line as blipscope/tokens -- bypasses all of that, so it is refused here.

  python3 scripts/check_token_file_readers.py [--tree <dir>]   exit 0 none found, 1 found (file:line, never content)
  python3 scripts/check_token_file_readers.py --selftest      G2: a planted direct open is caught; a clean file is not
"""
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


def scan_tree(tree):
    hits = []
    for rel in tracked_py(tree):
        try:
            text = open(os.path.join(tree, rel), encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        hits += [(rel, n) for n in scan_text(text)]
    return hits


def selftest():
    fails = []
    planted = 'tok = open(os.path.expanduser("~/.config/blipscope/tokens"), encoding="utf-8").read()\n'
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
    print("SELFTEST " + ("PASSED" if not fails else f"FAILED: {len(fails)}"))
    return 0 if not fails else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    tree = argv[argv.index("--tree") + 1] if "--tree" in argv else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    hits = scan_tree(tree)
    files = sorted({f for f, _ in hits})
    for f, n in hits:
        print(f"DIRECT OPEN: {f}:{n}")
    print(f"token-file readers: {len(files)} file(s), {len(hits)} direct open(s) -- " +
          ("OK" if not hits else "move them to tools/token_file.py (docs/token-file-python.md)"))
    return 0 if not hits else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
