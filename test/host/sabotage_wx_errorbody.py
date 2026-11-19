"""Runs test_wx_errorbody.cpp against SABOTAGED copies of src/weatherscope/WxErrorBody.h (W7, S4.43); each must turn the
suite red on EXACTLY the checks predicted in docs/weatherscope-stage4.md S4.43, and nothing else.

    python -I test/host/sabotage_wx_errorbody.py

  E-GUESS        no body read as "forbidden": a guessed kind      -> ERR-NOBODY
  E-TRUNCATE     a token too long for the buffer, cut to fit       -> ERR-LONG
  E-NOQUOTE      a body with no closing quote read to its end      -> ERR-MALFORMED
The real header is never edited; each plant asserts its anchor occurs once and the copy differs.
"""
import hashlib, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WX = os.path.join(ROOT, "src", "weatherscope")
HEADER = os.path.join(WX, "WxErrorBody.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_errorbody.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "E-GUESS": ([("    if (body == nullptr || len == 0) return;",
                  "    if (body == nullptr || len == 0) { memcpy(out, \"forbidden\", cap < 10 ? 0 : 10); return; }  // SABOTAGE")],
                {"ERR-NOBODY"}),
    "E-TRUNCATE": ([("                if (n == 0 || n >= cap) return;   // empty, or too long for `cap`: refused whole, never cut",
                     "                if (n == 0) return;\n"
                     "                if (n >= cap) { memcpy(out, body + start, cap - 1); out[cap - 1] = '\\0'; return; }  // SABOTAGE")],
                   {"ERR-LONG"}),
    "E-NOQUOTE": ([("        return;   // no closing quote: malformed",
                    "        { const size_t n = len - start; if (n > 0 && n < cap) { memcpy(out, body + start, n); out[n] = '\\0'; } }"
                    "\n        return;  // SABOTAGE")],
                  {"ERR-MALFORMED"}),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def plant(name, edits, src):
    out = src
    for anchor, repl in edits:
        sc.prove_colour(name, anchor, repl)
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    return out


def real():
    return open(HEADER, encoding="utf-8").read().replace("\r\n", "\n")


def build_run(td, name, text):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, "WxErrorBody.h"), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "include"), "-I" + WX, TEST, "-o", exe],
                       capture_output=True, text=True)
    if b.returncode != 0:
        return b, None
    return b, subprocess.run([exe], capture_output=True, text=True)


def main():
    before = sha(HEADER)
    ok = True
    rigs = sc.Rigs()
    with tempfile.TemporaryDirectory() as td:
        for name, (edits, expect) in SABOTAGES.items():
            if rigs.forced(name):
                ok = False
                continue
            b, r = build_run(td, name, plant(name, edits, real()))
            if r is None:
                print(f"{name}: did not compile (rig)\n{b.stderr}")
                rigs.add(name)
                ok = False
                continue
            fails = {l.split()[1] for l in r.stdout.splitlines() if l.strip().startswith("FAIL") and len(l.split()) > 1}
            good = r.returncode == 1 and fails == expect
            print(f"{name}: suite exit {r.returncode}; red {sorted(fails)} (want {sorted(expect)}) -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = sha(HEADER)
    print(f"real src/weatherscope/WxErrorBody.h unchanged: {before == after} ({after[:12]})")
    if ok and before == after:
        print("W7 ERROR BODY PLANTS AS PREDICTED")
    return rigs.close(0 if ok and before == after else 1)


def anchor_entries():
    """Every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    src = sc.read(HEADER)
    return [(name, os.path.relpath(HEADER, ROOT), src, a) for name, (edits, _) in SABOTAGES.items() for a, _ in edits]


# --control: this driver's unplanted run alone, the suite against its own count. run.sh runs it for every driver.
ERRORBODY_OKS = 6   # the suite's count: the one place, read by the run path and by CONTROL_TABLE
CONTROL_TABLE = {"test_wx_errorbody": ("ok", ERRORBODY_OKS)}


def control():
    with tempfile.TemporaryDirectory() as td:
        build_run(td, "control", real())


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
