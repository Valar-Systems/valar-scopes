"""P5 1c's static row (docs/speed-p5p6.md 4o): every pixel write in the radar-weather frame is marked for the dirty
restore, or forces a full one. The audit, as a check that runs.

  W-CANVAS  every method of include/BandCanvas.h that forwards a drawing call (s_.fill*, draw*, push*, write*, clear*)
            marks (dirtyclear::Mark*) or calls DirtyAll() in its body
  W-DIRECT  every direct write to a canvas buffer in src/AircraftManager.cpp, src/main.cpp and include/*.h (a
            `.sprite()` handle, `getBuffer()`, or `pushSprite(&`) lies in a function that marks (MarkSpan,
            dirtyclear::Restore) or calls DirtyAll(), unless ALLOWED below names it with its reason
  W-GEN     every function that writes INTO the weather layer (wxSprite.fill*/draw*/write*/clear*, or a memcpy into
            wxLayer565) bumps wxLayerGen

    python -I check_p5_writes.py <tree>             exit 0 every row ok; 1 a row off; 2 the rig (a row found no sites)
    python -I check_p5_writes.py --selftest <tree>  the clean tree green; each plant reads exactly its row red
"""
import os
import re
import shutil
import sys
import tempfile

CANVAS = "include/BandCanvas.h"
AM = "src/AircraftManager.cpp"
DIRECT_FILES = [AM, "src/main.cpp"]   # plus every include/*.h, found at run time

# Sites that touch a buffer without writing the canvas, each by its exact text (stripped) or a pattern, with why.
ALLOWED = [
    (re.compile(r"photoSprite\.getBuffer\(\) [!=]= nullptr"), "a null test on the photo sprite, not a write"),
    (re.compile(r"fb\.getBuffer\(\) != nullptr"), "a null test on the boot/splash sprite before the radar ever draws"),
    (re.compile(r"const_cast<LGFX_Sprite&>\(s\)\.getBuffer\(\)"), "the outline's 1-bit scratch (P5 1a), read; not the canvas"),
    (re.compile(r"uint8_t\* const p = \(uint8_t\*\)g_buf\[i\]->getBuffer\(\);"), "M3-9's coherency self-check, a DMA read"),
    (re.compile(r"if \(g_grab\) memcpy\(g_grab, s\.getBuffer\(\), n\);"), "/diag/fb's grab, a read"),
    (re.compile(r"m3bench::MaybeGrabMidDraw\(canvas\.sprite\(\)\);"), "M3-1b's mid-draw grab (bench), a read"),
]
SITE = re.compile(r"\.sprite\(\)|pushSprite\(&|getBuffer\(\)")
MARKS = re.compile(r"DirtyAll\(\)|MarkSpan\(|dirtyclear::Restore\(|dirtyclear::Mark")
LAYER_WRITE = re.compile(r"wxSprite\.(fill|draw|write|clear)\w*\(|memcpy\(wxLayer565")
GEN = re.compile(r"\+\+wxLayerGen|wxLayerGen\+\+")
DRAWS = re.compile(r"\bs_\.(fill|draw|push|write|clear)\w*\(")


def read(tree, rel):
    with open(os.path.join(tree, *rel.split("/")), encoding="utf-8") as f:
        return f.read().split("\n")


def function_of(lines, i):
    """The enclosing function's lines [start, end] for line i: braces at column 0, this codebase's style."""
    s = i
    while s > 0 and lines[s] != "{":
        s -= 1
    e = i
    while e < len(lines) - 1 and lines[e] != "}":
        e += 1
    return s, e


def canvas_methods(text):
    """[(name, body)] for every method defined in the class body."""
    out = []
    for m in re.finditer(r"(\w+)\(([^()]|\([^()]*\))*\)\s*(const\s*)?\{", text):
        name = m.group(1)
        if name in ("if", "for", "while", "switch", "BandCanvas"):
            continue
        depth, j = 0, m.end() - 1
        while j < len(text):
            depth += text[j] == "{"
            depth -= text[j] == "}"
            if depth == 0:
                break
            j += 1
        out.append((name, text[m.end():j]))
    return out


def check(tree):
    res = {}
    ctext = "\n".join(read(tree, CANVAS))
    methods = canvas_methods(ctext.split("class BandCanvas", 1)[1] if "class BandCanvas" in ctext else "")
    drawing = [(n, b) for n, b in methods if DRAWS.search(b)]
    unmarked = [n for n, b in drawing if not MARKS.search(b)]
    res["W-CANVAS"] = (bool(drawing) and not unmarked,
                       f"{len(drawing)} drawing methods, unmarked: {', '.join(unmarked) or 'none'}", len(drawing))

    files = DIRECT_FILES + sorted("include/" + n for n in os.listdir(os.path.join(tree, "include")) if n.endswith(".h"))
    sites = allowed = marked = 0
    bad = []
    for rel in files:
        lines = read(tree, rel)
        for i, l in enumerate(lines):
            if not SITE.search(l) or rel == CANVAS:
                continue
            sites += 1
            if any(p.search(l) for p, _ in ALLOWED):
                allowed += 1
                continue
            s, e = function_of(lines, i)
            if any(MARKS.search(x) for x in lines[s:e + 1]):
                marked += 1
            else:
                bad.append(f"{rel}:{i + 1}")
    res["W-DIRECT"] = (sites > 0 and not bad,
                       f"{sites} sites: {marked} in marking functions, {allowed} allowed; unmarked: "
                       f"{', '.join(bad) or 'none'}", sites)

    lines = read(tree, AM)
    writers, missing, seen = 0, [], set()
    for i, l in enumerate(lines):
        if not LAYER_WRITE.search(l):
            continue
        s, e = function_of(lines, i)
        if s in seen:
            continue
        seen.add(s)
        writers += 1
        if not any(GEN.search(x) for x in lines[s:e + 1]):
            missing.append(lines[s - 1].strip()[:60])
    res["W-GEN"] = (writers > 0 and not missing,
                    f"{writers} function(s) write the layer; without a generation bump: {', '.join(missing) or 'none'}",
                    writers)
    return res


def report(res):
    rig = any(n == 0 for _, _, n in res.values())
    for k, (ok, why, _) in res.items():
        print(f"  {'ok  ' if ok else 'FAIL'} {k}: {why}")
    return 2 if rig else (0 if all(ok for ok, _, _ in res.values()) else 1)


PLANTS = [
    ("P5-UNMARKED: BandCanvas::drawLine forwarding without marking", CANVAS,
     "        if (dirty_) dirtyclear::MarkLine(*dirty_, x0, y0, x1, y1);\n", "", {"W-CANVAS"}),
    ("P5-RAWWRITE: the photo push without its All()", AM,
     "        backbuffer.DirtyAll();   // P5 1c: an unmarked write; this buffer's next restore is full\n", "", {"W-DIRECT"}),
    ("P5-NOGEN: the layer's generation bump removed", AM,
     "    ++wxLayerGen;   // P5 1c: the layer changes from here, so every buffer's next restore is full\n", "", {"W-GEN"}),
]


def selftest(tree):
    base = check(tree)
    if any(not ok for ok, _, _ in base.values()):
        print("selftest RIG: the clean tree is not green")
        report(base)
        return 2
    bad = 0
    for label, rel, old, new, want in PLANTS:
        tmp = tempfile.mkdtemp(prefix="p5w-")
        try:
            for d in ("include", "src"):
                shutil.copytree(os.path.join(tree, d), os.path.join(tmp, d))
            path = os.path.join(tmp, *rel.split("/"))
            src = open(path, encoding="utf-8", newline="").read()
            if src.count(old) != 1:
                print(f"selftest RIG {label}: the anchor occurs {src.count(old)} times")
                return 2
            open(path, "w", encoding="utf-8", newline="").write(src.replace(old, new))
            got = {k for k, (ok, _, _) in check(tmp).items() if not ok}
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        ok = got == want
        bad += 0 if ok else 1
        print(f"selftest {'ok' if ok else 'FAIL'}: {label} reads exactly {sorted(want)} red (got {sorted(got)})")
    print("selftest PASSED" if bad == 0 else f"selftest FAILED ({bad})")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--selftest"]
    tree = args[0] if args else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    sys.exit(selftest(tree) if "--selftest" in sys.argv else report(check(tree)))
