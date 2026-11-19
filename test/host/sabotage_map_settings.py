"""Step D's host-row plants (docs/blipscope-map-client-steps.md, "Step D" D1 and "Step D's other half" (a), (b); the
basemap client's section 6, DOC1 and DOC2). Each plant goes into a COPY of the headers and must turn red exactly its own
rows, no fewer and no more:

  S-DOC1  MAP_DEFAULT_ON = false (include/ConfigMigration.h): exactly DOC1's nullptr and "" rows in test_config_migration
  S-DNC1  MAP_NAMES_DEFAULT_ON = false: exactly DNC1's nullptr and "" rows in test_config_migration
  S-DOC2  the page's header reads a literal "false" in place of ResolveToggle for the map: only DOC2 rows in
          test_map_settings_html
  S-DNC2  the same for the town names: only DNC2 rows
  S-D1    mapterrain::Classify's unknown branch returns Subtle: only D1 rows in test_map_terrain
  S-DNP1  mapcompose::Rebuild ignores `names` and draws them anyway: only DNP1 rows in test_map_names
  S-DNP2  `names` left out of mapcompose::SameRest: only DNP2 rows in test_map_names
D3's two plants (Subtle at gain 0.0751; Off with the terrain layer left on) are CONTROLS inside test_map_terrain itself,
run every time (ruled 2026-11-19 ~01:36Z). The page's source rows (DW1, DH1, D5, E3) are sabotage_map_draw.py's.

Method: include/, src/weatherscope/ and test/host/ref/ are copied into a temporary tree; the test sources and the
fixtures are read from the repo. The unplanted copy runs first, as the control. The real files' sha256 are compared
before and after.
    python -I test/host/sabotage_map_settings.py [--anchors | --control]   exit 0 = as predicted, 1 = not, 2 = the rig"""
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
HOST = os.path.join(ROOT, "test", "host")
FIX = os.path.join(ROOT, "test", "fixtures", "wx")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
COPY_DIRS = ["include", os.path.join("src", "weatherscope"), os.path.join("test", "host", "ref")]
# suite: (extra include dirs relative to the tree, takes the fixtures dir, its "ok" line count or None). THE ONE PLACE
# THE COUNTS LIVE (lint_driver_counts.py), read by the run path and the --control table alike; read from the suites'
# own output, 2026-11-19. test_config_migration prints no ok lines: its summary line stands in.
SUITES = {"test_config_migration": ([], False, None), "test_map_settings_html": ([], False, 22),
          "test_map_terrain": (["src/weatherscope", "test/host"], True, 10),
          "test_map_names": (["src/weatherscope"], True, 3)}


def passed(suite, r):
    """The unplanted suite as its row says: exit 0 and its ok count, or its summary line."""
    n = SUITES[suite][2]
    if r is None or r.returncode != 0:
        return False
    if n is None:
        return f"{suite}: all checks passed" in r.stdout
    return len(re.findall(r"^\s*ok\b", r.stdout, re.M)) == n and not re.search(r"^\s*FAIL\b", r.stdout, re.M)

CM, MSH, MT, MC = "include/ConfigMigration.h", "include/MapSettingsHtml.h", "include/MapTerrain.h", "include/MapCompose.h"
# name: (suite, file, old, new, want). `want` is the exact set of FAIL texts (test_config_migration) or row families
# (the others: the first word after FAIL).
SABOTAGES = {
    "S-DOC1": ("test_config_migration", CM, "constexpr bool MAP_DEFAULT_ON = true;", "constexpr bool MAP_DEFAULT_ON = false;",
               {"DOC1 map: unset (nullptr) -> ON", "DOC1 map: unset (empty) -> ON"}),
    "S-DNC1": ("test_config_migration", CM, "constexpr bool MAP_NAMES_DEFAULT_ON = true;",
               "constexpr bool MAP_NAMES_DEFAULT_ON = false;",
               {"DNC1 names: unset (nullptr) -> ON", "DNC1 names: unset (empty) -> ON"}),
    # the parameter stays named ((void)p, ...): -Werror would otherwise refuse it as unused, the rig and not a red
    "S-DOC2": ("test_map_settings_html", MSH, "configmigration::ResolveToggle(storedMap, configmigration::MAP_DEFAULT_ON)",
               'configmigration::ResolveToggle(((void)storedMap, "false"), configmigration::MAP_DEFAULT_ON)', {"DOC2"}),
    "S-DNC2": ("test_map_settings_html", MSH,
               "configmigration::ResolveToggle(storedNames, configmigration::MAP_NAMES_DEFAULT_ON)",
               'configmigration::ResolveToggle(((void)storedNames, "false"), configmigration::MAP_NAMES_DEFAULT_ON)',
               {"DNC2"}),
    "S-D1": ("test_map_terrain", MT, "    return kDefault;   // unset, empty or unknown",
             "    return Level::Subtle;   // PLANT S-D1", {"D1"}),
    "S-DNP1": ("test_map_names", MC,
               "    if (k.names)\n        DrawNames(m, pk, k, in.airports, in.ctx);\n    else\n        m.nPlaced = 0;",
               "    DrawNames(m, pk, k, in.airports, in.ctx);   // PLANT S-DNP1", {"DNP1"}),
    "S-DNP2": ("test_map_names", MC, "    if (a.names != b.names || a.terrainDrawn != b.terrainDrawn) return false;",
               "    if (a.terrainDrawn != b.terrainDrawn) return false;   // PLANT S-DNP2", {"DNP2"}),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def real_files():
    out = []
    for d in COPY_DIRS:
        for top, _, names in os.walk(os.path.join(ROOT, d)):
            out += [os.path.join(top, n) for n in names]
    return sorted(out)


def make_tree(td, tag, plant=None):
    tree = os.path.join(td, tag)
    for d in COPY_DIRS:
        shutil.copytree(os.path.join(ROOT, d), os.path.join(tree, d))
    # The test sources too: two of them include their headers as "../../include/...", relative to the SOURCE, so a
    # source compiled from the repo would read the real headers and never see a plant.
    for suite in SUITES:
        shutil.copy2(os.path.join(HOST, suite + ".cpp"), os.path.join(tree, "test", "host", suite + ".cpp"))
    # src/weatherscope/WxRaster.h includes the font as "../../test/fixtures/wx/glcdfont.c", relative to itself
    os.makedirs(os.path.join(tree, "test", "fixtures", "wx"), exist_ok=True)
    shutil.copy2(os.path.join(FIX, "glcdfont.c"), os.path.join(tree, "test", "fixtures", "wx", "glcdfont.c"))
    if plant:
        rel, old, new = plant
        p = os.path.join(tree, rel)
        src = open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")
        assert src.count(old) == 1, f"{tag}: the anchor occurs {src.count(old)} times, need exactly 1"
        out = src.replace(old, new)
        assert out != src, f"{tag}: the sabotage did not apply"
        open(p, "w", encoding="utf-8", newline="\n").write(out)
    return tree


def build_and_run(tree, td, tag, suite):
    extra, fixtures, _ = SUITES[suite]
    exe = os.path.join(td, f"{suite}-{tag}.exe")
    inc = ["-I", os.path.join(tree, "include")] + [x for e in extra for x in ("-I", os.path.join(tree, e))]
    b = subprocess.run([CXX] + FLAGS + inc + [os.path.join(tree, "test", "host", suite + ".cpp"), "-o", exe],
                       capture_output=True, text=True)
    if b.returncode != 0:
        return None, b.stderr
    return subprocess.run([exe] + ([FIX] if fixtures else []), capture_output=True, text=True), ""


def red_of(suite, out):
    if suite == "test_config_migration":
        return {m.group(1).strip() for m in re.finditer(r"^\s*FAIL: (.*)$", out, re.M)}
    return {m.group(1) for m in re.finditer(r"^\s*FAIL\s+(\S+)", out, re.M)}


def main():
    before = {p: sha(p) for p in real_files()}
    ok = True
    with tempfile.TemporaryDirectory() as td:
        ctl = make_tree(td, "control")
        for suite in SUITES:
            r, err = build_and_run(ctl, td, "control", suite)
            if not passed(suite, r):
                print(f"control (the real headers) {suite}: did not pass -- the rig\n{err}{r.stdout if r else ''}")
                return 2
        print(f"control (the real headers): {len(SUITES)} suite(s) pass")
        for i, (name, (suite, rel, old, new, want)) in enumerate(SABOTAGES.items()):
            r, err = build_and_run(make_tree(td, f"s{i}", (rel, old, new)), td, f"s{i}", suite)
            if r is None:
                print(f"{name}: did not compile (the rig)\n{err}")
                return 2
            if r.returncode not in (0, 1):
                print(f"{name}: {suite} exited {r.returncode} (the rig, not a red)\n{r.stdout[-600:]}")
                return 2
            red = red_of(suite, r.stdout)
            good = r.returncode == 1 and red == want
            print(f"{name}: {suite} exit {r.returncode}; red {sorted(red) or 'none'} (want {sorted(want)}) -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = {p: sha(p) for p in real_files()}
    print(f"MAP SETTINGS SABOTAGES ({len(SABOTAGES)}) {'AS PREDICTED' if ok else 'NOT AS PREDICTED'}")
    print(f"real files unchanged: {before == after}")
    return 0 if ok and before == after else 1


# --control: the unplanted run alone, each suite against its own count, derived from SUITES (the one place).
CONTROL_TABLE = {s: (("ok", n) if n is not None else ("line", f"{s}: all checks passed")) for s, (_, _, n) in SUITES.items()}


def control():
    with tempfile.TemporaryDirectory() as td:
        tree = make_tree(td, "control")
        for suite in SUITES:
            build_and_run(tree, td, "control", suite)


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    return [(name, rel, sc.read(os.path.join(ROOT, rel)), old) for name, (_, rel, old, _, _) in SABOTAGES.items()]


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
