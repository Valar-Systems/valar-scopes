"""Runs test_ota_install.cpp against SABOTAGED copies of include/OtaInstall.h; each must turn the suite red on EXACTLY
the checks frozen in docs/weatherscope-stage3.md A1.3 (O8) and A12.104, and nothing else.

    python -I test/host/sabotage_ota_install.py

  S-O8a  the withdrawn design: write, end() (which switches the boot), THEN decide, and switch back on a refusal
         -> I1-I5 each -log, -boot, -target; I6 -why (the cut stream fails in the flash), -log, -target; O10; MAP2.
            Its run also prints I1-I5's final boot as app0: a final-state assertion would PASS this design.
  S-O8b  decide after begin() and the first write()
         -> I1-I6 each -log and -target; O10; MAP2. Every -boot stays green: the target check works on its own.
  S-ID3  a missing record is written anyway
         -> I3 and I4, all four checks each; O10
The real headers are never edited: both are copied into a scratch include directory (OtaInstall.h includes
OtaIdentity.h by quotes, which searches its own directory first), the plant asserts its anchor occurs once and the copy
differs.
"""
import hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes (docs/weatherscope-stage3.md A12.50, A12.61)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INC = os.path.join(ROOT, "include")
HEADERS = ("OtaIdentity.h", "OtaInstall.h")
FIXTURES = os.path.join(ROOT, "test", "fixtures", "ota")
TEST = os.path.join(ROOT, "test", "host", "test_ota_install.cpp")
EXE = "decision.exe"   # not "install": Windows' installer detection refuses to start a 32-bit .exe so named (run.sh)
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

ORDER = ("    Result res = Decide(head, got, requested);\n"
         "    if (res.outcome != Outcome::Accepted) return res;\n"
         "    if (!StartImage(flash, total, head, got)) return Failed(flash, res);\n"
         "    return FinishImage(src, flash, total, got, res);\n")


def _refused(*suffixes, inputs=("I1", "I2", "I3", "I4", "I5", "I6")):
    return [i + "-" + s for i in inputs for s in suffixes]


SABOTAGES = {
    "S-O8a": ("OtaInstall.h", ORDER,
              "    Result blank;  // SABOTAGE S-O8a: the withdrawn design -- write the image and end() first,\n"
              "    memset(&blank, 0, sizeof blank);\n"
              "    if (!StartImage(flash, total, head, got)) return Failed(flash, blank);\n"
              "    Result res = FinishImage(src, flash, total, got, blank);\n"
              "    if (res.outcome != Outcome::Installed) return res;\n"
              "    const Result d = Decide(head, got, requested);   // then decide,\n"
              "    if (d.outcome != Outcome::Accepted) { flash.revert(); return d; }   // and switch back on a refusal\n"
              "    return res;\n",
              _refused("log", "boot", "target", inputs=("I1", "I2", "I3", "I4", "I5"))
              + ["I6-why", "I6-log", "I6-target", "O10", "MAP2"]),
    "S-O8b": ("OtaInstall.h", ORDER,
              "    if (!StartImage(flash, total, head, got)) { Result blank; memset(&blank, 0, sizeof blank);"
              " return Failed(flash, blank); }  // SABOTAGE S-O8b: begin + the first write before the decision\n"
              "    Result res = Decide(head, got, requested);\n"
              "    if (res.outcome != Outcome::Accepted) return res;\n"
              "    return FinishImage(src, flash, total, got, res);\n",
              _refused("log", "target") + ["O10", "MAP2"]),
    "S-ID3": ("OtaInstall.h",
              "    if (p == Parsed::NoRecord) { res.outcome = Outcome::NoRecord; return res; }\n",
              "    if (p == Parsed::NoRecord) { res.outcome = Outcome::Accepted; return res; }  // SABOTAGE S-ID3\n",
              _refused("why", "log", "boot", "target", inputs=("I3", "I4")) + ["O10"]),
}
FAIL_ID = re.compile(r"^\s*FAIL\s+(\S+)")
INFO = re.compile(r"^\s*info\s+(I\d) final boot=(\S+)")


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


def main():
    before = {h: sha(os.path.join(INC, h)) for h in HEADERS}
    src = sources()
    ok = True
    rigs = sc.Rigs()
    with tempfile.TemporaryDirectory() as td:
        for name, (header, anchor, repl, want) in SABOTAGES.items():
            if rigs.forced(name):
                ok = False
                continue
            assert src[header].count(anchor) == 1, f"{name}: anchor found {src[header].count(anchor)} times in {header}"
            files = dict(src)
            files[header] = src[header].replace(anchor, repl)
            assert files[header] != src[header], f"{name}: the copy is identical -- the sabotage did not apply"
            inc = stage(td, name, files)
            exe = os.path.join(td, name + "-" + EXE)
            b = subprocess.run([CXX, *FLAGS, "-I" + inc, TEST, "-o", exe], capture_output=True, text=True)
            if b.returncode != 0:
                print(f"{name}: did not compile (rig)\n{b.stderr}")
                rigs.add(name)
                ok = False
                continue
            r = subprocess.run([exe, FIXTURES], capture_output=True, text=True)
            fails = [m.group(1) for l in r.stdout.splitlines() for m in [FAIL_ID.match(l)] if m]
            good = r.returncode == 1 and sorted(fails) == sorted(want)
            print(f"{name}: suite exit {r.returncode}; {len(fails)} failing check(s), {len(want)} predicted "
                  f"-> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            missing, extra = sorted(set(want) - set(fails)), sorted(set(fails) - set(want))
            if missing or extra:
                print(f"    predicted but green: {missing or '-'}; red but not predicted: {extra or '-'}")
            for l in r.stdout.splitlines():
                if FAIL_ID.match(l):
                    print(f"    {l.strip()}")
            if name == "S-O8a":
                finals = [f"{m.group(1)}={m.group(2)}" for l in r.stdout.splitlines() for m in [INFO.match(l)] if m]
                print(f"    final boot labels (a final-state check would read these): {' '.join(finals)}")
            ok &= good
    after = {h: sha(os.path.join(INC, h)) for h in HEADERS}
    print("real headers unchanged: %s (%s)" % (before == after, ", ".join(f"{h} {after[h][:12]}" for h in HEADERS)))
    return rigs.close(0 if ok and before == after else 1)


def anchor_entries():
    """Q51: every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    return [(name, "include/" + header, sc.read(os.path.join(INC, header)), anchor)
            for name, (header, anchor, _, _) in SABOTAGES.items()]


# --control (Q57, A12.61): the suite unplanted against its own count, nothing planted. run.sh runs it.
INSTALL_CHECKS = 36    # test_ota_install's count, frozen in A12.104: the one place (A12.70)
CONTROL_TABLE = {"test_ota_install": ("checks", INSTALL_CHECKS)}


def control():
    with tempfile.TemporaryDirectory() as td:
        inc = stage(td, "control", sources())
        exe = os.path.join(td, EXE)
        subprocess.run([CXX, *FLAGS, "-I" + inc, TEST, "-o", exe], capture_output=True, text=True)
        subprocess.run([exe, FIXTURES], capture_output=True, text=True)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
