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
  S-O8a also turns HP2 and HP5 red (docs/ota-head-patience.md, amendment 1): writing before deciding moves a truncated
         head's failure into the flash, as for I6.
  P-NORETRY  ota::ReadSome gives up on the first empty read (docs/ota-head-patience.md, section 4)
         -> HP1, HP2, HP3, HP4, HP5, HP6
  P-BYPASS   ReadHead reads the source directly, as at C, past ReadSome
         -> HP1, HP2, HP3, HP4, HP5 and the wiring row W-RS1; HP6 stays green (ReadSome itself is intact)
The wiring rows, read from source (section 3): W-RS1 -- ReadHead calls ReadSome and calls src.read nowhere else (read
from the staged header, so a plant there is seen); W-RS2 -- the device's writeRest (src/OtaUpdater.cpp) calls
ota::ReadSome and keeps no empty counter, no 300, no delay(100). Before any plant, both must be green on the real sources
and the detector must read RED on C's writeRest, so the row can be seen failing.
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
              + ["I6-why", "I6-log", "I6-target", "O10", "MAP2", "HP2", "HP5"]),
    # The pragma is scoped to the planted lines only (docs/ota-head-patience.md, amendment 2): starting the flash before
    # the decision hands `head` to StartImage even when the head read returned 0 bytes, which g++ 13.3 flags as
    # maybe-uninitialized once ReadHead loops over ReadSome. A false positive on the plant's path alone: the real Install
    # uses `head` only after Decide has accepted 352 bytes.
    "S-O8b": ("OtaInstall.h", ORDER,
              "#pragma GCC diagnostic push\n"
              "#pragma GCC diagnostic ignored \"-Wmaybe-uninitialized\"\n"
              "    if (!StartImage(flash, total, head, got)) { Result blank; memset(&blank, 0, sizeof blank);"
              " return Failed(flash, blank); }  // SABOTAGE S-O8b: begin + the first write before the decision\n"
              "    Result res = Decide(head, got, requested);\n"
              "    if (res.outcome != Outcome::Accepted) return res;\n"
              "    return FinishImage(src, flash, total, got, res);\n"
              "#pragma GCC diagnostic pop\n",
              _refused("log", "target") + ["O10", "MAP2"]),
    "S-ID3": ("OtaInstall.h",
              "    if (p == Parsed::NoRecord) { res.outcome = Outcome::NoRecord; return res; }\n",
              "    if (p == Parsed::NoRecord) { res.outcome = Outcome::Accepted; return res; }  // SABOTAGE S-ID3\n",
              _refused("why", "log", "boot", "target", inputs=("I3", "I4")) + ["O10"]),
    # docs/ota-head-patience.md, section 4. P-NORETRY keeps a use of `empty`, so -Werror can't turn the plant into a rig.
    "P-NORETRY": ("OtaInstall.h",
                  "        if (++empty >= kEmptyReads) return 0;\n        src.waitEmpty();\n",
                  "        (void)empty; return 0;  // SABOTAGE P-NORETRY: the first empty read ends it\n",
                  ["HP1", "HP2", "HP3", "HP4", "HP5", "HP6"]),
    "P-BYPASS": ("OtaInstall.h",
                 "        const size_t n = ReadSome(src, head + got, want - got);\n",
                 "        const size_t n = src.read(head + got, want - got);  // SABOTAGE P-BYPASS: as at C\n",
                 ["HP1", "HP2", "HP3", "HP4", "HP5", "W-RS1"]),
}
FAIL_ID = re.compile(r"^\s*FAIL\s+(\S+)")


# ---- The wiring rows (docs/ota-head-patience.md, section 3) ---------------------------------------------------------
UPDATER = os.path.join(ROOT, "src", "OtaUpdater.cpp")


def _body(text, signature):
    """The body of the function whose definition contains `signature`, braces matched, comments removed; None if absent."""
    i = text.find(signature)
    if i < 0:
        return None
    j = text.find("{", i)
    depth, k = 0, j
    while k < len(text):
        depth += {"{": 1, "}": -1}.get(text[k], 0)
        k += 1
        if depth == 0:
            break
    return re.sub(r"//[^\n]*", "", text[j:k])


def wrs1_ok(header_text):   # ReadHead calls ReadSome, and src.read nowhere else
    b = _body(header_text, "size_t ReadHead(")
    return b is not None and "ReadSome(" in b and "src.read(" not in b


def wrs2_ok(cpp_text):      # the device's writeRest goes through ota::ReadSome and keeps no patience of its own
    b = _body(cpp_text, "size_t writeRest(Source& src, size_t n)")
    return b is not None and "ota::ReadSome(" in b and not re.search(r"\bempty\b|\b300\b|delay\(100\)", b)


# C's two functions, verbatim (3a322758eadc): the detectors must read RED on them, or the rows can't fail.
C_READHEAD = ("size_t ReadHead(Source& src, uint8_t* head, size_t want)\n{\n    size_t got = 0;\n    while (got < want) {\n"
              "        const size_t n = src.read(head + got, want - got);\n        if (n == 0) break;\n        got += n;\n"
              "    }\n    return got;\n}\n")
C_WRITEREST = ("    size_t writeRest(Source& src, size_t n)\n    {\n        uint8_t buf[1024];\n        size_t done = 0;\n"
               "        int empty = 0;\n        while (done < n) {\n"
               "            const size_t got = src.read(buf, n - done < sizeof buf ? n - done : sizeof buf);\n"
               "            if (got == 0) {\n                if (++empty >= 300) {\n                    err_ = UPDATE_ERROR_STREAM;\n"
               "                    Update.abort();\n                    return done;\n                }\n"
               "                delay(100);\n                continue;\n            }\n            empty = 0;\n"
               "            if (write(buf, got) != got) return done;\n            done += got;\n        }\n"
               "        return done;\n    }\n")


def wiring_reds(header_text):
    updater = open(UPDATER, encoding="utf-8").read().replace("\r\n", "\n")
    return [rid for rid, ok in (("W-RS1", wrs1_ok(header_text)), ("W-RS2", wrs2_ok(updater))) if not ok]
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
    # The wiring rows' controls, before any plant: green on the real sources, and RED on C's two functions.
    clean = wiring_reds(src["OtaInstall.h"])
    seen = (not wrs1_ok(C_READHEAD), not wrs2_ok(C_WRITEREST))
    print(f"wiring rows on the real sources: {'green' if not clean else 'RED ' + str(clean)}; "
          f"the detectors on C's ReadHead and writeRest: {'red, red (they can fail)' if all(seen) else 'BLIND ' + str(seen)}")
    ok &= not clean and all(seen)
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
            fails += wiring_reds(files["OtaInstall.h"])   # W-RS1 reads the PLANTED header, W-RS2 the real updater
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
INSTALL_CHECKS = 42    # test_ota_install's count, frozen in A12.104 (36), + HP1-HP6 (docs/ota-head-patience.md)
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
