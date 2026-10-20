"""WS7's sabotage (docs/wx-server.md): one run nibble flipped in ONE server-made crop must fail exactly that
crop -- and its `cells` check, not only its file hash -- while the other seven still pass.

    python -I test/host/sabotage_wx_server_crops.py

The committed fixtures are never edited: they are copied to a temporary folder and one byte of one copy is
changed (the code nibble of its first run). The plant asserts the copy differs from the original.
Exit 0 only when the suite turns red in exactly the predicted way.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "test", "fixtures", "wx", "server")
TEST = os.path.join(ROOT, "test", "host", "test_wx_server_crops.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
VICTIM = "kmci-80km-partly-outside.wxc"


def main():
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, "t.exe")
        b = subprocess.run([CXX, *FLAGS, "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe], capture_output=True, text=True)
        if b.returncode != 0:
            print("did not compile (rig)\n" + b.stderr)
            return 2
        clean = subprocess.run([exe, SRC], capture_output=True, text=True)
        print(f"control (committed fixtures): exit {clean.returncode}")
        fx = os.path.join(td, "fx")
        shutil.copytree(SRC, fx)
        p = os.path.join(fx, VICTIM)
        before = open(p, "rb").read()
        data = bytearray(before)
        data[32] ^= 0x10                     # the first run's code nibble
        open(p, "wb").write(bytes(data))
        assert open(p, "rb").read() != before, "the sabotage did not apply"
        r = subprocess.run([exe, fx], capture_output=True, text=True)
        fails = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith("FAIL")]
        only_victim = bool(fails) and all(VICTIM in l for l in fails)
        cells_failed = any(re.match(r"FAIL\s+cells\s+" + re.escape(VICTIM), l) for l in fails)
        good = clean.returncode == 0 and r.returncode == 1 and only_victim and cells_failed
        print(f"sabotage ({VICTIM}, byte 32 ^ 0x10): exit {r.returncode}; {len(fails)} FAIL line(s) -> "
              f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        for l in fails:
            print("    " + l)
        return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main())
