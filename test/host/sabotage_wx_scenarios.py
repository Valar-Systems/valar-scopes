"""Runs test_wx_scenarios.cpp (K3) against SABOTAGED copies of src/weatherscope/WxScenarios.h. Each must fail EXACTLY
the set frozen in docs/weatherscope-stage3.md A8.15, and nothing else.

    python -I test/host/sabotage_wx_scenarios.py

The real header is never edited: each plant asserts its anchor occurs once and that the copy differs, and the real
file's hash is compared before and after. The unmodified header runs first, as the control: 0 failures.
"""
import hashlib, os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "src", "weatherscope", "WxScenarios.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_scenarios.cpp")
LOOP = os.path.join(ROOT, "test", "fixtures", "wx", "loop")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "S-K3a": ([("constexpr int64_t kObsLeadS = 300; ", "constexpr int64_t kObsLeadS = 7201;  // SABOTAGE")],
              {"K3.here.choose", "K3.here.headline"}),
    "S-K3b": ([("    const double s0 = c.lat - kHalfDeg, n0 = c.lat + kHalfDeg,",
                "    const double s0 = c.lat + 0.5 - kHalfDeg, n0 = c.lat + 0.5 + kHalfDeg,  /* SABOTAGE */")],
              {"K3.warn.choose"}),
    "S-K3c": ([("    if (!NearestEchoCell(h, codes, s->here, &storm)) return false;",
                "    storm = LatLon{h.latNwMicro / 1e6, h.lonNwMicro / 1e6};  // SABOTAGE: the NW cell")],
              {"K3.warn.distance", "K3.warn.bearing"}),
    "S-K3d": ([("    IsoOnClock(w.expiresUtc, kOffsetMin, s->expiresIso, sizeof(s->expiresIso));",
                "    IsoOnClock(w.expiresUtc, 0, s->expiresIso, sizeof(s->expiresIso));  // SABOTAGE: UTC, Z")],
              {"K3.warn.until"}),
    "S-K3e": ([("constexpr int kFromDeg = 240; ", "constexpr int kFromDeg = 60;  // SABOTAGE")], {"K3.warn.travel"}),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def plant(name, edits, src):
    out = src
    for anchor, repl in edits:
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    return out


def run(td, name, text):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, "WxScenarios.h"), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "src", "weatherscope"),
                        "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        sys.exit(2)
    r = subprocess.run([exe, LOOP], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        print(f"{name}: the test did not run (exit {r.returncode}): the RIG\n{r.stdout}{r.stderr}")
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
    print(f"the real src/weatherscope/WxScenarios.h unchanged: {before == after} ({after[:12]})")
    print(f"{len(SABOTAGES)} sabotages: " + ("ALL AS PREDICTED" if ok and before == after else "OFF-PREDICTION: STOP"))
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
