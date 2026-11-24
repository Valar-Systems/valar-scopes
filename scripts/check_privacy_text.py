#!/usr/bin/env python3
"""A retired privacy claim stays retired, in both places it was published.

README.md's "Privacy & telemetry" section and the support page (proxy/pages/support.html) both said that a failed
request is logged with the Cloudflare data centre that served it "and nothing that identifies your device". That was
false, and was replaced on 2026-11-17 by a statement of what our request logs may hold and for how long. This check
fails if the retired sentence comes back in either file, in any letter case or line wrapping.

A check that finds nothing proves nothing on its own, so each file also has two controls:
  - the privacy section's own heading is present, so the check is reading the right text;
  - the retention statement is present ("for up to seven days"), so the replacement has not quietly gone too.
If a later edit rewords the retention statement on purpose, update REQUIRED below in the same change.

  python3 scripts/check_privacy_text.py              check the tree
  python3 scripts/check_privacy_text.py --selftest   plant each failure into an in-memory copy; each must turn
                                                     exactly its own row red

Exit 0 pass, 1 a row is red, 2 a file cannot be read, 3 the selftest is off its prediction.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# path -> the text that proves the check is reading that file's privacy section
FILES = {
    "README.md": "## privacy & telemetry",
    "proxy/pages/support.html": 'id="privacy"',
}
RETIRED = "nothing that identifies your device"
REQUIRED = ("cloudflare keeps these logs for up to seven days", "ever holds the device's key")


def norm(text):
    """Lower case, every run of whitespace one space, so a re-wrap or a capital cannot hide a match."""
    return re.sub(r"\s+", " ", text).lower()


def rows(path):
    return [f"P1 {path}: the retired sentence is absent",
            f"C1 {path}: the privacy section's heading is present",
            f"C2 {path}: the retention statement is present"]


def grade(texts):
    """texts: path -> raw text. Returns the set of red row labels."""
    red = set()
    for path, anchor in FILES.items():
        t = norm(texts[path])
        p1, c1, c2 = rows(path)
        if norm(RETIRED) in t:
            red.add(p1)
        if anchor not in t:
            red.add(c1)
        if not all(r in t for r in REQUIRED):
            red.add(c2)
    return red


def read_tree():
    texts = {}
    for path in FILES:
        full = os.path.join(ROOT, path)
        try:
            with open(full, encoding="utf-8", newline="") as f:
                texts[path] = f.read()
        except OSError as e:
            print(f"CANNOT READ {path}: {e.__class__.__name__}")
            sys.exit(2)
    return texts


def report(texts):
    red = grade(texts)
    for path in FILES:
        for row in rows(path):
            print(("FAIL " if row in red else "PASS ") + row)
    total = sum(len(rows(p)) for p in FILES)
    print(f"{total - len(red)} of {total} rows pass")
    return red


def selftest():
    base = read_tree()
    readme, support = "README.md", "proxy/pages/support.html"
    # The retired sentence planted with a capital and a line break inside it: norm() must still find it.
    planted = "When a request fails, it is logged with nothing that\n   Identifies your device."

    def with_(path, new):
        t = dict(base)
        if new == t[path]:
            raise SystemExit(f"selftest BLIND: a plant into {path} did not change the text")
        t[path] = new
        return t

    def drop(path, needle):
        n = norm(base[path])
        if needle not in n:
            raise SystemExit(f"selftest BLIND: {needle!r} is not in {path} to remove")
        return with_(path, n.replace(needle, "x"))

    cases = [
        ("clean tree", dict(base), set()),
        ("retired sentence in README", with_(readme, base[readme] + "\n" + planted + "\n"), {rows(readme)[0]}),
        ("retired sentence in support", with_(support, base[support] + "\n" + planted + "\n"), {rows(support)[0]}),
        ("README heading gone", drop(readme, FILES[readme]), {rows(readme)[1]}),
        ("support anchor gone", drop(support, FILES[support]), {rows(support)[1]}),
        ("README retention gone", drop(readme, REQUIRED[0]), {rows(readme)[2]}),
        ("support key sentence gone", drop(support, REQUIRED[1]), {rows(support)[2]}),
    ]
    off = 0
    for name, texts, want in cases:
        got = grade(texts)
        ok = got == want
        off += not ok
        print(f"{'as predicted' if ok else 'OFF'}: {name}: red {len(got)}, predicted {len(want)}")
        if not ok:
            for row in sorted(got ^ want):
                print(f"    {'unexpected' if row in got else 'missing'}: {row}")
    print(f"selftest: {len(cases) - off} of {len(cases)} cases as predicted")
    return 3 if off else 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    if sys.argv[1:]:
        print("usage: check_privacy_text.py [--selftest]")
        sys.exit(2)
    sys.exit(1 if report(read_tree()) else 0)


if __name__ == "__main__":
    main()
