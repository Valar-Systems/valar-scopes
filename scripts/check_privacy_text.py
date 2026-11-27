#!/usr/bin/env python3
"""Retired privacy claims stay retired, in every place they were published.

Each phrase in RETIRED was a public privacy statement found false, and replaced:
  - "... and nothing that identifies your device" (README, the support page): request logs can hold the device's
    identifier and network address (#747, 2026-11-17);
  - "anonymous usage counts": each report is kept against the device's identifier;
  - "not where you are": the radar's requests carry the location, rounded to about 1 km;
  - "operational telemetry only": there are usage counts too;
  - "(its API key and hardware model)": the device identifier and firmware version are sent and kept too;
  - "Counts only — no location" (the leaderboard page): a submission also carries the spotter name and the types;
  - "no location or flight data leaves your device" (the leaderboard page): in cloud mode the radar sends a rounded
    location (the owner, 2026-11-17).
  - "own receiver — then it never talks to us", "own receiver and the radar never talks to us", "which otherwise never
    talks to anyone": an own-receiver device with Aircraft details set to Blipscope Cloud asks us about every aircraft
    in view (the owner, 2026-11-26: true only with details Off);
  - "nothing external is contacted at all", "transmits nothing, by construction": with details Off the device still
    checks GitHub for updates and sets its clock from pool.ntp.org;
  - "not on an update, not ever": firmware 9 moved every "adsbdb direct" choice to Blipscope Cloud;
  - "we're never contacted; a third party is": the adsbdb direct option no longer exists;
  - "when you tap an aircraft, we're sent": the lookups cover every aircraft in view, not only a tapped one;
  - "which for the radar includes the area it asked about": the log keeps exactly what each request asked for, a
    detail card's and a photo's included;
  - "activating a device still logs": the activation record is kept until the device next activates, not only logged.
  - "we keep that until the Blipscope next activates": the activation record no longer keeps the country and network
    for any activation after the change (the owner, 2026-11-26: "activation: stop storing").
  - "Cloudflare reports for that address", "A copy also sits in the request log": activating no longer records the
    country or network anywhere, the request log included (the owner, relayed 2026-11-27: "activation: shorter").
This check fails if any of them comes back in any of FILES, in any letter case or line wrapping.

A check that finds nothing proves nothing on its own, so each file also has two controls:
  - its privacy text's own anchor is present, so the check is reading the right text;
  - its replacement wording is present (REQUIRED), so the fix has not quietly gone too.
If a later edit rewords a replacement on purpose, update FILES below in the same change.

  python3 scripts/check_privacy_text.py              check the tree
  python3 scripts/check_privacy_text.py --selftest   plant each failure into an in-memory copy; each must turn
                                                     exactly its own row red

Exit 0 pass, 1 a row is red, 2 a file cannot be read, 3 the selftest is off its prediction.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# path -> (the anchor proving the check reads that file's privacy text, the replacement wording that must be present)
# ACTIVATION is the activation sentence (the owner: "activation: stop storing", then "activation: shorter"): neither
# the record nor the console lines keep the country and network, and both pages say so in the same change as the server.
ACTIVATION = "activating a blipscope no longer records the country or network"
FILES = {
    "README.md": ("## privacy & telemetry",
                  ("cloudflare keeps these logs for up to seven days", "ever holds the device's key",
                   "usage counts for each device", ACTIVATION)),
    "proxy/pages/support.html": ('id="privacy"',
                                 ("cloudflare keeps these logs for up to seven days", "ever holds the device's key",
                                  "usage counts for each device", ACTIVATION)),
    "proxy/pages/leaderboard.html": ("opt in from your blipscope", ("your spotter name",)),
}
RETIRED = (
    "nothing that identifies your device",
    "anonymous usage counts",
    "not where you are",
    "operational telemetry only",
    "its api key and hardware model)",
    "counts only — no location",
    "no location or flight data leaves your device",
    "own receiver — then it never talks to us",
    "own receiver and the radar never talks to us",
    "which otherwise never talks to anyone",
    "nothing external is contacted at all",
    "transmits nothing, by construction",
    "not on an update, not ever",
    "we're never contacted; a third party is",
    "when you tap an aircraft, we're sent",
    "which for the radar includes the area it asked about",
    "activating a device still logs",
    "we keep that until the blipscope next activates",
    "cloudflare reports for that address",
    "a copy also sits in the request log",
)


def norm(text):
    """Lower case, every run of whitespace one space, so a re-wrap or a capital cannot hide a match."""
    return re.sub(r"\s+", " ", text).lower()


def rows(path):
    return [f"P1 {path}: no retired phrase is present",
            f"C1 {path}: the privacy text's anchor is present",
            f"C2 {path}: the replacement wording is present"]


def grade(texts):
    """texts: path -> raw text. Returns the set of red row labels."""
    red = set()
    for path, (anchor, required) in FILES.items():
        t = norm(texts[path])
        p1, c1, c2 = rows(path)
        if any(norm(r) in t for r in RETIRED):
            red.add(p1)
        if anchor not in t:
            red.add(c1)
        if not all(r in t for r in required):
            red.add(c2)
    return red


def read_tree():
    texts = {}
    for path in FILES:
        try:
            with open(os.path.join(ROOT, path), encoding="utf-8", newline="") as f:
                texts[path] = f.read()
        except OSError as e:
            print(f"CANNOT READ {path}: {e.__class__.__name__}")
            sys.exit(2)
    return texts


def report(texts):
    red = grade(texts)
    for path in FILES:
        for row in rows(path):
            print(("FAIL " if row in red else "PASS ") + row)
    total = sum(len(rows(p)) for p in FILES)
    print(f"{total - len(red)} of {total} rows pass")
    return red


def selftest():
    base = read_tree()
    readme, support, lb = "README.md", "proxy/pages/support.html", "proxy/pages/leaderboard.html"

    def with_(path, new):
        t = dict(base)
        if new == t[path]:
            raise SystemExit(f"selftest BLIND: a plant into {path} did not change the text")
        t[path] = new
        return t

    def plant(path, phrase):
        # Capitalised, with a line break and indent inside: norm() must still find it.
        words = phrase.split(" ")
        mid = max(1, len(words) // 2)
        shaped = (" ".join(words[:mid]) + "\n      " + " ".join(words[mid:])).upper()
        return with_(path, base[path] + "\n" + shaped + "\n")

    def drop(path, needle):
        n = norm(base[path])
        if needle not in n:
            raise SystemExit(f"selftest BLIND: {needle!r} is not in {path} to remove")
        return with_(path, n.replace(needle, "x"))

    cases = [("clean tree", dict(base), set())]
    for phrase in RETIRED:
        cases.append((f"README: retired {phrase!r}", plant(readme, phrase), {rows(readme)[0]}))
    cases.append(("support: a retired phrase", plant(support, RETIRED[2]), {rows(support)[0]}))
    cases.append(("leaderboard: a retired phrase", plant(lb, RETIRED[5]), {rows(lb)[0]}))
    for path in FILES:
        cases.append((f"{path}: anchor gone", drop(path, FILES[path][0]), {rows(path)[1]}))
    for path in FILES:
        cases.append((f"{path}: replacement gone", drop(path, FILES[path][1][0]), {rows(path)[2]}))
    for path in (readme, support):
        cases.append((f"{path}: the activation sentence gone", drop(path, ACTIVATION), {rows(path)[2]}))

    off = 0
    for name, texts, want in cases:
        got = grade(texts)
        ok = got == want
        off += not ok
        print(f"{'as predicted' if ok else 'OFF'}: {name}: red {len(got)}, predicted {len(want)}")
        if not ok:
            for row in sorted(got ^ want):
                print(f"    {'unexpected' if row in got else 'missing'}: {row}")
    print(f"selftest: {len(cases) - off} of {len(cases)} cases as predicted")
    return 3 if off else 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    if sys.argv[1:]:
        print("usage: check_privacy_text.py [--selftest]")
        sys.exit(2)
    sys.exit(1 if report(read_tree()) else 0)


if __name__ == "__main__":
    main()
