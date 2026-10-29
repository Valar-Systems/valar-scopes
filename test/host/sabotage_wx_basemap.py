"""Runs test_wx_basemap.cpp against SABOTAGED copies of 3.5b's three headers (include/BasemapPackage.h,
src/weatherscope/WxBasemap.h, src/weatherscope/WxScreens.h). Each must fail EXACTLY the set of named cases frozen in
docs/weatherscope-stage3.md A10.11 (as A12.39 restates it), and nothing else. Then validate.py's own two (A12.15).

    python -I test/host/sabotage_wx_basemap.py

Method, fixed before any run (A10.11, A12.39):
  - The real files are never edited. All three headers are copied into a temporary directory searched first, and the
    plant goes into exactly one copy. (GCC resolves a quoted include from the including file's own directory first, so
    a lone shadowed header would be included twice under two paths.) Each plant asserts its anchor occurs exactly once
    and that the copy differs; the real files' hashes are compared before and after.
  - The unmodified headers run first, as the control: 84 cases, 0 failures.
  - Each sabotage's failing lines are printed, so what a failing case saw can be read off the run.
"""
import hashlib, importlib.util, os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FILES = {
    "BasemapPackage.h": os.path.join(ROOT, "include", "BasemapPackage.h"),
    "WxBasemap.h": os.path.join(ROOT, "src", "weatherscope", "WxBasemap.h"),
    "WxScreens.h": os.path.join(ROOT, "src", "weatherscope", "WxScreens.h"),
}
TEST = os.path.join(ROOT, "test", "host", "test_wx_basemap.cpp")
WX = os.path.join(ROOT, "test", "fixtures", "wx")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
CASES = 84


def block(src, start, end):
    """The text from `start` (inclusive) to `end` (exclusive), each found exactly once."""
    assert src.count(start) == 1 and src.count(end) == 1, (start[:40], end[:40])
    i = src.index(start)
    j = src.index(end, i)
    return src[i:j]


def s_order(src):
    """Roads first, then water, then rivers (A10.11's S-ORDER)."""
    water = block(src, "    if (mask & kMaskWater) {", "    IndexSink sink(img);")
    sink = "    IndexSink sink(img);\n"
    rivers = block(src, "    if (mask & kMaskRivers) {", "    if (mask & kMaskRoads) {")
    roads = block(src, "    if (mask & kMaskRoads) {", "    if (info) *info = bi;")
    old = water + sink + rivers + roads
    assert src.count(old) == 1
    return [(old, sink + "    // SABOTAGE: roads, then water, then rivers\n" + roads + water + rivers)]


def s_peakfirst(src):
    """Peaks placed after the towns (A10.11's S-PEAKFIRST)."""
    peaks = block(src, "    for (int k = 0; peaks && k < peaks->Count() && np < kMaxPeaks; ++k) {",
                  "    for (int k = 0; k < n && np + nt < cap && nt < kMaxLabels; ++k) {")
    towns = block(src, "    for (int k = 0; k < n && np + nt < cap && nt < kMaxLabels; ++k) {", "    *nPeaks = np;")
    return [(peaks + towns, "    // SABOTAGE: towns first\n" + towns + peaks)]


# name: (file, edits or a function of the source, the frozen set)
SABOTAGES = {
    "S-PKCRC": ("BasemapPackage.h",
                [("    if (Crc32ZeroingField(p, total) != Le32(p + 28)) return Status::kCrc;\n",
                  "    // SABOTAGE: the CRC check skipped\n")], {"PK-crc"}),
    "S-FILL": ("WxBasemap.h",
               [("        const double py = y + 0.5;\n", "        const double py = y;  // SABOTAGE: the corner\n"),
                ("            int x0 = (int)ceil(cross[a] - 0.5), x1 = (int)ceil(cross[a + 1] - 0.5);\n",
                 "            int x0 = (int)ceil(cross[a]), x1 = (int)ceil(cross[a + 1]);  // SABOTAGE\n")],
               {"FL1", "FL2", "FL3", "FL5b", "FL6"}),
    "S-UNION": ("WxBasemap.h",
                [("                img[y * kSize + x] = value;\n",
                  "                img[y * kSize + x] = img[y * kSize + x] == value ? 0 : value;  // SABOTAGE: even-odd\n")],
                {"FL6"}),
    "S-SHIFT": ("WxBasemap.h",
                [("        *x = wxraster::kC + (lon - v_.homeLon) / v_.rLon * wxraster::kC;\n"
                  "        *y = wxraster::kC - (lat - v_.homeLat) / v_.rLat * wxraster::kC;\n",
                  "        *x = wxraster::kC + (lon - lon0_) / v_.rLon * wxraster::kC;  // SABOTAGE: home = origin\n"
                  "        *y = wxraster::kC - (lat - lat0_) / v_.rLat * wxraster::kC;\n"),
                 ("        const double latP = v_.homeLat + (c - (y + 0.5)) / c * v_.rLat;\n"
                  "        const double lonP = v_.homeLon + ((x + 0.5) - c) / c * v_.rLon;\n",
                  "        const double latP = lat0_ + (c - (y + 0.5)) / c * v_.rLat;  // SABOTAGE: home = origin\n"
                  "        const double lonP = lon0_ + ((x + 0.5) - c) / c * v_.rLon;\n")],
                {"TX5", "TR2", "TR3c"}),
    "S-ORDER": ("WxBasemap.h", s_order, {"LN1", "LN8"}),
    "S-MASK": ("WxBasemap.h", [("    if (mask & kMaskRivers) {\n", "    if (true) {  // SABOTAGE: the rivers bit ignored\n")],
               {"LN9"}),
    "S-FOLD": ("WxScreens.h",
               [("    if (c == 0 && base) return wxbase::PaletteRgb(base[y * wxraster::kSize + x]);\n",
                 "    if (c <= 13 && base) return wxbase::PaletteRgb(base[y * wxraster::kSize + x]);  // SABOTAGE\n")],
               {"BF3"}),
    "S-HATCH": ("WxScreens.h",
                [("(c == 15 || (c == 14 && terrain)) ?", "((void)terrain, c == 15) ? /* SABOTAGE: 14 solid */")],
                {"BF5"}),
    "S-BKEY": ("WxScreens.h", [("    k.radiusMi = radiusMi;\n    k.latNwMicro = h.latNwMicro;\n",
                                "    (void)radiusMi;  // SABOTAGE: the radius ignored\n    k.latNwMicro = h.latNwMicro;\n")],
               {"BK2"}),
    "S-PEAKFIRST": ("WxScreens.h", s_peakfirst, {"PE5"}),
    "S-PEAKCAP": ("WxScreens.h",
                  [("    for (int k = 0; peaks && k < peaks->Count() && np < kMaxPeaks; ++k) {\n",
                    "    for (int k = 0; peaks && k < peaks->Count() && k < kMaxPeaks; ++k) {  // SABOTAGE: attempts\n")],
                  {"PE3"}),
    "S-MT": ("WxScreens.h", [('        if (strncmp(name + i, "Mount ", 6) == 0 && o + 3 < cap) {\n',
                              '        if (false && strncmp(name + i, "Mount ", 6) == 0 && o + 3 < cap) {  // SABOTAGE\n')],
             {"PE6a", "PE6d"}),
    "S-TCOUNT": ("BasemapPackage.h", [("        if (count != 1) return Status::kTerrainCount;\n",
                                       "        // SABOTAGE: TERRAIN's count unchecked\n")], {"PK-terraincount"}),
    "S-PCOUNT": ("BasemapPackage.h",
                 [("        return count == lines ? Status::kOk : Status::kProvenanceCount;\n",
                   "        (void)lines;\n        return Status::kOk;  // SABOTAGE: PROVENANCE's count unchecked\n")],
                 {"PK-provcount"}),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def read(p):
    return open(p, encoding="utf-8").read().replace("\r\n", "\n")


def plant(name, edits, src):
    out = src
    for anchor, repl in (edits(src) if callable(edits) else edits):
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    return out


def run(td, name, texts):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    for fn, text in texts.items():
        open(os.path.join(inc, fn), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "include"),
                        "-I" + os.path.join(ROOT, "src", "weatherscope"), TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        sys.exit(2)
    r = subprocess.run([exe, WX], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        print(f"{name}: the suite did not run (exit {r.returncode}): the RIG\n{r.stdout}{r.stderr}")
        sys.exit(2)
    m = re.search(r"^(\d+) checks, (\d+) failures$", r.stdout, re.M)
    if not m or int(m.group(1)) != CASES:
        print(f"{name}: {m.group(1) if m else '?'} cases ran, not {CASES}: the RIG")
        sys.exit(2)
    lines = [x.group(0).strip() for x in re.finditer(r"^\s*FAIL \S+.*$", r.stdout, re.M)]
    return r.returncode, {x.split()[1] for x in lines}, lines


def validator_sabotages():
    """A12.15: validate.py with each new check removed changes exactly its own fixture's verdict, to kOk."""
    vp = os.path.join(ROOT, "tools", "weatherscope", "basemap", "validate.py")
    src = read(vp)
    plants = {
        "S-TCOUNT (validate.py)": ('        if count != 1:\n            raise Bad("kTerrainCount")', "neg-terrain-count"),
        "S-PCOUNT (validate.py)": ('        if count != lines:\n            raise Bad("kProvenanceCount")', "neg-prov-count"),
    }
    fixdir = os.path.join(WX, "basemap")
    files = sorted(f for f in os.listdir(fixdir) if f.endswith(".bmpk"))

    def verdicts(text, td, tag):
        p = os.path.join(td, f"validate_{tag}.py")
        open(p, "w", encoding="utf-8", newline="\n").write(text)
        spec = importlib.util.spec_from_file_location(f"validate_{tag}", p)
        m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(m)
        return {f: m.validate(open(os.path.join(fixdir, f), "rb").read())[0] for f in files}

    ok = True
    with tempfile.TemporaryDirectory() as td:
        base = verdicts(src, td, "real")
        for k, (needle, fixture) in plants.items():
            assert src.count(needle) == 1, k
            text = src.replace(needle, "        pass  # SABOTAGE")
            assert text != src
            got = verdicts(text, td, re.sub(r"\W", "", k))
            changed = {f for f in files if got[f] != base[f]}
            good = changed == {fixture + ".bmpk"} and got[fixture + ".bmpk"] == "kOk"
            ok &= good
            print(f"{k}: changed {sorted(changed)} -> {[got[f] for f in sorted(changed)]} "
                  f"(predicted only {fixture}.bmpk, to kOk) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
    return ok


def main():
    before = {k: sha(p) for k, p in FILES.items()}
    real = {k: read(p) for k, p in FILES.items()}
    ok, off = True, []
    with tempfile.TemporaryDirectory() as td:
        rc, fails, _ = run(td, "control", real)
        good = rc == 0 and not fails
        print(f"control (the real headers): exit {rc}, failing {sorted(fails)} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        for name, (fn, edits, frozen) in SABOTAGES.items():
            texts = dict(real)
            texts[fn] = plant(name, edits, real[fn])
            rc, fails, lines = run(td, name, texts)
            good = rc == 1 and fails == frozen
            if not good:
                off.append(name)
            print(f"{name} ({fn}): failing {sorted(fails)}, predicted {sorted(frozen)} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            for line in lines:
                print(f"    {line}")
            ok &= good
    vok = validator_sabotages()
    after = {k: sha(p) for k, p in FILES.items()}
    same = before == after
    print(f"the real headers unchanged: {same} ({', '.join(k + ' ' + v[:12] for k, v in after.items())})")
    print(f"{len(SABOTAGES)} device sabotages and 2 validator sabotages: " +
          ("ALL AS PREDICTED" if ok and vok and same else f"OFF-PREDICTION ({', '.join(off) or 'see above'}): STOP"))
    return 0 if ok and vok and same else 1


if __name__ == "__main__":
    sys.exit(main())
