"""WD6's sabotage (docs/wx-device.md): plant S-w -- the opaque wedge restored -- into a COPY of include/WxSweep.h and
require exactly test_wx_sweep.cpp's [W3-in] row to go red, every other row staying green. The real header is never
written; its sha256 is compared before and after.

    python -I test/host/sabotage_wx_sweep.py      exit 0 = as predicted, 1 = not, 2 = rig
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "include", "WxSweep.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_sweep.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "S-w  the opaque wedge restored": (["W3-in"], [(
        "            if (look == Look::SeeThrough) set(x, y, SeeThrough565(get(x, y), lvl));\n"
        "            else set(x, y, Lit565(code(x, y), pct, lvl));\n",
        "            (void)look; (void)pct; (void)get; (void)code;\n"
        "            set(x, y, Green565(lvl));   // SABOTAGE S-w: today's opaque wedge\n")]),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def run(inc, td, tag):
    exe = os.path.join(td, f"t-{tag}.exe")
    b = subprocess.run([CXX] + FLAGS + ["-I", inc, "-I", os.path.join(ROOT, "include"), TEST, "-o", exe],
                       capture_output=True, text=True)
    if b.returncode != 0:
        return None, b.stderr
    return subprocess.run([exe], capture_output=True, text=True), ""


def main():
    before = sha(HEADER)
    src = open(HEADER, encoding="utf-8").read().replace("\r\n", "\n")
    ok = True
    with tempfile.TemporaryDirectory() as td:
        r, err = run(os.path.join(ROOT, "include"), td, "control")
        if r is None or r.returncode != 0:
            print(f"control (the real header): did not pass -- the rig is broken\n{err}{r.stdout if r else ''}")
            return 2
        print("control (the real header): all sweep checks pass")
        for i, (name, (want, edits)) in enumerate(SABOTAGES.items()):
            out = src
            for old, new in edits:
                assert out.count(old) == 1, f"{name}: anchor found {out.count(old)} times"
                out = out.replace(old, new)
            assert out != src, f"{name}: the sabotage did not apply"
            inc = os.path.join(td, f"s{i}")
            os.makedirs(inc)
            open(os.path.join(inc, "WxSweep.h"), "w", encoding="utf-8", newline="\n").write(out)
            r, err = run(inc, td, f"s{i}")
            if r is None:
                print(f"{name}: did not compile (rig)\n{err}")
                return 2
            red = sorted({m.group(1) for l in r.stdout.splitlines() for m in [re.search(r"FAIL\s+\[([\w-]+)\]", l)] if m})
            good = r.returncode == 1 and red == want
            print(f"{name}: exit {r.returncode}; red rows {red or 'none'} (want {want}) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = sha(HEADER)
    print(f"real include/WxSweep.h unchanged: {before == after} ({after[:12]})")
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
