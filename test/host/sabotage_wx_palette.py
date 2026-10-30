"""Runs test_wx_palette.cpp against SABOTAGED copies of src/weatherscope/WxPalette.h; each must turn the suite
red, failing EXACTLY the checks predicted in docs/weatherscope-stage2.md (addenda 2, 4, 5 and 8), re-registered for
code 1's teal by docs/weatherscope-stage3.md A10.16, and nothing else.

    python -I test/host/sabotage_wx_palette.py

The real header is never edited: each sabotage is planted in a copy in a temporary folder, the test is built
with that folder on its include path, every anchor must occur exactly once, the copy must differ, and the
real header's sha256 is compared before and after.

  S-C3a   the missing hatch planted as Stage 1's violet (110, 0, 110) -> exactly C3a
  S-C3c   one rain entry off the table (25 dBZ green 200->204)        -> exactly C3c (Q52: 201 shared 200's RGB565)
  S-C3ef  the snow top set to white                                -> exactly C3e, C3f and C3i
  S-C3g   the snow start set to (0, 140, 140), near rain 1's teal  -> exactly C3g (A10.16: the old plant,
          (20, 30, 180) near the old dimmed blue, would now fail nothing)
  S-h     the snow ramp started at (165, 205, 245), near its top    -> exactly C3h (it barely steps)
  S-i     the snow top set to (240, 245, 255), near white          -> exactly C3i
  S-C3j   rain 12 (violet) set to (45, 45, 45), near no coverage    -> exactly C3c and C3j
  S-TEAL  rain 1 back to the old dimmed blue (0, 0, 135)            -> exactly C3d
  S-C3k   rain 1 set near the grey, (40, 46, 44)                    -> exactly C3d and C3k
  S-C3l   rain 1 brighter than rain 2, (200, 255, 230)              -> exactly C3d and C3l
  S-PIN   the REAL header, run on a fixtures copy whose pinned table has an id appended -> exactly pin
Exit 0 only when EVERY sabotage turns the suite red in exactly the predicted way.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass and the colour proof (docs/weatherscope-stage3.md A12.50)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "src", "weatherscope", "WxPalette.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_palette.cpp")
FIXTURES = os.path.join(ROOT, "test", "fixtures", "wx")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "S-C3a": ([("constexpr Rgb kMissingHatch = {72, 72, 72};", "constexpr Rgb kMissingHatch = {110, 0, 110};  // SABOTAGE")],
              [r"C3a\b"]),
    "S-C3c": ([("{0, 255, 0},   {0, 200, 0},", "{0, 255, 0},   {0, 204, 0},  /* SABOTAGE */")],
              [r"C3c\b"]),
    "S-C3ef": ([("{148, 190, 242}, {170, 212, 250}};", "{148, 190, 242}, {255, 255, 255}};  // SABOTAGE")],
               [r"C3e\b", r"C3f\b", r"C3i\b"]),
    "S-C3g": ([("kSnowTable[6] = {{0, 110, 210},", "kSnowTable[6] = {{0, 140, 140},  /* SABOTAGE */")],
              [r"C3g\b"]),
    "S-h": ([("constexpr Rgb kSnowTable[6] = {{0, 110, 210}, {66, 129, 218}, {98, 149, 226}, {124, 169, 234}, {148, 190, 242}, {170, 212, 250}};",
              "constexpr Rgb kSnowTable[6] = {{165, 205, 245}, {166, 206, 246}, {167, 208, 247}, {168, 209, 248}, "
              "{169, 211, 249}, {170, 212, 250}};  // SABOTAGE: starts near the top, barely steps")],
            [r"C3h\b"]),
    "S-C3j": ([("{255, 0, 255}, {153, 85, 201}};", "{255, 0, 255}, {45, 45, 45}};  // SABOTAGE")],
              [r"C3c\b", r"C3j\b"]),
    "S-i": ([("{148, 190, 242}, {170, 212, 250}};", "{148, 190, 242}, {240, 245, 255}};  // SABOTAGE: near white")],
            [r"C3i\b"]),
    "S-TEAL": ([("constexpr Rgb kRain1 = {0, 150, 135};", "constexpr Rgb kRain1 = {0, 0, 135};  // SABOTAGE: the old dimmed blue")],
               [r"C3d\b"]),
    "S-C3k": ([("constexpr Rgb kRain1 = {0, 150, 135};", "constexpr Rgb kRain1 = {40, 46, 44};  // SABOTAGE: near the grey")],
              [r"C3d\b", r"C3k\b"]),
    "S-C3l": ([("constexpr Rgb kRain1 = {0, 150, 135};", "constexpr Rgb kRain1 = {200, 255, 230};  // SABOTAGE: brighter than rain 2")],
              [r"C3d\b", r"C3l\b"]),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def plant(name, edits, src):
    out = src
    for anchor, repl in edits:
        sc.prove_colour(name, anchor, repl)   # Q50: refuses a colour the panel cannot see
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical to the original -- the sabotage did not apply"
    return out


def main():
    before = sha(HEADER)
    src = open(HEADER, encoding="utf-8").read()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        for name, (edits, expect) in SABOTAGES.items():
            inc = os.path.join(td, name)
            os.makedirs(inc)
            try:
                planted = plant(name, edits, src)
            except sc.Refused as e:   # Q50: not a result, and not as predicted
                print(f"{name}: REFUSED -- {e}")
                ok = False
                continue
            open(os.path.join(inc, "WxPalette.h"), "w", encoding="utf-8", newline="\n").write(planted)
            exe = os.path.join(td, name + ".exe")
            b = subprocess.run([CXX, *FLAGS, "-I" + inc, TEST, "-o", exe], capture_output=True, text=True)
            if b.returncode != 0:
                print(f"{name}: did not compile (rig)\n{b.stderr}")
                return 2
            r = subprocess.run([exe, FIXTURES], capture_output=True, text=True)
            fails = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith("FAIL")]
            matched = [sum(1 for l in fails if re.search(rx, l)) for rx in expect]
            good = r.returncode == 1 and all(m == 1 for m in matched) and len(fails) == len(expect)
            print(f"{name}: suite exit {r.returncode}; {len(fails)} failing check(s) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            for l in fails:
                print(f"    {l}")
            ok &= good
        # S-PIN (#439 review): the table, not the header, is sabotaged. The real header is built; the pinned file
        # in a copied fixtures folder gets an id appended, which leaves the 15 parsed entries unchanged.
        fx = os.path.join(td, "fixtures-pin")
        os.makedirs(fx)
        name = "NWSReflectivity-metpy-v1.6.3.tbl"
        data = open(os.path.join(FIXTURES, name), "rb").read()
        # EXAMPLE -- not a real device: the allowlisted synthetic id (scripts/device-id-allowlist.txt).
        open(os.path.join(fx, name), "wb").write(data + b"beefbeefbeefbeef\n")
        assert open(os.path.join(fx, name), "rb").read() != data, "S-PIN: the table copy did not change"
        exe = os.path.join(td, "real.exe")
        b = subprocess.run([CXX, *FLAGS, "-I" + os.path.dirname(HEADER), TEST, "-o", exe], capture_output=True, text=True)
        if b.returncode != 0:
            print(f"S-PIN: did not compile (rig)\n{b.stderr}")
            return 2
        r = subprocess.run([exe, fx], capture_output=True, text=True)
        fails = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith("FAIL")]
        good = r.returncode == 1 and len(fails) == 1 and re.search(r"FAIL\s+pin\b", fails[0]) is not None
        print(f"S-PIN: suite exit {r.returncode}; {len(fails)} failing check(s) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        for l in fails:
            print(f"    {l}")
        ok &= good
    after = sha(HEADER)
    print(f"real src/weatherscope/WxPalette.h unchanged: {before == after} ({after[:12]})")
    return 0 if ok and before == after else 1


def anchor_entries():
    """Q51: every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    src = sc.read(HEADER)
    out = [(name, os.path.relpath(HEADER, ROOT), src, a) for name, entry in SABOTAGES.items() for a, _ in entry[0]]
    pin = os.path.join(FIXTURES, "NWSReflectivity-metpy-v1.6.3.tbl")   # S-PIN plants a copy of the pinned table
    out.append(("S-PIN", os.path.relpath(pin, ROOT), sc.read(pin), lambda t: None))
    return out


if __name__ == "__main__":
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
