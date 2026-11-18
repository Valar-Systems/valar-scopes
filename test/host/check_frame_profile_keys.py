"""docs/frame-profile.md section 0 (e), the static half: the profile's answered keys are the right keys at the right
values, read from the tree's own source, never transcribed.
  K1  the info-* keys answered equal, as a set, the keys of AircraftInfoFields.cpp's table
  K2  each "true" answer (infotext, triangle, trail, scanline, fade) equals its member's compiled default in
      src/AircraftManager.h
  K3  every answered info-* key and wx-look answers "" (the image's own parse)
    python -I check_frame_profile_keys.py <tree>             exit 0 every row ok; 1 a row off
    python -I check_frame_profile_keys.py --selftest <tree>  the clean tree green; each plant reads exactly its row red"""
import os
import re
import sys

MEMBER = {"infotext": "displayInfoText", "triangle": "displayTriangles", "trail": "displayTrails",
          "scanline": "displaySweep", "fade": "displayFade"}


def load(tree):
    rd = lambda rel: open(os.path.join(tree, rel), encoding="utf-8").read()
    return {"scenes": rd("include/FrameProfileScenes.h"), "fields": rd("src/AircraftInfoFields.cpp"),
            "header": rd("src/AircraftManager.h")}


def arr(text, name):
    m = re.search(r"static const char\* const " + name + r"\[\] = \{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit(f"RIG: {name} not found in FrameProfileScenes.h")
    return re.findall(r'"([^"]*)"', m.group(1))


def rows(f):
    keys, answers = arr(f["scenes"], "kTargetKey"), arr(f["scenes"], "kTargetAnswer")
    if len(keys) != len(answers):
        raise SystemExit("RIG: kTargetKey and kTargetAnswer differ in length")
    ans = dict(zip(keys, answers))
    table = set(re.findall(r'^\s*\{ *"(info-[a-z]+)",', f["fields"], re.M))
    mine = {k for k in keys if k.startswith("info-")}
    out = {}
    out["K1"] = (mine == table and len(table) > 0,
                 f"answered {len(mine)}, table {len(table)}, only answered {sorted(mine - table)}, only table {sorted(table - mine)}")
    bad = []
    for key, member in MEMBER.items():
        m = re.search(r"bool\s+" + member + r"\s*=\s*(true|false)\s*;", f["header"])
        if not m:
            bad.append(f"{member}: no default")
        elif ans.get(key) != m.group(1):
            bad.append(f"{key}={ans.get(key)!r} vs {member}={m.group(1)}")
    out["K2"] = (not bad, "; ".join(bad) or f"{len(MEMBER)} defaults match")
    empt = [k for k in keys if (k.startswith("info-") or k == "wx-look") and ans[k] != ""]
    out["K3"] = (not empt, f"not empty: {empt}" if empt else "info-* and wx-look answer \"\"")
    return out


def report(f):
    r = rows(f)
    for k, (ok, why) in r.items():
        print(f"  {'ok ' if ok else 'OFF'}  {k}: {why}")
    off = [k for k, (ok, _) in r.items() if not ok]
    print(f"{len(r)} rows, {len(off)} failed")
    return 1 if off else 0


def selftest(tree):
    base = load(tree)
    rc = report(base)
    if rc:
        print("selftest FAILED: the clean tree is not green")
        return 1

    def plant(name, mutate, want):
        f = dict(base)
        mutate(f)
        got = sorted(k for k, (ok, _) in rows(f).items() if not ok)
        good = got == sorted(want)
        print(f"selftest {'ok' if good else 'OFF'}: {name} reads {got} red (want {sorted(want)})")
        return 0 if good else 1

    def drop(f):
        f["scenes"] = f["scenes"].replace('"info-route", ', "", 1)
        f["scenes"] = f["scenes"].replace('"", "", "", "", "", "", "", "", "", "", "", "", "", "", "",',
                                          '"", "", "", "", "", "", "", "", "", "", "", "", "", "",', 1)

    def flip(f):
        f["header"] = f["header"].replace("bool displayTrails = true;", "bool displayTrails = false;", 1)

    def answer(f):
        f["scenes"] = f["scenes"].replace('"true", "true", "true", "true", "true", "",',
                                          '"true", "true", "true", "true", "true", "A",', 1)
    rc |= plant("one info key dropped from the answers", drop, ["K1"])
    rc |= plant("a member's default flipped in the header", flip, ["K2"])
    rc |= plant("wx-look answered \"A\" instead of \"\"", answer, ["K3"])
    print("selftest PASSED" if rc == 0 else "selftest FAILED")
    return rc


if __name__ == "__main__":
    if sys.argv[1:2] == ["--selftest"]:
        sys.exit(selftest(sys.argv[2]))
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(report(load(sys.argv[1])))
