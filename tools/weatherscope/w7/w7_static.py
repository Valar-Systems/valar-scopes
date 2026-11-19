#!/usr/bin/env python3
"""W7's static rows (docs/weatherscope-stage4.md S4.33, S4.37). Each reads the code it guards, never a copy of it.

  W7-NOSHOW         the reviewer's row (S4.33): W7's live feed uses WxPolicy for FETCHING only, so Weatherscope's screens
                    keep the owner's Q15/Q18 display. No file under src/weatherscope/ names `WhatToShow`. Its control,
                    NOSHOW-CTL: the scanner must find the name in include/WxPolicy.h, where it is defined.
  W7-DEBOUNCE       the refusal latch's thresholds (src/weatherscope/WxSetup.h kLatchStreak, kLatchMs) equal the radar's
                    (src/AircraftManager.cpp REVERIFY_MIN_STREAK, REVERIFY_MIN_MS), READ FROM THE RADAR'S SOURCE, so a
                    change on either side fails here instead of drifting (5c's answer: the radar's debounce).
  W7-ENROLLED-SIDE  S4.36 ruling (a)'s named pasted-key row has two sides. The device's side is test_wx_answer.cpp's
                    KEY-PASTED-ROW; this is the page's: in src/ConfigurationWebServer.cpp, the `enrolled` line under the
                    comment citing S4.36 reads "cloud-key-fac" alone. If it ever read the pasted key too, the named
                    difference would be gone and the row must be revisited, so it fails.

    python -I w7_static.py <repo>              exit 0 clean; 1 a FAIL (each named); 2 BLIND (a row cannot see its input)
    python -I w7_static.py --selftest <repo>   the real tree reads clean; each plant reads 1, naming its row
"""
import contextlib
import io
import os
import re
import sys

TOKEN = re.compile(r"\bWhatToShow\b")
EXTS = (".h", ".hpp", ".c", ".cpp")
WXSETUP = "src/weatherscope/WxSetup.h"
RADAR = "src/AircraftManager.cpp"
PAGE = "src/ConfigurationWebServer.cpp"
RADAR_STREAK = re.compile(r"constexpr\s+uint8_t\s+REVERIFY_MIN_STREAK\s*=\s*([0-9UL\s*]+);")
RADAR_MS = re.compile(r"constexpr\s+unsigned\s+long\s+REVERIFY_MIN_MS\s*=\s*([0-9UL\s*]+);")
WX_STREAK = re.compile(r"constexpr\s+uint8_t\s+kLatchStreak\s*=\s*([0-9UL\s*]+);")
WX_MS = re.compile(r"constexpr\s+uint32_t\s+kLatchMs\s*=\s*([0-9UL\s*]+);")
ENROLLED = re.compile(r"\bbool\s+enrolled\s*=")


def read(repo, rel, override=None):
    if override and rel in override:
        return override[rel]
    with open(os.path.join(repo, *rel.split("/")), encoding="utf-8", errors="replace") as f:
        return f.read()


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
        for n, ln in enumerate(read(repo, rel, override).split("\n"), 1):
            if TOKEN.search(ln):
                hits.append((rel, n, ln.strip()[:90]))
    return hits


def product(expr):
    """A constant written as integers joined by '*' (each may carry U/L suffixes), or None for anything else."""
    parts = [p.strip().rstrip("UuLl") for p in expr.split("*")]
    if not parts or not all(p.isdigit() for p in parts):
        return None
    v = 1
    for p in parts:
        v *= int(p)
    return v


def one(rx, text):
    m = rx.findall(text)
    return product(m[0]) if len(m) == 1 else None


def row_noshow(repo, override):
    c = len(TOKEN.findall(read(repo, "include/WxPolicy.h", override)))
    print(f"  {'ok  ' if c else 'BLIND'}  NOSHOW-CTL  the scanner sees WhatToShow in include/WxPolicy.h: {c} occurrence(s)")
    if not c:
        return 2
    hits = scan(repo, override)
    for path, n, text in hits:
        print(f"        WhatToShow at {path}:{n}: {text}")
    print(f"  {'ok  ' if not hits else 'FAIL'}  W7-NOSHOW   no file under src/weatherscope/ names WhatToShow: "
          f"{len(hits)} hit(s) in {len(files(repo))} files")
    return 0 if not hits else 1


def row_debounce(repo, override):
    rt = read(repo, RADAR, override)
    wt = read(repo, WXSETUP, override)
    rs, rm, ws, wm = one(RADAR_STREAK, rt), one(RADAR_MS, rt), one(WX_STREAK, wt), one(WX_MS, wt)
    if rs is None or rm is None:
        print(f"  BLIND  W7-DEBOUNCE the radar's REVERIFY_MIN_STREAK / REVERIFY_MIN_MS are not each defined once in {RADAR}")
        return 2
    ok = ws == rs and wm == rm
    print(f"  {'ok  ' if ok else 'FAIL'}  W7-DEBOUNCE the refusal latch's thresholds equal the radar's: "
          f"radar {rs} / {rm} ms, Weatherscope {ws} / {wm} ms")
    return 0 if ok else 1


def row_enrolled(repo, override):
    lines = read(repo, PAGE, override).split("\n")
    cited = [i for i, ln in enumerate(lines) if "S4.36" in ln]
    if len(cited) != 1:
        print(f"  BLIND  W7-ENROLLED-SIDE {PAGE} cites S4.36 on {len(cited)} lines, not 1")
        return 2
    at = next((j for j in range(cited[0], min(cited[0] + 12, len(lines))) if ENROLLED.search(lines[j])), None)
    if at is None:
        print(f"  BLIND  W7-ENROLLED-SIDE no `bool enrolled =` within 12 lines of the S4.36 citation ({PAGE}:{cited[0] + 1})")
        return 2
    ln = lines[at]
    ok = '"cloud-key-fac"' in ln and '"cloud-key"' not in ln
    print(f"  {'ok  ' if ok else 'FAIL'}  W7-ENROLLED-SIDE the page's ENROLLED under the S4.36 citation "
          f"({PAGE}:{at + 1}) reads cloud-key-fac alone: {'yes' if ok else 'no'}")
    return 0 if ok else 1


def run(repo, override=None):
    rs = [row_noshow(repo, override), row_debounce(repo, override), row_enrolled(repo, override)]
    return 2 if 2 in rs else (1 if 1 in rs else 0)


def selftest(repo):
    print("the real tree:")
    r0 = run(repo)
    ok = r0 == 0
    mgr = "src/weatherscope/WeatherscopeManager.cpp"
    real_mgr = read(repo, mgr)
    real_wx = read(repo, WXSETUP)
    real_page = read(repo, PAGE)
    lines = real_page.split("\n")
    cite = next((i for i, ln in enumerate(lines) if "S4.36" in ln), None)
    at = None if cite is None else next((j for j in range(cite, min(cite + 12, len(lines)))
                                         if ENROLLED.search(lines[j])), None)
    if at is None:
        print("W7-STATIC SELFTEST FAILED: the page's cited ENROLLED line was not found, so it cannot be planted")
        return 1
    lines[at] = lines[at].replace('"cloud-key-fac"', '"cloud-key"')
    plants = [
        ("W7-NOSHOW", f"a WhatToShow call appended to {mgr}",
         {mgr: real_mgr + "\n// SABOTAGE (w7_static selftest)\nstatic bool PlantedShow(const wxpolicy::State& s) "
                           "{ return wxpolicy::WhatToShow(s, true, true, 0) == wxpolicy::Show::Crop; }\n"}),
        ("W7-DEBOUNCE", "kLatchStreak 6 in WxSetup.h",
         {WXSETUP: real_wx.replace("constexpr uint8_t kLatchStreak = 5;", "constexpr uint8_t kLatchStreak = 6;")}),
        ("W7-ENROLLED-SIDE", "the page's ENROLLED reading the pasted cloud-key",
         {PAGE: "\n".join(lines)}),
    ]
    for row, what, override in plants:
        for rel, text in override.items():
            assert text != read(repo, rel), f"{row}: the plant did not apply"
        print(f"the plant for {row} ({what}):")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            r = run(repo, override)
        out = buf.getvalue()
        print(out, end="")
        named = any(l.strip().startswith("FAIL") and row in l for l in out.splitlines())
        others = [l for l in out.splitlines() if l.strip().startswith(("FAIL", "BLIND")) and row not in l]
        good = r == 1 and named and not others
        print(f"  -> {'red on ' + row + ' alone' if good else 'NOT AS PLANTED'}")
        ok &= good
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
