"""Step B's source checks (docs/blipscope-map-client-steps.md, step B; registered 2026-11-11, "the refusal's commit").

CD4 alone until the draw files exist; CD1-CD3 join in the commit that adds the files they read.

  CD4  CB10's refusal runs, not only its rule. If src/AircraftManager.h or .cpp holds any region between a line
       `// THE MAP DRAW BEGIN` and a line `// THE MAP DRAW END`, the comment-stripped header holds CB10's `#if` line and
       its `#error` line, exactly (whitespace normalised). And BLIPSCOPE_MAP_STEP_C, step C's macro, is defined nowhere
       but in ALLOWED_STEP_C_DEFINERS (empty until step C names its header).
       THE DEFINER SCAN IS LINE-BASED, never through the C++ stripper (L1's first run: include/OtaCerts.h's raw string
       crashed it): a directive line `#define BLIPSCOPE_MAP_STEP_C` in include/ and src/, and `-DBLIPSCOPE_MAP_STEP_C`
       in platformio.ini and tools/ after ';' and '#' comments are removed. Its known direction: a matching line inside a
       raw string or a block comment is flagged, which fails safe and visibly (a red naming the file).

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


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    try:
        rows = [("CD4",) + cd4(root)]
    except Exception as e:   # ANY exception, the stripper's own Rig and a decode error included: the rig, never a red
        print(f"RIG: {type(e).__name__}: {e}")
        return 2
    fails = 0
    for name, why, regions_seen in rows:
        if why:
            fails += 1
            for w in why:
                print(f"  FAIL {name}: {w}")
        else:
            print(f"  ok   {name}: {regions_seen} draw region(s); the refusal present; {STEP_C} defined nowhere else")
    print(f"{len(rows)} checks, {fails} failures")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
