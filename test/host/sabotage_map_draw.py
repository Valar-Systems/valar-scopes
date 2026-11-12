"""Step B's sabotages so far (docs/blipscope-map-client-steps.md, step B; registered 2026-11-11, "the refusal's commit"):
two plants, each in a COPY of the tree, and each must turn red exactly CD4 in check_map_draw.py, no fewer and no more.

  S-CD-NOREFUSAL  CB10's #if / #error / #endif deleted from src/AircraftManager.h, the region kept: {CD4}
  S-CD-STEPC      `#define BLIPSCOPE_MAP_STEP_C 1` added to src/AircraftManager.cpp: {CD4}
  S-CB-SECONDPATH DrawAirports draws its code at x + 6 without radarbox::Airport: {CB8}
  S-CB-PINS       RING_LABEL_OUTER_INSET becomes 13 in src/AircraftManager.cpp: {CB8b}
  S-CB-PINVAL     MapPlaces.h's kDotsX0 becomes 100 while the drawers stay: {CB8b}
The wiring's (registered 2026-11-12, "The wiring: registered before its code"):
  S-CD-NVS        a Preferences write planted in DrawMapBase's draw region: {CD1}
  S-CD-PRINTNAME  a town's label printed in DrawMapBase's draw region: {CD2}
  S-CD-VIEW       DrawMapBase's view.rLat filled from radLat, not viewRadLat: {CD3}
  S-CD-NOSPIRAM   MapAlloc's MALLOC_CAP_SPIRAM dropped: {CD5}
  S-CD-NOGEN      the ++mapAirportsGen after `cloudAirports = std::move(...)` removed: {CD6}
  S-CD-CULL       MapAirportBoxes' strip threshold 0.55f becomes 0.5f: {CD7}

And three controls of the instrument's own direction (registered with L1's fix; none is a {CD4}-set plant):
  R-CD-RAWOK      a raw-string file added under include/: exit 0, all SOURCE_CHECKS pass (L1's input now reads clean)
  R-CD-CRASH      a raw string added to src/AircraftManager.h after CB10's region, which strip() still parses: exit 2,
                  "RIG: ...", never 1
  R-CD-DECODE     a file of undecodable bytes added under include/: exit 2, "RIG: ...", never 1

Method: include/, src/, platformio.ini and the text files under tools/ are copied into a temporary tree with the repo's
own layout (no git: this also runs under WSL in a worktree, where git cannot follow the worktree). The plant goes into
the copy only, after asserting its anchor occurs exactly once and that the copy changed. The real files' sha256 are
compared before and after. The unplanted copy runs first, as the control.
    python -I test/host/sabotage_map_draw.py [--anchors | --control]   exit 0 = as predicted, 1 = not, 2 = the rig"""
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
CHECK = os.path.join(ROOT, "test", "host", "check_map_draw.py")
AMH = os.path.join("src", "AircraftManager.h")
AMCPP = os.path.join("src", "AircraftManager.cpp")
PY = sys.executable
TOOL_EXTS = (".sh", ".ini", ".py", ".mjs", ".js", ".json")

REFUSAL = ("#if defined(BLIPSCOPE_MAP_FETCH) && !defined(WX_LOOK_AB) && !defined(BLIPSCOPE_MAP_STEP_C)\n"
           "#error \"step B draws the map without step C: the sweep erases it. Land step C before WX_LAYER_SHIPS ships.\"\n"
           "#endif\n")
STEPC_AT = '#include "MapLayer.h"       // maplayer::Load: the gate (CRC and format section 8), run on the fetch task\n'

PLACES = os.path.join("include", "MapPlaces.h")
SOURCE_CHECKS = 9   # CD4, CB8, CB8b, CD1-CD3, CD5-CD7: the one place the count lives (the run path, the --control table)
MAPBASE_AT = "    k.airportsGen = mapAirportsGen;\n"   # a line in DrawMapBase's draw region

SABOTAGES = {
    "S-CD-NOREFUSAL": (AMH, [(REFUSAL, "")], {"CD4"}),
    "S-CD-STEPC": (AMCPP, [(STEPC_AT, STEPC_AT + "#define BLIPSCOPE_MAP_STEP_C 1\n")], {"CD4"}),
    # CB8: DrawAirports draws its code at x + 6 without the helper.
    "S-CB-SECONDPATH": (AMCPP, [("        backbuffer.drawString(code, codeBox.x0, codeBox.y0);\n",
                                 "        backbuffer.drawString(code, x + 6, y - 3);\n")], {"CB8"}),
    # CB8b: a drawer's constant moves (the expression's value changes)...
    "S-CB-PINS": (AMCPP, [("static constexpr int RING_LABEL_OUTER_INSET = 14;",
                           "static constexpr int RING_LABEL_OUTER_INSET = 13;")], {"CB8b"}),
    # ...and a value the names use moves while the drawers stay (no C++ row sees it: places_plants.py read 0).
    "S-CB-PINVAL": (PLACES, [("constexpr int kDotsX0 = 99;", "constexpr int kDotsX0 = 100;")], {"CB8b"}),
    # The wiring's six (CD1-CD3, CD5-CD7), one row each.
    "S-CD-NVS": (AMCPP, [(MAPBASE_AT, MAPBASE_AT + '    { Preferences mapPrefs; mapPrefs.putUInt("mapgen", mapAirportsGen); }\n')],
                 {"CD1"}),
    "S-CD-PRINTNAME": (AMCPP, [(MAPBASE_AT, MAPBASE_AT + '    if (mapDraw.nPlaced > 0) Serial.printf("[map] %s\\n", '
                                                         'mapDraw.places->t[mapDraw.placed[0].town].label);\n')], {"CD2"}),
    "S-CD-VIEW": (AMCPP, [("    k.view.rLat = viewRadLat;\n", "    k.view.rLat = radLat;\n")], {"CD3"}),
    "S-CD-NOSPIRAM": (AMCPP, [("    return heap_caps_aligned_alloc(8, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);",
                               "    return heap_caps_aligned_alloc(8, bytes, MALLOC_CAP_8BIT);")], {"CD5"}),
    "S-CD-NOGEN": (AMCPP, [("            ++mapAirportsGen;   // a new long tail: the names' obstacles change (CD6)\n", "")],
                   {"CD6"}),
    "S-CD-CULL": (AMCPP, [("        const bool wide = viewRadLat > 0.55f;   // CD7: DrawAirports' strip threshold\n",
                           "        const bool wide = viewRadLat > 0.5f;   // CD7: DrawAirports' strip threshold\n")], {"CD7"}),
}

# The instrument's two directions (registered with L1's fix): none is a {CD4}-set plant. Each runs in its own copy and is
# graded on its exit code and output. ("add", path, bytes) writes a new file; ("edit", path, old, new) edits one.
RAW = b'static const char* const kRaw = R"RAW(\nline one\nline two\n)RAW";\n'
CB10_END = REFUSAL + "// THE MAP DRAW END\n"   # CB10's region end: unique, now the header holds four draw regions
CONTROLS = {
    "R-CD-RAWOK": (("add", os.path.join("include", "zz_rawok.h"), RAW), 0, f"{SOURCE_CHECKS} checks, 0 failures"),
    "R-CD-CRASH": (("edit", AMH, CB10_END, CB10_END + RAW.decode("utf-8")), 2, "RIG: "),
    "R-CD-DECODE": (("add", os.path.join("include", "zz_badbyte.h"), b"// \xff\xfe\n"), 2, "RIG: "),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def copy_tree(dst):
    for top in ("include", "src"):
        shutil.copytree(os.path.join(ROOT, top), os.path.join(dst, top))
    shutil.copy2(os.path.join(ROOT, "platformio.ini"), os.path.join(dst, "platformio.ini"))
    for d, dirs, names in os.walk(os.path.join(ROOT, "tools")):
        dirs[:] = [x for x in dirs if x not in ("node_modules", ".git")]
        for n in names:
            if n.endswith(TOOL_EXTS):
                src = os.path.join(d, n)
                out = os.path.join(dst, os.path.relpath(src, ROOT))
                os.makedirs(os.path.dirname(out), exist_ok=True)
                shutil.copy2(src, out)


def make_tree(td, tag, target=None, edits=()):
    tree = os.path.join(td, tag)
    copy_tree(tree)
    if target:
        p = os.path.join(tree, target)
        src = open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")
        out = src
        for old, new in edits:
            assert out.count(old) == 1, f"{tag}: the anchor occurs {out.count(old)} times, need exactly 1"
            out = out.replace(old, new)
        assert out != src, f"{tag}: the sabotage did not apply"
        open(p, "w", encoding="utf-8", newline="\n").write(out)
    return tree


def run_check(tree):
    """(red row names, None) or (None, why)."""
    c = subprocess.run([PY, "-I", CHECK, tree], capture_output=True, text=True)
    m = sc.COUNT.search(c.stdout)
    if c.returncode not in (0, 1) or not m or int(m.group(1)) != SOURCE_CHECKS:
        return None, f"the check did not run as {SOURCE_CHECKS} row(s) (exit {c.returncode})\n" + c.stdout + c.stderr
    return {mm.group(1) for mm in re.finditer(r"^\s*FAIL\s+(CD\d+|CB\d+b?)\b", c.stdout, re.M)}, None


def control_tree(td, tag, spec):
    """A copy with one control's input applied, asserted before anything reads it."""
    tree = make_tree(td, tag)
    kind, rel = spec[0], spec[1]
    p = os.path.join(tree, rel)
    if kind == "add":
        assert not os.path.exists(p), f"{tag}: {rel} already exists in the copy"
        open(p, "wb").write(spec[2])
        assert open(p, "rb").read() == spec[2], f"{tag}: the planted file does not hold its bytes"
    else:
        old, new = spec[2], spec[3]
        src = open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")
        assert src.count(old) == 1, f"{tag}: the anchor occurs {src.count(old)} times, need exactly 1"
        out = src.replace(old, new)
        assert out != src, f"{tag}: the control did not apply"
        open(p, "w", encoding="utf-8", newline="\n").write(out)
    return tree


def run_raw(tree):
    c = subprocess.run([PY, "-I", CHECK, tree], capture_output=True, text=True)
    return c.returncode, c.stdout + c.stderr


def real_files():
    return [os.path.join(ROOT, p) for p in (AMH, AMCPP, PLACES)]


def main():
    before = [sha(p) for p in real_files()]
    rigs = sc.Rigs()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        red, why = run_check(make_tree(td, "control"))
        if why or red:
            print(f"CONTROL (the real files): not clean -- the rig. {why or ''} red {sorted(red or [])}")
            return 2
        print(f"control (the real files): {SOURCE_CHECKS} check(s), all pass")
        for i, (name, (target, edits, want)) in enumerate(SABOTAGES.items()):
            def one():
                red, why = run_check(make_tree(td, f"s{i}", target, edits))
                if why:
                    print(f"{name}: {why}")
                    raise sc.Rig()
                return red
            red = rigs.run(name, one)
            if red is None:
                ok = False
                continue
            good = red == want
            extra, missing = sorted(red - want), sorted(want - red)
            print(f"{name}: {len(red)} red (want {len(want)}); extra {extra or 'none'}; missing {missing or 'none'}"
                  f" -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
        print(f"MAP DRAW SABOTAGES ({len(SABOTAGES)}) AS PREDICTED" if ok else "MAP DRAW SABOTAGES: NOT AS PREDICTED")
        cok = True
        for j, (name, (spec, want_rc, want_text)) in enumerate(CONTROLS.items()):
            rc, out = run_raw(control_tree(td, f"c{j}", spec))
            first = out.strip().splitlines()[-1] if want_rc == 0 and out.strip() else out.strip()
            good = rc == want_rc and (want_text in out if want_rc == 0 else out.lstrip().startswith(want_text))
            print(f"{name}: exit {rc} (want {want_rc}); {first[:100]!r} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            cok &= good
        print(f"MAP DRAW CONTROLS ({len(CONTROLS)}) AS PREDICTED" if cok else "MAP DRAW CONTROLS: NOT AS PREDICTED")
        ok &= cok
    same = before == [sha(p) for p in real_files()]
    print(f"real files unchanged: {same}")
    return rigs.close(0 if ok and same else 1)


# --control: the unplanted run alone, against this table. SOURCE_CHECKS (defined above) is the one place the count
# lives. The entry is a ("line", text) DERIVED from it, not a ("checks", n) count (W9's first run, ruled fix (A)): the
# exact line still carries the count and "0 failures", with exit 0.
CONTROL_TABLE = {"check_map_draw.py": ("line", f"{SOURCE_CHECKS} checks, 0 failures")}


def control():
    with tempfile.TemporaryDirectory() as td:
        run_check(make_tree(td, "control"))


def anchor_entries():
    """Every plant's anchor in the real files, for run.sh's --anchors pass (no plant runs)."""
    out = []
    for name, (target, edits, _) in SABOTAGES.items():
        text = sc.read(os.path.join(ROOT, target))
        for old, _ in edits:
            out.append((name, target, text, old))
    for name, (spec, _, _) in CONTROLS.items():
        if spec[0] == "edit":
            out.append((name, spec[1], sc.read(os.path.join(ROOT, spec[1])), spec[2]))
    return out


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
