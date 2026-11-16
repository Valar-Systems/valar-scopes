"""Step C's scope addition, its sabotages (docs/blipscope-map-client-steps.md, "Step C's scope addition: airportsGen moves
only when the airports CONTENT changes", and its amendment): seven plants in a COPY of include/MapAirportsKey.h, each of
which must turn red exactly its registered set of test_map_airports.cpp's cases, no fewer and no more. Every set was read
node by node against the rows before any code ran.

  S-AP-NOLAT    a row's lat bits dropped from the hash: {AP-LAT}
  S-AP-NOLON    a row's lon bits dropped: {AP-LON}
  S-AP-NOCODE   a row's code bytes dropped: {AP-CODE}
  S-AP-NOKIND   a row's kind dropped: {AP-KIND}
  S-AP-NOORDER  each row hashed alone and the rows combined by XOR, so the order is lost: {AP-ORDER}
  S-AP-EMPTYEQ  kEmptyHash defined as 0u, no longer the empty list's hash: {AP-EMPTYEQ}
  S-AP-BOOT     Held's default h is 0, not kEmptyHash: {AP-BOOT}

No plant drops the length: appending a row changes the bytes hashed anyway, so it could not change the result (AP-COUNT
stays a control). Method as sabotage_map_compose.py: include/ is copied into a temporary tree, the plant goes into the
copy only, after asserting each anchor occurs exactly once and that the copy changed; the real file's sha256 is compared
before and after; the unplanted copy runs first.
    python -I test/host/sabotage_map_airports.py [--anchors | --control]   exit 0 = as predicted, 1 = not, 2 = the rig"""
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
TEST = os.path.join(ROOT, "test", "host", "test_map_airports.cpp")
KEY = os.path.join("include", "MapAirportsKey.h")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

CASES = 13   # the suite's count: the one place it lives, read by the run path and the --control table

LAT_AT = "        h = MixWord(h, FloatBits(r.lat));\n"
LON_AT = "        h = MixWord(h, FloatBits(r.lon));\n"
CODE_AT = "        for (int c = 0; c < 5; ++c) h = MixByte(h, (uint8_t)r.code[c]);\n"
KIND_AT = "        h = MixByte(h, (uint8_t)r.kind);\n"
LOOP_AT = ("    for (size_t i = 0; i < n; ++i) {\n"
           "        const Row& r = rows[i];\n" + LAT_AT + LON_AT + CODE_AT + KIND_AT +
           "    }\n"
           "    return h;\n")
XOR_LOOP = ("    uint32_t x = 0;   // the plant: each row hashed alone, the rows combined by XOR, so the order is lost\n"
            "    for (size_t i = 0; i < n; ++i) {\n"
            "        const Row& r = rows[i];\n"
            "        uint32_t e = kFnvBasis;\n"
            "        e = MixWord(e, FloatBits(r.lat));\n"
            "        e = MixWord(e, FloatBits(r.lon));\n"
            "        for (int c = 0; c < 5; ++c) e = MixByte(e, (uint8_t)r.code[c]);\n"
            "        e = MixByte(e, (uint8_t)r.kind);\n"
            "        x ^= e;\n"
            "    }\n"
            "    return h ^ x;\n")
EMPTY_AT = ("constexpr uint32_t kEmptyHash = (uint32_t)((uint32_t)((uint32_t)((uint32_t)(kFnvBasis * kFnvPrime) * kFnvPrime) *\n"
            "                                                      kFnvPrime) * kFnvPrime);\n")
BOOT_AT = "    uint32_t h = kEmptyHash;\n"

SABOTAGES = {
    "S-AP-NOLAT": [(LAT_AT, "")],
    "S-AP-NOLON": [(LON_AT, "")],
    "S-AP-NOCODE": [(CODE_AT, "")],
    "S-AP-NOKIND": [(KIND_AT, "")],
    "S-AP-NOORDER": [(LOOP_AT, XOR_LOOP)],
    "S-AP-EMPTYEQ": [(EMPTY_AT, "constexpr uint32_t kEmptyHash = 0u;   // the plant: not the empty list's hash\n")],
    "S-AP-BOOT": [(BOOT_AT, "    uint32_t h = 0;   // the plant: the boot value is not the empty list's hash\n")],
}
WANT = {
    "S-AP-NOLAT": {"AP-LAT"},
    "S-AP-NOLON": {"AP-LON"},
    "S-AP-NOCODE": {"AP-CODE"},
    "S-AP-NOKIND": {"AP-KIND"},
    "S-AP-NOORDER": {"AP-ORDER"},
    "S-AP-EMPTYEQ": {"AP-EMPTYEQ"},
    "S-AP-BOOT": {"AP-BOOT"},
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def make_tree(td, tag, edits=()):
    tree = os.path.join(td, tag)
    shutil.copytree(os.path.join(ROOT, "include"), os.path.join(tree, "include"))
    if edits:
        p = os.path.join(tree, KEY)
        src = open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")
        out = src
        for old, new in edits:
            assert out.count(old) == 1, f"{tag}: the anchor occurs {out.count(old)} times, need exactly 1"
            out = out.replace(old, new)
        assert out != src, f"{tag}: the sabotage did not apply"
        open(p, "w", encoding="utf-8", newline="\n").write(out)
    return tree


def run_suite(tree, td, tag):
    """(red row names, the case count, None) or (None, None, why)."""
    exe = os.path.join(td, f"a-{tag}.exe")
    b = subprocess.run([CXX] + FLAGS + ["-I", os.path.join(tree, "include"), TEST, "-o", exe], capture_output=True,
                       text=True)
    if b.returncode != 0:
        return None, None, "did not compile:\n" + b.stderr[-2000:]
    r = subprocess.run([exe], capture_output=True, text=True)
    m = sc.COUNT.search(r.stdout)
    if r.returncode not in (0, 1) or not m:
        return None, None, f"the suite did not run (exit {r.returncode})\n" + r.stdout[-1500:]
    red = {mm.group(1) for mm in re.finditer(r"^\s*FAIL\s+(AP-[A-Z]+)\s*$", r.stdout, re.M)}
    return red, int(m.group(1)), None


def main():
    real = os.path.join(ROOT, KEY)
    before = sha(real)
    rigs = sc.Rigs()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        red, n, why = run_suite(make_tree(td, "control"), td, "control")
        if why or red or n != CASES:
            print(f"CONTROL (the real files): not clean -- the rig. {why or ''} red {sorted(red or [])}, {n} cases")
            return 2
        print(f"control (the real files): {n} cases, all pass")
        for i, (name, edits) in enumerate(SABOTAGES.items()):
            def one():
                red, n, why = run_suite(make_tree(td, f"s{i}", edits), td, f"s{i}")
                if why:
                    print(f"{name}: {why}")
                    raise sc.Rig()
                return red, n
            got = rigs.run(name, one)
            if got is None:
                ok = False
                continue
            red, n = got
            want = WANT[name]
            good = red == want and n == CASES
            extra, missing = sorted(red - want), sorted(want - red)
            print(f"{name}: {len(red)} red (want {len(want)}); extra {extra or 'none'}; missing {missing or 'none'}"
                  f" -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    same = before == sha(real)
    print(f"real files unchanged: {same}")
    print(f"MAP AIRPORTS SABOTAGES ({len(SABOTAGES)}) AS PREDICTED" if ok and same else
          "MAP AIRPORTS SABOTAGES: NOT AS PREDICTED")
    return rigs.close(0 if ok and same else 1)


CONTROL_TABLE = {"test_map_airports": ("checks", CASES)}


def control():
    with tempfile.TemporaryDirectory() as td:
        run_suite(make_tree(td, "control"), td, "control")


def anchor_entries():
    """Every plant's anchor in the real file, for run.sh's --anchors pass (no plant runs)."""
    text = sc.read(os.path.join(ROOT, KEY))
    return [(name, KEY, text, old) for name, edits in SABOTAGES.items() for old, _ in edits]


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
