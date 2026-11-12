"""Step B's source checks (docs/blipscope-map-client-steps.md, step B; registered 2026-11-11, "the refusal's commit").

CD4, CB8 and CB8b; the wiring's CD1-CD3 and CD5-CD7 (registered 2026-11-12, "The wiring: registered before its code").

  CD4  CB10's refusal runs, not only its rule. If src/AircraftManager.h or .cpp holds any region between a line
       `// THE MAP DRAW BEGIN` and a line `// THE MAP DRAW END`, the comment-stripped header holds CB10's `#if` line and
       its `#error` line, exactly (whitespace normalised). And BLIPSCOPE_MAP_STEP_C, step C's macro, is defined nowhere
       but in ALLOWED_STEP_C_DEFINERS (empty until step C names its header).
       THE DEFINER SCAN IS LINE-BASED, never through the C++ stripper (L1's first run: include/OtaCerts.h's raw string
       crashed it): a directive line `#define BLIPSCOPE_MAP_STEP_C` in include/ and src/, and `-DBLIPSCOPE_MAP_STEP_C`
       in platformio.ini and tools/ after ';' and '#' comments are removed. Its known direction: a matching line inside a
       raw string or a block comment is flagged, which fails safe and visibly (a red naming the file).

  CB8  ONE RULE for an airport's boxes: AircraftManager::DrawAirports takes its mark and code boxes from
       radarbox::Airport (include/AirportBoxes.h) and draws at them, and include/MapPlaces.h builds the names' airport
       obstacles from the same function.
  CB8b Q-MB9 (b): the three other drawers the names avoid are left as they are, and pinned. DrawRadarCircles and
       DrawScreenIndicator must still hold the exact expressions the names transcribe, AND every value MapPlaces.h uses
       (between its PINNED VALUES markers) must equal what those expressions give, derived here from the drawers' own
       text: SCREEN_SIZE from include/variants/s3_128.h, RING_LABEL_OUTER_INSET from AircraftManager.cpp, and the
       carousel's dot count from the Screen enum and InCarousel. A changed formula and a changed constant both fail.
  CD1  F8 for the map's files: MapCompose.h, MapPlaces.h, AirportBoxes.h and every draw region hold no Preferences, no
       .put*( call, no SPIFFS and no LittleFS.
  CD2  Privacy: the three headers print nothing (no Serial, printf( other than snprintf(, puts(, ESP_LOG or log_); in the
       draw regions no print's arguments, string literals removed, name a place, an anchor or a coordinate (the tokens
       in PRIVATE). A town's name near the device says where the device is.
  CD3  DrawMapBase fills the six View fields with the right-hand sides RebuildWeatherLayer gives the weather layer's.
  CD5  The allocator handed to mapcompose::Init calls heap_caps_aligned_alloc with MALLOC_CAP_SPIRAM in the same call:
       the price's premise (PSRAM costs nothing internally) made a check that runs.
  CD6  Every write of cloudAirports, airportsMin or displayAirports in AircraftManager.cpp is followed within 10 lines by
       `++mapAirportsGen;`. Line-based (the .cpp never goes through the stripper); a reference declaration
       (`const T& name = ...`) and another object's member are not writes. Finding no write at all is the rig.
  CD7  MapAirportBoxes holds DrawAirports' list, filter and cull lines (those naming fabsf(, airportsMin, cloudAirports,
       AIRPORT_COUNT, AIRPORTS[ or wide), comment-stripped and whitespace-normalised, in the same order: the second
       path's guard (Q-MW2). Fewer than 6 such lines in DrawAirports is the rig.
  CB-B1 The map's bench (docs/blipscope-map-client-steps.md, "The mapbench env"): every MAP_BENCH token in src/ and include/
       sits inside a `// THE MAP BENCH BEGIN` / `END` region, the regions are pinned (BENCH_COUNTS), and each holds
       `#ifdef MAP_BENCH`. Line-based, like CD4's definer scan: a comment naming the macro outside a region is flagged.
  CB-B2 The bench regions hold none of CD1's NVS list, and no print in them names a token in PRIVATE (CD2's rule).
  Function bodies are cut out before the comment stripper reads them: only the map's own text ever goes through it.

The region counts are pinned. A count off in either direction, or an unbalanced marker, is the rig (exit 2): a region the
check cannot see is code it cannot vouch for. ANY exception is the rig too (exit 2, "RIG: <type>: <message>"), never a
red: a crash is an instrument failure (the 11-04 rule). R-CD-RAWOK, R-CD-CRASH and R-CD-DECODE hold both directions.

    python -I test/host/check_map_draw.py <repo root>    exit 0 all pass; 1 a check failed; 2 the rig (cannot read)
Its plants are in test/host/sabotage_map_draw.py.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_map_fetch import strip   # one comment stripper for the map's source checks, never a second copy

REGION_COUNTS = {"src/AircraftManager.h": 4, "src/AircraftManager.cpp": 12}
MAP_FILES = ["include/MapCompose.h", "include/MapPlaces.h", "include/AirportBoxes.h"]
NVS = [(re.compile(r"\bPreferences\b"), "Preferences"), (re.compile(r"\.put[A-Z]\w*\s*\("), "a .put*( call"),
       (re.compile(r"\bSPIFFS\b"), "SPIFFS"), (re.compile(r"\bLittleFS\b"), "LittleFS")]
HEADER_PRINTS = [re.compile(r"\bSerial\b"), re.compile(r"(?<![sn])printf\s*\("), re.compile(r"\bputs\s*\("),
                 re.compile(r"\bESP_LOG"), re.compile(r"\blog_[a-z]\w*\s*\(")]
PRINT_CALL = re.compile(r"\b(?:Serial\.(?:printf|println|print)|printf|log_[a-z]\w*|ESP_LOG\w*)\s*\(")
PRIVATE = {"label", "places", "placed", "Town", "lat", "lon", "homeLat", "homeLon", "ix", "iy", "tx", "ty", "qx", "qy",
           "anchor"}
STRING = re.compile(r'"(?:\\.|[^"\\])*"')
SIX = ("homeLat", "homeLon", "rLat", "rLon", "rotSin", "rotCos")
WRITE = re.compile(r"\b(?:cloudAirports\s*=(?!=)|cloudAirports\.(?:clear|assign|swap|push_back|emplace_back|erase|insert"
                   r"|resize)\s*\(|airportsMin\s*=(?!=)|displayAirports\s*=(?!=))")
BUMP = "++mapAirportsGen;"
CULL_KEYS = ("fabsf(", "airportsMin", "cloudAirports", "AIRPORT_COUNT", "AIRPORTS[", "wide")
BENCH_COUNTS = {"src/AircraftManager.h": 1, "src/AircraftManager.cpp": 7}
BENCH_BEGIN, BENCH_END = "// THE MAP BENCH BEGIN", "// THE MAP BENCH END"
BENCH_TOKEN = re.compile(r"\bMAP_BENCH\b")
BEGIN, END = "// THE MAP DRAW BEGIN", "// THE MAP DRAW END"
REFUSAL_IF = "#if defined(BLIPSCOPE_MAP_FETCH) && !defined(WX_LOOK_AB) && !defined(BLIPSCOPE_MAP_STEP_C)"
REFUSAL_ERROR = ('#error "step B draws the map without step C: the sweep erases it. '
                 'Land step C before WX_LAYER_SHIPS ships."')
ALLOWED_STEP_C_DEFINERS = set()   # step C adds its header here, in the commit that defines the macro
STEP_C = "BLIPSCOPE_MAP_STEP_C"
CPP_DEFINE = re.compile(r"^\s*#\s*define\s+" + STEP_C + r"\b", re.M)
FLAG_DEFINE = re.compile(r"-D\s*" + STEP_C + r"\b")


class Rig(Exception):
    pass


def read(root, rel):
    try:
        return open(os.path.join(root, rel), "rb").read().decode("utf-8").replace("\r\n", "\n")
    except OSError as e:
        raise Rig(f"cannot read {rel}: {e}")


def regions(rel, text, want):
    found, cur = [], None
    for ln in text.split("\n"):
        s = ln.strip()
        if s == BEGIN:
            if cur is not None:
                raise Rig(f"{rel}: a BEGIN inside an open region")
            cur = []
        elif s == END:
            if cur is None:
                raise Rig(f"{rel}: an END without its BEGIN")
            found.append("\n".join(cur))
            cur = None
        elif cur is not None:
            cur.append(ln)
    if cur is not None:
        raise Rig(f"{rel}: a BEGIN without its END")
    if len(found) != want:
        raise Rig(f"{rel}: {len(found)} draw regions, the check pins {want}")
    return found


def norm(line):
    return " ".join(line.split())


def files_under(root, top, exts):
    base = os.path.join(root, top)
    if not os.path.isdir(base):
        return []
    out = []
    for d, _, names in os.walk(base):
        for n in names:
            if n.endswith(exts):
                out.append(os.path.relpath(os.path.join(d, n), root).replace(os.sep, "/"))
    return sorted(out)


def ini_strip(text):
    """platformio.ini and shell text with their comments removed: ';' and '#' to the end of a line."""
    return "\n".join(re.split(r"[;#]", ln, maxsplit=1)[0] for ln in text.split("\n"))


def cd4(root):
    why = []
    header = read(root, "src/AircraftManager.h")
    total = 0
    for rel, want in REGION_COUNTS.items():
        total += len(regions(rel, read(root, rel) if rel != "src/AircraftManager.h" else header, want))
    if total:
        lines = {norm(ln) for ln in strip(header).split("\n")}
        if norm(REFUSAL_IF) not in lines:
            why.append("a draw region exists, and CB10's #if line is missing from src/AircraftManager.h")
        if norm(REFUSAL_ERROR) not in lines:
            why.append("a draw region exists, and CB10's #error line is missing from src/AircraftManager.h")
    definers = []
    for rel in files_under(root, "include", (".h", ".hpp")) + files_under(root, "src", (".h", ".hpp", ".cpp", ".c")):
        if CPP_DEFINE.search(read(root, rel)):   # line-based: a directive begins its line, so a // comment cannot match
            definers.append(rel)
    for rel in (["platformio.ini"] if os.path.isfile(os.path.join(root, "platformio.ini")) else []) + \
            files_under(root, "tools", (".sh", ".ini", ".py", ".mjs", ".js", ".json")):
        if FLAG_DEFINE.search(ini_strip(read(root, rel))):
            definers.append(rel)
    stray = [d for d in definers if d not in ALLOWED_STEP_C_DEFINERS]
    if stray:
        why.append(f"{STEP_C} is defined outside step C's header: {', '.join(stray)}")
    return why, total


def body_of(text, signature, rel):
    """One function's text, from its signature to the first closing brace at column 0. The rig if it is not there."""
    i = text.find(signature)
    if i < 0:
        raise Rig(f"{rel}: cannot find {signature!r}")
    j = text.find("\n}\n", i)
    if j < 0:
        raise Rig(f"{rel}: {signature!r} has no closing brace at column 0")
    return text[i:j + 3]


def flat(code):
    return " ".join(strip(code).split())


def cb8(root):
    why = []
    amcpp = read(root, "src/AircraftManager.cpp")
    draw = flat(body_of(amcpp, "void AircraftManager::DrawAirports(", "src/AircraftManager.cpp"))
    for need, what in (("radarbox::Airport(x, y, (int)strlen(code), &mark, &codeBox);", "takes its boxes from radarbox::Airport"),
                       ("backbuffer.drawRect(mark.x0, mark.y0, mark.x1 - mark.x0 + 1, mark.y1 - mark.y0 + 1, MARK);",
                        "draws the mark at radarbox::Airport's box"),
                       ("backbuffer.drawString(code, codeBox.x0, codeBox.y0);", "draws the code at radarbox::Airport's box")):
        if need not in draw:
            why.append(f"DrawAirports no longer {what}")
    if "radarbox::Airport(x, y, (int)strlen(code), &out[0], &out[1]);" not in flat(read(root, "include/MapPlaces.h")):
        why.append("MapPlaces.h's airport obstacles do not come from radarbox::Airport")
    return why


RINGS = ["constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;",
         "constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;",
         "const int ringPx[3] = { OUTER, (OUTER / 3) * 2, OUTER / 3 };",
         "const int inset[3] = { RING_LABEL_OUTER_INSET, 3, 3 };",
         "String label = String(value, value < 10.0f ? 1 : 0);",
         "backbuffer.drawString(label, CENTRE + 4, CENTRE - ringPx[i] + inset[i]);",
         "const int labelR = CENTRE - 7;",
         "const int px = CENTRE + (int)lroundf(labelR * cosf(a));",
         "const int py = CENTRE + (int)lroundf(labelR * sinf(a));",
         "backbuffer.drawString(p.c, px - (int)backbuffer.textWidth(p.c) / 2, py - 4);"]
DOTS = ["constexpr int cx = SCREEN_SIZE_DIV_2;",
        "const int y = SCREEN_SIZE - 16;",
        "backbuffer.fillCircle(cx - (shown - 1) * 6 + slot * 12, y, active ? 3 : 2,"]


def one_int(pattern, text, rel):
    m = re.findall(pattern, text)
    if len(m) != 1:
        raise Rig(f"{rel}: {pattern!r} found {len(m)} times, need exactly 1")
    return int(m[0])


def cb8b(root):
    why = []
    amcpp = read(root, "src/AircraftManager.cpp")
    rings = flat(body_of(amcpp, "void AircraftManager::DrawRadarCircles(", "src/AircraftManager.cpp"))
    dots = flat(body_of(amcpp, "void AircraftManager::DrawScreenIndicator(", "src/AircraftManager.cpp"))
    for need in RINGS:
        if need not in rings:
            why.append(f"DrawRadarCircles no longer holds `{need}`")
    for need in DOTS:
        if need not in dots:
            why.append(f"DrawScreenIndicator no longer holds `{need}`")
    size = one_int(r"constexpr int SCREEN_SIZE = (\d+);", read(root, "include/variants/s3_128.h"), "include/variants/s3_128.h")
    inset = one_int(r"static constexpr int RING_LABEL_OUTER_INSET = (\d+);", amcpp, "src/AircraftManager.cpp")
    amh = read(root, "src/AircraftManager.h")
    enum = re.findall(r"enum class Screen \{([^}]*)\};", amh)
    if len(enum) != 1 or "return s != Screen::Follow;" not in amh:
        raise Rig("src/AircraftManager.h: cannot read the Screen enum or InCarousel's device rule")
    shown = len([s for s in enum[0].split(",") if s.strip()]) - 1   # every screen but Follow (a modal)
    centre = size // 2 - 1
    outer = size // 2 - 1
    ring_px = [outer, (outer // 3) * 2, outer // 3]
    insets = [inset, 3, 3]
    cx, y = size // 2, size - 16
    xs = [cx - (shown - 1) * 6 + slot * 12 for slot in range(shown)]
    derived = {"kRingCentre": centre, "kRingLabelX": centre + 4,
               "kRingLabelY0": centre - ring_px[0] + insets[0], "kRingLabelY1": centre - ring_px[1] + insets[1],
               "kRingLabelY2": centre - ring_px[2] + insets[2], "kCardinalR": centre - 7,
               "kDotsX0": xs[0] - 3, "kDotsY0": y - 3, "kDotsX1": xs[-1] + (2 if shown > 1 else 3), "kDotsY1": y + 3}
    places = read(root, "include/MapPlaces.h")
    a, b = places.find("// THE PINNED VALUES BEGIN"), places.find("// THE PINNED VALUES END")
    if a < 0 or b < a:
        raise Rig("include/MapPlaces.h: no PINNED VALUES region")
    pinned = {k: int(v) for k, v in re.findall(r"constexpr int (k\w+) = (-?\d+);", places[a:b])}
    if set(pinned) != set(derived):
        raise Rig(f"include/MapPlaces.h: the pinned names {sorted(pinned)} are not {sorted(derived)}")
    for k, v in derived.items():
        if pinned[k] != v:
            why.append(f"MapPlaces.h's {k} is {pinned[k]}, but the drawers give {v}")
    return why


def draw_regions(root):
    """Every draw region's text, comment-stripped, named by file and position."""
    out = []
    for rel, want in REGION_COUNTS.items():
        for i, r in enumerate(regions(rel, read(root, rel), want)):
            out.append((f"{rel} draw region {i + 1}", strip(r)))
    return out


def cd1(root):
    why = []
    texts = [(rel, strip(read(root, rel))) for rel in MAP_FILES] + draw_regions(root)
    for where, code in texts:
        for pat, what in NVS:
            if pat.search(code):
                why.append(f"{where} holds {what}")
    return why, len(texts) - len(MAP_FILES)


def print_args(code):
    """Each print call's argument text, its string literals removed first."""
    code = STRING.sub('""', code)
    out = []
    for m in PRINT_CALL.finditer(code):
        depth, i = 1, m.end()
        while i < len(code) and depth:
            depth += {"(": 1, ")": -1}.get(code[i], 0)
            i += 1
        out.append(code[m.end():i - 1])
    return out


def cd2(root):
    why = []
    for rel in MAP_FILES:
        code = strip(read(root, rel))
        for pat in HEADER_PRINTS:
            if pat.search(code):
                why.append(f"{rel} prints ({pat.pattern})")
    for where, code in draw_regions(root):
        for args in print_args(code):
            named = sorted(PRIVATE & set(re.findall(r"[A-Za-z_]\w*", args)))
            if named:
                why.append(f"{where} prints {', '.join(named)}")
    return why


def assigned(body, prefix):
    out = {}
    for field, rhs in re.findall(r"(?<![\w.])" + prefix + r"\.(\w+)\s*=\s*([^;]+);", body):
        if field in SIX:
            out.setdefault(field, []).append(norm(rhs))
    return out


def cd3(root):
    amcpp = read(root, "src/AircraftManager.cpp")
    wx = assigned(strip(body_of(amcpp, "bool AircraftManager::RebuildWeatherLayer(", "src/AircraftManager.cpp")), r"v")
    mp = assigned(strip(body_of(amcpp, "bool AircraftManager::DrawMapBase(", "src/AircraftManager.cpp")), r"k\.view")
    if sorted(wx) != sorted(SIX) or any(len(v) != 1 for v in wx.values()):
        raise Rig(f"RebuildWeatherLayer: cannot read its six View fields exactly once each ({sorted(wx)})")
    why = []
    for f in SIX:
        if f not in mp:
            why.append(f"DrawMapBase does not fill view.{f}")
        elif mp[f] != wx[f]:
            why.append(f"DrawMapBase fills view.{f} with {mp[f]}, the weather layer with {wx[f]}")
    return why


def cd5(root):
    amcpp = read(root, "src/AircraftManager.cpp")
    calls = []
    for _, code in draw_regions(root):
        calls += re.findall(r"mapcompose::Init\(\s*\w+\s*,\s*(\w+)\s*,\s*(\w+)\s*\)", code)
    if len(calls) != 1:
        return [f"{len(calls)} mapcompose::Init calls in the draw regions, not exactly 1"]
    alloc = calls[0][0]
    body = strip(body_of(amcpp, f"void* AircraftManager::{alloc}(", "src/AircraftManager.cpp"))
    asks = re.findall(r"heap_caps_aligned_alloc\s*\(([^;]*)\)\s*;", body)
    if not any("MALLOC_CAP_SPIRAM" in a for a in asks):
        return [f"{alloc}, the allocator given to mapcompose::Init, does not ask heap_caps_aligned_alloc for MALLOC_CAP_SPIRAM"]
    return []


def cd6(root):
    lines = read(root, "src/AircraftManager.cpp").split("\n")
    code = [ln.split("//", 1)[0] for ln in lines]   # line-based: the .cpp never goes through the stripper
    sites, why = [], []
    for i, c in enumerate(code):
        for m in WRITE.finditer(c):
            before = c[:m.start()]
            if re.search(r"&\s*$", before) or re.search(r"(\.|->)\s*$", before):
                continue   # a reference declaration, or another object's member: not a write of ours
            sites.append(i)
    if not sites:
        raise Rig("src/AircraftManager.cpp: no write of cloudAirports, airportsMin or displayAirports found")
    for i in sites:
        if not any(BUMP in c for c in code[i + 1:i + 11]):
            why.append(f"src/AircraftManager.cpp:{i + 1} writes the airport list or its filters without {BUMP} after it")
    return why, len(sites)


def cull_lines(code):
    return [norm(ln) for ln in strip(code).split("\n") if norm(ln) and any(k in ln for k in CULL_KEYS)]


def cd7(root):
    amcpp = read(root, "src/AircraftManager.cpp")
    want = cull_lines(body_of(amcpp, "void AircraftManager::DrawAirports(", "src/AircraftManager.cpp"))
    if len(want) < 6:
        raise Rig(f"DrawAirports: {len(want)} list, filter and cull lines found, fewer than 6")
    have = [norm(ln) for ln in strip(body_of(amcpp, "int AircraftManager::MapAirportBoxes(", "src/AircraftManager.cpp"))
            .split("\n") if norm(ln)]
    j = 0
    for w in want:
        while j < len(have) and have[j] != w:
            j += 1
        if j == len(have):
            return [f"MapAirportBoxes does not hold DrawAirports' `{w}` in its place"], len(want)
        j += 1
    return [], len(want)


def bench_regions(rel, text, want):
    """Each bench region's (first line number, its lines), the markers balanced and counted. Line-based."""
    found, cur, start = [], None, 0
    for i, ln in enumerate(text.split("\n")):
        s = ln.strip()
        if s == BENCH_BEGIN:
            if cur is not None:
                raise Rig(f"{rel}: a bench BEGIN inside an open bench region")
            cur, start = [], i
        elif s == BENCH_END:
            if cur is None:
                raise Rig(f"{rel}: a bench END without its BEGIN")
            found.append((start, i, cur))
            cur = None
        elif cur is not None:
            cur.append(ln)
    if cur is not None:
        raise Rig(f"{rel}: a bench BEGIN without its END")
    if len(found) != want:
        raise Rig(f"{rel}: {len(found)} bench regions, the check pins {want}")
    return found


def cbb1(root):
    why, tokens = [], 0
    spans = {}
    for rel, want in BENCH_COUNTS.items():
        spans[rel] = bench_regions(rel, read(root, rel), want)
        for start, _, lines in spans[rel]:
            if not any(ln.strip() == "#ifdef MAP_BENCH" for ln in lines):
                why.append(f"{rel}: the bench region at line {start + 1} has no #ifdef MAP_BENCH")
    for rel in files_under(root, "include", (".h", ".hpp")) + files_under(root, "src", (".h", ".hpp", ".cpp", ".c")):
        text = read(root, rel)
        if not BENCH_TOKEN.search(text):
            continue
        inside = [(s, e) for s, e, _ in spans.get(rel, [])]
        for i, ln in enumerate(text.split("\n")):
            if BENCH_TOKEN.search(ln):
                tokens += 1
                if not any(s < i < e for s, e in inside):
                    why.append(f"{rel}:{i + 1} names MAP_BENCH outside a bench region")
    return why, (tokens, sum(len(v) for v in spans.values()))


def cbb2(root):
    why = []
    for rel, want in BENCH_COUNTS.items():
        for start, _, lines in bench_regions(rel, read(root, rel), want):
            code = strip("\n".join(lines))
            where = f"{rel} bench region at line {start + 1}"
            for pat, what in NVS:
                if pat.search(code):
                    why.append(f"{where} holds {what}")
            for args in print_args(code):
                named = sorted(PRIVATE & set(re.findall(r"[A-Za-z_]\w*", args)))
                if named:
                    why.append(f"{where} prints {', '.join(named)}")
    return why


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    try:
        rows = [("CD4",) + cd4(root), ("CB8", cb8(root), None), ("CB8b", cb8b(root), None), ("CD1",) + cd1(root),
                ("CD2", cd2(root), None), ("CD3", cd3(root), None), ("CD5", cd5(root), None), ("CD6",) + cd6(root),
                ("CD7",) + cd7(root), ("CB-B1",) + cbb1(root), ("CB-B2", cbb2(root), None)]
    except Exception as e:   # ANY exception, the stripper's own Rig and a decode error included: the rig, never a red
        print(f"RIG: {type(e).__name__}: {e}")
        return 2
    fails = 0
    for name, why, regions_seen in rows:
        if why:
            fails += 1
            for w in why:
                print(f"  FAIL {name}: {w}")
        elif name == "CD4":
            print(f"  ok   {name}: {regions_seen} draw region(s); the refusal present; {STEP_C} defined nowhere else")
        elif name == "CB8":
            print(f"  ok   {name}: DrawAirports and the names' obstacles both read radarbox::Airport")
        elif name == "CB8b":
            print(f"  ok   {name}: the drawers hold their expressions, and MapPlaces.h's pinned values equal them")
        elif name == "CD1":
            print(f"  ok   {name}: the map's 3 files and {regions_seen} draw region(s) hold no Preferences, .put*(, "
                  "SPIFFS or LittleFS")
        elif name == "CD2":
            print(f"  ok   {name}: the map's files print nothing, and no draw region prints a place, an anchor or a "
                  "coordinate")
        elif name == "CD3":
            print(f"  ok   {name}: the map's View takes the weather layer's six fields")
        elif name == "CD5":
            print(f"  ok   {name}: the allocator given to mapcompose::Init asks for MALLOC_CAP_SPIRAM")
        elif name == "CD6":
            print(f"  ok   {name}: {regions_seen} write site(s), each followed by {BUMP}")
        elif name == "CD7":
            print(f"  ok   {name}: MapAirportBoxes holds DrawAirports' {regions_seen} list, filter and cull lines, in order")
        elif name == "CB-B1":
            print(f"  ok   {name}: {regions_seen[0]} MAP_BENCH line(s), all inside the {regions_seen[1]} bench region(s), "
                  "each under #ifdef MAP_BENCH")
        else:
            print(f"  ok   {name}: the bench regions hold no NVS and print no place, anchor or coordinate")
    print(f"{len(rows)} checks, {fails} failures")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
