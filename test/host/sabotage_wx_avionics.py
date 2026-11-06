"""The avionics look's sabotages (docs/wx-device.md, "The ruling (~15:07Z), and the avionics look's predictions" and
"Re-registered (ruling of ~15:18Z)"), each planted in a COPY of the headers, requiring test_wx_avionics.cpp to turn red
in exactly the rows named, no fewer and no more:
  S-AV-drizzle  kFirstDrawn = 1: code 1 drawn as dim green (the ruled one)        -> exactly AV1, AV7
  S-AV-read     EchoAt reads Weatherscope's Rain() again (the ruled one)          -> exactly AV1, AV2, AV5, AV6
  S-AV-edit     one byte of Weatherscope's kRainTable[0] changed (the ruled one)  -> exactly AV3
  S-AV-alt      Plane()'s < becomes <=, so 3,000.0 m turns cyan                    -> exactly AV4
  S-AV-plane    Plane() returns the live green                                    -> exactly AV4, AV5
The copies keep the repo's layout (include/ beside src/weatherscope/), because WxS2.h reaches its palette by a relative
path and a quoted include searches the including file's own folder first. The real headers are never written; their
sha256 is compared before and after.
    python -I test/host/sabotage_wx_avionics.py      exit 0 = as predicted, 1 = not, 2 = rig"""
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
TEST = os.path.join(ROOT, "test", "host", "test_wx_avionics.cpp")
FILES = {"include/WxSweep.h": None, "include/WxS2.h": None, "include/WxAvionics.h": None,
         "src/weatherscope/WxPalette.h": None}
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

PLANE = "inline Rgb Plane(float altMeters) { return altMeters < kPlaneSplitM ? kPlaneLow : kPlaneHigh; }"
SABOTAGES = {
    "S-AV-drizzle  code 1 drawn as dim green": (["AV1", "AV7"], "include/WxAvionics.h",
        "constexpr uint8_t kFirstDrawn = 2;",
        "constexpr uint8_t kFirstDrawn = 1;   // SABOTAGE S-AV-drizzle"),
    "S-AV-read     EchoAt reads Weatherscope's Rain()": (["AV1", "AV2", "AV5", "AV6"], "include/WxS2.h",
        "    if (wxavionics::Drawn(code)) return wxpalette::Dim(wxavionics::Echo(code), pct);",
        "    if (wxavionics::Drawn(code)) return wxpalette::Dim(wxpalette::Rain(code), pct);   // SABOTAGE S-AV-read"),
    "S-AV-edit     Weatherscope's table edited": (["AV3"], "src/weatherscope/WxPalette.h",
        "{0, 0, 246},",
        "{1, 0, 246},   /* SABOTAGE S-AV-edit */"),
    "S-AV-alt      Plane()'s < becomes <=": (["AV4"], "include/WxAvionics.h",
        PLANE, PLANE.replace("altMeters < kPlaneSplitM", "altMeters <= kPlaneSplitM") + "   // SABOTAGE S-AV-alt"),
    "S-AV-plane    Plane() returns the live green": (["AV4", "AV5"], "include/WxAvionics.h",
        PLANE, "inline Rgb Plane(float altMeters) { (void)altMeters; return Rgb{0, 255, 0}; }   // SABOTAGE S-AV-plane"),
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
        print("control (the real headers): all avionics checks pass")
        for i, (name, (want, rel, old, new)) in enumerate(SABOTAGES.items()):
            r, err = build_and_run(make_tree(td, f"s{i}", (rel, old, new)), td, f"s{i}")
            if r is None:
                print(f"{name}: did not compile (rig)\n{err}")
                return 2
            red = sorted({m.group(1) for l in r.stdout.splitlines() for m in [re.search(r"FAIL\s+(AV\d)\b", l)] if m})
            good = r.returncode == 1 and red == want
            print(f"{name}: exit {r.returncode}; red rows {red or 'none'} (want {want}) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = {rel: sha(os.path.join(ROOT, rel)) for rel in FILES}
    print(f"real headers unchanged: {before == after}")
    print("WX AVIONICS SABOTAGES AS PREDICTED" if ok and before == after else "WX AVIONICS SABOTAGES: NOT AS PREDICTED")
    return 0 if ok and before == after else 1


# --control (#510; docs/wx-device.md, "#510's --control"): this driver's unplanted run alone, its suite against
# its own count, nothing planted. OK_COUNT is the one place the count lives: main()'s unplanted run reads it too.
OK_COUNT = 7
CONTROL_TABLE = {"test_wx_avionics": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        build_and_run(make_tree(td, "control"), td, "control")


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    return [(name, rel, sc.read(os.path.join(ROOT, rel)), old) for name, (_, rel, old, _) in SABOTAGES.items()]


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
