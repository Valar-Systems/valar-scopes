"""The kiosk's and the airfield's sabotages (docs/web-kiosk-airfield.md), each planted in a COPY of its header, requiring
the tests to turn red in exactly the rows named, no fewer and no more:
  S-KI-device   KioskOn ignores kWeb: a device build can turn the kiosk on            -> exactly KI1
  S-KI-ignored  IntervalMs ignores the kiosk                                          -> exactly KI2
  S-KI-reboot   PreventiveRebootDue ignores kWeb                                      -> exactly KI3
  S-KI-order    night outranks a recent touch                                         -> exactly KI1, KI2, KI5
  S-KI-eager    PreventiveRebootDue evaluates the night test first                    -> exactly KI5
  S-AF-always   /airports forwarded whatever the key                                  -> exactly AF1
  S-AF-never    /airports never forwarded                                             -> exactly AF2
The real headers are never written; their sha256 is compared before and after.
    python -I test/host/sabotage_web_kiosk.py      exit 0 = as predicted, 1 = not, 2 = rig"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes (docs/wx-device.md, "CI's anchors pass")

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CADENCE = "include/PollCadence.h"
ROUTE = "tools/web-renderer/web/web_route.h"
TESTS = {   # test file -> (the header directory it includes, the header)
    "test_poll_cadence": ("include", CADENCE),
    "test_poll_cadence_web": ("include", CADENCE),
    "test_web_route": ("tools/web-renderer/web", ROUTE),
}
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

KIOSK = 'inline bool KioskOn(const char* stored) { return kWeb && stored && std::strcmp(stored, "true") == 0; }'
ACTIVE = "    if ((kWeb && kiosk) || msSinceTouch <= idleAfterMs) return activeMs;\n"
NIGHT = "    if (clockValid && nightNow()) return nightMs;\n"
REBOOT_WEB = "    if (kWeb) return false;\n    return nowMs"
REBOOT = "    return nowMs >= rebootAfterMs && !inDetail && clockValid && msSinceTouch >= 10UL * 60UL * 1000UL && nightNow();"
AIRPORTS = "    return airportsRemote && StartsWith(path, kAirports);"

SABOTAGES = {
    "S-KI-device   KioskOn ignores kWeb": (["KI1"], CADENCE, KIOSK,
        'inline bool KioskOn(const char* stored) { return stored && std::strcmp(stored, "true") == 0; }   /* SABOTAGE */'),
    "S-KI-ignored  IntervalMs ignores the kiosk": (["KI2"], CADENCE, ACTIVE,
        "    (void)kiosk; if (msSinceTouch <= idleAfterMs) return activeMs;   /* SABOTAGE S-KI-ignored */\n"),
    "S-KI-reboot   PreventiveRebootDue ignores kWeb": (["KI3"], CADENCE, REBOOT_WEB, "    /* SABOTAGE S-KI-reboot */\n    return nowMs"),
    "S-KI-order    night outranks a recent touch": (["KI1", "KI2", "KI5"], CADENCE, ACTIVE + NIGHT,
        NIGHT.rstrip("\n") + "   /* SABOTAGE S-KI-order */\n" + ACTIVE),
    "S-KI-eager    the night test first": (["KI5"], CADENCE, REBOOT,
        "    const bool n = nightNow();   /* SABOTAGE S-KI-eager */\n"
        "    return nowMs >= rebootAfterMs && !inDetail && clockValid && msSinceTouch >= 10UL * 60UL * 1000UL && n;"),
    "S-AF-always   /airports whatever the key": (["AF1"], ROUTE, AIRPORTS,
        "    (void)airportsRemote; return StartsWith(path, kAirports);   /* SABOTAGE S-AF-always */"),
    "S-AF-never    /airports never": (["AF2"], ROUTE, AIRPORTS, "    (void)airportsRemote; return false;   /* SABOTAGE S-AF-never */"),
}

# --control (#510): this driver's unplanted run alone, each test against its own count. OK_COUNTS is the one place the counts live.
OK_COUNTS = {"test_poll_cadence": 3, "test_poll_cadence_web": 2, "test_web_route": 2}
CONTROL_TABLE = {k: ("ok", v) for k, v in OK_COUNTS.items()}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def make_tree(td, tag, plant=None):
    tree = os.path.join(td, tag)
    for h in (CADENCE, ROUTE):
        os.makedirs(os.path.dirname(os.path.join(tree, h)), exist_ok=True)
        shutil.copyfile(os.path.join(ROOT, h), os.path.join(tree, h))
    if plant:
        header, old, new = plant
        p = os.path.join(tree, header)
        src = open(p, encoding="utf-8").read().replace("\r\n", "\n")
        assert src.count(old) == 1, f"{tag}: anchor found {src.count(old)} times"
        out = src.replace(old, new)
        assert out != src, f"{tag}: the sabotage did not apply"
        open(p, "w", encoding="utf-8", newline="\n").write(out)
    return tree


def run_all(tree, td, tag):
    """Every test against this tree. Returns (red rows, ok counts, rig error)."""
    red, oks = set(), {}
    for name, (inc, _) in TESTS.items():
        exe = os.path.join(td, f"{tag}-{name}.exe")
        b = subprocess.run([CXX] + FLAGS + ["-I", os.path.join(tree, inc), os.path.join(ROOT, "test", "host", name + ".cpp"), "-o", exe],
                           capture_output=True, text=True)
        if b.returncode != 0:
            return None, None, f"{name} did not compile:\n{b.stderr[-1500:]}"
        r = subprocess.run([exe], capture_output=True, text=True)
        if r.returncode not in (0, 1):
            return None, None, f"{name} exited {r.returncode}"
        red |= {m.group(1) for l in r.stdout.splitlines() for m in [re.search(r"FAIL\s+((?:KI|AF)\d)\b", l)] if m}
        oks[name] = len(re.findall(r"^\s*ok\b", r.stdout, re.M))
    return red, oks, ""


def main():
    before = {h: sha(os.path.join(ROOT, h)) for h in (CADENCE, ROUTE)}
    ok = True
    with tempfile.TemporaryDirectory() as td:
        red, oks, err = run_all(make_tree(td, "control"), td, "control")
        if err or red or oks != OK_COUNTS:
            print(f"control (the real headers): did not pass -- the rig is broken. {err} red {sorted(red or [])} ok {oks}")
            return 2
        print(f"control (the real headers): every row passes {oks}")
        for i, (name, (want, header, old, new)) in enumerate(SABOTAGES.items()):
            red, _, err = run_all(make_tree(td, f"s{i}", (header, old, new)), td, f"s{i}")
            if err:
                print(f"{name}: {err} (rig)")
                return 2
            good = sorted(red) == sorted(want)
            print(f"{name}: red rows {sorted(red) or 'none'} (want {want}) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    same = all(before[h] == sha(os.path.join(ROOT, h)) for h in before)
    print(f"real headers unchanged: {same}")
    print("WEB KIOSK SABOTAGES AS PREDICTED" if ok and same else "WEB KIOSK SABOTAGES: NOT AS PREDICTED")
    return 0 if ok and same else 1


def control():
    with tempfile.TemporaryDirectory() as td:
        run_all(make_tree(td, "control"), td, "control")


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    return [(name, header, sc.read(os.path.join(ROOT, header)), old) for name, (_, header, old, _) in SABOTAGES.items()]


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
