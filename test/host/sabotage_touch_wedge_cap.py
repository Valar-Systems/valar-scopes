"""Runs test_touch_wedge_cap.cpp against SABOTAGED copies of include/TouchWedgeCap.h and include/SaveLock.h; each must
turn the suite red on EXACTLY the checks frozen in docs/weatherscope-stage3.md A12.119-A12.121, and nothing else.

    python -I test/host/sabotage_touch_wedge_cap.py

  S-CR1  the cap removed: the rung ignores OnRung's refusal AND RebootDue drops !unavailable  -> CR1, CR2, CR4, CR28
         (two sites, because either one alone still stops a 4th reboot)
  S-CR2  the idle rule removed: RebootDue's idle term is true (a reboot mid-touch)            -> CR5, CR6
  S-CR3  the screen not drawn: Cap::Draw no longer calls DrawStrip                           -> CR18, CR19, CR20
  S-CR4  the hold ignored: the save/flash hold check returns true                            -> CR9, CR10, CR11
  S-CR5  the refused save's message dropped: both refusals sent with an empty body           -> CR30, CR31
The real headers are never edited. A plant's copy goes in a scratch directory searched BEFORE include/ (-I order), so
every unplanted header still resolves to the real one; each anchor must occur once, and the copy must differ.
The unmodified suite runs first, as the control: CAP_CHECKS checks, 0 failures.
"""
import hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes (docs/weatherscope-stage3.md A12.50, A12.61)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INC = os.path.join(ROOT, "include")
WX = os.path.join(ROOT, "src", "weatherscope")
HEADERS = ("TouchWedgeCap.h", "SaveLock.h")
TEST = os.path.join(ROOT, "test", "host", "test_touch_wedge_cap.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

DUE = "    return wedged && !s.unavailable && now - lastActivityMs >= idleMs;\n"
SABOTAGES = {
    "S-CR1": ("TouchWedgeCap.h",
              # (void)s: without it -Werror refuses the unused parameter, and the plant reads as the RIG (A12.122)
              [(DUE, "    (void)s; return wedged && now - lastActivityMs >= idleMs;  // SABOTAGE S-CR1\n"),
               ("            if (r.action == Action::EnterUnavailable) {\n",
                "            if (false) {  // SABOTAGE S-CR1: the rung's refusal ignored\n")],
              ["CR1", "CR2", "CR4", "CR28"]),
    "S-CR2": ("TouchWedgeCap.h",
              [(DUE, "    return wedged && !s.unavailable && (true || now - lastActivityMs >= idleMs);  // SABOTAGE S-CR2\n")],
              ["CR5", "CR6"]),
    "S-CR3": ("TouchWedgeCap.h",
              [("        DrawStrip(c, StripFor(deviceId), ink, screenSize);\n",
                "        (void)c; (void)deviceId; (void)ink; (void)screenSize;  // SABOTAGE S-CR3: not drawn\n")],
              ["CR18", "CR19", "CR20"]),
    "S-CR4": ("TouchWedgeCap.h",
              [("    static bool Hold(Io& io) { return !io.FlashWriteActive() && io.TryHoldSave(); }\n",
                "    static bool Hold(Io& io) { (void)io; return true; }  // SABOTAGE S-CR4: the hold ignored\n")],
              ["CR9", "CR10", "CR11"]),
    "S-CR5": ("SaveLock.h",
              [("        request->send(REFUSED_STATUS, \"application/json\", body);\n",
                "        request->send(REFUSED_STATUS, \"application/json\", \"\");  // SABOTAGE S-CR5\n"),
               ("        request->send(REFUSED_STATUS, \"text/plain\", REFUSED_TEXT);\n",
                "        request->send(REFUSED_STATUS, \"text/plain\", \"\");  // SABOTAGE S-CR5\n")],
              ["CR30", "CR31"]),
}
FAIL_ID = re.compile(r"^\s*FAIL\s+(CR\d+):")

CAP_CHECKS = 32   # test_touch_wedge_cap's count, frozen in A12.120: the one place (A12.70)
CONTROL_TABLE = {"test_touch_wedge_cap": ("checks", CAP_CHECKS)}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def sources():
    return {h: open(os.path.join(INC, h), encoding="utf-8").read().replace("\r\n", "\n") for h in HEADERS}


def build_and_run(td, name, planted, rigs):
    """planted: {header: text} staged ahead of include/. Returns the run, or None when it did not compile (the rig)."""
    inc = os.path.join(td, name)
    os.makedirs(inc)
    for h, text in planted.items():
        open(os.path.join(inc, h), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + "-cap.exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + INC, "-I" + WX, TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: test_touch_wedge_cap did not compile (rig)\n{b.stderr}")
        rigs.add(name)
        return None
    return subprocess.run([exe], capture_output=True, text=True)


def counts(out):
    m = sc.COUNT.findall(out)
    return (int(m[-1][0]), int(m[-1][1])) if m else (None, None)


def main():
    before = {h: sha(os.path.join(INC, h)) for h in HEADERS}
    src = sources()
    ok = True
    rigs = sc.Rigs()
    with tempfile.TemporaryDirectory() as td:
        r = build_and_run(td, "control", {}, rigs)
        if r is None:
            return rigs.close(1)
        n, f = counts(r.stdout)
        good = r.returncode == 0 and n == CAP_CHECKS and f == 0
        print(f"control: {n} checks, {f} failures, exit {r.returncode} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        for name, (header, plants, want) in SABOTAGES.items():
            if rigs.forced(name):
                ok = False
                continue
            text = src[header]
            for anchor, repl in plants:
                assert text.count(anchor) == 1, f"{name}: anchor found {text.count(anchor)} times in {header}"
                text = text.replace(anchor, repl)
            assert text != src[header], f"{name}: the copy is identical -- the sabotage did not apply"
            r = build_and_run(td, name, {header: text}, rigs)
            if r is None:
                ok = False
                continue
            fails = [m.group(1) for l in r.stdout.splitlines() for m in [FAIL_ID.match(l)] if m]
            n, f = counts(r.stdout)
            g = r.returncode == 1 and sorted(fails) == sorted(want) and n == CAP_CHECKS
            print(f"{name}: exit {r.returncode}; {n} checks; failing {fails or '-'} (want {want}) "
                  f"-> {'AS PREDICTED' if g else 'OFF-PREDICTION'}")
            for l in r.stdout.splitlines():
                if FAIL_ID.match(l):
                    print(f"    {l.strip()}")
            ok &= g
    after = {h: sha(os.path.join(INC, h)) for h in HEADERS}
    print("real headers unchanged: %s (%s)" % (before == after, ", ".join(f"{h} {after[h][:12]}" for h in HEADERS)))
    return rigs.close(0 if ok and before == after else 1)


def anchor_entries():
    """Q51: every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    return [(name, "include/" + header, sc.read(os.path.join(INC, header)), anchor)
            for name, (header, plants, _) in SABOTAGES.items() for anchor, _ in plants]


def control():
    """--control (Q57, A12.61): the suite unplanted, against its count. run.sh runs it."""
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, "cap.exe")
        subprocess.run([CXX, *FLAGS, "-I" + INC, "-I" + WX, TEST, "-o", exe], capture_output=True, text=True)
        subprocess.run([exe], capture_output=True, text=True)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
