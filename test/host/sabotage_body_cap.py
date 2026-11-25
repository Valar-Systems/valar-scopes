"""The exact-cap boundary's plants (include/BodyCap.h; bodycap_registration.txt T2): plant each into a COPY of
include/BodyCap.h and require exactly its predicted rows of test_body_cap.cpp to go red, every other row staying green.
The real header is never written; its sha256 is compared before and after.
P1 is the red first, the one a review would miss: `>` made `>=` refuses a body of exactly cap bytes -- a package at its
cap, which the server builds by construction -- on the Content-Length path. P2 is its chunked twin, the cut one byte
early.

    python -I test/host/sabotage_body_cap.py      exit 0 = as predicted, 1 = not, 2 = rig
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
CAP = os.path.join(INC, "BodyCap.h")
TEST = os.path.join(ROOT, "test", "host", "test_body_cap.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

# name: (the rows that must go red, [(anchor, replacement)]), each anchor found exactly once in BodyCap.h.
SABOTAGES = {
    "P1  Fits refuses exactly cap (> made >=)": (["BC-CL211-E", "BC-CL64-E"], [
        ("inline bool Fits(size_t declared, size_t cap) { return !(declared > cap); }",
         "inline bool Fits(size_t declared, size_t cap) { return !(declared >= cap); }   // SABOTAGE P1")]),
    "P2  the chunked cut one byte early (cap - 1)": (["BC-CH211-E", "BC-CH64-E"], [
        ("        if (*n == cap) return true;   // byte cap + 1: refused\n",
         "        if (*n == cap - 1) return true;   // SABOTAGE P2\n")]),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def plant(src, edits, name):
    out = src
    for anchor, new in edits:
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times"
        out = out.replace(anchor, new)
    assert out != src, f"{name}: the sabotage did not apply"
    return out


def run(inc, td, tag):
    exe = os.path.join(td, f"t-{tag}.exe")
    b = subprocess.run([CXX] + FLAGS + ["-I", inc, TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        return None, b.stderr
    return subprocess.run([exe], capture_output=True, text=True), ""


def main():
    before = sha(CAP)
    text = sc.read(CAP)
    ok = True
    with tempfile.TemporaryDirectory() as td:
        r, err = run(INC, td, "control")
        if r is None or r.returncode != 0 or len(re.findall(r"^\s*ok\b", r.stdout, re.M)) != OK_COUNT:
            print(f"control (the real header): did not pass -- the rig is broken\n{err}{r.stdout if r else ''}")
            return 2
        print(f"control (the real header): all {OK_COUNT} rows pass")
        for i, (name, (want, edits)) in enumerate(SABOTAGES.items()):
            inc = os.path.join(td, f"s{i}")
            os.makedirs(inc)
            open(os.path.join(inc, "BodyCap.h"), "w", encoding="utf-8", newline="\n").write(plant(text, edits, name))
            r, err = run(inc, td, f"s{i}")
            if r is None:
                print(f"{name}: did not compile (rig)\n{err}")
                return 2
            red = sorted({m.group(1) for l in r.stdout.splitlines() for m in [re.search(r"FAIL\s+\[([\w-]+)\]", l)] if m})
            good = r.returncode == 1 and red == want
            print(f"{name}: exit {r.returncode}; red rows {red or 'none'} (want {want}) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    same = sha(CAP) == before
    print(f"real BodyCap.h unchanged: {same}")
    return 0 if ok and same else 1


# --control (#510): this driver's unplanted run alone, its suite against its own count, nothing planted. OK_COUNT is
# the one place the count lives: main()'s unplanted run reads it too.
OK_COUNT = 12
CONTROL_TABLE = {"test_body_cap": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        run(INC, td, "control")


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    return [(name, os.path.relpath(CAP, ROOT), sc.read(CAP), a)
            for name, (_, edits) in SABOTAGES.items() for a, _ in edits]


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
