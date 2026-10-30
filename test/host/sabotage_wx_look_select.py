"""K1/K2's sabotages (docs/wx-device.md, "The look as a shipping setting"), each planted in a COPY of the headers:
  S-label    a label one character off ("outlined plane")       -> exactly [K1] red
  S-default  absence read as look B (the rule inverted)          -> exactly [K1] and [K2] red (K1's "A selected for an
             empty value" reads through the same rule; stated before the first run)
The copies keep the repo's layout (include/ beside src/weatherscope/), because WxS2.h reaches its palette by a relative
path and a quoted include searches the including file's own folder first. The real headers are never written; their
sha256 is compared before and after.
    python -I test/host/sabotage_wx_look_select.py      exit 0 = as predicted, 1 = not, 2 = rig"""
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
TEST = os.path.join(ROOT, "test", "host", "test_wx_look_select.cpp")
FILES = {"include/WxS2.h": None, "include/WxLookSelectHtml.h": None, "src/weatherscope/WxPalette.h": None}
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "S-label    a label one character off": (["K1"], "include/WxLookSelectHtml.h",
        'constexpr const char* kLabelA = "Brighter rain, outlined planes";',
        'constexpr const char* kLabelA = "Brighter rain, outlined plane";   // SABOTAGE S-label'),
    "S-default  absence read as look B": (["K1", "K2"], "include/WxS2.h",
        "(s != nullptr && s[0] == 'B' && s[1] == '\\0') ? Look::B : Look::A",
        "(s != nullptr && s[0] == 'A' && s[1] == '\\0') ? Look::A : Look::B"),
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
    for rel in FILES:
        os.makedirs(os.path.dirname(os.path.join(tree, rel)), exist_ok=True)
        shutil.copyfile(os.path.join(ROOT, rel), os.path.join(tree, rel))
    if plant:
        rel, old, new = plant
        p = os.path.join(tree, rel)
        src = open(p, encoding="utf-8").read()
        assert src.count(old) == 1, f"{tag}: anchor found {src.count(old)} times"
        out = src.replace(old, new)
        assert out != src, f"{tag}: the sabotage did not apply"
        open(p, "w", encoding="utf-8", newline="\n").write(out)
    return tree


def main():
    before = {rel: sha(os.path.join(ROOT, rel)) for rel in FILES}
    ok = True
    with tempfile.TemporaryDirectory() as td:
        r, err = build_and_run(make_tree(td, "control"), td, "control")
        if r is None or r.returncode != 0 or len(re.findall(r"^\s*ok\b", r.stdout, re.M)) != OK_COUNT:
            print(f"control (the real headers): did not pass -- the rig is broken\n{err}{r.stdout if r else ''}")
            return 2
        print("control (the real headers): all look select checks pass")
        for i, (name, (want, rel, old, new)) in enumerate(SABOTAGES.items()):
            r, err = build_and_run(make_tree(td, f"s{i}", (rel, old, new)), td, f"s{i}")
            if r is None:
                print(f"{name}: did not compile (rig)\n{err}")
                return 2
            red = sorted({m.group(1) for l in r.stdout.splitlines() for m in [re.search(r"FAIL\s+\[([\w-]+)\]", l)] if m})
            good = r.returncode == 1 and red == want
            print(f"{name}: exit {r.returncode}; red rows {red or 'none'} (want {want}) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = {rel: sha(os.path.join(ROOT, rel)) for rel in FILES}
    print(f"real headers unchanged: {before == after}")
    return 0 if ok and before == after else 1


# --control (#510; docs/wx-device.md, "#510's --control"): this driver's unplanted run alone, its suite against
# its own count, nothing planted. OK_COUNT is the one place the count lives: main()'s unplanted run reads it too.
OK_COUNT = 2
CONTROL_TABLE = {"test_wx_look_select": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        build_and_run(make_tree(td, "control"), td, "control")


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    return [(name, rel, sc.read(os.path.join(ROOT, rel)), old) for name, (_, rel, old, _) in SABOTAGES.items()]


if __name__ == "__main__":
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
