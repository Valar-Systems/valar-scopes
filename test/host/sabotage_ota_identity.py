"""Runs test_ota_identity.cpp and test_ota_install.cpp against SABOTAGED copies of include/OtaIdentity.h's Accept; each
must turn the suites red on EXACTLY the checks frozen in docs/weatherscope-stage3.md A12.104, and nothing else.

    python -I test/host/sabotage_ota_identity.py

  S-ID1b  Accept compares the board only (Q75's rule (a))  -> M3, O4-ii, O4-iii; I2's four checks
  S-ID2b  Accept compares the scope only                    -> M1, M2, M3, O4-v-a, O4-v-b; I1's four checks
The real headers are never edited: both are copied into a scratch include directory (OtaInstall.h includes
OtaIdentity.h by quotes, which searches its OWN directory first, so the copy must sit beside it), the plant asserts its
anchor occurs once and the copy differs.
"""
import hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes (docs/weatherscope-stage3.md A12.50, A12.61)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INC = os.path.join(ROOT, "include")
HEADERS = ("OtaIdentity.h", "OtaInstall.h")
FIXTURES = os.path.join(ROOT, "test", "fixtures", "ota")
TESTS = {"test_ota_identity": os.path.join(ROOT, "test", "host", "test_ota_identity.cpp"),
         "test_ota_install": os.path.join(ROOT, "test", "host", "test_ota_install.cpp")}
# Not "install": Windows' installer detection refuses to start a 32-bit .exe so named (run.sh says why).
EXE = {"test_ota_identity": "identity.exe", "test_ota_install": "decision.exe"}
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

ACCEPT = "    return strcmp(requested.board, candidate.board) == 0 && strcmp(requested.scope, candidate.scope) == 0;\n"
SABOTAGES = {
    "S-ID1b": ("OtaIdentity.h", ACCEPT,
               "    return strcmp(requested.board, candidate.board) == 0;  // SABOTAGE S-ID1b: the board only\n",
               {"test_ota_identity": ["M3", "O4-ii", "O4-iii"],
                "test_ota_install": ["I2-why", "I2-log", "I2-boot", "I2-target"]}),
    "S-ID2b": ("OtaIdentity.h", ACCEPT,
               "    return strcmp(requested.scope, candidate.scope) == 0;  // SABOTAGE S-ID2b: the scope only\n",
               {"test_ota_identity": ["M1", "M2", "M3", "O4-v-a", "O4-v-b"],
                "test_ota_install": ["I1-why", "I1-log", "I1-boot", "I1-target"]}),
}
FAIL_ID = re.compile(r"^\s*FAIL\s+(\S+)")


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def sources():
    return {h: open(os.path.join(INC, h), encoding="utf-8").read().replace("\r\n", "\n") for h in HEADERS}


def stage(td, name, files):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    for h, text in files.items():
        open(os.path.join(inc, h), "w", encoding="utf-8", newline="\n").write(text)
    return inc


def build_and_run(td, inc, suite, rigs, name):
    exe = os.path.join(td, os.path.basename(inc) + "-" + EXE[suite])
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, TESTS[suite], "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: {suite} did not compile (rig)\n{b.stderr}")
        rigs.add(name)
        return None
    return subprocess.run([exe, FIXTURES], capture_output=True, text=True)


def main():
    before = {h: sha(os.path.join(INC, h)) for h in HEADERS}
    src = sources()
    ok = True
    rigs = sc.Rigs()
    with tempfile.TemporaryDirectory() as td:
        for name, (header, anchor, repl, expect) in SABOTAGES.items():
            if rigs.forced(name):
                ok = False
                continue
            assert src[header].count(anchor) == 1, f"{name}: anchor found {src[header].count(anchor)} times in {header}"
            files = dict(src)
            files[header] = src[header].replace(anchor, repl)
            assert files[header] != src[header], f"{name}: the copy is identical -- the sabotage did not apply"
            inc = stage(td, name, files)
            good = True
            for suite in TESTS:
                r = build_and_run(td, inc, suite, rigs, name)
                if r is None:
                    good = False
                    continue
                fails = [m.group(1) for l in r.stdout.splitlines() for m in [FAIL_ID.match(l)] if m]
                want = expect.get(suite, [])
                g = (r.returncode == (1 if want else 0)) and sorted(fails) == sorted(want)
                good &= g
                print(f"{name}: {suite} exit {r.returncode}; failing {fails or '-'} (want {want or '-'}) "
                      f"-> {'AS PREDICTED' if g else 'OFF-PREDICTION'}")
                for l in r.stdout.splitlines():
                    if FAIL_ID.match(l):
                        print(f"    {l.strip()}")
            n = sum(len(v) for v in expect.values())
            print(f"{name}: {n} predicted failing check(s) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = {h: sha(os.path.join(INC, h)) for h in HEADERS}
    print("real headers unchanged: %s (%s)" % (before == after, ", ".join(f"{h} {after[h][:12]}" for h in HEADERS)))
    return rigs.close(0 if ok and before == after else 1)


def anchor_entries():
    """Q51: every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    return [(name, "include/" + header, sc.read(os.path.join(INC, header)), anchor)
            for name, (header, anchor, _, _) in SABOTAGES.items()]


# --control (Q57, A12.61): both suites unplanted, each against its own count, nothing planted. run.sh runs it.
IDENTITY_CHECKS = 22   # test_ota_identity's count, frozen in A12.104: the one place (A12.70)
INSTALL_CHECKS = 36    # test_ota_install's count, frozen in A12.104
CONTROL_TABLE = {"test_ota_identity": ("checks", IDENTITY_CHECKS), "test_ota_install": ("checks", INSTALL_CHECKS)}


def control():
    with tempfile.TemporaryDirectory() as td:
        inc = stage(td, "control", sources())
        for suite in TESTS:
            exe = os.path.join(td, EXE[suite])
            subprocess.run([CXX, *FLAGS, "-I" + inc, TESTS[suite], "-o", exe], capture_output=True, text=True)
            subprocess.run([exe, FIXTURES], capture_output=True, text=True)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
