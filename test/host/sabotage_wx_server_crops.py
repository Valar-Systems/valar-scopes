"""WS7's sabotage (docs/wx-server.md): one run nibble flipped in ONE server-made crop must fail exactly that
crop -- and its `cells` check, not only its file hash -- while the other seven still pass.

    python -I test/host/sabotage_wx_server_crops.py                          WS7: server/, kmci-80km-partly-outside.wxc
    python -I test/host/sabotage_wx_server_crops.py opuk man-161km.wxc       K12 (docs/wx-opuk.md): the opuk set

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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass and the colour proof (docs/weatherscope-stage3.md A12.50)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]   # --anchors and --control are not a set
SET = _ARGS[0] if _ARGS else "server"
SRC = os.path.join(ROOT, "test", "fixtures", "wx", SET)
TEST = os.path.join(ROOT, "test", "host", "test_wx_server_crops.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
VICTIM = _ARGS[1] if len(_ARGS) > 1 else "kmci-80km-partly-outside.wxc"


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
        print(f"sabotage ({SET}/{VICTIM}, byte 32 ^ 0x10): exit {r.returncode}; {len(fails)} FAIL line(s) -> "
              f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        for l in fails:
            print("    " + l)
        return 0 if good else 1


def anchor_entries():
    """Q51: the victim crops the two runs flip (run.sh's comments: the server set's default, and opuk's), each longer
    than the byte 32 it flips, for --anchors (no plant runs)."""
    out = []
    for s, v in (("server", "kmci-80km-partly-outside.wxc"), ("opuk", "man-161km.wxc")):
        p = os.path.join(ROOT, "test", "fixtures", "wx", s, v)
        try:
            data = open(p, "rb").read()
        except OSError:
            data = None
        def check(b):
            assert len(b) > 32, f"{len(b)} bytes, the plant flips byte 32"
        out.append((f"S-SC ({s}/{v})", os.path.relpath(p, ROOT), data, check))
    return out

# --control (Q57, docs/weatherscope-stage3.md A12.61): this driver's unplanted run alone, each suite against its own
# count, nothing planted. run.sh runs it for every driver.
CONTROL_TABLE = {"test_wx_server_crops": ("ok", 16)}


def control():
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, "t.exe")
        subprocess.run([CXX, *FLAGS, "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe], capture_output=True, text=True)
        subprocess.run([exe, SRC], capture_output=True, text=True)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
