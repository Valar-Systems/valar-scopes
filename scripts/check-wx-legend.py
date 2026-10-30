#!/usr/bin/env python3
"""WD8c (docs/wx-device.md): the config page's weather legend, read off the SERVED page, against the colours the
layer draws, computed here from the source -- WxPalette.h's colours, WxS2.h's percents and dithers -- never typed.

    python scripts/check-wx-legend.py --url http://<device>     grade a live device's page
    python scripts/check-wx-legend.py --selftest                 prove the check catches each defect (S-leg included)

The look graded is the one the page itself selects (the wx-look select, a shipping setting since K1), or A where the page has no such select
(the shipping image draws A). Exit: 0 PASS, 1 FAIL, 2 rig (the source or the page could not be read).
"""
from __future__ import annotations

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROWS = [("Light rain", 1, 4), ("Moderate", 5, 7), ("Heavy", 8, 10), ("Hail possible", 11, 13),
        ("No radar coverage", 14, 14), ("No data", 15, 15)]
BLACK = (0, 0, 0)


def read(rel: str) -> str:
    return open(os.path.join(ROOT, rel), encoding="utf-8").read()


def triples(text: str) -> list[tuple[int, int, int]]:
    return [tuple(int(v) for v in m) for m in re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", text)]


def expected() -> dict:
    """{look: {code: colour (1-13) or {(x, y): colour} (14, 15)}}, from the source, its rules' shapes asserted."""
    pal, s2 = read("src/weatherscope/WxPalette.h"), read("include/WxS2.h")
    one = lambda name: triples(re.search(name + r"\s*=\s*(\{[^}]*\})", pal).group(1))[0]
    table = triples(re.search(r"kRainTable\[12\]\s*=\s*\{(.*?)\};", pal, re.S).group(1))
    rain1, rain75, nocov, hatch = one("kRain1"), one("kRain75"), one("kNoCoverage"), one("kMissingHatch")
    assert len(table) == 12, "kRainTable is not 12 entries"
    assert "constexpr uint8_t Dim(uint8_t v, int pct) { return (uint8_t)((v * pct + 50) / 100); }" in pal, "Dim changed"
    assert "return wxpalette::Dim(wxpalette::Rain(code), pct);" in s2, "EchoAt no longer dims Rain(code)"
    assert "return code == 1 ? (pct * kCode1Num + kCode1Den / 2) / kCode1Den : pct;" in s2, "CodePercent changed"
    assert "inline bool NoCoverageLit(int x, int y) { return ((x + y) & 1) == 0; }" in s2, "14's dither changed"
    assert "inline bool NoDataLit(int x, int y) { return (x & 1) == 0 && (y & 1) == 0; }" in s2, "15's dither changed"
    num = int(re.search(r"kCode1Num = (\d+);", s2).group(1))
    den = int(re.search(r"kCode1Den = (\d+);", s2).group(1))
    base = {1: rain1, **{c: table[c - 1] for c in range(2, 13)}, 13: rain75}
    dim = lambda c, p: tuple((v * p + 50) // 100 for v in c)
    out = {}
    for look in "AB":
        pct = int(re.search(rf"kPercent{look} = (\d+);", s2).group(1))
        e = {c: dim(b, (pct * num + den // 2) // den if c == 1 else pct) for c, b in base.items()}
        e[14] = {(x, y): nocov if (x + y) % 2 == 0 else BLACK for x in (0, 1) for y in (0, 1)}
        e[15] = {(x, y): hatch if x % 2 == 0 and y % 2 == 0 else BLACK for x in (0, 1) for y in (0, 1)}
        out[look] = e
    return out


def page_look(html: str) -> str:
    m = re.search(r"""<select[^>]*name=["']wx-look["'][^>]*>(.*?)</select>""", html, re.S)
    s = m and re.search(r"""<option[^>]*value=["']([^"']*)["'][^>]*\bselected\b""", m.group(1))
    return s.group(1) if s else "A"


def grade(html: str, exp: dict) -> tuple[list[str], list[str]]:
    """(problems, report lines) for one served page."""
    probs, rep = [], []
    i = html.find('<div class="wxlegend"')
    if i < 0:
        return ["no weather legend on the page"], rep
    j = html.find("</fieldset>", i)
    block = html[i:j if j > 0 else len(html)]
    look = page_look(html)
    m = re.match(r'<div class="wxlegend" data-look="([AB])">', block)
    if not m or m.group(1) != look:
        probs.append(f"the legend says look {m.group(1) if m else '?'}, the page selects look {look}")
    if "%" in block:
        probs.append("a literal '%' in the legend")
    if re.search(r"snow", block, re.I):
        probs.append("a snow row: the layer cannot draw snow")
    rows = re.findall(r'<div class="wxrow"[^>]*><span[^>]*>([^<]*)</span>(.*?)</div>', block)
    labels = [r[0] for r in rows]
    if labels != [r[0] for r in ROWS]:
        probs.append(f"rows {labels}, not {[r[0] for r in ROWS]}")
    e = exp[look]
    bad = 0
    for (label, first, last), (_, body) in zip(ROWS, rows):
        sws = re.findall(r'<svg [^>]*data-code="(\d+)">(.*?)</svg>', body)
        codes = [int(c) for c, _ in sws]
        if codes != list(range(first, last + 1)):
            probs.append(f"{label}: codes {codes}, not {first}-{last}"); continue
        for c, svg in sws:
            c = int(c)
            if c <= 13:
                f = re.search(r'fill="rgb\((\d+),(\d+),(\d+)\)"', svg)
                got = tuple(int(v) for v in f.groups()) if f else None
                if got != e[c]:
                    bad += 1; probs.append(f"code {c} ({label}) is {got}, the layer draws {e[c]} in look {look}")
            else:
                cells = {(int(x), int(y)): (int(r), int(g), int(b)) for x, y, r, g, b in
                         re.findall(r'<rect x="(\d)" y="(\d)" width="1" height="1" fill="rgb\((\d+),(\d+),(\d+)\)"/>', svg)}
                if cells != e[c]:
                    bad += 1; probs.append(f"code {c} ({label})'s dither is {cells}, the layer draws {e[c]}")
    rep.append(f"look {look}: {len(rows)} rows, {sum(len(re.findall('<svg ', b)) for _, b in rows)} swatches, "
               f"{bad} not the layer's colour")
    return probs, rep


def synth(exp: dict, look: str, plant: str | None = None) -> str:
    """A served page as WxLegendHtml.h writes it, for the selftest; `plant` breaks one thing."""
    e = exp[look]
    rgb = lambda c: f"rgb({c[0]},{c[1]},{c[2]})"
    out = f'<select name="wx-look"><option value="{look}" selected>x</option></select>' if look == "B" or plant == "look" else ""
    shown = "A" if plant == "look" else look
    if plant == "look":
        out = '<select name="wx-look"><option value="B" selected>x</option></select>'
    out += f'<fieldset><div class="wxlegend" data-look="{shown}">'
    rows = ROWS + ([("Snow light>heavy", 1, 1)] if plant == "snow" else [])
    for label, first, last in rows:
        out += f'<div class="wxrow" style="x"><span style="y">{label}</span>'
        for c in range(first, last + 1):
            if plant == "missing" and c == 9: continue
            out += f'<svg width="12" height="12" data-code="{c}">'
            if c <= 13:
                col = e[c]
                if plant == "S-leg" and c == 5:   # typed in by hand, one level off: its colour at 41 %, not 40 %
                    col = tuple((v * 41 + 50) // 100 for v in (255, 255, 0))
                out += f'<rect width="12" height="12" fill="{rgb(col)}"/>'
            else:
                out += f'<defs><pattern id="wxd{c}" width="2" height="2" patternUnits="userSpaceOnUse">'
                for y in (0, 1):
                    for x in (0, 1):
                        col = e[c][(x, y)]
                        if plant == "dither" and c == 14 and (x, y) == (1, 0): col = (40, 40, 40)
                        out += f'<rect x="{x}" y="{y}" width="1" height="1" fill="{rgb(col)}"/>'
                out += f'</pattern></defs><rect width="12" height="12" fill="url(#wxd{c})"/>'
            out += "</svg>"
        out += "</div>"
    return out + "</div></fieldset>"


def selftest() -> int:
    exp = expected()
    assert exp["A"][5] == (102, 102, 0), "the selftest's S-leg assumes code 5 is (255, 255, 0) at 40 %"
    cases = [("the legend as drawn, look A", synth(exp, "A"), True),
             ("the legend as drawn, look B", synth(exp, "B"), True),
             ("S-leg: code 5 typed in by hand at 41 %", synth(exp, "A", "S-leg"), False),
             ("a dither cell wrong (14 lit at (1, 0))", synth(exp, "A", "dither"), False),
             ("a swatch missing (9)", synth(exp, "A", "missing"), False),
             ("a snow row", synth(exp, "A", "snow"), False),
             ("the page selects B, the legend is drawn in A", synth(exp, "A", "look"), False)]
    bad = 0
    for label, html, want_pass in cases:
        probs, _ = grade(html, exp)
        ok = (not probs) == want_pass
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'}  {'allows' if want_pass else 'detects'}: {label}" + ("" if ok else f" -- {probs}"))
    print("self-test PASSED" if not bad else f"self-test FAILED ({bad})")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    try:
        exp = expected()
    except (AssertionError, AttributeError, OSError) as e:
        print(f"RIG: could not read the source: {e}"); return 2
    if a.selftest:
        return selftest()
    if not a.url:
        print(__doc__); return 2
    import urllib.request
    try:
        html = urllib.request.urlopen(a.url.rstrip("/") + "/", timeout=20).read().decode("utf-8", "replace")
    except OSError as e:
        print(f"RIG: the page could not be read: {e}"); return 2
    probs, rep = grade(html, exp)
    print("\n".join(rep + [f"  - {p}" for p in probs]))
    print("PASS" if not probs else "FAIL")
    return 0 if not probs else 1


if __name__ == "__main__":
    sys.exit(main())
