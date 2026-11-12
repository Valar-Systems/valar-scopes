"""Step B's composer sabotages (docs/blipscope-map-client-steps.md, "MapCompose.h (CB1-CB5, CB9, CB11)", Q-MC14's
re-freeze and the 01:54:47Z ruling on S-CB-WXBUILD): fourteen plants in a COPY of include/MapCompose.h, each of which
must turn red exactly its registered set of test_map_draw.cpp's 26 cases, no fewer and no more. Every set was read node
by node against the rows before any code ran.

  S-CB-ORDER       the compose writes the layer over the weather
  S-CB-NAMESUNDER  the names drawn before Build
  S-CB-NOSWAP      the swap to the canvas's byte order removed
  S-CB-EAGER       the three fixed-size buffers allocated at the Map's start (the scratch stays at the package)
  S-CB-NOROT       rotSin and rotCos left out of the key
  S-CB-ROTONLY     the key compares the rotation alone
  S-CB-PERFRAME    the stale test always true
  S-CB-WXBUILD     the weather's generation added to the map's rebuild test
  S-CB-WXSTALE     the weather's generation left out of the compose's test
  S-CB-HALF        the failure branch keeps the buffers that succeeded
  S-CB-RETRY       a failed attempt's key not recorded
  S-CB-AIRTRUNC    over 128 airports, the list cut to 128 instead of refusing the names
  S-CB-NOFORGET    Forget leaves the refusal
  S-CB-NOGROW      the growth check removed

Method: include/ and src/weatherscope/ are copied into a temporary tree with the repo's own layout, with
test/fixtures/wx/glcdfont.c, which src/weatherscope/WxRaster.h includes by a parent-relative path (as
sabotage_map_layer.py and sabotage_map_places.py copy it). The plant goes into the copy only, after asserting each anchor
occurs exactly once and that the copy changed. The suite reads the real fixtures (test/fixtures/wx), read-only. The real
file's sha256 is compared before and after. The unplanted copy runs first.
    python -I test/host/sabotage_map_compose.py [--anchors | --control]   exit 0 = as predicted, 1 = not, 2 = the rig"""
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
TEST = os.path.join(ROOT, "test", "host", "test_map_draw.cpp")
FIXTURES = os.path.join(ROOT, "test", "fixtures", "wx")
COMPOSE = os.path.join("include", "MapCompose.h")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

CASES = 26   # the suite's count: the one place it lives, read by the run path and the --control table

COMPOSE_AT = "            out[p] = wxlayer::OffDisc(x, y, kSize) ? 0 : (wx[p] != 0 ? wx[p] : layer[p]);\n"
NAMES_AT = "    DrawNames(m, pk, k, in.airports, in.ctx);\n"
BUILD_AT = "    if (!in.build || !in.build(pk, k.view, in.askedMm, kLayers,"
SWAP_AT = "    for (size_t i = 0; i < kPixels; ++i) m.layer[i] = Swap(m.layer[i]);   // to the canvas's byte order\n"
INIT_AT = "    m.freeFn = freeFn;\n"
ROT_AT = "    if (a.view.rotSin != b.view.rotSin || a.view.rotCos != b.view.rotCos) return false;   // THE ROTATION\n"
REST_AT = "    return SameRest(a, b);\n"
STALE_AT = "    return !m.attempted || !Same(m.built, k);\n"
REBUILD_AT = "    if (Stale(m, k)) Rebuild(m, *pk, k, in);\n"
RECOMPOSE_AT = "    if (m.layerFresh || !m.lastShown || wxGen != m.composedWxGen) {\n"
FAIL_AT = "        Fail(m);   // CB9: one failure frees every buffer and refuses the map\n"
ATTEMPT_AT = ("    m.attempted = true;   // A FAILED ATTEMPT TOO: a Build that fails waits for the key to change, never per "
              "frame\n")
READY_AT = "    m.ready = true;\n    m.layerFresh = true;\n"
AIRPORTS_AT = ("    if (nAp < 0 || nAp > kAirportsMax) {\n"
               "        ++m.namesRefused;   // CB11: a cut obstacle list would let a name cover an airport nobody listed\n"
               "        return;\n"
               "    }\n")
FORGET_AT = "    m.refused = false;\n"
GROW_AT = ("    if (m.allocated && (size_t)pk->maxVertices + 1 > m.sc.cap) Release(m);   // a larger package: all again, at "
           "its size\n")

SABOTAGES = {
    "S-CB-ORDER": [(COMPOSE_AT, COMPOSE_AT.replace("(wx[p] != 0 ? wx[p] : layer[p])", "(layer[p] != 0 ? layer[p] : wx[p])"))],
    "S-CB-NAMESUNDER": [(NAMES_AT, ""), (BUILD_AT, NAMES_AT + BUILD_AT)],
    "S-CB-NOSWAP": [(SWAP_AT, "")],
    "S-CB-EAGER": [(INIT_AT, INIT_AT + "    AllocFixed(m);\n    if (!m.layer || !m.composite || !m.idx) Fail(m);\n")],
    "S-CB-NOROT": [(ROT_AT, "")],
    "S-CB-ROTONLY": [(REST_AT, "    return true;\n")],
    "S-CB-PERFRAME": [(STALE_AT, "    (void)m;\n    (void)k;\n    return true;\n")],
    "S-CB-WXBUILD": [(REBUILD_AT, "    if (Stale(m, k) || wxGen != m.composedWxGen) Rebuild(m, *pk, k, in);\n")],
    "S-CB-WXSTALE": [(RECOMPOSE_AT, "    if (m.layerFresh || !m.lastShown) {\n")],
    "S-CB-HALF": [(FAIL_AT, "        m.refused = true;   // the plant: the buffers that succeeded are kept\n")],
    "S-CB-RETRY": [(ATTEMPT_AT, ""), (READY_AT, "    m.ready = true;\n    m.attempted = true;\n    m.layerFresh = true;\n")],
    "S-CB-AIRTRUNC": [(AIRPORTS_AT, "    if (nAp > kAirportsMax) nAp = kAirportsMax;\n")],
    "S-CB-NOFORGET": [(FORGET_AT, "")],
    "S-CB-NOGROW": [(GROW_AT, "")],
}
WANT = {
    "S-CB-ORDER": {"CB1 compose"},
    "S-CB-NAMESUNDER": {"CB1 names"},
    "S-CB-NOSWAP": {"CB2"},
    "S-CB-EAGER": {"CB3 alloc"},
    "S-CB-NOROT": {"CB4 rotSin", "CB4 rotCos"},
    "S-CB-ROTONLY": {"CB4 homeLat", "CB4 homeLon", "CB4 rLat", "CB4 rLon", "CB4 radiusMi", "CB4 package",
                     "CB4 airports", "CB4 fixed"},
    "S-CB-PERFRAME": {"CB4 steady", "CB4 buildfail", "CB5"},
    "S-CB-WXBUILD": {"CB4 buildfail", "CB5"},   # re-frozen 01:54:47Z: a never-composed map leaves the generation unequal
    "S-CB-WXSTALE": {"CB5"},
    "S-CB-HALF": {"CB9 layer", "CB9 composite", "CB9 index", "CB9 scratch"},
    "S-CB-RETRY": {"CB4 buildfail"},
    "S-CB-AIRTRUNC": {"CB11 airports 129"},
    "S-CB-NOFORGET": {"CB9 forget"},
    "S-CB-NOGROW": {"CB9 grow"},
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def make_tree(td, tag, edits=()):
    tree = os.path.join(td, tag)
    shutil.copytree(os.path.join(ROOT, "include"), os.path.join(tree, "include"))
    shutil.copytree(os.path.join(ROOT, "src", "weatherscope"), os.path.join(tree, "src", "weatherscope"))
    font = os.path.join("test", "fixtures", "wx", "glcdfont.c")   # WxRaster.h's one include outside the two directories
    os.makedirs(os.path.dirname(os.path.join(tree, font)), exist_ok=True)
    shutil.copyfile(os.path.join(ROOT, font), os.path.join(tree, font))
    if edits:
        p = os.path.join(tree, COMPOSE)
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
    exe = os.path.join(td, f"c-{tag}.exe")
    b = subprocess.run([CXX] + FLAGS + ["-I", os.path.join(tree, "include"), "-I", os.path.join(tree, "src", "weatherscope"),
                                        TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        return None, None, "did not compile:\n" + b.stderr[-2000:]
    r = subprocess.run([exe, FIXTURES], capture_output=True, text=True)
    m = sc.COUNT.search(r.stdout)
    if r.returncode not in (0, 1) or not m:
        return None, None, f"the suite did not run (exit {r.returncode})\n" + r.stdout[-1500:]
    red = {mm.group(1).strip() for mm in re.finditer(r"^\s*FAIL\s+(CB\d+[^\n]*)$", r.stdout, re.M)}
    return red, int(m.group(1)), None


def main():
    real = os.path.join(ROOT, COMPOSE)
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
    print(f"MAP COMPOSE SABOTAGES ({len(SABOTAGES)}) AS PREDICTED" if ok and same else "MAP COMPOSE SABOTAGES: NOT AS PREDICTED")
    return rigs.close(0 if ok and same else 1)


CONTROL_TABLE = {"test_map_draw": ("checks", CASES)}


def control():
    with tempfile.TemporaryDirectory() as td:
        run_suite(make_tree(td, "control"), td, "control")


def anchor_entries():
    """Every plant's anchor in the real file, for run.sh's --anchors pass (no plant runs)."""
    text = sc.read(os.path.join(ROOT, COMPOSE))
    return [(name, COMPOSE, text, old) for name, edits in SABOTAGES.items() for old, _ in edits]


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
