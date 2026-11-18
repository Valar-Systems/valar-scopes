#!/usr/bin/env python3
"""docs/weatherscope-stage4.md S4.33, the reviewer's row: W7's live feed uses WxPolicy for FETCHING only and never calls
WhatToShow, so Weatherscope's screens keep the owner's Q15/Q18 display (shown to 30 min, orange age past 10 min, RADAR
STALE past 30). Enforced, not remembered: no file under src/weatherscope/ names `WhatToShow`.

    python -I w7_static.py <repo>              exit 0 clean; 1 a hit (each named); 2 BLIND (the control cannot see it)
    python -I w7_static.py --selftest <repo>   the real tree reads clean; a planted call reads 1, naming its file

The control: the scanner must find the token in include/WxPolicy.h, where WhatToShow is defined; a scanner that cannot
see it there proves nothing about its absence elsewhere.
"""
import os
import re
import sys

TOKEN = re.compile(r"\bWhatToShow\b")
EXTS = (".h", ".hpp", ".c", ".cpp")


def files(repo):
    base = os.path.join(repo, "src", "weatherscope")
    out = []
    for dp, _, fn in os.walk(base):
        for f in fn:
            if f.endswith(EXTS):
                out.append(os.path.relpath(os.path.join(dp, f), repo).replace(os.sep, "/"))
    return sorted(out)


def scan(repo, override=None):
    """[(path, line, text)] of every WhatToShow under src/weatherscope/. override: {path: text} replaces a file's text."""
    hits = []
    for rel in files(repo):
        if override and rel in override:
            text = override[rel]
        else:
            with open(os.path.join(repo, *rel.split("/")), encoding="utf-8", errors="replace") as f:
                text = f.read()
        for n, ln in enumerate(text.split("\n"), 1):
            if TOKEN.search(ln):
                hits.append((rel, n, ln.strip()[:90]))
    return hits


def control(repo):
    with open(os.path.join(repo, "include", "WxPolicy.h"), encoding="utf-8", errors="replace") as f:
        return len(TOKEN.findall(f.read()))


def run(repo, override=None):
    c = control(repo)
    print(f"  {'ok  ' if c else 'BLIND'}  NOSHOW-CTL  the scanner sees WhatToShow in include/WxPolicy.h: {c} occurrence(s)")
    if not c:
        return 2
    hits = scan(repo, override)
    for path, n, text in hits:
        print(f"        WhatToShow at {path}:{n}: {text}")
    print(f"  {'ok  ' if not hits else 'FAIL'}  W7-NOSHOW   no file under src/weatherscope/ names WhatToShow: "
          f"{len(hits)} hit(s) in {len(files(repo))} files")
    return 0 if not hits else 1


def selftest(repo):
    print("the real tree:")
    r1 = run(repo)
    rel = "src/weatherscope/WeatherscopeManager.cpp"
    with open(os.path.join(repo, *rel.split("/")), encoding="utf-8") as f:
        real = f.read()
    planted = real + "\n// SABOTAGE (w7_static selftest)\nstatic bool PlantedShow(const wxpolicy::State& s) " \
                     "{ return wxpolicy::WhatToShow(s, true, true, 0) == wxpolicy::Show::Crop; }\n"
    assert planted != real
    print(f"the plant (a WhatToShow call appended to {rel}):")
    r2 = run(repo, {rel: planted})
    named = any(p == rel for p, _, _ in scan(repo, {rel: planted}))
    ok = r1 == 0 and r2 == 1 and named
    print("W7-STATIC SELFTEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "--selftest":
        sys.exit(selftest(a[1]))
    if len(a) == 1:
        sys.exit(run(a[0]))
    print(__doc__)
    sys.exit(2)
