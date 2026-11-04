"""Runs test_remote_policy.cpp against SABOTAGED copies of src/remote/CoverPolicy.h. Each plant must fail EXACTLY
the set of named checks frozen in docs/remote-edition.md section 4 (76ddfbd), and nothing else.

    python -I test/host/sabotage_remote_policy.py          (CXX=... to pick the compiler; MinGW by default)

Method, fixed before any run (section 4):
  - The real header is never edited. It is copied into a temporary directory searched first (the test includes
    "CoverPolicy.h" by bare name; LongPressPolicy.h still resolves from include/), and the plant goes into the copy.
    Each plant asserts its anchor occurs exactly once and that the copy differs; the real file's hash is compared
    before and after.
  - The unplanted copy runs first, as the control: 34 checks, 0 failures.
  - A plant keeps every variable used: under -Werror an unused one is a compile failure, which is the RIG.
Exit: 0 every plant failed exactly its frozen set; 1 a plant missed or overshot; 2 the rig (refused plant,
compile failure, a suite that did not run, or a control that is not clean).
"""
import hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass run.sh makes over every driver

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "src", "remote", "CoverPolicy.h")
TEST = os.path.join(ROOT, "test", "host", "test_remote_policy.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
CHECKS = 34

# name: ([(anchor, replacement)], the frozen failing set)
SABOTAGES = {
    "SB1": ([("    // STOP FIRST, on the press edge, whatever the gesture, layout, alarm or rejoin.\n    if (Moving(d, s, now)) {\n",
              "    // STOP FIRST, on the press edge, whatever the gesture, layout, alarm or rejoin.\n    if (false && Moving(d, s, now)) {   // SABOTAGE SB1\n")],
            {"B1", "B2", "B3", "B4", "B5", "G2"}),
    "SB2": ([("    if (d.motionSeen && now - d.lastMotionMs <= SETTLE_MS) return true;\n",
              "    if (false && d.motionSeen && now - d.lastMotionMs <= SETTLE_MS) return true;   // SABOTAGE SB2\n")],
            {"B3", "J1"}),
    "SB3": ([("    if (s.commanded && now - s.commandMs <= EXPECT_MS) return true;\n",
              "    if (false && s.commanded && now - s.commandMs <= EXPECT_MS) return true;   // SABOTAGE SB3\n")],
            {"B4"}),
    "SB4": ([("held >= g.holdMs", "held > g.holdMs")],
            {"C1", "C2", "C4"}),
    "SB5": ([("    return t.target != Target::None && t.liveSincePress && ControlsEnabled(d, s, now);\n",
              "    return t.target != Target::None && (t.liveSincePress || true) && ControlsEnabled(d, s, now);   // SABOTAGE SB5\n")],
            {"A2", "A3"}),
    "SB6": ([("    if (g.gesture == Gesture::SingleTap) {\n",
              "    if (g.gesture == Gesture::SingleTap || g.gesture == Gesture::Hold) {   // SABOTAGE SB6\n")],
            {"D1", "E2"}),
    "SB7": ([("    return d.conn == Conn::Live && !Moving(d, s, now) && !d.alarmPending;\n",
              "    return d.conn == Conn::Live && !Moving(d, s, now);   // SABOTAGE SB7\n")],
            {"C8", "G1", "J1"}),
    "SB8": ([("constexpr uint8_t  RESEND_MAX      = 3;", "constexpr uint8_t  RESEND_MAX      = 4;   // SABOTAGE SB8")],
            {"H1", "H3"}),
    "SB9": ([("    if (s.st.phase == Phase::Grace && now - s.st.releaseMs <= REJOIN_MS\n",
              "    if (false && s.st.phase == Phase::Grace && now - s.st.releaseMs <= REJOIN_MS   // SABOTAGE SB9\n")],
            {"C4"}),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def run(td, name, text):
    inc = os.path.join(td, name)
    os.makedirs(inc, exist_ok=True)
    open(os.path.join(inc, "CoverPolicy.h"), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe],
                       capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        return None
    r = subprocess.run([exe], capture_output=True, text=True)
    m = re.search(r"^(\d+) checks, (\d+) failures$", r.stdout, re.M)
    if r.returncode not in (0, 1) or not m:
        print(f"{name}: the suite did not run (exit {r.returncode}): the RIG\n{r.stdout}{r.stderr}")
        return None
    failed = set(re.findall(r"^  FAIL  (\S+)", r.stdout, re.M))
    if int(m.group(1)) != CHECKS or int(m.group(2)) != len(failed):
        print(f"{name}: {m.group(0)} but {len(failed)} FAIL line(s) parsed: the RIG")
        return None
    return failed


def main():
    if sc.wants_anchors():
        # run.sh's pass: every plant's anchor still occurs exactly once in the REAL header, without building.
        text = sc.read(HEADER)
        entries = [(name, "src/remote/CoverPolicy.h", text, anchor)
                   for name, (edits, _) in SABOTAGES.items() for anchor, _ in edits]
        return sc.anchors_mode("sabotage_remote_policy.py", entries)
    before = sha(HEADER)
    original = open(HEADER, "rb").read().decode("utf-8").replace("\r\n", "\n")
    print(f"compiler: {subprocess.run([CXX, '--version'], capture_output=True, text=True).stdout.splitlines()[0]}")
    rc = 0
    with tempfile.TemporaryDirectory() as td:
        control = run(td, "control", original)
        if control is None or control:
            print(f"CONTROL  not clean: {sorted(control) if control else 'did not run'} -- refusing every plant")
            return 2
        print(f"CONTROL  {CHECKS} checks, 0 failures")
        for name, (edits, want) in SABOTAGES.items():
            text = original
            for anchor, repl in edits:
                n = text.count(anchor)
                if n != 1:
                    print(f"{name}  REFUSED: anchor occurs {n} times (the RIG)")
                    return 2
                text = text.replace(anchor, repl)
            if text == original:
                print(f"{name}  REFUSED: the copy is byte-identical (the RIG)")
                return 2
            got = run(td, name, text)
            if got is None:
                return 2
            ok = got == want
            extra, missing = sorted(got - want), sorted(want - got)
            print(f"{name}  {'PASS' if ok else 'FAIL'}  failed {sorted(got)}"
                  + ("" if ok else f"  (unexpected {extra}, missing {missing})"))
            if not ok:
                rc = 1
    after = sha(HEADER)
    if after != before:
        print("the REAL header changed during the run: the RIG")
        return 2
    print(f"real header unchanged (sha256 {before[:12]})")
    print("SABOTAGE PASSED" if rc == 0 else "SABOTAGE FAILED")
    return rc


if __name__ == "__main__":
    sys.exit(main())
