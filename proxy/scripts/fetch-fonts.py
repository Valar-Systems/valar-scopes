#!/usr/bin/env python3
"""Regenerate proxy/fonts/*.woff2 -- the self-hosted webfonts for /leaderboard.

WHY SELF-HOSTED. The page previously pulled three families from Google Fonts,
which sends every visitor's IP and User-Agent to a third party. That is not a
contradiction of the device privacy promise (which is about the device, not the
visitor) but it is a distinction that would have to be explained, and the product
should not need to explain itself. Self-hosting also removes a render-blocking
external dependency and keeps the typography on networks that block Google.

WHY THESE FILES ARE SMALL. Google serves Inter, JetBrains Mono and Space Grotesk
as VARIABLE fonts: one file per family covers every weight. That was not obvious
-- the per-weight URLs in their CSS are byte-identical, which is how it was
found. So "three families, all weights" is three files, not the seven-plus static
files the size budget was originally scoped against.

WHAT THIS SCRIPT DOES BEYOND DOWNLOADING. Each font's weight axis is instanced
down to the range the page actually renders. Google ships the full designer axis
(Inter 100-900) and the deltas for weights nobody draws are dead bytes:

    Inter           48,256 -> 36,156   (wght 100..900 -> 400..700)
    JetBrains Mono  31,432 -> 30,232   (wght 400..800 -> 400..700)
    Space Grotesk   22,288 -> 20,908   (wght 300..700 -> 500..700)
    total           99.6 KB -> 85.2 KB

CONSEQUENCE WORTH KNOWING: a weight outside the instanced range clamps rather
than interpolating. If the page starts using Inter 300, widen the range here --
it will not fail loudly, it will just render at 400.

Only the `latin` unicode-range is taken. Display names are the only user-supplied
text on the page and are capped at 24 characters; a name outside latin falls back
to the system stack rather than breaking the layout.

All three families are SIL OFL 1.1, which permits self-hosting. LICENSE-FONTS.txt
ships beside them.

    python scripts/fetch-fonts.py          regenerate
    python scripts/fetch-fonts.py --check  verify sizes match what is committed

Requires: fontTools + brotli  (pip install fonttools brotli)
"""
from __future__ import annotations

import io
import os
import re
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "fonts"))
UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36")

# family spec -> (output name, weight range the page renders)
FONTS = [
    ("Inter:wght@100..900", "inter.woff2", (400, 700)),
    ("JetBrains+Mono:wght@100..800", "mono.woff2", (400, 700)),
    ("Space+Grotesk:wght@300..700", "grotesk.woff2", (500, 700)),
]


def fetch(url: str, binary: bool = False):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read() if binary else r.read().decode("utf-8")


def latin_url(css: str) -> str:
    """The woff2 URL of the `latin` block. Explicitly not the first block: the
    CSS leads with cyrillic-ext, and taking [0] would silently ship the wrong
    subset -- correct-looking on our text and broken on nobody's."""
    for m in re.finditer(r"/\*\s*([a-z0-9-]+)\s*\*/\s*@font-face\s*\{(.*?)\}", css, re.S):
        if m.group(1) != "latin":
            continue
        u = re.search(r"url\((https://[^)]+\.woff2)\)", m.group(2))
        if u:
            return u.group(1)
    raise SystemExit("no `latin` @font-face block found -- has the CSS format changed?")


def main() -> int:
    check = "--check" in sys.argv
    os.makedirs(OUT, exist_ok=True)
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer

    total = 0
    problems = []
    for spec, name, (lo, hi) in FONTS:
        css = fetch("https://fonts.googleapis.com/css2?family=%s&display=swap" % spec)
        raw = fetch(latin_url(css), binary=True)

        tmp = os.path.join(OUT, "." + name + ".tmp")
        io.open(tmp, "wb").write(raw)
        f = TTFont(tmp)
        if "fvar" in f:
            instancer.instantiateVariableFont(f, {"wght": (lo, hi)}, inplace=True,
                                              updateFontNames=False)
        f.flavor = "woff2"
        dst = os.path.join(OUT, name)
        if check:
            f.save(tmp + "2")
            want = os.path.getsize(tmp + "2")
            have = os.path.getsize(dst) if os.path.exists(dst) else -1
            # Sizes, not bytes: woff2 compression is not bit-reproducible across
            # brotli versions, so a byte compare would fail for no real reason.
            if have < 0:
                problems.append("%s is missing" % name)
            elif abs(have - want) > 2048:
                problems.append("%s is %d B, regenerates to %d B" % (name, have, want))
            os.remove(tmp + "2")
            total += have if have > 0 else 0
        else:
            f.save(dst)
            total += os.path.getsize(dst)
            print("%-16s %6d B  wght %g..%g" % (name, os.path.getsize(dst), lo, hi))
        f.close()
        os.remove(tmp)

    print("\ntotal %d B (%.1f KB) across %d file(s)" % (total, total / 1024.0, len(FONTS)))
    if problems:
        for p in problems:
            print("STALE: " + p)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
