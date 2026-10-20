"""Runs test_wx_crop.cpp against two SABOTAGED copies of include/WxCrop.h; each must turn the suite red.

    python -I test/host/sabotage_wx_crop.py

The real header is never edited: each sabotage is planted in a copy in a temporary folder, and the test
is built with that folder on its include path. Each plant asserts that its anchor occurs exactly once
and that the copy differs from the original (a sabotage that did not apply proves nothing), and the
real header's sha256 is compared before and after.

  S-A1  the decoder skips the "runs add up to rows x cols" check.
        Predicted (docs/weatherscope-stage1.md): A2 fails 1 (neg-run-short accepted), A5 fails 50/50.
  S-A2  the decoder allocates a std::vector on every call.
        Predicted: A6 reads >= 1 allocation per decode.
Exit 0 only when BOTH sabotages turn the suite red in exactly the predicted way.
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

SABOTAGES = {
    "S-A1": ("    if (p != total) return Status::Underrun;\n",
             "    (void)total;  // SABOTAGE S-A1: the underrun check is gone\n"),
    "S-A2": ("inline Status Validate(const uint8_t* data, size_t len, Header* out)\n{\n",
             "inline Status Validate(const uint8_t* data, size_t len, Header* out)\n{\n"
             "    std::vector<uint8_t> scratch(16, 1);  // SABOTAGE S-A2: an allocation on every call\n"
             "    if (scratch[0] != 1) return Status::TooShort;\n"),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def run(name, anchor, repl, src, td):
    assert src.count(anchor) == 1, f"{name}: anchor found {src.count(anchor)} times, need exactly 1"
    planted = src.replace(anchor, repl)
    if name == "S-A2":
        planted = planted.replace("#include <string.h>\n", "#include <string.h>\n#include <vector>\n", 1)
    assert planted != src, f"{name}: the copy is identical to the original -- the sabotage did not apply"
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, "WxCrop.h"), "w", encoding="utf-8", newline="\n").write(planted)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (rig)\n{b.stderr}")
        return None
    r = subprocess.run([exe, FIXTURES], capture_output=True, text=True)
    return r.returncode, r.stdout


def main():
    before = sha(HEADER)
    src = open(HEADER, encoding="utf-8").read()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        for name, (anchor, repl) in SABOTAGES.items():
            res = run(name, anchor, repl, src, td)
            if res is None:
                return 2
            rc, out = res
            fails = [l.strip() for l in out.splitlines() if l.strip().startswith("FAIL")]
            print(f"{name}: suite exit {rc}; {len(fails)} failing check(s)")
            for l in fails:
                print(f"    {l}")
            if name == "S-A1":
                a2 = [l for l in fails if " A2 " in f" {l} "]
                a5 = re.search(r"A5\s+(\d+)/(\d+) sabotaged twins refused", out)
                good = rc == 1 and len(a2) == 1 and "neg-run-short" in a2[0] and a5 and a5.group(1) == "0" \
                    and all((" A2 " in f" {l} ") or (" A5 " in f" {l} ") for l in fails)
                print(f"    predicted: A2 fails 1 (neg-run-short), A5 fails 50/50 -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            else:
                a6 = re.search(r"A6\s+(\d+) heap allocations", out)
                n = int(a6.group(1)) if a6 else -1
                good = rc == 1 and n >= 1 and all(" A6 " in f" {l} " for l in fails)
                print(f"    predicted: A6 >= 1 allocation per decode -> {n} counted -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = sha(HEADER)
    print(f"real include/WxCrop.h unchanged: {before == after} ({after[:12]})")
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
