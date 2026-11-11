"""Step A's source checks, CA12-CA14 (docs/blipscope-map-client-steps.md, step A; registered 2026-11-11, item 6).

The map's files are include/MapQuery.h, include/MapPolicy.h and include/MapLayer.h, and the regions of
src/AircraftManager.h and src/AircraftManager.cpp between a line `// THE MAP FETCH BEGIN` and a line
`// THE MAP FETCH END`. The number of regions in each file is pinned below: a count off in either direction, or a
BEGIN without its END, is the rig (exit 2), because a region the check cannot see is code it cannot vouch for.
Comments are stripped before anything is read.

  CA12  F8, held in RAM only: no `Preferences`, no put...( call, no nvs_set, nvs_commit, SPIFFS or LittleFS in the map's
        files (string literals are blanked too, so a log message cannot trip it).
  CA13  privacy: every printf, print and println in the map's files is read, and an argument whose identifier holds one
        of the words lat, lon, latitude, longitude, id, leaderboard, hash, fnv, spread, key, mac is refused
        (identifiers split into words at camelCase and underscores). A "lat" or "lon" request parameter must be built
        by wireloc::Center(, and at least one of each must be found. Prints how many print calls it read; none is the rig.
  CA14  GetBytes' kCollectHeaders in src/HttpRequestManager.cpp is exactly Transfer-Encoding, ETag, X-Cache,
        X-Upstream, X-Wx-Max-Age, Retry-After, in that order, and Retry-After is read into HttpResult::retryAfter.

    python -I test/host/check_map_fetch.py <repo root>    exit 0 all pass; 1 a check failed; 2 the rig (cannot read)
Its plants are S-CA-NVS, S-CA-PRINTD and S-CA-NORA in test/host/sabotage_map_fetch.py.
"""
import re
import sys

REGION_COUNTS = {"src/AircraftManager.h": 3, "src/AircraftManager.cpp": 9}
WHOLE = ["include/MapQuery.h", "include/MapPolicy.h", "include/MapLayer.h"]
BEGIN, END = "// THE MAP FETCH BEGIN", "// THE MAP FETCH END"
FORBIDDEN_WORDS = {"lat", "lon", "latitude", "longitude", "id", "leaderboard", "hash", "fnv", "spread", "key", "mac"}
HEADERS = ["Transfer-Encoding", "ETag", "X-Cache", "X-Upstream", "X-Wx-Max-Age", "Retry-After"]


class Rig(Exception):
    pass


def strip(code, blank_strings=False):
    """C++ with comments removed (newlines kept). With blank_strings, each string and char literal keeps its quotes
    and loses its contents."""
    out, i, n = [], 0, len(code)
    while i < n:
        c = code[i]
        if c == "/" and i + 1 < n and code[i + 1] == "/":
            while i < n and code[i] != "\n":
                i += 1
        elif c == "/" and i + 1 < n and code[i + 1] == "*":
            j = code.find("*/", i + 2)
            if j < 0:
                raise Rig("an unterminated /* comment")
            out.append("\n" * code.count("\n", i, j))
            i = j + 2
        elif c in "\"'":
            j = i + 1
            while j < n and code[j] != c:
                if code[j] == "\\":
                    j += 1
                if j < n and code[j] == "\n":
                    raise Rig(f"an unterminated literal near: {code[i:i + 40]!r}")
                j += 1
            if j >= n:
                raise Rig("an unterminated literal")
            out.append(c + c if blank_strings else code[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def regions(path, text, want):
    """The text of each region, in order. The rig unless BEGIN and END alternate, starting with BEGIN, `want` times."""
    found, cur = [], None
    for ln in text.split("\n"):
        s = ln.strip()
        if s == BEGIN:
            if cur is not None:
                raise Rig(f"{path}: a BEGIN inside an open region")
            cur = []
        elif s == END:
            if cur is None:
                raise Rig(f"{path}: an END without its BEGIN")
            found.append("\n".join(cur))
            cur = None
        elif cur is not None:
            cur.append(ln)
    if cur is not None:
        raise Rig(f"{path}: a BEGIN without its END")
    if len(found) != want:
        raise Rig(f"{path}: {len(found)} map regions, the check pins {want}")
    return found


def words(identifier):
    parts = []
    for chunk in identifier.split("_"):
        parts += re.findall(r"[A-Z]+(?=[A-Z][a-z]|\d|\b)|[A-Z]?[a-z]+|[A-Z]+|\d+", chunk)
    return [p.lower() for p in parts if p]


def call_args(code, open_paren):
    """The top-level arguments of the call whose '(' is at open_paren, in code whose comments are stripped."""
    depth, i, start, args = 0, open_paren, open_paren + 1, []
    while i < len(code):
        c = code[i]
        if c in "\"'":
            j = i + 1
            while code[j] != c:
                j += 2 if code[j] == "\\" else 1
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append(code[start:i])
                return args
        elif c == "," and depth == 1:
            args.append(code[start:i])
            start = i + 1
        i += 1
    raise Rig("an unbalanced print call")


def identifiers(expr):
    return re.findall(r"[A-Za-z_]\w*", re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', " ", expr))


def ca12(map_code):
    blanked = [strip(t, blank_strings=True) for t in map_code]
    pats = [("Preferences", r"\bPreferences\b"), ("a put...( call", r"\bput\w*\s*\("), ("nvs_set", r"\bnvs_set\w*"),
            ("nvs_commit", r"\bnvs_commit\b"), ("SPIFFS", r"\bSPIFFS\b"), ("LittleFS", r"\bLittleFS\b")]
    hits = [(name, m.group(0)) for name, p in pats for t in blanked for m in re.finditer(p, t)]
    return (not hits), ("nothing that writes to flash" if not hits else f"refused: {hits}")


def ca13(map_code):
    code = [strip(t) for t in map_code]
    calls, refused = 0, []
    for t in code:
        for m in re.finditer(r"\b(printf|print|println)\s*\(", t):
            calls += 1
            for arg in call_args(t, m.end() - 1):
                for ident in identifiers(arg):
                    bad = FORBIDDEN_WORDS.intersection(words(ident))
                    if bad:
                        refused.append(f"{m.group(1)}: {ident} ({', '.join(sorted(bad))})")
    if calls == 0:
        raise Rig("CA13 read no print call in the map's files")
    params = [(m.group(1), m.group(2).strip()) for t in code
              for m in re.finditer(r'\{\s*"(lat|lon)"\s*,\s*([^{}]*?)\s*\}', t)]
    off = [f'"{k}" from {v}' for k, v in params if not v.startswith("wireloc::Center(")]
    have = {k for k, _ in params}
    missing = sorted({"lat", "lon"} - have)
    ok = not refused and not off and not missing
    why = f"{calls} print calls read; {len(params)} lat/lon parameters, all through wireloc::Center"
    if refused:
        why = f"{calls} print calls read; refused {refused}"
    elif off:
        why = f"a lat/lon parameter not through wireloc::Center: {off}"
    elif missing:
        why = f"no {' or '.join(missing)} parameter in the map's files"
    return ok, why


def ca14(hrm):
    start = hrm.find("HttpResult HttpRequestManager::GetBytes(")
    if start < 0:
        raise Rig("GetBytes not found in HttpRequestManager.cpp")
    nxt = hrm.find("\nHttpResult HttpRequestManager::", start + 1)
    body = strip(hrm[start:nxt if nxt > 0 else len(hrm)])
    m = re.search(r"static const char\* kCollectHeaders\[\] = \{([^}]*)\};", body)
    if not m:
        raise Rig("GetBytes' kCollectHeaders not found")
    got = re.findall(r'"([^"]*)"', m.group(1))
    read = re.search(r'\bresult\.retryAfter\s*=\s*http\.header\("Retry-After"\)\s*;', body) is not None
    ok = got == HEADERS and read
    return ok, (f"kCollectHeaders {got}; Retry-After read into HttpResult::retryAfter: {read}")


def main():
    if len(sys.argv) < 2:
        print("usage: check_map_fetch.py <repo root>")
        return 2
    root = sys.argv[1]
    try:
        src = {p: open(f"{root}/{p}", "rb").read().decode("utf-8").replace("\r\n", "\n")
               for p in WHOLE + list(REGION_COUNTS) + ["src/HttpRequestManager.cpp"]}
    except OSError as e:
        print(f"FAIL: cannot read the sources ({e}). This is the RIG, not the code.")
        return 2
    try:
        map_code = [src[p] for p in WHOLE]
        for p, n in REGION_COUNTS.items():
            map_code += regions(p, src[p], n)
        rows = [("CA12", ca12(map_code)), ("CA13", ca13(map_code)), ("CA14", ca14(src["src/HttpRequestManager.cpp"]))]
    except Rig as e:
        print(f"FAIL: {e}. This is the RIG, not the code.")
        return 2
    fails = 0
    for name, (ok, why) in rows:
        print(f"  {'ok  ' if ok else 'FAIL'} {name} {why}")
        fails += 0 if ok else 1
    print(f"{len(rows)} checks, {fails} failures")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
