"""Step B's sabotages so far (docs/blipscope-map-client-steps.md, step B; registered 2026-11-11, "the refusal's commit"):
two plants, each in a COPY of the tree, and each must turn red exactly CD4 in check_map_draw.py, no fewer and no more.

  S-CD-NOREFUSAL  CB10's #if / #error / #endif deleted from src/AircraftManager.h, the region kept: {CD4}
  S-CD-STEPC      `#define BLIPSCOPE_MAP_STEP_C 1` added to src/AircraftManager.cpp: {CD4}

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

SABOTAGES = {
    "S-CD-NOREFUSAL": (AMH, [(REFUSAL, "")], {"CD4"}),
    "S-CD-STEPC": (AMCPP, [(STEPC_AT, STEPC_AT + "#define BLIPSCOPE_MAP_STEP_C 1\n")], {"CD4"}),
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
    return {mm.group(1) for mm in re.finditer(r"^\s*FAIL\s+(CD\d+)\b", c.stdout, re.M)}, None


def real_files():
    return [os.path.join(ROOT, p) for p in (AMH, AMCPP)]


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
    same = before == [sha(p) for p in real_files()]
    print(f"real files unchanged: {same}")
    print(f"MAP DRAW SABOTAGES ({len(SABOTAGES)}) AS PREDICTED" if ok and same else "MAP DRAW SABOTAGES: NOT AS PREDICTED")
    return rigs.close(0 if ok and same else 1)


# --control: the unplanted run alone, against this table. SOURCE_CHECKS is the one place the count lives.
SOURCE_CHECKS = 1   # CD4 until the draw files exist; CD1-CD3 join with them
CONTROL_TABLE = {"check_map_draw.py": ("checks", SOURCE_CHECKS)}


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
    return out


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
