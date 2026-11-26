"""The one shared afterglow constant's plants (include/SweepWedge.h, the owner's ruling of 2026-11-22): plant each into
a COPY of include/SweepWedge.h or include/DrawHelpers.h and require exactly its predicted rows of test_sweep_wedge.cpp
to go red, every other row staying green. The real files are never written; their sha256 is compared before and after.
S-tail is the red first: the divergence the one constant exists to prevent -- the weather and map sweeps at kTail, the
device's opaque wedge at its own 1.0, which is what changing kTail alone did before the move.

    python -I test/host/sabotage_sweep_wedge.py      exit 0 = as predicted, 1 = not, 2 = rig
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes (run.sh, Q51 and Q57)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INC = os.path.join(ROOT, "include")
WEDGE = os.path.join(INC, "SweepWedge.h")
SWEEP = os.path.join(INC, "WxSweep.h")
HELPERS = os.path.join(INC, "DrawHelpers.h")
TEST = os.path.join(ROOT, "test", "host", "test_sweep_wedge.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

CALL = "    wxsweep::FillWedge(buf, cx, cy, radius, angle, [](uint8_t lvl) { return lgfx::color888(0, lvl, 0); });\n"
# DrawRadarSweep's own wedge from main cbd999db594f, before the move: its three constants and its loop, the comments
# and blank lines left out.
MAIN_BODY = (
    "    constexpr float TAIL = 1.0f;   // afterglow span behind the beam, radians (~57 deg)\n"
    "    constexpr int   SEGS = 18;     // wedge slices; more = smoother fade, more fills\n"
    "    constexpr int   PEAK = 120;    // peak trail green, kept under the blips' brightness\n"
    "    const int xLead = cx + (int)(std::cos(angle) * radius);\n"
    "    const int yLead = cy + (int)(std::sin(angle) * radius);\n"
    "    int xPrev = xLead, yPrev = yLead;\n"
    "    for (int i = 1; i <= SEGS; i++) {\n"
    "        const float frac = i / (float)SEGS;\n"
    "        const float a = angle - frac * TAIL;\n"
    "        const int x = cx + (int)(std::cos(a) * radius);\n"
    "        const int y = cy + (int)(std::sin(a) * radius);\n"
    "        const float g = 1.0f - frac;\n"
    "        const uint8_t lvl = (uint8_t)(g * g * PEAK);\n"
    "        buf.fillTriangle(cx, cy, xPrev, yPrev, x, y, lgfx::color888(0, lvl, 0));\n"
    "        xPrev = x;\n"
    "        yPrev = y;\n"
    "    }   // SABOTAGE S-own: main's own wedge restored\n")

# name: (the file planted, the rows that must go red, [(anchor, replacement, whole line?)]). A whole-line edit replaces
# the one line that STARTS with its anchor, so it holds whatever value kTail has (1.0 before L3a, 0.5 after).
SABOTAGES = {
    "S-tail  kTail 0.5, the device's own 1.0": ("SweepWedge.h", ["SW2"], [
        ("constexpr float kTail = ", "constexpr float kTail = 0.5f;   // SABOTAGE S-tail\n", True),
        ("    constexpr float TAIL = kTail;", "    constexpr float TAIL = 1.0f; /* SABOTAGE S-tail */", False)]),
    "S-segs  a slice dropped": ("SweepWedge.h", ["SW1", "SW2"], [
        ("    for (int i = 1; i <= SEGS; i++) {\n", "    for (int i = 1; i < SEGS; i++) {   // SABOTAGE S-segs\n", False)]),
    "S-peak  the device's own peak": ("SweepWedge.h", ["SW1"], [
        ("    constexpr int   PEAK = kPeak;\n", "    constexpr int   PEAK = 121;   // SABOTAGE S-peak\n", False)]),
    "S-own  main's DrawRadarSweep restored": ("DrawHelpers.h", ["SW0"], [(CALL, MAIN_BODY, False)]),
    "S-call  the wedge call dropped": ("DrawHelpers.h", ["SW0"], [
        (CALL, "    // SABOTAGE S-call: the wedge call dropped\n", False)]),
}
PATHS = {"SweepWedge.h": WEDGE, "DrawHelpers.h": HELPERS}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def plant(src, edits, name):
    out = src
    for anchor, new, whole in edits:
        if whole:
            lines = [l for l in out.splitlines(True) if l.startswith(anchor)]
            assert len(lines) == 1, f"{name}: anchor's line found {len(lines)} times"
            out = out.replace(lines[0], new)
        else:
            assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times"
            out = out.replace(anchor, new)
    assert out != src, f"{name}: the sabotage did not apply"
    return out


def run(inc, helpers, td, tag):
    exe = os.path.join(td, f"t-{tag}.exe")
    b = subprocess.run([CXX] + FLAGS + ["-I", inc, "-I", INC, TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        return None, b.stderr
    return subprocess.run([exe, helpers], capture_output=True, text=True), ""


def main():
    before = {p: sha(p) for p in (WEDGE, SWEEP, HELPERS)}
    texts = {k: sc.read(p) for k, p in PATHS.items()}
    sweep = sc.read(SWEEP)
    ok = True
    with tempfile.TemporaryDirectory() as td:
        r, err = run(INC, HELPERS, td, "control")
        if r is None or r.returncode != 0 or len(re.findall(r"^\s*ok\b", r.stdout, re.M)) != OK_COUNT:
            print(f"control (the real headers): did not pass -- the rig is broken\n{err}{r.stdout if r else ''}")
            return 2
        print("control (the real headers): all wedge checks pass")
        for i, (name, (fname, want, edits)) in enumerate(SABOTAGES.items()):
            planted = dict(texts)
            planted[fname] = plant(texts[fname], edits, name)
            inc = os.path.join(td, f"s{i}")
            os.makedirs(inc)
            # WxSweep.h is copied beside the planted SweepWedge.h, so its own #include "SweepWedge.h" finds the plant.
            for fn, text in (("SweepWedge.h", planted["SweepWedge.h"]), ("WxSweep.h", sweep),
                             ("DrawHelpers.h", planted["DrawHelpers.h"])):
                open(os.path.join(inc, fn), "w", encoding="utf-8", newline="\n").write(text)
            r, err = run(inc, os.path.join(inc, "DrawHelpers.h"), td, f"s{i}")
            if r is None:
                print(f"{name}: did not compile (rig)\n{err}")
                return 2
            red = sorted({m.group(1) for l in r.stdout.splitlines() for m in [re.search(r"FAIL\s+\[([\w-]+)\]", l)] if m})
            good = r.returncode == 1 and red == want
            print(f"{name}: exit {r.returncode}; red rows {red or 'none'} (want {want}) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    same = all(sha(p) == s for p, s in before.items())
    print(f"real SweepWedge.h, WxSweep.h, DrawHelpers.h unchanged: {same}")
    return 0 if ok and same else 1


# --control (#510): this driver's unplanted run alone, its suite against its own count, nothing planted. OK_COUNT is
# the one place the count lives: main()'s unplanted run reads it too.
OK_COUNT = 4
CONTROL_TABLE = {"test_sweep_wedge": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        run(INC, HELPERS, td, "control")


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    return [(name, os.path.relpath(PATHS[fname], ROOT), sc.read(PATHS[fname]), a)
            for name, (fname, _, edits) in SABOTAGES.items() for a, _, _ in edits]


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
