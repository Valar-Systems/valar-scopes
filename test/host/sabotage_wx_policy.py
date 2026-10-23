"""WD3's sabotages (docs/wx-device.md): each plants one break into a COPY of include/WxPolicy.h, and the
policy test must turn red on EXACTLY that break's property -- every FAIL line tagged with it, none other.

    python -I test/host/sabotage_wx_policy.py

The real header is never edited. Each plant asserts its anchors occur exactly once and that the copy differs
from the original (a sabotage that did not apply proves nothing); the real header's sha256 is compared before
and after. Property (d) has two guards (OnQuery clears the ETag; IfNoneMatch also checks the query), so its
sabotage removes both: "the ETag kept across a query change" -- kept AND sent.
Exit 0 only when every sabotage turns exactly its own property red.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "include", "WxPolicy.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_policy.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "S-a  no enabled check": ("a", [("    if (!enabled) return false;                           // (a)\n",
                                     "    (void)enabled;   // SABOTAGE S-a: the enabled check is gone\n")]),
    "S-b1 no size cap": ("b", [("    if (bytes > kMaxBytes) return Outcome::Kept;          // (b) over the cap\n",
                                "    (void)bytes;   // SABOTAGE S-b1: the cap is gone\n")]),
    "S-b2 an older epoch accepted": ("b", [("    if (s.haveCrop && frameEpoch < s.frameEpoch) return Outcome::Kept;   // (b) never backwards\n", "")]),
    "S-c1 no age check": ("c", [("    if (nowEpoch > s.frameEpoch && nowEpoch - s.frameEpoch > kMaxAgeS) return Show::Nothing;   // (c)\n",
                                 "    (void)nowEpoch;   // SABOTAGE S-c1: the age check is gone\n")]),
    "S-c2 an unset clock read as epoch 0": ("c", [("    if (!clockSet) return Show::Nothing;                  // (c) never without a set clock\n",
                                                   "    (void)clockSet;   // SABOTAGE S-c2: an unset clock is trusted\n")]),
    "S-d  the ETag kept and sent across a query change": ("d", [
        ("    s.etag[0] = 0;                                        // (d) and so did its ETag\n", ""),
        ("    if (s.query != q) return nullptr;                     // (d) never an ETag earned by another query\n",
         "    (void)q;   // SABOTAGE S-d: any query's ETag is sent\n")]),
    "S-e  no backoff": ("e", [("        Backoff(s);\n        return Outcome::Error;\n", "        return Outcome::Error;\n")]),
    "S-f  all-14 shown as dry": ("f", [("    if (s.allNoCoverage) return Show::OutsideCoverage;    // (f) the note, never an empty overlay\n", "")]),
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


def run(inc, td, tag):
    exe = os.path.join(td, tag + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        return None, b.stderr
    r = subprocess.run([exe], capture_output=True, text=True)
    return r, ""


def main():
    before = sha(HEADER)
    src = open(HEADER, encoding="utf-8").read().replace("\r\n", "\n")
    ok = True
    with tempfile.TemporaryDirectory() as td:
        r, err = run(os.path.join(ROOT, "include"), td, "control")
        if r is None or r.returncode != 0:
            print(f"control (the real header): did not pass -- the rig is broken\n{err}{r.stdout if r else ''}")
            return 2
        print("control (the real header): all policy checks pass")
        for i, (name, (prop, edits)) in enumerate(SABOTAGES.items()):
            inc = os.path.join(td, f"s{i}")
            os.makedirs(inc)
            open(os.path.join(inc, "WxPolicy.h"), "w", encoding="utf-8", newline="\n").write(plant(name, edits, src))
            r, err = run(inc, td, f"s{i}")
            if r is None:
                print(f"{name}: did not compile (rig)\n{err}")
                return 2
            fails = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith("FAIL")]
            tags = sorted({m.group(1) for l in fails for m in [re.search(r"FAIL\s+\(([a-f])\)", l)] if m})
            good = r.returncode == 1 and tags == [prop]
            print(f"{name}: exit {r.returncode}; red properties {tags or 'none'} (want ['{prop}']) -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = sha(HEADER)
    print(f"real include/WxPolicy.h unchanged: {before == after} ({after[:12]})")
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
