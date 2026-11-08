"""F1 (docs/wx-opera-europe.md §6, docs/wx-opera-code-stage.md §3): the device asks for one product, and never by place.

Read from the firmware's own source, never transcribed:
  - every `WeatherUrl(` call site under src/ and include/ passes the product "cref", and there is at least one (else
    BLIND: a check that found no call site proves nothing);
  - the body of `AircraftManager::WxEnabled()` names no latitude or longitude (no identifier with a lat/lon part), and the
    body was found (else BLIND).
So which OPERA product answers is the server's choice alone, from the quantised point (proxy/src/wx/region.ts), and there
is no device-side list to keep in step.

  python scripts/check_wx_device_cref.py              exit 0 PASS, 1 FAIL, 2 BLIND
  python scripts/check_wx_device_cref.py --sabotage   plants into a temporary copy: F1s-a, a second call passing "opwe";
                                                      F1s-b, a latitude test in WxEnabled(). Each must read FAIL, the clean
                                                      copy PASS, and each plant must change its file. Exit 0 only then.
"""
import os
import re
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ("src", "include")
EXT = (".cpp", ".h", ".hpp", ".c", ".inc")
LATLON = {"lat", "lon", "lng", "latitude", "longitude"}


def strip_comments(s):
    """C/C++ comments out, string and character literals kept (a URL's // is not a comment)."""
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c in "\"'":
            j = i + 1
            while j < n and s[j] != c:
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif s.startswith("//", i):
            j = s.find("\n", i)
            i = n if j < 0 else j
        elif s.startswith("/*", i):
            j = s.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def args_at(s, open_paren):
    """The top-level arguments of the call whose '(' is at open_paren."""
    depth, i, start, args = 0, open_paren, open_paren + 1, []
    while i < len(s):
        c = s[i]
        if c in "\"'":
            j = i + 1
            while j < len(s) and s[j] != c:
                j += 2 if s[j] == "\\" else 1
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append(s[start:i].strip())
                return args
        elif c == "," and depth == 1:
            args.append(s[start:i].strip())
            start = i + 1
        i += 1
    return None


def parts(identifier):
    """camelCase, snake_case and digits apart: hasLatFix -> has, lat, fix; lat100 -> lat."""
    return [p.lower() for p in re.findall(r"[A-Z]?[a-z]+|[A-Z]+(?![a-z])", identifier)]


def check(root):
    calls, decls = [], 0
    body = None
    for d in DIRS:
        for dp, _, files in os.walk(os.path.join(root, d)):
            for f in files:
                if not f.endswith(EXT):
                    continue
                path = os.path.join(dp, f)
                src = strip_comments(open(path, encoding="utf-8", errors="replace").read())
                for m in re.finditer(r"\bWeatherUrl\s*\(", src):
                    if re.search(r"\bString\s+$", src[max(0, m.start() - 40):m.start()]):
                        decls += 1                      # the declaration or the definition, not a call
                        continue
                    a = args_at(src, m.end() - 1)
                    calls.append((os.path.relpath(path, root).replace("\\", "/"), a[1] if a and len(a) > 1 else None))
                w = re.search(r"\bbool\s+AircraftManager::WxEnabled\s*\(\s*\)\s*const\s*\{", src)
                if w:
                    depth, i = 1, w.end()
                    while i < len(src) and depth:
                        depth += {"{": 1, "}": -1}.get(src[i], 0)
                        i += 1
                    body = src[w.end():i - 1]
    products = sorted({a for _, a in calls}, key=str)
    idents = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", body or "")
    located = sorted({x for x in idents if LATLON & set(parts(x))})
    print(f"F1: {len(calls)} WeatherUrl( call site(s), {decls} declaration(s); products passed {products}")
    print(f"F1: WxEnabled() body {'found' if body is not None else 'NOT FOUND'}, {len(idents)} identifier(s); "
          f"lat/lon identifiers {located}")
    if not calls or body is None or not idents:
        print("F1: BLIND -- no call site, or no WxEnabled() body, so nothing was proven")
        return 2
    if products == ['"cref"'] and not located:
        print("F1: PASS")
        return 0
    print("F1: FAIL")
    return 1


def sabotage():
    ok = True
    tmp = tempfile.mkdtemp(prefix="f1-")
    try:
        for d in DIRS:
            shutil.copytree(os.path.join(ROOT, d), os.path.join(tmp, d))
        am = os.path.join(tmp, "src", "AircraftManager.cpp")
        orig = open(am, encoding="utf-8", newline="").read()
        print("-- the clean copy")
        if check(tmp) != 0:
            print("SABOTAGE RUN FAIL: the clean copy does not PASS")
            ok = False
        plants = {
            "F1s-a": orig + "\nstatic String f1sA(const String& b) { return CloudFeed::WeatherUrl(b, \"opwe\"); }\n",
            "F1s-b": re.sub(r"(bool\s+AircraftManager::WxEnabled\s*\(\s*\)\s*const\s*\{\s*return\s)", r"\1lat > 30.0 && ", orig, count=1),
        }
        for name, text in plants.items():
            if text == orig:
                print(f"SABOTAGE RUN FAIL: {name} did not change the file")
                ok = False
                continue
            open(am, "w", encoding="utf-8", newline="").write(text)
            print(f"-- {name}")
            rc = check(tmp)
            if rc != 1:
                print(f"SABOTAGE RUN FAIL: {name} read exit {rc}, not FAIL (1)")
                ok = False
            open(am, "w", encoding="utf-8", newline="").write(orig)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("SABOTAGE: every plant read FAIL, the clean copy PASS" if ok else "SABOTAGE: OFF")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(sabotage() if "--sabotage" in sys.argv else check(ROOT))
