"""Step C's sabotages (docs/blipscope-map-client-steps.md, "Step C: the wedge's green added to the map"): six plants in a
COPY of include/MapSweep.h, each of which must turn red exactly its frozen set of test_map_sweep.cpp's rows, and nothing
else. WxSweep.h is never planted (it is the weather's, WD6).

    python -I test/host/sabotage_map_sweep.py [--anchors | --control]   exit 0 = as predicted, 1 = not, 2 = the rig

  S-CS-NOSAT    the add without its cap at 63: a high green spills into red      -> CS2, CS5-once
  S-CS-RED      the level added to red instead of green                          -> CS1, CS2, CS3, CS5-once
  S-CS-OPAQUE   Green565 instead of the add (step B's wedge, erasing the map)    -> CS5-once, CS6
  S-CS-DOUBLE   each marked pixel blended twice                                  -> CS3, CS5-once
  S-CS-NOCLEAR  the marks not cleared after the blend (stale marks are blended   -> CS5-once, CS5-clean
                by no one and counted by the next frame)
  S-CS-NOCODE   the weather's code ignored: a drawn code gets the add            -> CS4-A, CS4-B
The real header is never edited: the copy goes into a scratch include directory that comes first on the path, the plant
asserts its anchor occurs once and the copy differs, and the real header's sha256 is compared before and after.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INC = os.path.join(ROOT, "include")
HEADER = "MapSweep.h"
FIXTURES = os.path.join(ROOT, "test", "fixtures", "wx")
TEST = os.path.join(ROOT, "test", "host", "test_map_sweep.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
FAIL_ID = re.compile(r"^\s*FAIL\s+(\S+)")

ADD_G = "    const unsigned g = ((px >> 5) & 0x3Fu) + ((unsigned)lvl >> 2);\n"
ADD_RET = "    return (uint16_t)((px & 0xF81Fu) | ((g > 63u ? 63u : g) << 5));\n"
ONE = "    set(x, y, wxavionics::Drawn(c) ? wxsweep::Lit565(c, pct, lvl) : Add565(get(x, y), lvl));\n"
VISIT = "            detail::BlendOne(pct, row[x], x, y, set, get, code);\n"
CLEAR = "            row[x] = 0;\n"
SABOTAGES = {
    "S-CS-NOSAT": ([(ADD_RET, "    return (uint16_t)((px & 0xF81Fu) | (g << 5));\n")], {"CS2", "CS5-once"}),
    "S-CS-RED": ([(ADD_G + ADD_RET, "    const unsigned r = ((px >> 11) & 0x1Fu) + ((unsigned)lvl >> 2);\n"
                                    "    return (uint16_t)((px & 0x07FFu) | ((r > 31u ? 31u : r) << 11));\n")],
                 {"CS1", "CS2", "CS3", "CS5-once"}),
    "S-CS-OPAQUE": ([(ONE, "    set(x, y, wxavionics::Drawn(c) ? wxsweep::Lit565(c, pct, lvl) : ((void)get(x, y), "
                           "wxsweep::Green565(lvl)));\n")], {"CS5-once", "CS6"}),
    "S-CS-DOUBLE": ([(VISIT, VISIT + VISIT)], {"CS3", "CS5-once"}),
    "S-CS-NOCLEAR": ([(CLEAR, "")], {"CS5-once", "CS5-clean"}),
    "S-CS-NOCODE": ([(ONE, "    set(x, y, ((void)c, (void)pct, Add565(get(x, y), lvl)));\n")], {"CS4-A", "CS4-B"}),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def source():
    return open(os.path.join(INC, HEADER), encoding="utf-8").read().replace("\r\n", "\n")


def build_and_run(td, name, text, rigs):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, HEADER), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + "-sweep.exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + INC, "-I" + os.path.join(ROOT, "src", "weatherscope"), TEST,
                        "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: test_map_sweep did not compile (rig)\n{b.stderr}")
        rigs.add(name)
        return None
    return subprocess.run([exe, FIXTURES], capture_output=True, text=True)


def main():
    before = sha(os.path.join(INC, HEADER))
    src = source()
    ok = True
    rigs = sc.Rigs()
    with tempfile.TemporaryDirectory() as td:
        r = build_and_run(td, "control", src, rigs)
        if r is None:
            return 2
        good = r.returncode == 0 and not [l for l in r.stdout.splitlines() if FAIL_ID.match(l)]
        print(f"control (the real header): exit {r.returncode} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        for name, (edits, want) in SABOTAGES.items():
            if rigs.forced(name):
                ok = False
                continue
            text = src
            for anchor, repl in edits:
                assert text.count(anchor) == 1, f"{name}: anchor found {text.count(anchor)} times"
                text = text.replace(anchor, repl)
            assert text != src, f"{name}: the copy is identical -- the sabotage did not apply"
            r = build_and_run(td, name, text, rigs)
            if r is None:
                ok = False
                continue
            fails = sorted(m.group(1) for l in r.stdout.splitlines() for m in [FAIL_ID.match(l)] if m)
            g = r.returncode == 1 and fails == sorted(want)
            print(f"{name}: failing {fails} (want {sorted(want)}) -> {'AS PREDICTED' if g else 'OFF-PREDICTION'}")
            ok &= g
    after = sha(os.path.join(INC, HEADER))
    print(f"real header unchanged: {before == after} ({after[:12]})")
    print(f"{len(SABOTAGES)} sabotages: " + ("ALL AS PREDICTED" if ok and before == after else "OFF-PREDICTION: STOP"))
    return rigs.close(0 if ok and before == after else 1)


def anchor_entries():
    src = sc.read(os.path.join(INC, HEADER))
    return [(name, "include/" + HEADER, src, anchor) for name, (edits, _) in SABOTAGES.items() for anchor, _ in edits]


# --control: the suite unplanted, against its own count (CS1-CS3, CS4-A/-B, CS5 x 3, CS6). The one place it lives.
SWEEP_CHECKS = 9
CONTROL_TABLE = {"test_map_sweep": ("checks", SWEEP_CHECKS)}


def control():
    with tempfile.TemporaryDirectory() as td:
        build_and_run(td, "control", source(), sc.Rigs())


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
