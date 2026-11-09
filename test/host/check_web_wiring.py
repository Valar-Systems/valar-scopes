"""KW1 (docs/web-kiosk-airfield.md): the firmware goes through include/PollCadence.h, and the kiosk key is web-only.
The host rows grade the helpers; this grades that AircraftManager actually calls them. It reads the source, because the
call sites cannot be run on a host:
  - CurrentPollIntervalMs returns through pollcadence::IntervalMs and keeps no copy of the rule;
  - the weekly-reboot block tests pollcadence::PreventiveRebootDue;
  - "kiosk" is read exactly once, through pollcadence::KioskOn, inside #ifdef BLIPSCOPE_WEB_RENDERER.
--selftest plants each failure in a copy of the text and requires KW1 to refuse it.
    python -I test/host/check_web_wiring.py <repo root> [--selftest]      exit 0 ok, 1 off, 2 rig"""
import re
import sys


def body_of(src, signature):
    i = src.find(signature)
    if i < 0:
        return None
    j = src.find("{", i)
    depth = 0
    for k in range(j, len(src)):
        depth += {"{": 1, "}": -1}.get(src[k], 0)
        if depth == 0:
            return src[j:k + 1]
    return None


def kw1(src):
    why = []
    b = body_of(src, "unsigned long AircraftManager::CurrentPollIntervalMs() const")
    if b is None:
        return False, "CurrentPollIntervalMs not found: re-derive"
    if "return pollcadence::IntervalMs(" not in b:
        why.append("CurrentPollIntervalMs does not return through pollcadence::IntervalMs")
    if re.search(r"return\s+cloudCfg\.poll(Active|Idle|Night)Ms\s*;", b):
        why.append("CurrentPollIntervalMs keeps its own copy of the rule")
    if src.count("pollcadence::PreventiveRebootDue(") != 1:
        why.append("the weekly-reboot block does not test pollcadence::PreventiveRebootDue exactly once")
    reads = [m.start() for m in re.finditer(r'"kiosk"', src)]
    if len(reads) != 1:
        why.append(f'"kiosk" appears {len(reads)} times, not once')
    else:
        at = reads[0]
        line = src[src.rfind("\n", 0, at) + 1:src.find("\n", at)]
        opened = src.rfind("#ifdef BLIPSCOPE_WEB_RENDERER", 0, at)
        closed = src.rfind("#endif", 0, at)
        if "pollcadence::KioskOn(" not in line:
            why.append('the "kiosk" read does not go through pollcadence::KioskOn')
        if opened < 0 or closed > opened:
            why.append('the "kiosk" read is not inside #ifdef BLIPSCOPE_WEB_RENDERER')
    return not why, "; ".join(why) or "through IntervalMs and PreventiveRebootDue; one kiosk read, web-only, through KioskOn"


PLANTS = [
    ("S-KW1 CurrentPollIntervalMs keeps its own copy of the rule",
     lambda s: s.replace("return pollcadence::IntervalMs(", "if (kioskOn) return cloudCfg.pollActiveMs;\n        return pollcadence::IntervalMs(")),
    ("the reboot block's own condition",
     lambda s: s.replace("pollcadence::PreventiveRebootDue(", "pollcadence_PreventiveRebootDue_copy(")),
    ("the kiosk read outside the web fence",
     lambda s: s.replace('#ifdef BLIPSCOPE_WEB_RENDERER\n    // The web build\'s kiosk', '#if 1\n    // The web build\'s kiosk')),
    ("the kiosk read not through KioskOn",
     lambda s: s.replace('pollcadence::KioskOn(configServer.GetStoredString("kiosk").c_str())',
                         'configServer.GetStoredString("kiosk") == "true"')),
]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    src = open(argv[1].rstrip("/") + "/src/AircraftManager.cpp", encoding="utf-8").read().replace("\r\n", "\n")
    ok, why = kw1(src)
    print(f"  {'ok  ' if ok else 'FAIL'} KW1: {why}")
    if not ok:
        return 1
    if "--selftest" in argv:
        bad = 0
        for name, plant in PLANTS:
            planted = plant(src)
            if planted == src:
                print(f"  RIG  {name}: the plant did not apply")
                return 2
            refused = not kw1(planted)[0]
            bad += not refused
            print(f"  {'ok  ' if refused else 'FAIL'} plant: {name} -> KW1 {'refuses it' if refused else 'MISSED IT'}")
        if bad:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
