"""Runs test_ota_model.cpp against SABOTAGED copies of include/OtaIdentity.h's ModelString; each must turn the suite red on
EXACTLY the checks frozen in docs/weatherscope-stage4.md S4.5, and nothing else.

    python -I test/host/sabotage_ota_model.py

  S-M1   the model is the board alone          -> M1 for every row with a scope (3 today), none of the 5 without
  S-M1b  a short buffer is cut, not refused    -> M1b-short only
The real header is never edited: it is copied into a scratch include directory, the plant asserts its anchor occurs once
and the copy differs. The scoped rows are read from test/fixtures/ota/identities.txt, so the prediction follows the table.
"""
import hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INC = os.path.join(ROOT, "include")
HEADER = "OtaIdentity.h"
FIXTURES = os.path.join(ROOT, "test", "fixtures", "ota")
TEST = os.path.join(ROOT, "test", "host", "test_ota_model.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
FAIL_ID = re.compile(r"^\s*FAIL\s+(\S+)")


def scoped_rows():
    rows = []
    for line in open(os.path.join(FIXTURES, "identities.txt"), encoding="utf-8"):
        p = line.split()
        if len(p) == 3 and not line.startswith("#") and p[1] != "-":
            rows.append("M1-" + p[0])
    return rows


COPY = "    memcpy(out, id.scope, s);\n    memcpy(out + s, id.board, b);\n    out[s + b] = '\\0';\n"
REFUSE = "    if (b == 0 || s + b + 1 > cap) return false;\n"
SABOTAGES = {
    "S-M1": (COPY, "    (void)s;\n    memcpy(out, id.board, b);   // SABOTAGE S-M1: the board alone\n    out[b] = '\\0';\n",
             lambda: scoped_rows()),
    "S-M1b": (REFUSE, "    if (b == 0 || cap == 0) return false;\n"
                      "    if (s + b + 1 > cap) {   // SABOTAGE S-M1b: cut, not refused\n"
                      "        char t[2 * kFieldMax + 1];\n"
                      "        memcpy(t, id.scope, s);\n"
                      "        memcpy(t + s, id.board, b);\n"
                      "        memcpy(out, t, cap - 1);\n"
                      "        out[cap - 1] = '\\0';\n"
                      "        return true;\n"
                      "    }\n",
              lambda: ["M1b-short"]),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def source():
    return open(os.path.join(INC, HEADER), encoding="utf-8").read().replace("\r\n", "\n")


def build_and_run(td, name, text, rigs):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, HEADER), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + "-model.exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: test_ota_model did not compile (rig)\n{b.stderr}")
        rigs.add(name)
        return None
    return subprocess.run([exe, FIXTURES], capture_output=True, text=True)


def main():
    before = sha(os.path.join(INC, HEADER))
    src = source()
    ok = True
    rigs = sc.Rigs()
    with tempfile.TemporaryDirectory() as td:
        r = build_and_run(td, "control", src, rigs)
        if r is None:
            return 2
        good = r.returncode == 0 and not [l for l in r.stdout.splitlines() if FAIL_ID.match(l)]
        print(f"control (the real header): exit {r.returncode} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        for name, (anchor, repl, expect_fn) in SABOTAGES.items():
            if rigs.forced(name):
                ok = False
                continue
            assert src.count(anchor) == 1, f"{name}: anchor found {src.count(anchor)} times"
            text = src.replace(anchor, repl)
            assert text != src, f"{name}: the copy is identical -- the sabotage did not apply"
            r = build_and_run(td, name, text, rigs)
            if r is None:
                ok = False
                continue
            fails = sorted(m.group(1) for l in r.stdout.splitlines() for m in [FAIL_ID.match(l)] if m)
            want = sorted(expect_fn())
            g = r.returncode == 1 and fails == want
            print(f"{name}: failing {fails} (want {want}) -> {'AS PREDICTED' if g else 'OFF-PREDICTION'}")
            ok &= g
    after = sha(os.path.join(INC, HEADER))
    print(f"real header unchanged: {before == after} ({after[:12]})")
    print(f"{len(SABOTAGES)} sabotages: " + ("ALL AS PREDICTED" if ok and before == after else "OFF-PREDICTION: STOP"))
    return rigs.close(0 if ok and before == after else 1)


def anchor_entries():
    src = sc.read(os.path.join(INC, HEADER))
    return [(name, "include/" + HEADER, src, anchor) for name, (anchor, _, _) in SABOTAGES.items()]


# --control: the suite unplanted, against its own count (8 + 5 + 3 today, S4.5). The one place this number lives.
MODEL_CHECKS = 16
CONTROL_TABLE = {"test_ota_model": ("checks", MODEL_CHECKS)}


def control():
    with tempfile.TemporaryDirectory() as td:
        build_and_run(td, "control", source(), sc.Rigs())


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
