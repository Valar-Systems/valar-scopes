"""Step B's place-name sabotages (docs/blipscope-map-client-steps.md, "MapPlaces.h (CB6-CB8), registered before its
code"): four plants in a COPY of include/MapPlaces.h, each of which must turn red exactly its registered set of
test_map_places.cpp's 32 cases, no fewer and no more. The sets were computed before the code, by the independent scratch
reading run under each plant (places_plants.py, which executes the registered places_ref.py).

  S-CB-AIRPORT    AirportObstacles returns no boxes (the airports are not obstacles)
  S-CB-FILEORDER  Place takes the towns in file order (Rank itself, which CB6 eq holds, is untouched)
  S-CB-NOCAP      Place has no cap (neither Cap nor kMaxLabels)
  S-CB-NORTHUP    AnchorAffine drops the window-up rotation (the Weatherscope's north-up mapper)
(CB8 and CB8b are source rows: their plants are in sabotage_map_draw.py.)

Method: include/ and src/weatherscope/ are copied into a temporary tree with the repo's own layout; the plant goes into
the copy only, after asserting its anchor occurs exactly once and that the copy changed. The suite reads the real
fixtures (test/fixtures/wx), read-only. The real file's sha256 is compared before and after. The unplanted copy runs first.
    python -I test/host/sabotage_map_places.py [--anchors | --control]   exit 0 = as predicted, 1 = not, 2 = the rig"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEST = os.path.join(ROOT, "test", "host", "test_map_places.cpp")
FIXTURES = os.path.join(ROOT, "test", "fixtures", "wx")
PLACES = os.path.join("include", "MapPlaces.h")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

CASES = 32   # the suite's count: the one place it lives, read by the run path and the --control table

AIRPORT_AT = ("    radarbox::Airport(x, y, (int)strlen(code), &out[0], &out[1]);\n"
              "    return 2;\n")
ORDER_AT = ("    Rank(l, order);\n"
            "    const int cap = Cap(radiusMi);\n")
CAP_AT = "    const int cap = Cap(radiusMi);\n"
LOOP_AT = "    for (int k = 0; k < l.n && n < cap && n < kMaxLabels; ++k) {\n"
ROT_AT = "    return maplayer::AffineFor(pk, pk.radiusMm, v);\n"

SABOTAGES = {
    "S-CB-AIRPORT": [(AIRPORT_AT, AIRPORT_AT.replace("return 2;", "return 0;"))],
    "S-CB-FILEORDER": [(ORDER_AT, "    for (int i = 0; i < l.n; ++i) order[i] = i;\n    const int cap = Cap(radiusMi);\n")],
    "S-CB-NOCAP": [(CAP_AT, "    const int cap = 1000000;\n"),
                   (LOOP_AT, "    for (int k = 0; k < l.n && n < cap; ++k) {\n")],
    "S-CB-NORTHUP": [(ROT_AT, "    wxlayer::View north = v;\n    north.rotSin = 0.0f;\n    north.rotCos = 1.0f;\n"
                              "    return maplayer::AffineFor(pk, pk.radiusMm, north);\n")],
}
WANT = {
    "S-CB-AIRPORT": {"CB6 airports", "CB6 rank", "CB6 bend 25mi", "CB6 bend 50mi", "CB6 bend 100mi"},
    "S-CB-FILEORDER": {"CB6 rank", "CB6 kjac 50mi", "CB6 kjac 100mi"},
    "S-CB-NOCAP": {"CB6 cap", "CB6 kjac 50mi", "CB6 kjac 100mi", "CB6 ksfo 10mi", "CB6 ksfo 25mi", "CB6 ksfo 50mi",
                   "CB6 ksfo 100mi"},
    "S-CB-NORTHUP": {"CB6 rank", "CB6 kjac 25mi", "CB6 kjac 50mi", "CB6 kjac 100mi", "CB6 ksfo 5mi", "CB6 ksfo 10mi",
                     "CB6 bend 25mi", "CB6 bend 50mi", "CB6 bend 100mi"} | {f"CB7 r{d}" for d in range(45, 360, 45)},
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def make_tree(td, tag, edits=()):
    tree = os.path.join(td, tag)
    shutil.copytree(os.path.join(ROOT, "include"), os.path.join(tree, "include"))
    shutil.copytree(os.path.join(ROOT, "src", "weatherscope"), os.path.join(tree, "src", "weatherscope"))
    if edits:
        p = os.path.join(tree, PLACES)
        src = open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")
        out = src
        for old, new in edits:
            assert out.count(old) == 1, f"{tag}: the anchor occurs {out.count(old)} times, need exactly 1"
            out = out.replace(old, new)
        assert out != src, f"{tag}: the sabotage did not apply"
        open(p, "w", encoding="utf-8", newline="\n").write(out)
    return tree


def run_suite(tree, td, tag):
    """(red row names, the case count, None) or (None, None, why)."""
    exe = os.path.join(td, f"p-{tag}.exe")
    b = subprocess.run([CXX] + FLAGS + ["-I", os.path.join(tree, "include"), "-I", os.path.join(tree, "src", "weatherscope"),
                                        TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        return None, None, "did not compile:\n" + b.stderr[-2000:]
    r = subprocess.run([exe, FIXTURES], capture_output=True, text=True)
    m = sc.COUNT.search(r.stdout)
    if r.returncode not in (0, 1) or not m:
        return None, None, f"the suite did not run (exit {r.returncode})\n" + r.stdout[-1500:]
    red = {mm.group(1).strip() for mm in re.finditer(r"^\s*FAIL\s+(CB[67][^\n]*)$", r.stdout, re.M)}
    return red, int(m.group(1)), None


def main():
    real = os.path.join(ROOT, PLACES)
    before = sha(real)
    rigs = sc.Rigs()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        red, n, why = run_suite(make_tree(td, "control"), td, "control")
        if why or red or n != CASES:
            print(f"CONTROL (the real files): not clean -- the rig. {why or ''} red {sorted(red or [])}, {n} cases")
            return 2
        print(f"control (the real files): {n} cases, all pass")
        for i, (name, edits) in enumerate(SABOTAGES.items()):
            def one():
                red, n, why = run_suite(make_tree(td, f"s{i}", edits), td, f"s{i}")
                if why:
                    print(f"{name}: {why}")
                    raise sc.Rig()
                return red, n
            got = rigs.run(name, one)
            if got is None:
                ok = False
                continue
            red, n = got
            want = WANT[name]
            good = red == want and n == CASES
            extra, missing = sorted(red - want), sorted(want - red)
            print(f"{name}: {len(red)} red (want {len(want)}); extra {extra or 'none'}; missing {missing or 'none'}"
                  f" -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    same = before == sha(real)
    print(f"real files unchanged: {same}")
    print(f"MAP PLACES SABOTAGES ({len(SABOTAGES)}) AS PREDICTED" if ok and same else "MAP PLACES SABOTAGES: NOT AS PREDICTED")
    return rigs.close(0 if ok and same else 1)


CONTROL_TABLE = {"test_map_places": ("checks", CASES)}


def control():
    with tempfile.TemporaryDirectory() as td:
        run_suite(make_tree(td, "control"), td, "control")


def anchor_entries():
    """Every plant's anchor in the real file, for run.sh's --anchors pass (no plant runs)."""
    text = sc.read(os.path.join(ROOT, PLACES))
    return [(name, PLACES, text, old) for name, edits in SABOTAGES.items() for old, _ in edits]


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
