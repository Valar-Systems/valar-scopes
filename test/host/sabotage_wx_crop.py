"""Runs test_wx_crop.cpp against SABOTAGED copies of include/WxCrop.h; each must turn the suite red,
failing EXACTLY the checks predicted in docs/weatherscope-stage1.md and nothing else.

    python -I test/host/sabotage_wx_crop.py

The real header is never edited: each sabotage is planted in a copy in a temporary folder, and the test
is built with that folder on its include path. Each plant asserts that every anchor occurs exactly once
and that the copy differs from the original (a sabotage that did not apply proves nothing), and the
real header's sha256 is compared before and after.

  S-A1   no "runs add up to rows x cols" check   -> A2 fails 1 (neg-run-short), A5 fails 50/50
  S-A2   a std::vector allocated on every call   -> A6 counts >= 1 allocation per decode
  S-A3a  no zero-length-run check                -> A3 fails exactly 1 (the appended 00 00)
  S-A3b  no zero-cell-size checks                -> A3 fails exactly 2 (cell height 0, cell width 0)
  S-A3c  no reserved-byte / unknown-flags checks -> A3 fails exactly 2 (reserved byte, flags 0x02)
Exit 0 only when EVERY sabotage turns the suite red in exactly the predicted way.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "include", "WxCrop.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_crop.cpp")
FIXTURES = os.path.join(ROOT, "test", "fixtures", "wx")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

# name: ([(anchor, replacement), ...], [regexes: each must match exactly one FAIL line; no other FAIL lines])
SABOTAGES = {
    "S-A1": ([("    if (p != total) return Status::Underrun;\n",
               "    (void)total;  // SABOTAGE S-A1: the underrun check is gone\n")],
             [r"A2\s+neg-run-short", r"A5\s+0/50 sabotaged twins refused"]),
    "S-A2": ([("#include <string.h>\n", "#include <string.h>\n#include <vector>\n"),
              ("inline Status Validate(const uint8_t* data, size_t len, Header* out)\n{\n",
               "inline Status Validate(const uint8_t* data, size_t len, Header* out)\n{\n"
               "    std::vector<uint8_t> scratch(16, 1);  // SABOTAGE S-A2: an allocation on every call\n"
               "    if (scratch[0] != 1) return Status::TooShort;\n")],
             [r"A6\s+\d+ heap allocations"]),
    "S-A3a": ([("    if (v == 0) return Status::ZeroRun;\n", "    // SABOTAGE S-A3a: zero-length runs accepted\n")],
              [r"A3\s+a zero-length long run"]),
    "S-A3b": ([("    if (h.cellLatMicro == 0) return Status::ZeroCellHeight;\n"
                "    if (h.cellLonMicro == 0) return Status::ZeroCellWidth;\n",
                "    // SABOTAGE S-A3b: zero cell sizes accepted\n")],
              [r"A3\s+cell height 0", r"A3\s+cell width 0"]),
    "S-A3c": ([("    if (data[7] != 0) return Status::ReservedByte;\n"
                "    if (h.flags & 0xFE) return Status::UnknownFlags;\n",
                "    // SABOTAGE S-A3c: reserved byte and unknown flags accepted\n")],
              [r"A3\s+reserved byte", r"A3\s+flags 0x02"]),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def plant(name, edits, src):
    out = src
    for anchor, repl in edits:
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
            open(os.path.join(inc, "WxCrop.h"), "w", encoding="utf-8", newline="\n").write(plant(name, edits, src))
            exe = os.path.join(td, name + ".exe")
            b = subprocess.run([CXX, *FLAGS, "-I" + inc, TEST, "-o", exe], capture_output=True, text=True)
            if b.returncode != 0:
                print(f"{name}: did not compile (rig)\n{b.stderr}")
                return 2
            r = subprocess.run([exe, FIXTURES], capture_output=True, text=True)
            fails = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith("FAIL")]
            matched = [sum(1 for l in fails if re.search(rx, l)) for rx in expect]
            good = r.returncode == 1 and all(m == 1 for m in matched) and len(fails) == len(expect)
            extra = ""
            if name == "S-A2":
                a6 = re.search(r"A6\s+(\d+) heap allocations", r.stdout)
                extra = f" ({a6.group(1) if a6 else '?'} allocations counted)"
            print(f"{name}: suite exit {r.returncode}; {len(fails)} failing check(s){extra} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            for l in fails:
                print(f"    {l}")
            ok &= good
    after = sha(HEADER)
    print(f"real include/WxCrop.h unchanged: {before == after} ({after[:12]})")
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
