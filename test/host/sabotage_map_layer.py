"""Step M's sabotages (docs/blipscope-map-client-steps.md), each planted in a COPY of include/MapLayer.h. Each must turn
red exactly its registered set across test_map_layer.cpp and check_map_layer.py (CM9), no fewer and no more:
  S-CMCRC   the gate also accepts kCrc                          -> {CM1-neg-crc}
  S-CMROT   the vertex path's rotation sign flipped             -> CM2's 45 and CM3's 27 rotated cases
  S-CMCEN   the vertex path centred on the package's origin     -> CM2's 40 and CM3's 16 offset cases
  S-CMS1    "in view" becomes "in the package" for primaries    -> {CM4-5mi, CM4-10mi}
  S-CMGAIN  S2's gain x 10                                      -> {CM5}
  S-CMORD   roads drawn before rivers                           -> {CM6}
  S-CMR     the requested radius, not the header's              -> {CM7}
  S-CMDISC  the colouring pass ignores the round screen         -> {CM8}
  S-CMLIT   a colour literal outside the theme table            -> {CM9}
  S-CMPLAN  the Dark row's place colour moved                   -> {CM9}
  S-CMLVL   always the largest terrain level                    -> {CM10-5mi, CM10-10mi, CM10-25mi}
  S-CMSAMP  terrain sampled at the pixel's corner               -> CM10's 6
  S-CMLINE  the line rounds down instead of to nearest          -> CM11's 20
Method: include/, src/weatherscope/ and the glcd font are copied into a temporary tree with the repo's own layout, so
every relative include resolves inside the copy and no header is reached by two paths; the plant goes into the copy's
MapLayer.h only, after asserting its anchor occurs exactly once and that the copy changed. The real header's sha256 is
compared before and after. The unplanted copy runs first, as the control.
    python -I test/host/sabotage_map_layer.py [--anchors | --control]   exit 0 = as predicted, 1 = not, 2 = the rig"""
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes (docs/weatherscope-stage3.md A12.50, A12.61)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEST = os.path.join(ROOT, "test", "host", "test_map_layer.cpp")
CHECK = os.path.join(ROOT, "test", "host", "check_map_layer.py")
PLAN = os.path.join(ROOT, "docs", "blipscope-basemap-client.md")
WX = os.path.join(ROOT, "test", "fixtures", "wx")
HEADER = os.path.join("include", "MapLayer.h")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
PY = sys.executable

# The case names, built from the cases test_map_layer.cpp registers (CM2's and CM3's lists, CM10's views, CM11's).
VIEWS, ROTS = (5, 10, 25, 50, 100), (0, 30, 90, 211)
CM2 = [(mi, r, o) for mi in VIEWS for r in ROTS for o in (0, 1, 2)]
CM3 = [(mi, r, 0) for mi in VIEWS for r in ROTS] + [(mi, r, o) for mi in (5, 10) for r in ROTS for o in (1, 2)]
CM10 = [f"CM10-{mi}mi" for mi in (5, 10, 25, 50, 60, 100)]
CM11 = [f"CM11-{mi}mi-r{r}" for mi in VIEWS for r in ROTS]
ROTATED = {f"CM2-{mi}mi-r{r}-o{o}" for mi, r, o in CM2 if r} | {f"CM3-{mi}mi-r{r}-o{o}" for mi, r, o in CM3 if r}
OFFSET = {f"CM2-{mi}mi-r{r}-o{o}" for mi, r, o in CM2 if o} | {f"CM3-{mi}mi-r{r}-o{o}" for mi, r, o in CM3 if o}

ROT_LINE = ("    // radarproj::Project's rotation: x = C + u cos + w sin, y = C - u sin + w cos.\n"
            "    const double c = v.rotCos, s = v.rotSin;\n")
CEN_LINES = ("    const double u0 = (lon0 - v.homeLon) / v.rLon * C;\n"
             "    const double w0 = (v.homeLat - lat0) / v.rLat * C;\n")
DISC = "        if (wxlayer::OffDisc(x, y, kSize)) {\n            out[p] = 0;\n            return;\n        }\n"
LINE = ("        const int x = (int)floor(x0 + dx * k / n + 0.5), y = (int)floor(y0 + dy * k / n + 0.5);\n")
DARK = "    {{6, 8, 10}, 0.075, {18, 34, 56}, {30, 56, 104}, {72, 72, 72}, {86, 86, 86}},\n"
RIVERS = ("    // THE RIVERS PASS\n", "    // THE RIVERS PASS END\n")
ROADS = ("    // THE ROADS PASS: primary always; secondary only when no primary is in view (S1).\n",
         "    // THE ROADS PASS END\n")


def block(src, start, end):
    """From `start` to the end of `end`, each found exactly once, `end` after `start`."""
    assert src.count(start) == 1 and src.count(end) == 1, (start.strip()[:40], end.strip()[:40])
    i = src.index(start)
    j = src.index(end, i) + len(end)
    return src[i:j]


def s_order(src):
    rivers, roads = block(src, *RIVERS), block(src, *ROADS)
    old = rivers + "\n" + roads
    assert src.count(old) == 1, "the two passes are not adjacent"
    return [(old, "    // SABOTAGE S-CMORD: roads before rivers\n" + roads + "\n" + rivers)]


# name: (the edits, or a function of the source returning them; the registered set)
SABOTAGES = {
    "S-CMCRC": ([("    return Verdict{s == bmpk::Status::kOk, s};\n",
                  "    return Verdict{s == bmpk::Status::kOk || s == bmpk::Status::kCrc, s};  // SABOTAGE S-CMCRC\n")],
                {"CM1-neg-crc"}),
    "S-CMROT": ([(ROT_LINE, ROT_LINE.replace("s = v.rotSin;", "s = -v.rotSin;  // SABOTAGE S-CMROT"))], ROTATED),
    "S-CMCEN": ([(CEN_LINES, "    const double u0 = 0 * lon0, w0 = 0 * lat0;  // SABOTAGE S-CMCEN\n")], OFFSET),
    "S-CMS1": ([("            if (near) ++bi.primaryInView;\n", "            ++bi.primaryInView;  // SABOTAGE S-CMS1\n")],
               {"CM4-5mi", "CM4-10mi"}),
    "S-CMGAIN": ([("    const int c = (int)floor(land + (v - 128) * gain + 0.5);\n",
                   "    const int c = (int)floor(land + (v - 128) * gain * 10 + 0.5);  // SABOTAGE S-CMGAIN\n")], {"CM5"}),
    "S-CMORD": (s_order, {"CM6"}),
    "S-CMR": ([("    const uint32_t rMm = pk.radiusMm;   // F4: the disc the header names\n",
                "    const uint32_t rMm = askedMm;   // SABOTAGE S-CMR\n")], {"CM7"}),
    "S-CMDISC": ([(DISC, DISC.replace("if (wxlayer::OffDisc(x, y, kSize)) {", "if (false) {  // SABOTAGE S-CMDISC"))],
                 {"CM8"}),
    "S-CMLIT": ([("// THE THEME TABLE END\n",
                  "// THE THEME TABLE END\nconstexpr Rgb8 kStray = {40, 40, 40};  // SABOTAGE S-CMLIT\n")], {"CM9"}),
    "S-CMPLAN": ([(DARK, DARK.replace("{86, 86, 86}}", "{86, 86, 94}}"))], {"CM9"}),
    "S-CMLVL": ([("    const int pick = best >= 0 ? best : largest;\n",
                  "    const int pick = largest;  // SABOTAGE S-CMLVL\n")], set(CM10[:3])),
    "S-CMSAMP": ([("        const double Y = y + 0.5 - C, X0 = 0.5 - C;\n",
                   "        const double Y = y - C, X0 = -C;  // SABOTAGE S-CMSAMP\n")], set(CM10)),
    "S-CMLINE": ([(LINE, LINE.replace(" + 0.5)", ")") + "        // SABOTAGE S-CMLINE\n")], set(CM11)),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def edits_for(spec, src):
    return spec(src) if callable(spec) else spec


def make_tree(td, tag, spec=None):
    """A copy of the headers this test reaches, in the repo's own layout; `spec` planted into its MapLayer.h."""
    tree = os.path.join(td, tag)
    for sub, pat in (("include", "*.h"), (os.path.join("src", "weatherscope"), "*.h")):
        os.makedirs(os.path.join(tree, sub), exist_ok=True)
        for f in glob.glob(os.path.join(ROOT, sub, pat)):
            shutil.copyfile(f, os.path.join(tree, sub, os.path.basename(f)))
    font = os.path.join("test", "fixtures", "wx", "glcdfont.c")
    os.makedirs(os.path.dirname(os.path.join(tree, font)), exist_ok=True)
    shutil.copyfile(os.path.join(ROOT, font), os.path.join(tree, font))
    if spec is not None:
        p = os.path.join(tree, HEADER)
        src = open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")
        out = src
        for old, new in edits_for(spec, src):
            assert out.count(old) == 1, f"{tag}: anchor found {out.count(old)} times: {old.strip()[:60]!r}"
            out = out.replace(old, new)
        assert out != src, f"{tag}: the sabotage did not apply"
        open(p, "w", encoding="utf-8", newline="\n").write(out)
    return tree


def run_suites(tree, td, tag):
    """The C++ suite and CM9 against the tree. (failing case names, the C++ case count, None) or (None, None, why)."""
    exe = os.path.join(td, f"t-{tag}.exe")
    b = subprocess.run([CXX] + FLAGS + ["-I", os.path.join(tree, "include"), "-I",
                        os.path.join(tree, "src", "weatherscope"), TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        return None, None, "did not compile:\n" + b.stderr[-2000:]
    r = subprocess.run([exe, WX], capture_output=True, text=True)
    m = sc.COUNT.search(r.stdout)
    if r.returncode not in (0, 1) or not m:
        return None, None, f"the C++ suite did not run (exit {r.returncode})\n" + r.stdout[-1500:]
    red = {mm.group(1) for mm in re.finditer(r"^\s*FAIL\s+(CM[\w-]+)", r.stdout, re.M)}
    c = subprocess.run([PY, "-I", CHECK, "--header", os.path.join(tree, HEADER), "--plan", PLAN],
                       capture_output=True, text=True)
    if c.returncode not in (0, 1):
        return None, None, f"CM9's check did not run (exit {c.returncode})\n" + c.stdout + c.stderr
    if c.returncode == 1:
        red.add("CM9")
    return red, int(m.group(1)), None


def main():
    before = sha(os.path.join(ROOT, HEADER))
    rigs = sc.Rigs()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        red, n, why = run_suites(make_tree(td, "control"), td, "control")
        if why or red or n != CASES:
            print(f"CONTROL (the real header): not clean -- the rig. {why or ''} red {sorted(red or [])}, {n} cases")
            return 2
        print(f"control (the real header): {n} cases and CM9, all pass")
        for i, (name, (spec, want)) in enumerate(SABOTAGES.items()):
            def one():
                red, n, why = run_suites(make_tree(td, f"s{i}", spec), td, f"s{i}")
                if why:
                    print(f"{name}: {why}")
                    raise sc.Rig()
                return red, n
            got = rigs.run(name, one)
            if got is None:
                ok = False
                continue
            red, n = got
            good = red == want and n == CASES
            extra, missing = sorted(red - want), sorted(want - red)
            print(f"{name}: {len(red)} red (want {len(want)}); extra {extra or 'none'}; missing {missing or 'none'}"
                  f" -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    same = before == sha(os.path.join(ROOT, HEADER))
    print(f"real header unchanged: {same}")
    print("MAP LAYER SABOTAGES AS PREDICTED" if ok and same else "MAP LAYER SABOTAGES: NOT AS PREDICTED")
    return rigs.close(0 if ok and same else 1)


# --control: the unplanted run alone, each suite against this table. CASES is the one place the count lives.
CASES = 160
CONTROL_TABLE = {"test_map_layer": ("checks", CASES), "check_map_layer.py": ("line", "  ok   CM9")}


def control():
    with tempfile.TemporaryDirectory() as td:
        run_suites(make_tree(td, "control"), td, "control")


def anchor_entries():
    """Every plant's anchor in the real header, for run.sh's --anchors pass (no plant runs)."""
    text = sc.read(os.path.join(ROOT, HEADER))
    out = []
    for name, (spec, _) in SABOTAGES.items():
        if callable(spec):
            out.append((name, HEADER, text, spec))
        else:
            for old, _ in spec:
                out.append((name, HEADER, text, old))
    return out


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
