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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass (main's #506; docs/wx-device.md, "CI's anchors pass")

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
    # Today's wiring before WD5 addendum 6: the attempt recorded at the REPLY, no in-flight guard. G3 must burst.
    "S-g  the attempt recorded at the reply": ("g", [
        ("    s.attempted = true;                                   // (g) the attempt is the send\n"
         "    s.lastAttemptMs = nowMs;                              // (g) the interval runs from here\n"
         "    s.inFlight = true;                                    // (g) one at a time\n",
         "    (void)nowMs;   // SABOTAGE S-g: the send records nothing\n"),
        ("    if (!s.inFlight || seq != s.seq) return Outcome::Kept;   // (g) late or stray: changes nothing\n"
         "    s.inFlight = false;                                   // (g) answered\n"
         "    s.lastReplyMs = nowMs;                                // (g) never lastAttemptMs\n",
         "    (void)seq;   // SABOTAGE S-g: the attempt is recorded at the reply, as before addendum 6\n"
         "    s.attempted = true;\n    s.lastAttemptMs = nowMs;\n"),
        # ... and the comparisons as they were then: unsigned (WD5 addendum 7 hardened them).
        ("        if ((int32_t)(nowMs - s.sentMs) < (int32_t)kReplyTimeoutMs) return false;   // (g) one in flight\n",
         "        if ((uint32_t)(nowMs - s.sentMs) < kReplyTimeoutMs) return false;   // SABOTAGE: unsigned again\n"),
        ("    return (int32_t)(nowMs - s.lastAttemptMs) >= (int32_t)s.intervalMs;   // (e), and (g): later than now is not due\n",
         "    return (uint32_t)(nowMs - s.lastAttemptMs) >= s.intervalMs;   // SABOTAGE: unsigned again\n"),
    ]),
    # The hardening alone removed, the attempt still recorded at the send: H1's row goes red; the loop does not burst.
    "S-h  the elapsed time unsigned again": ("g", [
        ("        if ((int32_t)(nowMs - s.sentMs) < (int32_t)kReplyTimeoutMs) return false;   // (g) one in flight\n",
         "        if ((uint32_t)(nowMs - s.sentMs) < kReplyTimeoutMs) return false;   // SABOTAGE: unsigned again\n"),
        ("    return (int32_t)(nowMs - s.lastAttemptMs) >= (int32_t)s.intervalMs;   // (e), and (g): later than now is not due\n",
         "    return (uint32_t)(nowMs - s.lastAttemptMs) >= s.intervalMs;   // SABOTAGE: unsigned again\n"),
    ]),
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
        if r is None or r.returncode != 0 or len(re.findall(r"^\s*ok\b", r.stdout, re.M)) != OK_COUNT:
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
            tags = sorted({m.group(1) for l in fails for m in [re.search(r"FAIL\s+\(([a-g])\)", l)] if m})
            good = r.returncode == 1 and tags == [prop]
            print(f"{name}: exit {r.returncode}; red properties {tags or 'none'} (want ['{prop}']) -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = sha(HEADER)
    print(f"real include/WxPolicy.h unchanged: {before == after} ({after[:12]})")
    return 0 if ok and before == after else 1


# --control (#510; docs/wx-device.md, "#510's --control"): this driver's unplanted run alone, its suite against
# its own count, nothing planted. OK_COUNT is the one place the count lives: main()'s unplanted run reads it too.
OK_COUNT = 27
CONTROL_TABLE = {"test_wx_policy": ("ok", OK_COUNT)}


def control():
    with tempfile.TemporaryDirectory() as td:
        run(os.path.join(ROOT, "include"), td, "control")


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    src = sc.read(HEADER)
    return [(name, os.path.relpath(HEADER, ROOT), src, a) for name, (_, edits) in SABOTAGES.items() for a, _ in edits]


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
