"""The radar config page's save counts no toggle the page never renders (the v20 "left unchanged" fix).

/save counts a toggle as "left unchanged" when the form never mentioned it (SaveToggle's third branch in
src/ConfigurationWebServer.cpp). The form's vocabulary is every checkbox name in the DOM as rendered, so a
SaveToggle("x") compiled into a build that never renders a box named "x" is counted on EVERY save, and the page then
says "1 setting this page does not know about was left unchanged - reload to see it", which no reload can show.
v19 shipped exactly that for "wx-layer": its box renders only with the weather layer, its save ran in every radar build.

The page cannot be served on a host, so this reads the source and evaluates the preprocessor for a named build:

  V1  the -D names of env:blipscope-s3-128, read from platformio.ini with its ${...} resolved (the shipping radar)
  V2  V1 + WX_LAYER_SHIPS (the joint release's documented one-line switch, [weather] layer = -DWX_LAYER_SHIPS)

  SAVED       the live SaveToggle("x") names
  BOXES       the live checkbox names (<input name="x" ... type="checkbox"> in a string literal)
  LEFT_ALONE  SAVED - BOXES: what a full-form save counts as "left unchanged". Must be empty.
  UNSAVED     BOXES - SAVED: a box the save never writes. Printed, not asserted (an ACTION box such as ntfy-regen
              is in it by design).

The define set is -D only: a gating macro that comes from a header is invisible here. A name other than the one being
fixed turning up in LEFT_ALONE is therefore a STOP (read its gate), never a conclusion.

    python -I check_save_vocab.py <repo root>              exit 0 all pass; 1 an assertion failed; 2 cannot read or
                                                          evaluate (the RIG, not the code)
    python -I check_save_vocab.py <repo root> --selftest   C1-C3: each plant on a copy of the source must turn exactly
                                                          its registered red set (exit 3 if not)
"""
import re
import sys

ENV = "env:blipscope-s3-128"
EDITION_PREFIXES = ("eam-", "sp-", "se-", "bd-", "fi-", "cl-", "sc-")
GATE = "#if defined(FEATURE_CLOUD_FEED) && defined(WX_LAYER_SHIPS)"


class Rig(Exception):
    """The instrument cannot answer: a file it cannot read or parse, or a directive it cannot evaluate."""


# --------------------------------------------------------------------------------------------- platformio.ini
def ini_sections(text):
    """[section] -> {key: raw value}. ';' and '#' comment lines and ' ;' inline comments are dropped; an indented line
    continues the previous key, as PlatformIO reads it."""
    secs, cur, key = {}, None, None
    for raw in text.split("\n"):
        line = re.sub(r"\s;.*$", "", raw).rstrip()
        if not line.strip() or line.lstrip().startswith((";", "#")):
            continue
        m = re.match(r"^\[([^\]]+)\]\s*$", line)
        if m:
            cur, key = m.group(1).strip(), None
            secs.setdefault(cur, {})
            continue
        if cur is None:
            continue
        if line[0] in " \t" and key is not None:
            secs[cur][key] += "\n" + line.strip()
            continue
        m = re.match(r"^([A-Za-z0-9_.\-]+)\s*=\s*(.*)$", line)
        if m:
            key = m.group(1)
            secs[cur][key] = m.group(2).strip()
    return secs


def ini_get(secs, sec, key, depth=0):
    if depth > 20:
        raise Rig(f"platformio.ini: ${{...}} nests deeper than 20 at [{sec}] {key}")
    s = sec
    while s is not None:
        if key in secs.get(s, {}):
            val = secs[s][key]
            break
        ext = secs.get(s, {}).get("extends")
        s = (ext if ext.startswith("env:") or ext in secs else "env:" + ext) if ext else None
    else:
        return ""
    return re.sub(r"\$\{([^.}]+)\.([^}]+)\}", lambda m: ini_get(secs, m.group(1), m.group(2), depth + 1), val)


def env_defines(ini_text, env=ENV):
    secs = ini_sections(ini_text)
    if env not in secs:
        raise Rig(f"platformio.ini has no [{env}]")
    flags = ini_get(secs, env, "build_flags")
    d, u = {}, set()
    for m in re.finditer(r"(?:^|\s)-([DU])\s*([A-Za-z_][A-Za-z0-9_]*)(?:=(\S*))?", flags):
        if m.group(1) == "D":
            d[m.group(2)] = m.group(3) if m.group(3) is not None else "1"
        else:
            u.add(m.group(2))
    for name in u:          # -U lands in CCFLAGS and beats a -D whatever the order (CLAUDE.md)
        d.pop(name, None)
    return d


# ----------------------------------------------------------------------------------------- the C++ source
def scan(src):
    """Walk the source once. Returns (directives, literals, calls):
       directives  [(offset, line number, text)] for every preprocessor line outside comments and raw strings
       literals    [(offset, text)] for every string literal, raw or escaped (escapes \\" folded to ")
       calls       [(offset, name)] for every SaveToggle("name") in CODE (not in a comment or a string)"""
    directives, literals, calls = [], [], []
    i, n, line, at_line_start = 0, len(src), 1, True
    code = []                                   # code text with comments and literal BODIES blanked, for calls
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1; at_line_start = True; code.append(c); i += 1; continue
        if at_line_start and c in " \t":
            code.append(c); i += 1; continue
        if at_line_start and c == "#":
            j, text = i, []
            while j < n:                        # to the end of the line, following backslash continuations
                k = src.find("\n", j)
                k = n if k < 0 else k
                seg = src[j:k]
                if seg.endswith("\\"):
                    text.append(seg[:-1]); line += 1; j = k + 1; continue
                text.append(seg); j = k; break
            t = " ".join(text)
            t = re.sub(r"/\*.*?\*/", " ", t); t = re.sub(r"//.*$", "", t)
            directives.append((i, line, t.strip()))
            # a #define body can hold string literals; scan them as literals so nothing is silently skipped
            for m in re.finditer(r'R"([^(\s]{0,16})\((.*?)\)\1"', t):
                literals.append((i, m.group(2)))
            code.append(" " * (j - i)); i = j; continue
        at_line_start = False
        if src.startswith("//", i):
            k = src.find("\n", i); k = n if k < 0 else k
            code.append(" " * (k - i)); i = k; continue
        if src.startswith("/*", i):
            k = src.find("*/", i + 2)
            if k < 0:
                raise Rig(f"an unterminated /* comment at line {line}")
            body = src[i:k + 2]; line += body.count("\n")
            code.append(re.sub(r"[^\n]", " ", body)); i = k + 2; continue
        m = re.match(r'(?:u8|u|U|L)?R"([^(\s\\]{0,16})\(', src[i:i + 24])
        if m and (i == 0 or not (src[i - 1].isalnum() or src[i - 1] == "_")):
            delim = ")" + m.group(1) + '"'
            start = i + m.end()
            k = src.find(delim, start)
            if k < 0:
                raise Rig(f"an unterminated raw string at line {line}")
            literals.append((i, src[start:k]))
            body = src[i:k + len(delim)]; line += body.count("\n")
            code.append('""' + re.sub(r"[^\n]", " ", body[2:])); i = k + len(delim); continue
        if c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == "\\" else 1
            literals.append((i, src[i + 1:j].replace('\\"', '"')))
            code.append(src[i:j + 1]); i = j + 1; continue
        if c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            code.append(" " * (j + 1 - i)); i = j + 1; continue
        code.append(c); i += 1
    code = "".join(code)
    for m in re.finditer(r'\bSaveToggle\("([A-Za-z0-9_\-]+)"\)', code):
        calls.append((m.start(), m.group(1)))
    # step D: the map's section is rendered by include/MapSettingsHtml.h, not by a literal in this file
    htmls = [m.start() for m in re.finditer(r"\bmapsettingshtml::Html\(", code)]
    return directives, literals, calls, htmls


def header_boxes(header):
    """The checkbox names include/MapSettingsHtml.h renders: read from the header's own string literals with the page's
    own BOX rule (the other side of the contract), never typed here. None found is the rig."""
    _, literals, _, _ = scan(header)
    names = {m.group(1) or m.group(2) for _, text in literals for m in BOX.finditer(text)}
    if not names:
        raise Rig("include/MapSettingsHtml.h holds no checkbox literal: the map section's boxes cannot be read")
    return names


def _tokens(expr, where):
    toks = re.findall(r"\s*(defined|[A-Za-z_][A-Za-z0-9_]*|0[xX][0-9A-Fa-f]+|\d+[uUlL]*|&&|\|\||==|!=|<=|>=|[()!<>])", expr)
    if "".join(t.strip() for t in toks) != re.sub(r"\s+", "", expr):
        raise Rig(f"cannot evaluate the directive at {where}: '{expr}'")
    return [t.strip() for t in toks]


def evaluate(expr, macros, where):
    toks, pos = _tokens(expr, where), [0]

    def peek():
        return toks[pos[0]] if pos[0] < len(toks) else None

    def take(want=None):
        t = peek()
        if t is None or (want is not None and t != want):
            raise Rig(f"cannot evaluate the directive at {where}: '{expr}'")
        pos[0] += 1
        return t

    def value(name):
        if name not in macros:
            return 0
        v = macros[name]
        if re.fullmatch(r"\d+[uUlL]*", v):
            return int(re.sub(r"[uUlL]+$", "", v))
        if v == "":
            return 1
        raise Rig(f"cannot evaluate the directive at {where}: {name} = '{v}' is not a number")

    def primary():
        t = take()
        if t == "defined":
            if peek() == "(":
                take("("); name = take(); take(")")
            else:
                name = take()
            return 1 if name in macros else 0
        if t == "(":
            v = orx(); take(")"); return v
        if t == "!":
            return 0 if primary() else 1
        if re.fullmatch(r"0[xX][0-9A-Fa-f]+", t):
            return int(t, 16)
        if re.fullmatch(r"\d+[uUlL]*", t):
            return int(re.sub(r"[uUlL]+$", "", t))
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", t):
            if peek() == "(":
                raise Rig(f"cannot evaluate the directive at {where}: a function-like {t}(...)")
            return value(t)
        raise Rig(f"cannot evaluate the directive at {where}: '{expr}'")

    def cmp():
        v = primary()
        if peek() in ("==", "!=", "<", ">", "<=", ">="):
            op = take(); w = primary()
            v = {"==": v == w, "!=": v != w, "<": v < w, ">": v > w, "<=": v <= w, ">=": v >= w}[op]
            return 1 if v else 0
        return v

    def andx():
        v = cmp()
        while peek() == "&&":
            take(); w = cmp(); v = 1 if (v and w) else 0
        return v

    def orx():
        v = andx()
        while peek() == "||":
            take(); w = andx(); v = 1 if (v or w) else 0
        return v

    v = orx()
    if peek() is not None:
        raise Rig(f"cannot evaluate the directive at {where}: '{expr}'")
    return v


def liveness(directives, defines):
    """[(offset, live)] breakpoints: the code from each offset on is live or dead, for this define set. #define and
    #undef in a live region update the macros, as the preprocessor would."""
    macros = dict(defines)
    stack = []                      # [parent_live, this_live, taken]
    points = [(0, True)]
    for off, ln, text in directives:
        m = re.match(r"#\s*(\w+)\s*(.*)$", text)
        if not m:
            continue
        kw, rest = m.group(1), m.group(2).strip()
        live = all(e[1] for e in stack)
        where = f"line {ln}"
        if kw in ("if", "ifdef", "ifndef"):
            if not live:
                stack.append([False, False, True])
            else:
                if kw == "ifdef":
                    v = rest.split()[0] in macros
                elif kw == "ifndef":
                    v = rest.split()[0] not in macros
                else:
                    v = bool(evaluate(rest, macros, where))
                stack.append([True, v, v])
        elif kw == "elif":
            if not stack:
                raise Rig(f"#elif without #if at {where}")
            e = stack[-1]
            if not e[0] or e[2]:
                e[1] = False
            else:
                v = bool(evaluate(rest, macros, where)); e[1] = v; e[2] = v
        elif kw == "else":
            if not stack:
                raise Rig(f"#else without #if at {where}")
            e = stack[-1]; e[1] = e[0] and not e[2]; e[2] = True
        elif kw == "endif":
            if not stack:
                raise Rig(f"#endif without #if at {where}")
            stack.pop()
        elif kw == "define" and live:
            dm = re.match(r"([A-Za-z_][A-Za-z0-9_]*)(\()?\s*(.*)$", rest)
            if dm and not dm.group(2):
                macros[dm.group(1)] = dm.group(3).strip()
        elif kw == "undef" and live:
            macros.pop(rest.split()[0], None)
        points.append((off, all(e[1] for e in stack)))
    if stack:
        raise Rig(f"{len(stack)} #if block(s) never closed")
    return points


def live_at(points, off):
    lo, hi = 0, len(points) - 1
    while lo < hi:                  # the last breakpoint at or before off
        mid = (lo + hi + 1) // 2
        if points[mid][0] <= off:
            lo = mid
        else:
            hi = mid - 1
    return points[lo][1]


BOX = re.compile(r'<input\b[^>]*?\bname="([A-Za-z0-9_\-]+)"[^>]*?\btype="checkbox"'
                 r'|<input\b[^>]*?\btype="checkbox"[^>]*?\bname="([A-Za-z0-9_\-]+)"')


def measure(src, defines, hboxes=frozenset()):
    directives, literals, calls, htmls = scan(src)
    points = liveness(directives, defines)
    saved = {name for off, name in calls if live_at(points, off)}
    boxes, box_live = set(), {}
    for off, text in literals:
        lv = live_at(points, off)
        for m in BOX.finditer(text):
            name = m.group(1) or m.group(2)
            box_live[name] = box_live.get(name, False) or lv
            if lv:
                boxes.add(name)
    for off in htmls:   # step D: a live mapsettingshtml::Html( call renders the header's boxes
        lv = live_at(points, off)
        for name in hboxes:
            box_live[name] = box_live.get(name, False) or lv
            if lv:
                boxes.add(name)
    return saved, boxes, box_live


# ------------------------------------------------------------------------------------------------- the check
def run(src, ini, header, out=print):
    hboxes = header_boxes(header)
    v1 = env_defines(ini)
    if "WX_LAYER_SHIPS" in v1:
        raise Rig(f"{ENV} already defines WX_LAYER_SHIPS: V1 would not be the build without weather")
    v2 = dict(v1); v2["WX_LAYER_SHIPS"] = "1"
    out(f"  V1 = {ENV}, -D only ({len(v1)} names): {' '.join(sorted(v1))}")
    out(f"  V2 = V1 + WX_LAYER_SHIPS")
    res, sets = {}, {}
    for tag, d in (("V1", v1), ("V2", v2)):
        saved, boxes, box_live = measure(src, d, hboxes)
        left, unsaved = sorted(saved - boxes), sorted(boxes - saved)
        sets[tag] = (saved, boxes, box_live)
        out(f"  {tag}: |SAVED| = {len(saved)}, |BOXES| = {len(boxes)}, LEFT_ALONE = {{{', '.join(left)}}}, "
            f"UNSAVED = {{{', '.join(unsaved)}}}")
        res[f"A_{tag}"] = (not left, f"LEFT_ALONE({tag}) = {{{', '.join(left)}}}")
        res[f"U_{tag}"] = (not unsaved, f"UNSAVED({tag}) = {{{', '.join(unsaved)}}}")
        ed = [x for x in unsaved if x.startswith(EDITION_PREFIXES)]
        if ed:
            raise Rig(f"UNSAVED({tag}) holds edition names {ed}: the page scan is not scoped to the radar page")
    l1 = sets["V1"][1].__contains__("wx-layer"); l2 = sets["V2"][1].__contains__("wx-layer")
    res["G"] = ((not l1) and l2, f"the wx-layer box: live in V1 {l1}, live in V2 {l2} (want False, True)")
    # step D ((b), beside G): the map's two boxes, rendered by MapSettingsHtml.h under the map's gate
    m1 = sorted(n for n in hboxes if n in sets["V1"][1]); m2 = sorted(n for n in hboxes if n in sets["V2"][1])
    res["M"] = (not m1 and m2 == sorted(hboxes),
                f"the map's boxes {sorted(hboxes)}: live in V1 {m1}, live in V2 {m2} (want none, both)")
    return res


ORDER = ["A_V1", "A_V2", "G", "M"]
# UNSAVED is printed, not asserted: on e1c725a0 it reads {ntfy-regen} in both builds, an ACTION box (/save reads it with
# hasParam and regenerates the ntfy topic; it is never a stored toggle). The registration's pre-registered branch for a
# non-empty UNSAVED (ruled 2026-11-18): information only, and its plant (C4) dropped.
INFO = ["U_V1", "U_V2"]


def main():
    root = sys.argv[1]
    try:
        src = open(f"{root}/src/ConfigurationWebServer.cpp", encoding="utf-8", newline="").read()
        ini = open(f"{root}/platformio.ini", encoding="utf-8", newline="").read()
        hdr = open(f"{root}/include/MapSettingsHtml.h", encoding="utf-8", newline="").read()
    except OSError as e:
        print(f"RIG: cannot read the sources ({e})"); return 2
    crlf = [p for p, t in (("src/ConfigurationWebServer.cpp", src), ("platformio.ini", ini),
                           ("include/MapSettingsHtml.h", hdr)) if "\r" in t]
    if crlf:
        print(f"RIG: CRLF in {crlf}: a checkout with core.autocrlf on; this check reads LF sources only"); return 2
    try:
        if "--selftest" in sys.argv:
            return selftest(src, ini, hdr)
        res = run(src, ini, hdr)
    except Rig as e:
        print(f"RIG: {e}"); return 2
    fails = 0
    for k in ORDER:
        ok, why = res[k]
        print(f"  {'ok  ' if ok else 'FAIL'} {k}: {why}")
        fails += 0 if ok else 1
    for k in INFO:
        print(f"  info {k}: {res[k][1]}")
    return 0 if fails == 0 else 1


FIX_OPEN = GATE + "\n        SaveToggle(\"wx-layer\");"


MAP_GATE = "#if defined(FEATURE_CLOUD_FEED) && defined(WX_LAYER_SHIPS) && !defined(BLIPSCOPE_WEB_RENDERER)"
MAP_SAVE = MAP_GATE + "\n        // Step D: only where the map's section renders"


def selftest(src, ini, hdr):
    """C1-C3 (the registration's addendum 1; C4 dropped with UNSAVED's assertion): each plant runs on a COPY of the
    FIXED source. Step D's plant (docs/blipscope-map-client-steps.md (b)), C4 again by number: SaveToggle("map-layer")
    moved outside the map's gate makes V1's LEFT_ALONE exactly {map-layer}."""
    fix = re.search(r'#if defined\(FEATURE_CLOUD_FEED\) && defined\(WX_LAYER_SHIPS\)\n(\s*SaveToggle\("wx-layer"\);[^\n]*\n'
                    r'(?:\s*//[^\n]*\n)*)#endif\n', src)
    if not fix:
        print("SELFTEST: the fixed save (SaveToggle(\"wx-layer\") under its box's condition) is not in this source; "
              "the plants are defined against the fixed code")
        return 3
    box_gate = src.find(GATE + "\n                // Only a build that can fetch weather shows the box.")
    plants = [
        ("C1 the gate is evaluated: box and save both under FEATURE_CLOUD_FEED alone", {"G"},
         lambda s: s.replace(fix.group(0), "#if defined(FEATURE_CLOUD_FEED)\n" + fix.group(1) + "#endif\n")
                    .replace(GATE + "\n                // Only a build", "#if defined(FEATURE_CLOUD_FEED)\n                // Only a build", 1)),
        ("C2 today restored: the fix's #if removed", {"A_V1"},
         lambda s: s.replace(fix.group(0), fix.group(1))),
        ("C3 a new unboxed toggle: SaveToggle(\"zz-plant\")", {"A_V1", "A_V2"},
         lambda s: s.replace('        SaveToggle("mqtt-disco");\n',
                             '        SaveToggle("mqtt-disco");\n        SaveToggle("zz-plant");\n', 1)),
        ("C4 step D: SaveToggle(\"map-layer\") outside the map's gate", {"A_V1"},
         lambda s: s.replace('        SaveToggle("map-layer");\n', "", 1)
                    .replace(MAP_SAVE, '        SaveToggle("map-layer");\n' + MAP_SAVE, 1)),
    ]
    if box_gate < 0:
        print("SELFTEST: the box's gate (followed by its 'Only a build that can fetch weather' comment) is not found")
        return 3
    if src.count(MAP_SAVE) != 1 or src.count('        SaveToggle("map-layer");\n') != 1:
        print("SELFTEST: the map's gated save (step D) is not found exactly once")
        return 3
    bad = 0
    for label, want, plant in plants:
        copy = plant(src)
        if copy == src:
            print(f"  SELFTEST {label}: the plant did NOT apply"); bad += 1; continue
        try:
            res = run(copy, ini, hdr, out=lambda *_: None)
        except Rig as e:
            print(f"  SELFTEST {label}: RIG {e}"); bad += 1; continue
        red = {k for k in ORDER if not res[k][0]}
        ok = red == want
        if label.startswith("C4") and res["A_V1"][1] != "LEFT_ALONE(V1) = {map-layer}":
            ok = False   # the registered red is exactly {map-layer} in V1's LEFT_ALONE
            print(f"  SELFTEST {label}: {res['A_V1'][1]}, want LEFT_ALONE(V1) = {{map-layer}}")
        print(f"  SELFTEST {label}: red = {sorted(red)}, want {sorted(want)} ({'exact' if ok else 'WRONG'})")
        bad += 0 if ok else 1
    print("SELFTEST PASSED" if bad == 0 else f"SELFTEST FAILED ({bad})")
    return 0 if bad == 0 else 3


if __name__ == "__main__":
    sys.exit(main())
