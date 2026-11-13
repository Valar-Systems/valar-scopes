#!/usr/bin/env python3
"""Is the fresh-boot acceptance's HANDS-ON run due? (RELEASING.md step 0; docs/fresh-boot-automation.md, the cadence.)

The owner, 2026-11-12 16:57Z: "The glass reset and the phone's QR setup get the hands-on test whenever their code
changes, and at least every third release." An automated step 0 never exercises either, so this decides, from the
repository alone, whether the automated run may go ahead. scripts/fresh-boot-auto.sh calls it first and refuses on
anything but exit 0.

Read-only: git show / rev-parse / diff / log against the repository. It never checks out, writes or fetches.

  python3 scripts/fresh-boot-cadence.py --candidate <ref> [--repo <dir>]
      exit 0  ALLOWED: the hands-on run is not due; the automated run may go ahead
      exit 1  DUE: refused; every condition that holds is printed
      exit 2  INSTRUMENT: the counter is missing or doubled, the counter's tag is missing, FW_VERSION is unreadable,
              a menu function cannot be found, or git failed. Refused, and never read as allowed
  python3 scripts/fresh-boot-cadence.py --selftest
      the five registered controls (K-due-count, K-due-code, K-due-func, K-not-due, K-counter) against a synthetic
      repository built in a temporary directory and removed afterwards; exit 0 only if every one reads as registered

THE COUNTER is the one line in the CANDIDATE's RELEASING.md, `fresh-boot hands-on: last v<N> (<where, when>)`.
DUE when either holds:
  (a) by count: the candidate's FW_VERSION (src/OtaUpdater.h) minus N is 3 or more;
  (b) by code, since tag v<N>: DrawResetMenu, HandleResetMenuTap or CloseResetMenu changed in src/AircraftManager.cpp
      (read function by function with git log -L, because a file-level diff of that file means nothing); or
      src/FactoryReset.cpp, include/FactoryReset.h, include/SetupQr.h, include/QrRender.h or
      include/WiFiManagerHelpers.h changed; or the tzapu/WiFiManager line(s) of platformio.ini changed (only that line:
      the rest of platformio.ini changes every release).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

COUNTER = re.compile(r"fresh-boot hands-on: last v(\d+) \(([^)\n]*)\)")
FW = re.compile(r"\bFW_VERSION\s*=\s*(\d+)\s*;")
FUNCS = ("DrawResetMenu", "HandleResetMenuTap", "CloseResetMenu")
FILES = ("src/FactoryReset.cpp", "include/FactoryReset.h", "include/SetupQr.h", "include/QrRender.h",
         "include/WiFiManagerHelpers.h")
WM = "tzapu/WiFiManager"
DUE_AT = 3


class Instrument(Exception):
    pass


def git(repo, *args):
    r = subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        first = (r.stderr.strip().splitlines() or ["(no stderr)"])[0][:200]
        raise Instrument(f"git {args[0]} failed (exit {r.returncode}): {first}")
    return r.stdout


def show(repo, ref, path):
    return git(repo, "show", f"{ref}:{path}")


def decide(repo, cand):
    """(verdict, lines): verdict is 'ALLOWED' or 'DUE'; raises Instrument."""
    found = COUNTER.findall(show(repo, cand, "RELEASING.md"))
    if len(found) != 1:
        raise Instrument(f"the counter line appears {len(found)} times in RELEASING.md (it must appear exactly once)")
    last = int(found[0][0])
    tag = f"v{last}"
    git(repo, "rev-parse", "--verify", "--quiet", f"{tag}^{{commit}}")
    fws = FW.findall(show(repo, cand, "src/OtaUpdater.h"))
    if len(fws) != 1:
        raise Instrument(f"FW_VERSION appears {len(fws)} times in src/OtaUpdater.h (it must appear exactly once)")
    fw = int(fws[0])
    due = []
    if fw - last >= DUE_AT:
        due.append(f"due by count ({fw} - {last} = {fw - last})")
    changed = [f for f in git(repo, "diff", "--name-only", tag, cand, "--", *FILES).split() if f]
    for f in FILES:
        if f in changed:
            due.append(f"due by code: {f}")
    for fn in FUNCS:
        # -s: commits only, no patches. The regex is `<name>[(]`: it may not contain a colon (`-L :<regex>:<file>` splits
        # on them, so `AircraftManager::Name` cannot be written), and `[(]` is a literal paren in both BRE and ERE. It
        # matches the first FUNCNAME line (column 0) naming the function: its definition, never an indented call.
        # A function missing at the candidate makes git fail: Instrument, never allowed.
        out = git(repo, "log", "-s", "--format=%H", "-L", f":{fn}[(]:src/AircraftManager.cpp", f"{tag}..{cand}")
        if out.strip():
            due.append(f"due by code: {fn}")
    wm_then = [l.strip() for l in show(repo, tag, "platformio.ini").splitlines() if WM in l]
    wm_now = [l.strip() for l in show(repo, cand, "platformio.ini").splitlines() if WM in l]
    if not wm_now:
        raise Instrument(f"no {WM} line in the candidate's platformio.ini")
    if wm_then != wm_now:
        due.append(f"due by code: the {WM} line of platformio.ini")
    head = f"counter v{last} ({found[0][1]}); candidate FW_VERSION {fw}"
    return ("DUE", [head] + due) if due else ("ALLOWED", [head, f"not due: {fw} - {last} < {DUE_AT}, and no trigger changed since {tag}"])


def run(repo, cand):
    try:
        verdict, lines = decide(repo, cand)
    except Instrument as e:
        print(f"INSTRUMENT: {e}. The automated run is refused.")
        return 2
    print(f"{verdict}: " + "; ".join(lines))
    if verdict == "DUE":
        print("The hands-on run is due: the automated run is refused. Run step 0 by hand, as on v19, and update the counter.")
        return 1
    return 0


# ----------------------------------------------------------------------------------------------- the five controls
AM = """// synthetic
void AircraftManager::DrawRadar() {
  int radar = 1;
}

void AircraftManager::DrawResetMenu() {
  int draw = 1;
}

void AircraftManager::HandleResetMenuTap(int x, int y) {
  int tap = x + y;
}

void AircraftManager::CloseResetMenu() {
  int close = 1;
}
"""


def selftest():
    tmp = tempfile.mkdtemp(prefix="fbcadence-")
    try:
        def w(path, text):
            p = os.path.join(tmp, path)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)

        def g(*a):
            subprocess.run(["git", "-C", tmp, *a], check=True, capture_output=True)

        def commit(msg):
            g("add", "-A")
            g("-c", "user.name=selftest", "-c", "user.email=selftest@example.invalid", "commit", "-q", "-m", msg)
            return git(tmp, "rev-parse", "HEAD").strip()

        def releasing(n, times=1):
            return "# Releasing\n\n" + "".join(f"   `fresh-boot hands-on: last v{n} (step 4', 2026-01-0{i + 1})`\n" for i in range(times))

        g("init", "-q")
        w("RELEASING.md", releasing(16))
        w("src/OtaUpdater.h", "constexpr int FW_VERSION = 16;\n")
        w("src/AircraftManager.cpp", AM)
        for f in FILES:
            if f != "src/AircraftManager.cpp":
                w(f, f"// {f}\n")
        w("platformio.ini", "[env]\nlib_deps =\n    tzapu/WiFiManager @ 2.0.17\n    other/Lib @ 1.0\n")
        base = commit("v16")
        g("tag", "v16")

        def branch(name, edits):
            g("checkout", "-q", "-B", name, base)
            for path, text in edits:
                w(path, text)
            return commit(name)

        cases = []
        # K-due-count: the counter at v16, a v19 candidate, nothing else changed.
        c = branch("k-count", [("src/OtaUpdater.h", "constexpr int FW_VERSION = 19;\n")])
        cases.append(("K-due-count", c, 1, ["due by count (19 - 16 = 3)"], []))
        # K-due-code: one trigger file changed, the count not due.
        c = branch("k-code", [("src/OtaUpdater.h", "constexpr int FW_VERSION = 17;\n"), ("include/SetupQr.h", "// changed\n")])
        cases.append(("K-due-code", c, 1, ["due by code: include/SetupQr.h"], ["due by count"]))
        # K-due-func: only HandleResetMenuTap's body changed inside AircraftManager.cpp.
        c = branch("k-func", [("src/OtaUpdater.h", "constexpr int FW_VERSION = 17;\n"),
                              ("src/AircraftManager.cpp", AM.replace("int tap = x + y;", "int tap = x * y;"))])
        cases.append(("K-due-func", c, 1, ["due by code: HandleResetMenuTap"],
                      ["due by code: DrawResetMenu", "due by code: CloseResetMenu", "due by count"]))
        # K-not-due: the counter's own release, a candidate one above, touching AircraftManager.cpp OUTSIDE the three
        # functions and platformio.ini OUTSIDE the WiFiManager line: allowed. The control that refusals aren't unconditional.
        c = branch("k-notdue", [("src/OtaUpdater.h", "constexpr int FW_VERSION = 17;\n"),
                                ("src/AircraftManager.cpp", AM.replace("int radar = 1;", "int radar = 2;")),
                                ("platformio.ini", "[env]\nlib_deps =\n    tzapu/WiFiManager @ 2.0.17\n    other/Lib @ 1.1\n")])
        cases.append(("K-not-due", c, 0, ["not due"], ["due by"]))
        # K-counter: the line removed, then doubled: INSTRUMENT both times, never allowed.
        c = branch("k-nocounter", [("RELEASING.md", "# Releasing\n\nno counter here\n")])
        cases.append(("K-counter (removed)", c, 2, ["appears 0 times"], []))
        c = branch("k-twocounters", [("RELEASING.md", releasing(16, times=2))])
        cases.append(("K-counter (doubled)", c, 2, ["appears 2 times"], []))

        bad = 0
        for name, cand, want_rc, must, mustnt in cases:
            import io
            import contextlib
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = run(tmp, cand)
            out = buf.getvalue()
            ok = rc == want_rc and all(m in out for m in must) and not any(m in out for m in mustnt)
            bad += 0 if ok else 1
            print(f"{'PASS' if ok else 'FAIL'} {name}: exit {rc} (want {want_rc}); {out.strip().splitlines()[0]}")
        print("SELFTEST PASSED" if not bad else f"SELFTEST FAILED: {bad} control(s) not as registered")
        return 0 if not bad else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv):
    if "--selftest" in argv:
        return selftest()
    repo = os.getcwd()
    cand = None
    it = iter(argv)
    for a in it:
        if a == "--candidate":
            cand = next(it, None)
        elif a == "--repo":
            repo = next(it, repo)
    if not cand:
        print(__doc__)
        return 2
    return run(repo, cand)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
