"""WD5b and WD5t4 (docs/wx-device.md), read from the source, not remembered.

  WD5b  the "wx-layer" switch is OFF by default in BOTH of its readers:
        - src/AircraftManager.cpp reads it as  GetStoredString("wx-layer") == "true"  (absent -> "" -> off);
        - src/ConfigurationWebServer.cpp renders it from  prefs.getString("wx-layer", "false").
  WD5t4 HttpRequestManager::GetBytes holds the body in no String: no getString( and no String assigned from
        the stream.
  WA1   the age limit's header reaches the policy (ruled 2026-11-04 ~18:25Z): GetBytes collects X-Wx-Max-Age with
        the list's own size as the count and returns it; the fetch task stores it, and ConsumeWeather's OnResponse
        call hands it over. A header never collected would read as absent -- 600 -- forever, with every policy
        test green: this is the path those tests cannot see.
  CAR   the device carousel is Radar, List, Stats and Connect -- CarouselCount() == 4 on every PlatformIO env
        (docs/wx-device.md, M1h). InCarousel's #else branch is EVALUATED over the Screen enum, not matched as text, and
        CarouselCount() must count InCarousel over SCREEN_COUNT. scripts/wx-fb-check.py's screen dots assume exactly
        this; a change to it fails here, in the PR that makes it, rather than on the bench.

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


def k3_rule(s2):
    """K3: the one rule for the stored look -- exactly "B" is B, anything else (absence included) is A."""
    m = re.search(r"inline Look LookFromStored\(const char\* s\)\s*\{\s*return\s*(.*?);\s*\}", s2, re.S)
    if not m:
        return False, "no wxs2::LookFromStored in WxS2.h"
    want = "(s != nullptr && s[0] == 'B' && s[1] == '\\0') ? Look::B : Look::A"
    body = " ".join(m.group(1).split())
    return body == want, ("exactly \"B\" is B, else A (absent = A)" if body == want else f"the rule reads: {body}")


def k3_am(am):
    """K3: Initialise() reads "wx-look" once, through LookFromStored, and NOT only in the bench image."""
    reads = re.findall(r'[^\n]*GetStoredString\("wx-look"\)[^\n]*', am)
    if len(reads) != 1:
        return False, f"{len(reads)} read(s) of \"wx-look\" in AircraftManager.cpp, not 1"
    if "wxs2::LookFromStored(configServer.GetStoredString(\"wx-look\").c_str())" not in reads[0]:
        return False, f"not through LookFromStored: {reads[0].strip()}"
    depth_bench = 0
    stack = []
    for line in am[:am.index(reads[0])].splitlines():
        t = line.strip()
        if t.startswith("#if"):
            stack.append("WX_LOOK_AB" in t)
        elif t.startswith("#endif") and stack:
            stack.pop()
        elif t.startswith("#else") and stack:
            stack[-1] = False
    depth_bench = sum(1 for b in stack if b)
    return depth_bench == 0, ("one read, through LookFromStored, in shipping code" if depth_bench == 0
                              else "the read is inside #ifdef WX_LOOK_AB: the shipping image would ignore the setting")


def k3_page(page):
    """K3: the page reads "wx-look" with an absent default that LookFromStored maps to A ("" or "A")."""
    defaults = re.findall(r'getString\("wx-look",\s*"([^"]*)"\)', page)
    if not defaults:
        return False, "no getString(\"wx-look\", ...) in ConfigurationWebServer.cpp"
    bad = [d for d in defaults if d not in ("", "A")]
    through = "wxlookselect::Html(wxLookStored.c_str())" in page and "wxs2::LookFromStored(wxLookStored.c_str())" in page
    ok = not bad and through
    return ok, (f"{len(defaults)} default(s) {defaults}, the select and the legend through the rule" if ok
                else f"defaults {defaults}; select and legend through the rule: {through}")


def wa1_hrm(hrm):
    """WA1: GetBytes collects X-Wx-Max-Age, counts the list by its own size, and returns the value."""
    body = get_bytes_body(hrm)
    if body is None:
        return False, "HttpRequestManager::GetBytes not found"
    arr = re.search(r'kCollectHeaders\[\]\s*=\s*\{([^}]*)\}', body)
    listed = bool(arr) and '"X-Wx-Max-Age"' in arr.group(1)
    sized = "http.collectHeaders(kCollectHeaders, sizeof(kCollectHeaders) / sizeof(kCollectHeaders[0]));" in body
    returned = 'maxAge = http.header("X-Wx-Max-Age");' in body
    ok = listed and sized and returned
    return ok, (f"listed {listed}, counted by its own size {sized}, returned {returned}")


def wa1_am(am):
    """WA1: the fetch task stores the value, ConsumeFetchResult passes it, ConsumeWeather hands it to OnResponse."""
    stored = "res->wxMaxAge = maxAge;" in am
    passed = "ConsumeWeather(res->wxStatus, res->wxLen, res->wxClaimed, res->wxEtag, res->wxMaxAge," in am
    m = re.search(r'\nvoid AircraftManager::ConsumeWeather\(.*?\n\}\n', am, re.S)
    call = re.search(r'wxpolicy::OnResponse\(wxState,[^;]*;', m.group(0), re.S) if m else None
    handed = bool(call) and "maxAge.c_str()" in call.group(0)
    ok = stored and passed and handed
    return ok, f"stored {stored}, passed {passed}, handed to OnResponse {handed}"


CAR_DEVICE = ["Radar", "List", "Stats", "Connect"]


def car(amh):
    """CAR: InCarousel's device (#else) branch, evaluated over the Screen enum, admits exactly CAR_DEVICE."""
    enum = re.search(r"enum class Screen \{([^}]*)\};", amh)
    m = re.search(r"static constexpr bool InCarousel\(Screen s\)\s*\{\s*#ifdef BLIPSCOPE_WEB_RENDERER[^#]*#else\s*"
                  r"return ([^;]*);\s*#endif\s*\}", amh)
    if not enum or not m:
        return False, "the Screen enum or InCarousel's #ifdef BLIPSCOPE_WEB_RENDERER / #else form not found"
    screens = [s.strip() for s in enum.group(1).split(",")]
    expr = " ".join(m.group(1).split())
    # Only the forms this evaluator understands; anything else is refused, never guessed.
    if not re.fullmatch(r"(?:\s|\(|\)|&&|\|\||==|!=|\bs\b|Screen::\w+)+", expr):
        return False, f"InCarousel's device branch is not a comparison of s with Screen:: values: {expr}"
    py = re.sub(r"Screen::(\w+)", r"'\1'", expr).replace("&&", " and ").replace("||", " or ")
    unknown = [n for n in re.findall(r"Screen::(\w+)", expr) if n not in screens]
    if unknown:
        return False, f"InCarousel names screens the enum lacks: {unknown}"
    members = [n for n in screens if eval(py, {"__builtins__": {}}, {"s": n})]
    counted = re.search(r"static constexpr int CarouselCount\(\)\s*\{\s*int n = 0;\s*for \(int i = 0; i < SCREEN_COUNT; "
                        r"\+\+i\) n \+= InCarousel\(\(Screen\)i\) \? 1 : 0;\s*return n;\s*\}", amh) is not None
    ok = members == CAR_DEVICE and counted
    return ok, f"device carousel {members} (CarouselCount() = {len(members)}, want 4), counted from InCarousel {counted}"


CHECKS = [("WD5b (AircraftManager)", "am", wd5b_am), ("WD5b (config page)", "page", wd5b_page), ("WD5t4", "hrm", wd5t4),
          ("K3 (the rule)", "wxs2", k3_rule), ("K3 (AircraftManager)", "am", k3_am), ("K3 (config page)", "page", k3_page),
          ("WA1 (GetBytes)", "hrm", wa1_hrm), ("WA1 (AircraftManager)", "am", wa1_am), ("CAR", "amh", car)]


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
               "hrm": open(f"{root}/src/HttpRequestManager.cpp", encoding="utf-8").read(),
               "wxs2": open(f"{root}/include/WxS2.h", encoding="utf-8").read(),
               "amh": open(f"{root}/src/AircraftManager.h", encoding="utf-8").read()}
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
            ("the look's rule inverted (absent = B)", "K3 (the rule)", "wxs2",
             lambda s: s.replace("s[0] == 'B' && s[1] == '\\0') ? Look::B : Look::A",
                                 "s[0] == 'A' && s[1] == '\\0') ? Look::A : Look::B")),
            ("the look read only on the bench", "K3 (AircraftManager)", "am",
             lambda s: s.replace("    wxLook = wxs2::LookFromStored(configServer.GetStoredString(\"wx-look\").c_str());\n",
                                 "#ifdef WX_LOOK_AB\n    wxLook = wxs2::LookFromStored(configServer.GetStoredString(\"wx-look\").c_str());\n#endif\n")),
            ("the page's look default B", "K3 (config page)", "page",
             lambda s: s.replace('prefs.getString("wx-look", "")', 'prefs.getString("wx-look", "B")')),
            ("the age header dropped from the collect list", "WA1 (GetBytes)", "hrm",
             lambda s: s.replace('"X-Upstream", "X-Wx-Max-Age" };', '"X-Upstream" };')),
            ("ConsumeWeather hands OnResponse nullptr for the age", "WA1 (AircraftManager)", "am",
             lambda s: s.replace("maxAge.isEmpty() ? nullptr : maxAge.c_str(),   // (h)", "nullptr,   // SABOTAGE: the age dropped")),
            ("the device carousel also leaves out Stats", "CAR", "amh",
             lambda s: s.replace("        return s != Screen::Follow;\n",
                                 "        return s != Screen::Follow && s != Screen::Stats;   // SABOTAGE\n")),
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
