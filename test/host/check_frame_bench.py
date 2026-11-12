"""I-2, THE FRAME BENCH's source row (docs/frame-time-bench.md, section 1): the instrument is confined.
  FB1  every src/ and include/ line naming FRAME_BENCH, framebench or FrameBench.h lies inside include/FrameBench.h or
       inside a `// THE FRAME BENCH BEGIN` .. `END` region
  FB2  every region's first code line is `#ifdef FRAME_BENCH` and its last is `#endif`
  FB3  no region, and not FrameBench.h, holds Preferences, .put*(, nvs_, SPIFFS or LittleFS
  FB4  the homes (kHomeLat, kHomeLon) appear only in FrameBench.h's table and its Override() returns: never printed
  FB5  FrameBench.h's code is all under `#ifdef FRAME_BENCH` (only comments and `#pragma once` before it)
  The recording plan's rows (docs/frame-time-bench.md, "The recorder, survey and replay code"):
  FB-R1  the cloud-poll region (framebench::OnCloudPoll) exists exactly once, inside a region, inside the cloud branch
         after CloudFeed::ParseBlips and before the local branch
  FB-R2  in FrameBench.h, the recorder prints a body only after the `if (!homeOk)` branch has returned
  FB-R3  git tracks no recording and no generated recording header (names holding framebench_rec, *.fbrec, recordings/)
  FB-R4  no print in a region or in FrameBench.h carries a latitude or longitude value
  FB-R5  FrameBench.h's airport strings equal include/Airports.h's values at 2 dp, code by code
  FB-R6  the rung set is the single kRungs[] constant: one definition, and no other {25, 50, 100}-style list
    python -I check_frame_bench.py <tree>             the rows; exit 0 all pass, 1 any fails
    python -I check_frame_bench.py --selftest <tree>  each row planted red on a copy in memory, and the clean tree green"""
import os
import re
import subprocess
import sys

BEGIN, END = "// THE FRAME BENCH BEGIN", "// THE FRAME BENCH END"
NAMES = re.compile(r"FRAME_BENCH|framebench|FrameBench\.h")
NVS = re.compile(r"\bPreferences\b|\.put[A-Z][A-Za-z]*\(|\bnvs_|\bSPIFFS\b|\bLittleFS\b")
HEADER = "include/FrameBench.h"
COORD = re.compile(r"kAptLat|kAptLon|kHomeLat|kHomeLon|latSent|lonSent|req->lat|req->lon|\blat0\b|\blon0\b|\.latitude|\.longitude")
PRINT = re.compile(r"Serial\.(print|printf|println)\s*\(")
TRACKED_REC = re.compile(r"framebench_rec|\.fbrec$|(^|/)recordings/")


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


def tracked(tree):
    try:
        out = subprocess.run(["git", "-C", tree, "ls-files"], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return out.splitlines()


def rows(files, listing=None):
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
    # FB-R1: the cloud-poll region, once, in the cloud branch after ParseBlips and before the local branch
    hits = []
    for rel, lines in files.items():
        if rel == HEADER:
            continue
        regs = regions(lines)
        for i, l in enumerate(lines):
            if "framebench::OnCloudPoll(" in l:
                inside = any(a < i < b for a, b in regs)
                pb = max((j for j in range(i) if "CloudFeed::ParseBlips(" in lines[j]), default=-1)
                lb = next((j for j in range(i, len(lines)) if "else if (req->local)" in lines[j]), None)
                cb = max((j for j in range(i) if "else if (req->cloud)" in lines[j]), default=-1)
                hits.append((rel, i, inside and pb > cb >= 0 and lb is not None))
    r1 = len(hits) == 1 and hits[0][2]
    res["FB-R1"] = (r1, f"cloud-poll call sites: {len(hits)}; in place: {[h[2] for h in hits]}")
    # FB-R2: the body is printed only after the not-home branch returned
    text = "\n".join(hdr)
    gate = text.find("if (!homeOk) {")
    body = text.find('"[frame-bench] rec A%u %d %lu %ld %d/%d %s')
    skip_ret = gate >= 0 and "return;" in text[gate:text.find("}", gate) + 1]
    r2 = gate >= 0 and body > gate and skip_ret
    res["FB-R2"] = (r2, "the body print follows the home-match gate's return" if r2 else
                    f"the body print is not behind the home-match gate (gate {gate}, print {body})")
    # FB-R3: nothing recorded or generated is tracked
    lst = listing if listing is not None else []
    bad_t = [f for f in lst if TRACKED_REC.search(f)]
    res["FB-R3"] = (listing is not None and not bad_t,
                    "no tracked file listing to read" if listing is None else f"tracked recording files: {bad_t[:3] or 0}")
    # FB-R4: no print carries a coordinate
    bad_p = []
    for rel, lines in files.items():
        spans = [(0, len(lines) - 1)] if rel == HEADER else regions(lines)
        for a, b in spans:
            for i in range(a, b + 1):
                l = lines[i].split("//")[0]
                if PRINT.search(l):
                    stmt, k = l, i
                    while ";" not in stmt and k + 1 < len(lines):
                        k += 1
                        stmt += lines[k].split("//")[0]
                    if COORD.search(stmt):
                        bad_p.append(f"{rel}:{i + 1}")
    res["FB-R4"] = (not bad_p, f"prints carrying a coordinate: {bad_p[:3] or 0}")
    # FB-R5: the airport strings equal Airports.h at 2 dp
    apts = {}
    for l in files.get("include/Airports.h", []):
        for m in re.finditer(r'\{\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*"([A-Z]{3})"\s*\}', l):
            apts.setdefault(m.group(3), (f"{float(m.group(1)):.2f}", f"{float(m.group(2)):.2f}"))
    arr = {}
    for name in ("kAptCode", "kAptLat", "kAptLon"):
        m = re.search(name + r"\[\]\s*=\s*\{([^}]*)\}", text)
        arr[name] = re.findall(r'"([^"]*)"', m.group(1)) if m else []
    mism = [c for c, la, lo in zip(arr["kAptCode"], arr["kAptLat"], arr["kAptLon"]) if apts.get(c) != (la, lo)]
    r5 = len(arr["kAptCode"]) == 9 == len(arr["kAptLat"]) == len(arr["kAptLon"]) and not mism
    res["FB-R5"] = (r5, "9 airports, each equal to Airports.h at 2 dp" if r5 else
                    f"airports {len(arr['kAptCode'])}, mismatched {mism[:3]}")
    # FB-R6: one kRungs[] and no other rung list
    defs = len(re.findall(r"static const uint8_t kRungs\[\]\s*=\s*\{[^}]*\}", text))
    others = []
    for rel, lines in files.items():
        for i, l in enumerate(lines):
            code = l.split("//")[0]
            if re.search(r"\{\s*25\s*,\s*50\s*,\s*100\s*\}|\{\s*10\s*,\s*25\s*,\s*50\s*\}", code) and "kRungs[]" not in code:
                others.append(f"{rel}:{i + 1}")
    res["FB-R6"] = (defs == 1 and not others, f"kRungs[] definitions {defs}; other rung lists {others[:3] or 0}")
    return res


def report(res):
    for k, (ok, d) in res.items():
        print(f"  {'ok  ' if ok else 'FAIL'} {k}: {d}")
    bad = sum(not ok for ok, _ in res.values())
    print(f"{len(res)} rows, {bad} failed")
    return 1 if bad else 0


def selftest(tree):
    base = load(tree)
    listing = tracked(tree)
    rc = report(rows(base, listing))
    if rc:
        print("selftest FAILED: the clean tree is not green")
        return 1
    some = next(r for r in base if r != HEADER and regions(base[r]))

    def plant(name, fn, row, lst=None):
        files = {k: list(v) for k, v in base.items()}
        fn(files)
        res = rows(files, listing if lst is None else lst)
        red = {k for k, (ok, _) in res.items() if not ok}
        want = set(row) if isinstance(row, (set, frozenset)) else {row}
        ok = red == want
        print(f"selftest {'ok' if ok else 'FAIL'}: {name} reads exactly {sorted(want)} red (got {sorted(red)})")
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
    rc |= plant("FB4, a home printed (also a coordinate printed: FB-R4)", home, {"FB4", "FB-R4"})
    rc |= plant("FB5, header code outside #ifdef", hdr, "FB5")
    am = "src/AircraftManager.cpp"

    def r1(f):
        f[am] = [l.replace("framebench::OnCloudPoll(", "framebench::NoSuchPoll(") for l in f[am]]

    def r2(f):
        f[HEADER] = [l.replace("if (!homeOk) {", "if (false) {") for l in f[HEADER]]

    def r4(f):
        i = next(j for j, l in enumerate(f[HEADER]) if "skip home-ok 0" in l)
        f[HEADER].insert(i, '        Serial.printf("%s", latSent);')

    def r5(f):
        f[HEADER] = [l.replace('"33.64"', '"33.65"') for l in f[HEADER]]

    def r6(f):
        a, b = regions(f[am])[0]
        f[am].insert(b - 1, "    static const uint8_t r[] = {25, 50, 100};")

    rc |= plant("FB-R1, the cloud-poll call removed", r1, "FB-R1")
    rc |= plant("FB-R2, the home-match gate disabled", r2, "FB-R2")
    rc |= plant("FB-R3, a recording tracked", lambda f: None, "FB-R3",
                lst=(listing or []) + ["bench/framebench_rec_SFO.fbrec"])
    rc |= plant("FB-R4, a coordinate printed", r4, "FB-R4")
    rc |= plant("FB-R5, one airport digit changed", r5, "FB-R5")
    rc |= plant("FB-R6, a second rung list", r6, "FB-R6")
    print("selftest " + ("PASSED" if rc == 0 else "FAILED"))
    return rc


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "--selftest":
        sys.exit(selftest(a[1]))
    if len(a) == 1:
        sys.exit(report(rows(load(a[0]), tracked(a[0]))))
    print(__doc__)
    sys.exit(2)
