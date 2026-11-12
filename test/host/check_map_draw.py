"""Step B's source checks (docs/blipscope-map-client-steps.md, step B; registered 2026-11-11, "the refusal's commit").

CD4, CB8 and CB8b; CD1-CD3 join in the commit that adds the files they read.

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
  Function bodies are cut out before the comment stripper reads them: only the map's own text ever goes through it.

The region counts are pinned. A count off in either direction, or an unbalanced marker, is the rig (exit 2): a region the
check cannot see is code it cannot vouch for. ANY exception is the rig too (exit 2, "RIG: <type>: <message>"), never a
red: a crash is an instrument failure (the 11-04 rule). R-CD-RAWOK, R-CD-CRASH and R-CD-DECODE hold both directions.

    python -I test/host/check_map_draw.py <repo root>    exit 0 all pass; 1 a check failed; 2 the rig (cannot read)
Its plants are S-CD-NOREFUSAL and S-CD-STEPC in test/host/sabotage_map_draw.py.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_map_fetch import strip   # one comment stripper for the map's source checks, never a second copy

REGION_COUNTS = {"src/AircraftManager.h": 1, "src/AircraftManager.cpp": 0}
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


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    try:
        rows = [("CD4",) + cd4(root), ("CB8", cb8(root), None), ("CB8b", cb8b(root), None)]
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
        else:
            print(f"  ok   {name}: the drawers hold their expressions, and MapPlaces.h's pinned values equal them")
    print(f"{len(rows)} checks, {fails} failures")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
