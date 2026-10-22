"""WD5b and WD5t4 (docs/wx-device.md), read from the source, not remembered.

  WD5b  the "wx-layer" switch is OFF by default in BOTH of its readers:
        - src/AircraftManager.cpp reads it as  GetStoredString("wx-layer") == "true"  (absent -> "" -> off);
        - src/ConfigurationWebServer.cpp renders it from  prefs.getString("wx-layer", "false").
  WD5t4 HttpRequestManager::GetBytes holds the body in no String: no getString( and no String assigned from
        the stream.

    python -I check_wx_wiring.py <repo root>              exit 0 all pass; 1 a check failed; 2 cannot read
    python -I check_wx_wiring.py <repo root> --selftest   each check against a copy with ONE planted break; every
                                                          break must turn exactly its own check red (exit 3 if not)
"""
import re
import sys


def wd5b_am(am):
    reads = re.findall(r'GetStoredString\("wx-layer"\)[^;\n]*', am)
    if not reads:
        return False, "no read of \"wx-layer\" in AircraftManager.cpp"
    bad = [r for r in reads if not re.fullmatch(r'GetStoredString\("wx-layer"\)\s*==\s*"true"', r.strip())]
    return (not bad), (f"{len(reads)} read(s), all == \"true\" (absent = off)" if not bad else f"not off-by-default: {bad}")


def wd5b_page(page):
    defaults = re.findall(r'getString\("wx-layer",\s*"([^"]*)"\)', page)
    if not defaults:
        return False, "no getString(\"wx-layer\", ...) in ConfigurationWebServer.cpp"
    bad = [d for d in defaults if d != "false"]
    return (not bad), (f"{len(defaults)} default(s), all \"false\"" if not bad else f"defaults {defaults}")


def get_bytes_body(hrm):
    m = re.search(r'\nHttpResult HttpRequestManager::GetBytes\(.*?\n\}\n', hrm, re.S)
    return m.group(0) if m else None


def wd5t4(hrm):
    body = get_bytes_body(hrm)
    if body is None:
        return False, "HttpRequestManager::GetBytes not found"
    hits = re.findall(r'getString\(', body) + re.findall(r'\bString\s+\w+\s*=\s*[^;]*(?:getStream|body\.|stream)', body)
    return (not hits), (f"GetBytes ({body.count(chr(10))} lines): no String body" if not hits else f"String body: {hits}")


CHECKS = [("WD5b (AircraftManager)", "am", wd5b_am), ("WD5b (config page)", "page", wd5b_page), ("WD5t4", "hrm", wd5t4)]


def run(src):
    results = []
    for name, key, fn in CHECKS:
        ok, why = fn(src[key])
        results.append((name, ok, why))
    return results


def main():
    root = sys.argv[1]
    try:
        src = {"am": open(f"{root}/src/AircraftManager.cpp", encoding="utf-8").read(),
               "page": open(f"{root}/src/ConfigurationWebServer.cpp", encoding="utf-8").read(),
               "hrm": open(f"{root}/src/HttpRequestManager.cpp", encoding="utf-8").read()}
    except OSError as e:
        print(f"FAIL: cannot read the sources ({e}). This is the RIG, not the code."); return 2
    if "--selftest" in sys.argv:
        body = get_bytes_body(src["hrm"])
        sabotages = [
            ("absent read as ON", "WD5b (AircraftManager)", "am",
             lambda s: s.replace('GetStoredString("wx-layer") == "true"', 'GetStoredString("wx-layer") != "false"')),
            ("page default true", "WD5b (config page)", "page",
             lambda s: s.replace('getString("wx-layer", "false")', 'getString("wx-layer", "true")')),
            ("a String body", "WD5t4", "hrm",
             lambda s: s.replace(body, body.replace("    xSemaphoreGive(mutex);\n",
                                                    "    const String dump = http.getString();\n    xSemaphoreGive(mutex);\n", 1)) if body else s),
        ]
        bad = 0
        for label, target, key, plant in sabotages:
            copy = dict(src); copy[key] = plant(src[key])
            if copy[key] == src[key]:
                print(f"  SELFTEST {label}: the sabotage did NOT apply"); bad += 1; continue
            red = [n for n, ok, _ in run(copy) if not ok]
            ok = red == [target]
            print(f"  SELFTEST {label}: red = {red} ({'exactly its own check' if ok else 'WRONG'})")
            bad += 0 if ok else 1
        print("SELFTEST PASSED" if bad == 0 else f"SELFTEST FAILED ({bad})")
        return 0 if bad == 0 else 3
    fails = 0
    for name, ok, why in run(src):
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {why}")
        fails += 0 if ok else 1
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
