"""I-2, THE FRAME BENCH's source row (docs/frame-time-bench.md, section 1): the instrument is confined.
  FB1  every src/ and include/ line naming FRAME_BENCH, framebench or FrameBench.h lies inside include/FrameBench.h or
       inside a `// THE FRAME BENCH BEGIN` .. `END` region
  FB2  every region's first code line is `#ifdef FRAME_BENCH` and its last is `#endif`
  FB3  no region, and not FrameBench.h, holds Preferences, .put*(, nvs_, SPIFFS or LittleFS
  FB4  the homes (kHomeLat, kHomeLon) appear only in FrameBench.h's table and its Override() returns: never printed
  FB5  FrameBench.h's code is all under `#ifdef FRAME_BENCH` (only comments and `#pragma once` before it)
    python -I check_frame_bench.py <tree>             the rows; exit 0 all pass, 1 any fails
    python -I check_frame_bench.py --selftest <tree>  each row planted red on a copy in memory, and the clean tree green"""
import os
import re
import sys

BEGIN, END = "// THE FRAME BENCH BEGIN", "// THE FRAME BENCH END"
NAMES = re.compile(r"FRAME_BENCH|framebench|FrameBench\.h")
NVS = re.compile(r"\bPreferences\b|\.put[A-Z][A-Za-z]*\(|\bnvs_|\bSPIFFS\b|\bLittleFS\b")
HEADER = "include/FrameBench.h"


def load(tree):
    files = {}
    for top in ("src", "include"):
        for root, _, names in os.walk(os.path.join(tree, top)):
            for n in names:
                if n.endswith((".cpp", ".h", ".hpp", ".c", ".inc")):
                    p = os.path.join(root, n)
                    rel = os.path.relpath(p, tree).replace(os.sep, "/")
                    files[rel] = open(p, "rb").read().decode("utf-8", errors="replace").replace("\r\n", "\n").split("\n")
    return files


def regions(lines):
    out, start = [], None
    for i, l in enumerate(lines):
        if l.strip() == BEGIN:
            start = i
        elif l.strip() == END and start is not None:
            out.append((start, i))
            start = None
    return out


def rows(files):
    res = {}
    bad1, bad2, bad3, bad4 = [], [], [], []
    nreg = 0
    for rel, lines in files.items():
        if rel == HEADER:
            continue
        regs = regions(lines)
        nreg += len(regs)
        inside = set(i for a, b in regs for i in range(a, b + 1))
        for i, l in enumerate(lines):
            if NAMES.search(l) and i not in inside:
                bad1.append(f"{rel}:{i + 1}")
            if re.search(r"kHomeLat|kHomeLon", l):
                bad4.append(f"{rel}:{i + 1}")
        for a, b in regs:
            code = [lines[i].strip() for i in range(a + 1, b) if lines[i].strip() and not lines[i].strip().startswith("//")]
            if not code or code[0] != "#ifdef FRAME_BENCH" or code[-1] != "#endif":
                bad2.append(f"{rel}:{a + 1}")
            for i in range(a, b + 1):
                if NVS.search(lines[i].split("//")[0]):
                    bad3.append(f"{rel}:{i + 1}")
    hdr = files.get(HEADER, [])
    for i, l in enumerate(hdr):
        if NVS.search(l.split("//")[0]):
            bad3.append(f"{HEADER}:{i + 1}")
        if re.search(r"kHomeLat|kHomeLon", l) and not re.search(r"static const char\* const kHome(Lat|Lon)\[\]|return kHome(Lat|Lon)\[sc\.home\];", l):
            bad4.append(f"{HEADER}:{i + 1}")
    code = [l.strip() for l in hdr if l.strip() and not l.strip().startswith("//")]
    fb5 = len(code) >= 3 and code[0] == "#pragma once" and code[1] == "#ifdef FRAME_BENCH" and code[-1].startswith("#endif")
    res["FB1"] = (not bad1, f"{nreg} region(s); lines naming the bench outside a region: {bad1[:3] or 0}")
    res["FB2"] = (not bad2 and nreg > 0, f"regions not opened by #ifdef FRAME_BENCH and closed by #endif: {bad2[:3] or 0}")
    res["FB3"] = (not bad3, f"NVS or file-system calls in the instrument: {bad3[:3] or 0}")
    res["FB4"] = (not bad4, f"the homes used outside the table and Override(): {bad4[:3] or 0}")
    res["FB5"] = (fb5, "FrameBench.h is all under #ifdef FRAME_BENCH" if fb5 else "FrameBench.h has code outside #ifdef FRAME_BENCH")
    return res


def report(res):
    for k, (ok, d) in res.items():
        print(f"  {'ok  ' if ok else 'FAIL'} {k}: {d}")
    bad = sum(not ok for ok, _ in res.values())
    print(f"{len(res)} rows, {bad} failed")
    return 1 if bad else 0


def selftest(tree):
    base = load(tree)
    rc = report(rows(base))
    if rc:
        print("selftest FAILED: the clean tree is not green")
        return 1
    some = next(r for r in base if r != HEADER and regions(base[r]))

    def plant(name, fn, row):
        files = {k: list(v) for k, v in base.items()}
        fn(files)
        res = rows(files)
        red = {k for k, (ok, _) in res.items() if not ok}
        ok = red == {row}
        print(f"selftest {'ok' if ok else 'FAIL'}: {name} reads exactly {{{row}}} red (got {sorted(red)})")
        return 0 if ok else 1

    def outside(f):
        f[some].insert(0, "#ifdef FRAME_BENCH")

    def no_ifdef(f):
        a, b = regions(f[some])[0]
        f[some] = [l for i, l in enumerate(f[some]) if not (a < i < b and l.strip() == "#ifdef FRAME_BENCH")]

    def nvs(f):
        a, b = regions(f[some])[0]
        f[some].insert(b - 1, "    Preferences p;")

    def home(f):
        a, b = regions(f[some])[0]
        f[some].insert(b - 1, '    Serial.printf("%s", framebench::kHomeLat[0]);')

    def hdr(f):
        f[HEADER].append("int leaked = 1;")

    rc |= plant("FB1, a bench line outside a region", outside, "FB1")
    rc |= plant("FB2, a region without its #ifdef", no_ifdef, "FB2")
    rc |= plant("FB3, Preferences inside a region", nvs, "FB3")
    rc |= plant("FB4, a home printed", home, "FB4")
    rc |= plant("FB5, header code outside #ifdef", hdr, "FB5")
    print("selftest " + ("PASSED" if rc == 0 else "FAILED"))
    return rc


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "--selftest":
        sys.exit(selftest(a[1]))
    if len(a) == 1:
        sys.exit(report(rows(load(a[0]))))
    print(__doc__)
    sys.exit(2)
