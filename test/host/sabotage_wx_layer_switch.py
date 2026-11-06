"""The weather switch's sabotages (docs/wx-device.md, "The layer on by default"), each planted in a COPY of the header,
requiring test_wx_layer_switch.cpp to turn red in exactly the rows named, no fewer and no more:
  S-WS-today     today's rule restored, == "true" (the ruled one)   -> exactly WS1
  S-WS-always    always on, an owner's "false" ignored              -> exactly WS3, WS4
  S-WS-notfalse  != "false", any stray value reads on               -> exactly WS4
The real header is never written; its sha256 is compared before and after.
    python -I test/host/sabotage_wx_layer_switch.py      exit 0 = as predicted, 1 = not, 2 = rig"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass (main's #506; docs/wx-device.md, "CI's anchors pass")

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEST = os.path.join(ROOT, "test", "host", "test_wx_layer_switch.cpp")
HEADER = "include/WxLayerSwitch.h"
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

RULE = "return stored[0] == '\\0' || strcmp(stored, \"true\") == 0;"
SABOTAGES = {
    "S-WS-today     today's rule restored": (["WS1"], RULE, "return strcmp(stored, \"true\") == 0;   /* SABOTAGE S-WS-today */"),
    "S-WS-always    always on": (["WS3", "WS4"], RULE, "(void)stored; return true;   /* SABOTAGE S-WS-always */"),
    "S-WS-notfalse  != \"false\"": (["WS4"], RULE, "return strcmp(stored, \"false\") != 0;   /* SABOTAGE S-WS-notfalse */"),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def build_and_run(tree, td, tag):
    exe = os.path.join(td, f"t-{tag}.exe")
    b = subprocess.run([CXX] + FLAGS + ["-I", os.path.join(tree, "include"), TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        return None, b.stderr
    return subprocess.run([exe], capture_output=True, text=True), ""


def make_tree(td, tag, plant=None):
    tree = os.path.join(td, tag)
    os.makedirs(os.path.join(tree, "include"), exist_ok=True)
    shutil.copyfile(os.path.join(ROOT, HEADER), os.path.join(tree, HEADER))
    if plant:
        old, new = plant
        p = os.path.join(tree, HEADER)
        src = open(p, encoding="utf-8").read()
        assert src.count(old) == 1, f"{tag}: anchor found {src.count(old)} times"
        out = src.replace(old, new)
        assert out != src, f"{tag}: the sabotage did not apply"
        open(p, "w", encoding="utf-8", newline="\n").write(out)
    return tree


def main():
    before = sha(os.path.join(ROOT, HEADER))
    ok = True
    with tempfile.TemporaryDirectory() as td:
        r, err = build_and_run(make_tree(td, "control"), td, "control")
        if r is None or r.returncode != 0 or len(re.findall(r"^\s*ok\b", r.stdout, re.M)) != OK_COUNT:
            print(f"control (the real header): did not pass -- the rig is broken\n{err}{r.stdout if r else ''}")
            return 2
        print("control (the real header): all switch checks pass")
        for i, (name, (want, old, new)) in enumerate(SABOTAGES.items()):
            r, err = build_and_run(make_tree(td, f"s{i}", (old, new)), td, f"s{i}")
            if r is None:
                print(f"{name}: did not compile (rig)\n{err}")
                return 2
            red = sorted({m.group(1) for l in r.stdout.splitlines() for m in [re.search(r"FAIL\s+(WS\d)\b", l)] if m})
            good = r.returncode == 1 and red == want
            print(f"{name}: exit {r.returncode}; red rows {red or 'none'} (want {want}) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    same = before == sha(os.path.join(ROOT, HEADER))
    print(f"real header unchanged: {same}")
    print("WX SWITCH SABOTAGES AS PREDICTED" if ok and same else "WX SWITCH SABOTAGES: NOT AS PREDICTED")
    return 0 if ok and same else 1


# --control (#510; docs/wx-device.md, "#510's --control"): this driver's unplanted run alone, its suite against
# its own count, nothing planted. OK_COUNT is the one place the count lives: main()'s unplanted run reads it too.
OK_COUNT = 4
CONTROL_TABLE = {"test_wx_layer_switch": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        build_and_run(make_tree(td, "control"), td, "control")


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    return [(name, HEADER, sc.read(os.path.join(ROOT, HEADER)), old) for name, (_, old, _) in SABOTAGES.items()]


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
