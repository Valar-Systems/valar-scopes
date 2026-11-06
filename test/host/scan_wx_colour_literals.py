"""NL (docs/weatherscope-stage3.md A11.5, A12.47): no colour literal in the Weatherscope's drawing code. Every colour a
screen draws is a role in src/weatherscope/WxTheme.h, and drawing code names a role, never a number.

    python -I test/host/scan_wx_colour_literals.py            # the scope: src/weatherscope/, minus the exclusions
    python -I test/host/scan_wx_colour_literals.py <file>...  # those files only (the control, and the plants)

A colour literal is any of, after comments are blanked (line numbers kept):
  R1  color888( or color565( whose first argument is a number;
  R2  an Rgb opened on a number: Rgb{1, ...}, Rgb x{1, ...}, Rgb x = {1, ...};
  R3  a TFT_ colour name (TFT_BLACK, ...);
  R4  a colour-taking call with a numeric colour: an argument that is a {r, g, b} triple of integers (the wxraster
      calls: Put, Line, Fill, Text, RingDots, HomeMarker, PeakGlyph, DrawArrow), or a last argument that is a bare number
      (the LovyanGFX calls: fillSprite, fillScreen, fillRect, drawPixel, drawCircle, fillCircle, drawLine, drawFastHLine,
      drawFastVLine, setTextColor, fillArc, drawArc);
  R5  a {r, g, b} triple inside a Theme's initializer (Theme name = { ... }): only WxTheme.h defines one, and it is
      excluded from the scope -- R5 is what lets the control find that file's literals.
An integer triple anywhere else (an int array, a ladder) is not a colour and is not flagged.

The scope excludes WxTheme.h (the table), WxPalette.h (the pinned palette) and bench/ (bench tools). The allowlist is
empty, and its length is printed. Exit 1 on any hit, 0 on none, 2 if a file cannot be read.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCOPE = os.path.join(ROOT, "src", "weatherscope")
EXCLUDE = {"WxTheme.h", "WxPalette.h"}
ALLOWLIST = []   # (file, line) pairs; empty by ruling

NUM = r"(?:0[xX][0-9A-Fa-f]+|\d+)[uUlL]*"
TRIPLE = re.compile(r"^\{\s*\d+\s*,\s*\d+\s*,\s*\d+\s*\}$")
R1 = re.compile(r"\bcolor(?:888|565)\s*\(\s*" + NUM)
R2 = re.compile(r"\bRgb\s*(?:[A-Za-z_]\w*\s*)?(?:=\s*)?\{\s*\d")
R3 = re.compile(r"\bTFT_[A-Z][A-Z_0-9]*\b")
WX_CALLS = ("Put", "Line", "Fill", "Text", "RingDots", "HomeMarker", "PeakGlyph", "DrawArrow")
GFX_CALLS = ("fillSprite", "fillScreen", "fillRect", "drawPixel", "drawCircle", "fillCircle", "drawLine",
             "drawFastHLine", "drawFastVLine", "setTextColor", "fillArc", "drawArc")
CALL = re.compile(r"\b(" + "|".join(WX_CALLS + GFX_CALLS) + r")\s*\(")
THEME_INIT = re.compile(r"\bTheme\s+\w+\s*=\s*\{")


def blank_comments(src):
    """Comments become spaces; newlines stay, so offsets map to the same lines."""
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(c if c == "\n" else " " for c in src[i:j]))
            i = j
        elif src[i] == '"':   # a string literal: kept, but skipped as a unit so "//" inside it is not a comment
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def args_at(src, open_paren):
    """The top-level arguments of the call whose '(' is at open_paren, and the index after its ')'."""
    depth, i, start, args = 0, open_paren, open_paren + 1, []
    while i < len(src):
        c = src[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append(src[start:i])
                return [a.strip() for a in args], i + 1
        elif c == "," and depth == 1:
            args.append(src[start:i])
            start = i + 1
        i += 1
    return [], len(src)


def line_of(src, pos):
    return src.count("\n", 0, pos) + 1


def scan(path):
    try:
        src = blank_comments(open(path, "rb").read().decode("utf-8").replace("\r\n", "\n"))
    except OSError:
        return None
    hits = set()
    for rule, rx in (("R1", R1), ("R2", R2), ("R3", R3)):
        for m in rx.finditer(src):
            hits.add((line_of(src, m.start()), rule))
    for m in CALL.finditer(src):
        name = m.group(1)
        args, _ = args_at(src, m.end() - 1)
        if not args:
            continue
        if name in WX_CALLS and any(TRIPLE.match(a) for a in args):
            hits.add((line_of(src, m.start()), "R4"))
        if name in GFX_CALLS and re.fullmatch(NUM, args[-1]):
            hits.add((line_of(src, m.start()), "R4"))
    for m in THEME_INIT.finditer(src):
        _, end = args_at(src, m.end() - 1)   # the braced block's end (args_at balances {} as it does ())
        block = src[m.end() - 1:end]
        for t in re.finditer(r"\{\s*\d+\s*,\s*\d+\s*,\s*\d+\s*\}", block):
            hits.add((line_of(src, m.end() - 1 + t.start()), "R5"))
    return sorted(hits)


def main(argv):
    if len(argv) > 1:
        files = argv[1:]
    else:
        files = sorted(f for f in glob.glob(os.path.join(SCOPE, "*.h")) + glob.glob(os.path.join(SCOPE, "*.cpp"))
                       if os.path.basename(f) not in EXCLUDE)
    total, lines = 0, set()
    for f in files:
        hits = scan(f)
        if hits is None:
            print(f"FAIL: cannot read {f} (the RIG)")
            return 2
        for ln, rule in hits:
            if (os.path.basename(f), ln) in ALLOWLIST:
                continue
            print(f"  {os.path.relpath(f, ROOT)}:{ln}: {rule}")
            total += 1
            lines.add((f, ln))
    print(f"NL: {len(files)} file(s) scanned, {total} colour literal(s) on {len(lines)} line(s); allowlist {len(ALLOWLIST)}")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
