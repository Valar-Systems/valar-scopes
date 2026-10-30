"""Runs test_wx_screens.cpp against SABOTAGED copies of src/weatherscope/WxScreens.h. Each must fail EXACTLY the set of
named cases frozen in docs/weatherscope-stage3.md A8.20 (S-LF and S-UN retired with LF and UN by A9.10), and A12.53's
S-CYN, and nothing else.

    python -I test/host/sabotage_wx_screens.py

The real header is never edited: each plant asserts its anchor occurs once and that the copy differs, and the real
file's hash is compared before and after. The unmodified header runs first, as the control: 0 failures.
"""
import hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass and the colour proof (docs/weatherscope-stage3.md A12.50)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "src", "weatherscope", "WxScreens.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_screens.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "S-CY": ([("    if (!storedFeeds) return false;\n", "    (void)storedFeeds;  // SABOTAGE: the stored-feeds test removed\n")],
             {"CY3"}),
    "S-CY2": ([("    if (c.paused) return false;\n", "    // SABOTAGE: the pause ignored\n")], {"CY4", "CY5"}),
    # 3.5b (A12.53): the cycle wraps at its own count, so Bend's two are visited
    "S-CYN": ([("    c.index = (c.index + 1) % c.count;\n",
                "    c.index = (c.index + 1) % kScenarios;  // SABOTAGE: Dallas's six only\n")], {"CY8", "CY9"}),
    "S-WT": ([("    if (rank < 0 || rank > 3) return false;",
               "    if (rank < 0 || rank > 3 || rank == 1) return false;  // SABOTAGE: Snow Squall's title dropped")],
             {"WT2"}),
    "S-CK": ([("h % 12 == 0 ? 12 : h % 12, m,", "h % 12, m,  /* SABOTAGE */")], {"CK2", "CK3"}),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def plant(name, edits, src):
    out = src
    for anchor, repl in edits:
        sc.prove_colour(name, anchor, repl)   # Q50: refuses a colour the panel cannot see
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    return out


def run(td, name, text):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, "WxScreens.h"), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "src", "weatherscope"),
                        "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        sys.exit(2)
    r = subprocess.run([exe], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        print(f"{name}: the suite did not run (exit {r.returncode}): the RIG\n{r.stdout}{r.stderr}")
        sys.exit(2)
    return r.returncode, {m.group(1) for m in re.finditer(r"^\s*FAIL (\S+)", r.stdout, re.M)}


def main():
    before = sha(HEADER)
    src = open(HEADER, encoding="utf-8").read().replace("\r\n", "\n")
    ok = True
    with tempfile.TemporaryDirectory() as td:
        rc, fails = run(td, "control", src)
        good = rc == 0 and not fails
        print(f"control (the real header): exit {rc}, failing {sorted(fails)} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        for name, (edits, expect) in SABOTAGES.items():
            rc, fails = run(td, name, plant(name, edits, src))
            good = rc == 1 and fails == expect
            print(f"{name}: failing {sorted(fails)}, predicted {sorted(expect)} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = sha(HEADER)
    print(f"the real src/weatherscope/WxScreens.h unchanged: {before == after} ({after[:12]})")
    print(f"{len(SABOTAGES)} sabotages: " + ("ALL AS PREDICTED" if ok and before == after else "OFF-PREDICTION: STOP"))
    return 0 if ok and before == after else 1


def anchor_entries():
    """Q51: every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    src = sc.read(HEADER)
    out = [(name, os.path.relpath(HEADER, ROOT), src, a) for name, entry in SABOTAGES.items() for a, _ in entry[0]]
    return out


if __name__ == "__main__":
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
