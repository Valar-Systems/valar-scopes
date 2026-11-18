#!/usr/bin/env python3
"""docs/speed-p5p6.md 16e: P5 without 1c, checked on the diff (OLD bench/speed-p5 03e62aad5df5 -> NEW).

    python -I check_drop_1c.py <repo> <OLD> <NEW>              exit 0 every row as registered, 1 a row off, 2 usage
    python -I check_drop_1c.py --selftest <repo> <OLD> <NEW>   P1-P6 each named by its registered row; CLEAN reads clean

Rows (16e):
  D-SCOPE  (1) every changed path is in 16e's table (plus this tool's own file); BandCanvas.h equals the base exactly.
           (2) every REMOVED line blames at OLD to C1C (2feb035c only in its three 1c paths) or is a named OLD line.
               A deleted run that borders an identical line can be aligned more than one way, with the SAME new text;
               (2) holds when SOME alignment that yields that text attributes every removed line to C1C.
           (3) every ADDED line is a restoration (its text among the lines C1C removed from that path, each used once)
               or a named line with exactly its registered text (each used once).
  D-RESIDUE   none of the 23 symbols in any file under include/ src/ test/ scripts/ at NEW.
  D-KINDNUM   every line naming ANCHOR_BG, ANCHOR_8COPY or Kind is printed; none compares a kind with a number.
  D-BENCHDIFF check_bench_diff.check(OLD, NEW) is NOT CLEAN, and its named set equals this tool's own enumeration of
              the changed lines outside a FRAME_BENCH region (tools/, docs/ and FrameBench.h excepted, as it does).
  R-CORE      1a's and 1b's cores and host files are byte-identical at OLD and NEW.
"""
import collections
import importlib.util
import os
import re
import subprocess
import sys
import tempfile

BASE = "4b00c97b3dbd"
C1C_SHORT = ["4f06c3a2", "979d3e15", "73dc246b", "8e73d3d5"]
FEB = "2feb035c"
FEB_PATHS = {"include/FrameBench.h", "src/main.cpp", "tools/speed-p5p6/check_nomark.py"}
SELF = "tools/speed-p5p6/check_drop_1c.py"
DELETED = {"include/DirtyClear.h", "test/host/test_p5_clear.cpp", "test/host/sabotage_p5_clear.py",
           "test/host/check_p5_writes.py", "tools/speed-p5p6/check_nomark.py"}
WHOLE = {"include/BandCanvas.h"}
HUNKS = {"src/main.cpp", "src/AircraftManager.h", "src/AircraftManager.cpp", "include/FrameBench.h", "test/host/run.sh"}
ALLOWED = DELETED | WHOLE | HUNKS | {SELF}
NAMED_OLD = {("include/FrameBench.h", n) for n in (49, 78, 79, 114)}
NAMED_NEW = {"include/FrameBench.h": [
    "                      SWEEP_OLD = 6, SWEEP_LUT = 7, ANCHOR_BG = 8, ANCHOR_8COPY = 9, kKinds };",
    "// 8COPY), then the margin scene last.",
    "#define FRAMEBENCH_A2_SCENES {C1}, {C1}, {ANCHOR_BG}, {ANCHOR_8COPY, kAnchorLevel}, \\",
    "                                        \"sweep-old\", \"sweep-lut\", \"anchor-bg\", \"anchor-8copy\"};",
    "constexpr uint32_t kPushDelayMs[] = {0, 10, 60, 0, 0, 0, 0, 0, 0, 0};"]}
SYMBOLS = ["DirtyClear", "dirtyclear", "SetDirty", "WxDirty", "wxDirty", "wxLayerGen", "MarkSpan", "DirtyAll",
           "BANDCANVAS_DIRTY_MARKS", "CLEAR_OLD", "CLEAR_DIRTY", "CLEAR_CHECK", "CLEAR_NOMARK", "clear-old",
           "clear-dirty", "clear-check", "clear-nomark", "ClearOld", "ClearCheck", "ClearNoMark", "NoteClear",
           "clearFrames", "clearOffPx"]
CORE = ["include/OutlineMask.h", "include/WxSweepLut.h", "test/host/LgfxPort.h", "test/host/p5_outline_port.h",
        "test/host/test_p5_outline.cpp", "test/host/sabotage_p5_outline.py", "test/host/test_p5_blend.cpp",
        "test/host/sabotage_p5_blend.py"]
BEGIN, END = "// THE FRAME BENCH BEGIN", "// THE FRAME BENCH END"
NUMCMP = re.compile(r"\bkind\b\s*(==|!=|<=|>=|<|>)\s*\d|\(\s*Kind\s*\)\s*\d|static_cast<\s*Kind\s*>\s*\(\s*\d", re.I)


def git(repo, *a, ok=(0,)):
    r = subprocess.run(["git", "-C", repo, *a], capture_output=True)
    if r.returncode not in ok:
        raise SystemExit(f"git {' '.join(a)} failed ({r.returncode}): {r.stderr.decode('utf-8', 'replace')[:200]}")
    return r.stdout.decode("utf-8", "replace")


def show(repo, rev, path):
    r = subprocess.run(["git", "-C", repo, "show", f"{rev}:{path}"], capture_output=True)
    return r.stdout.decode("utf-8", "replace") if r.returncode == 0 else None


def lines(text):
    return [] if text is None else text.split("\n")


def hunks(old, new):
    """[(a, b, c, d)] from git diff --no-index -U0: b lines removed at old line a, d added at new line c (1-based)."""
    with tempfile.TemporaryDirectory() as t:
        pa, pb = os.path.join(t, "a"), os.path.join(t, "b")
        open(pa, "w", encoding="utf-8", newline="").write(old or "")
        open(pb, "w", encoding="utf-8", newline="").write(new or "")
        d = subprocess.run(["git", "diff", "--no-index", "-U0", pa, pb], capture_output=True).stdout.decode("utf-8", "replace")
    out = []
    for ln in d.split("\n"):
        m = re.match(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", ln)
        if m:
            out.append((int(m.group(1)), int(m.group(2) or 1), int(m.group(3)), int(m.group(4) or 1)))
    return out


def regions(text):
    inside, on = set(), False
    for n, ln in enumerate(lines(text), 1):
        if ln.strip() == BEGIN:
            on = True
        if on:
            inside.add(n)
        if ln.strip() == END:
            on = False
    return inside


class Ctx:
    def __init__(self, repo, old, new, override=None):
        self.repo, self.old, self.new, self.ov = repo, old, new, override or {}
        self.c1c = {git(repo, "rev-parse", s).strip() for s in C1C_SHORT}
        self.feb = git(repo, "rev-parse", FEB).strip()
        names = [p for p in git(repo, "diff", "--name-only", old, new).split("\n") if p]
        self.paths = sorted(set(names) | set(self.ov))
        self._blame = {}

    def new_text(self, path):
        return self.ov[path] if path in self.ov else show(self.repo, self.new, path)

    def blame(self, path):
        if path not in self._blame:
            m = {}
            for ln in git(self.repo, "blame", "-l", "-s", self.old, "--", path).split("\n"):
                mm = re.match(r"^\^?([0-9a-f]{40})\s+(\d+)\)", ln)
                if mm:
                    m[int(mm.group(2))] = mm.group(1)
            self._blame[path] = m
        return self._blame[path]

    def allowed(self, path, n):
        if (path, n) in NAMED_OLD:
            return True
        sha = self.blame(path).get(n, "")
        return sha in self.c1c or (sha == self.feb and path in FEB_PATHS)

    def pool(self, path):
        """The lines C1C removed from `path`, as a multiset (2feb035c only on its 1c paths)."""
        c = collections.Counter()
        for sha in sorted(self.c1c) + ([self.feb] if path in FEB_PATHS else []):
            for ln in git(self.repo, "show", "--format=", "-U0", sha, "--", path).split("\n"):
                if ln.startswith("-") and not ln.startswith("---"):
                    c[ln[1:]] += 1
        return c


def row(rows, rid, ok, text):
    rows.append((rid, ok))
    print(f"  {'ok  ' if ok else 'OFF '}  {rid:12} {text}")


def check(repo, old, new, override=None, quiet_kind=False):
    cx = Ctx(repo, old, new, override)
    rows = []
    # D-SCOPE (1)
    outside = [p for p in cx.paths if p not in ALLOWED]
    whole_ok = cx.new_text("include/BandCanvas.h") == show(repo, BASE, "include/BandCanvas.h")
    gone = [p for p in DELETED if cx.new_text(p) is not None]
    row(rows, "D-SCOPE-1", not outside and whole_ok and not gone,
        f"paths outside the table {outside}; BandCanvas.h == base {whole_ok}; deleted but present {gone}")
    # D-SCOPE (2) and (3)
    bad_rm, bad_add = [], []
    for p in cx.paths:
        if p == SELF:
            continue
        o, n = show(repo, old, p), cx.new_text(p)
        ol, nl = lines(o), lines(n)
        pool = cx.pool(p)
        named = collections.Counter(NAMED_NEW.get(p, []))
        for a, b, c, d in hunks(o, n):
            if b:
                cands = [0]
                if d == 0:   # a pure deletion may slide while it borders an identical line: same new text either way
                    k = 0
                    while a - 2 - k >= 0 and ol[a - 2 - k] == ol[a + b - 2 - k]:
                        k += 1
                        cands.append(-k)
                    k = 0
                    while a + b - 1 + k < len(ol) and ol[a - 1 + k] == ol[a + b - 1 + k]:
                        k += 1
                        cands.append(k)
                fits = [s for s in cands if all(cx.allowed(p, a + s + i) for i in range(b))]
                if not fits:
                    for i in range(b):
                        if not cx.allowed(p, a + i):
                            bad_rm.append(f"{p}:{a + i}: {ol[a - 1 + i].strip()[:80]}")
            for i in range(d):
                t = nl[c - 1 + i]
                if pool[t] > 0:
                    pool[t] -= 1
                elif named[t] > 0:
                    named[t] -= 1
                else:
                    bad_add.append(f"{p}:{c + i}: {t.strip()[:80]}")
    row(rows, "D-SCOPE-2", not bad_rm, f"removed lines not 1c's: {len(bad_rm)} {bad_rm[:6]}")
    row(rows, "D-SCOPE-3", not bad_add, f"added lines neither restored nor named: {len(bad_add)} {bad_add[:6]}")
    # D-RESIDUE
    files = [p for p in git(repo, "ls-tree", "-r", "--name-only", new, "--", "include", "src", "test", "scripts").split("\n") if p]
    files = sorted(set(files) | {p for p in cx.ov if p.split("/")[0] in ("include", "src", "test", "scripts")})
    hits = []
    for p in files:
        t = cx.new_text(p)
        if t is None:
            continue
        for k, ln in enumerate(lines(t), 1):
            for s in SYMBOLS:
                if s in ln:
                    hits.append(f"{p}:{k}: {s}")
    row(rows, "D-RESIDUE", not hits, f"{len(hits)} hit(s) {hits[:6]}")
    # D-KINDNUM
    uses, cmps = [], []
    for p in files:
        t = cx.new_text(p)
        if t is None or not p.endswith((".h", ".cpp", ".hpp", ".c")):
            continue
        for k, ln in enumerate(lines(t), 1):
            if re.search(r"ANCHOR_BG|ANCHOR_8COPY|\bKind\b", ln):
                uses.append(f"{p}:{k}: {ln.strip()[:110]}")
            if NUMCMP.search(ln):
                cmps.append(f"{p}:{k}: {ln.strip()[:110]}")
    if not quiet_kind:
        for u in uses:
            print(f"        use  {u}")
    row(rows, "D-KINDNUM", not cmps, f"{len(uses)} use(s) printed above; numeric comparisons {cmps}")
    # D-BENCHDIFF
    spec = importlib.util.spec_from_file_location("cbd", os.path.join(repo, "tools", "speed-p5p6", "check_bench_diff.py"))
    cbd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cbd)
    their = {(p, ln, side) for p, ln, side, _ in cbd.check(repo, old, new, override=cx.ov or None)}
    mine = set()
    for p in cx.paths:
        if p.startswith(("tools/", "docs/")) or p == "include/FrameBench.h":
            continue
        o, n = show(repo, old, p), cx.new_text(p)
        ro, rn = regions(o), regions(n)
        for a, b, c, d in hunks(o, n):
            mine |= {(p, a + i, "removed") for i in range(b) if a + i not in ro}
            mine |= {(p, c + i, "added") for i in range(d) if c + i not in rn}
    row(rows, "D-BENCHDIFF", bool(their) and their == mine,
        f"check_bench_diff names {len(their)} (NOT CLEAN: {bool(their)}); this tool {len(mine)}; "
        f"only theirs {sorted(their - mine)[:4]}; only mine {sorted(mine - their)[:4]}")
    # R-CORE
    moved = [p for p in CORE if show(repo, old, p) != cx.new_text(p)]
    row(rows, "R-CORE", not moved, f"1a/1b files that differ {moved}")
    return rows


def run(repo, old, new):
    rows = check(repo, old, new)
    off = [r for r, ok in rows if not ok]
    print("CHECK-DROP-1C " + ("CLEAN: every row as registered" if not off else f"OFF: {off}"))
    return 1 if off else 0


def selftest(repo, old, new):
    def text(p):
        t = show(repo, new, p)
        assert t is not None, p
        return t

    def one(name, ov, want, needle):
        print(f"--- {name}")
        for p, t in ov.items():
            assert t != show(repo, new, p), f"{name}: the plant did not change {p}"
        rows = check(repo, old, new, ov, quiet_kind=True)
        red = {r for r, ok in rows if not ok}
        return want in red, red

    am, mn, fb = text("src/AircraftManager.cpp"), text("src/main.cpp"), text("include/FrameBench.h")
    la = [ln for ln in am.split("\n") if "outlinemask" in ln.lower()]
    assert la, "P1: no 1a line to remove"
    p1 = am.replace(la[0] + "\n", "", 1)
    anchor = "void loop()\n{\n"
    assert mn.count(anchor) == 1
    p2 = mn.replace(anchor, anchor + "    volatile int planted_not_a_restoration = 0;\n", 1)
    named = NAMED_NEW["include/FrameBench.h"][3]
    assert fb.count(named) == 1, "P3: the named kKindName line is not exactly once at NEW"
    p3 = fb.replace(named, named.replace("anchor-8copy", "anchor-8cop"), 1)
    rd = text("README.md")
    p4 = rd + "planted outside the table\n"
    p5 = mn.replace(anchor, anchor + "    canvas.SetDirty(nullptr);\n", 1)
    om = text("include/OutlineMask.h")
    p6 = om.replace("\n", "\n ", 1)
    results = [
        ("P1 a 1a line removed", {"src/AircraftManager.cpp": p1}, "D-SCOPE-2"),
        ("P2 an added line, neither restored nor named", {"src/main.cpp": p2}, "D-SCOPE-3"),
        ("P3 a named line one character off", {"include/FrameBench.h": p3}, "D-SCOPE-3"),
        ("P4 a path outside the table", {"README.md": p4}, "D-SCOPE-1"),
        ("P5 SetDirty left in main.cpp", {"src/main.cpp": p5}, "D-RESIDUE"),
        ("P6 one byte in OutlineMask.h", {"include/OutlineMask.h": p6}, "R-CORE"),
    ]
    ok_all = True
    for name, ov, want in results:
        hit, red = one(name, ov, want, None)
        print(f"  {'ok  ' if hit else 'FAIL'}  {name}: {want} red: {hit} (red rows {sorted(red)})")
        ok_all &= hit
    print("--- CLEAN: the real drop")
    rows = check(repo, old, new, quiet_kind=True)
    clean = all(ok for _, ok in rows)
    print(f"  {'ok  ' if clean else 'FAIL'}  CLEAN: every row ok: {clean}")
    ok_all &= clean
    print("CHECK-DROP-1C SELFTEST " + ("PASSED" if ok_all else "FAILED"))
    return 0 if ok_all else 1


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 4 and a[0] == "--selftest":
        sys.exit(selftest(*a[1:]))
    if len(a) == 3:
        sys.exit(run(*a))
    print(__doc__)
    sys.exit(2)
