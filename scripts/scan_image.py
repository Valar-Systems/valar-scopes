#!/usr/bin/env python3
"""scan_image.py -- refuse a firmware image that carries a secret.

WHY THIS EXISTS. The shipping image must hold no credential: device keys live in
each board's NVS, never in the app. But platformio.ini invites a local
`-DCLOUD_FEED_KEY=...` build flag, and a factory image built on a machine that
sets it carries that key into EVERY board flashed from it. CI never sets the flag,
which is why the release publishes the factory image -- and why this scan runs on
every image CI publishes, and again in valar-flasher before every write.

ONE IMPLEMENTATION. valar-flasher vendors this file byte-for-byte and its CI diffs
the copy against this one; a second, hand-ported scanner is the one that drifts.
Stdlib only, Python 3.8+.

WHAT IT PRINTS: terms, counts, character classes and offsets. NEVER a matched
string -- a scanner that echoes what it found leaks the secret into the very log
that reports it.

Terms, each a hit on sight:
  * the NAMES DEVICE_KEY_SECRET and CLOUD_FEED_KEY
  * a 64-hex run (the shape of a device key / HMAC secret)
  * a GitHub token prefix, or "Bearer <token>"
And one shape that is only a hit when UNTRACED:
  * a 40-char token-shaped run [A-Za-z0-9_-]{40} (a Cloudflare API token's shape).
    Frameworks are full of these, so each is TRACED: found verbatim in a public
    input of the build (--trace), or its sha256 listed in a report CI published
    for this release (--public-runs). An untraced one is a hit.

CONTROLS, run on every scan, before any result is believed:
  1. anchor   -- a string known to be in this image must be FOUND. "Zero hits"
                 from a scanner that cannot see the image's strings is not evidence.
  2. planted  -- a copy with a fake 64-hex key appended must be FLAGGED.
  3. untraced -- (when 40-char runs are being judged) a copy with a random 40-char
                 run appended must come back UNTRACED, or the trace is accepting
                 everything and the 40-char rule protects nothing.
A failed control is exit 3: the scan is untrustworthy, which is not the same as clean.

    scan_image.py IMAGE --anchor S --trace DIR [--trace DIR ...] [--emit REPORT.json]   (CI)
    scan_image.py IMAGE --anchor S --public-runs REPORT.json                            (flasher)
    scan_image.py IMAGE --anchor S                     (40-char runs counted, not judged)
    scan_image.py --selftest

Exit: 0 clean, 1 hit, 3 a control failed (untrustworthy), 2 usage.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import secrets
import sys

MIN_STRING = 6
RUN40 = re.compile(rb"(?<![A-Za-z0-9_-])[A-Za-z0-9_-]{40}(?![A-Za-z0-9_-])")
DEFINITE = [
    ("name DEVICE_KEY_SECRET", re.compile(rb"DEVICE_KEY_SECRET")),
    ("name CLOUD_FEED_KEY", re.compile(rb"CLOUD_FEED_KEY")),
    ("64-hex run", re.compile(rb"(?<![0-9a-fA-F])[0-9a-fA-F]{64}(?![0-9a-fA-F])")),
    ("GitHub token prefix", re.compile(rb"(?:ghp_|gho_|ghu_|ghs_|github_pat_)[A-Za-z0-9_]{20,}")),
    ("Bearer token", re.compile(rb"Bearer\s+[A-Za-z0-9._-]{20,}")),
]
RUN40_LABEL = "40-char token-shaped run"
TRACE_EXT = re.compile(r"\.(c|cc|cpp|h|hpp|ino|s|ld|txt|csv|json|a|in|py|mk|cmake|inc|pem|crt)$", re.I)
TRACE_SKIP_DIRS = re.compile(r"^(\.git|docs?|examples?|tests?)$", re.I)


def strings(data: bytes):
    """(offset, bytes) for every printable-ASCII run of MIN_STRING or more."""
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % MIN_STRING, data):
        yield m.start(), m.group(0)


def klass(s: bytes) -> str:
    """A coarse, non-revealing description of a match: which alphabets it uses."""
    return "".join(c for c, ok in (("a", re.search(rb"[a-z]", s)), ("A", re.search(rb"[A-Z]", s)),
                                   ("9", re.search(rb"[0-9]", s)), ("_", re.search(rb"[_-]", s))) if ok)


def sha(s: bytes) -> str:
    return hashlib.sha256(s).hexdigest()


def find(data: bytes):
    """{label: [(offset, match_bytes)]} for every term, plus the 40-char runs."""
    out = {label: [] for label, _ in DEFINITE}
    out[RUN40_LABEL] = []
    for off, s in strings(data):
        for label, rx in DEFINITE:
            out[label] += [(off + m.start(), m.group(0)) for m in rx.finditer(s)]
        out[RUN40_LABEL] += [(off + m.start(), m.group(0)) for m in RUN40.finditer(s)]
    return out


def trace(runs: set, roots: list) -> dict:
    """{run: root it was found under}. A run found in no root is absent."""
    found, want = {}, set(runs)
    for root in roots:
        if not want:
            break
        if os.path.isfile(root):
            files = [root]
        else:
            files = []
            for d, subdirs, names in os.walk(root):
                subdirs[:] = [x for x in subdirs if not TRACE_SKIP_DIRS.match(x)]
                files += [os.path.join(d, n) for n in names if TRACE_EXT.search(n)]
        for p in files:
            if not want:
                break
            try:
                if os.path.getsize(p) > 64 * 1024 * 1024:
                    continue
                with open(p, "rb") as f:
                    blob = f.read()
            except OSError:
                continue
            for r in [r for r in want if r in blob]:
                found[r] = root
                want.discard(r)
    return found


class Verdict:
    def __init__(self):
        self.lines, self.hits, self.control_failed, self.report = [], 0, False, {}

    def say(self, s):
        self.lines.append(s)


def scan(data: bytes, anchor: str, trace_roots=None, public_runs=None, name="image") -> Verdict:
    v = Verdict()
    judging_runs = trace_roots is not None or public_runs is not None

    real = find(data)
    runs = {m for _, m in real[RUN40_LABEL]}
    fake = secrets.token_urlsafe(30)[:40].encode()
    # ONE walk of the trace roots for the image's runs AND the control's plant:
    # the package tree is large, and walking it per question tripled the cost.
    where = trace(runs | {fake}, trace_roots) if trace_roots is not None else {}

    def untraced_of(rs: set) -> set:
        if trace_roots is not None:
            return {r for r in rs if r not in where}
        return {r for r in rs if sha(r) not in public_runs}

    v.say(f"image: {name} ({len(data)} bytes)")

    # CONTROL 1: the anchor.
    ok = anchor.encode() in data
    v.say(f"CONTROL anchor {anchor!r} found: {ok}")
    v.control_failed |= not ok
    # CONTROL 2: a planted 64-hex key must be flagged.
    planted = find(data + b"\0" + b"ab12" * 16 + b"\0")
    ok = len(planted["64-hex run"]) == len(real["64-hex run"]) + 1
    v.say(f"CONTROL planted 64-hex key flagged: {ok}")
    v.control_failed |= not ok
    # CONTROL 3: a random 40-char run must come back untraced.
    if judging_runs:
        ok = fake in untraced_of({fake})
        v.say(f"CONTROL planted 40-char run comes back untraced: {ok}")
        v.control_failed |= not ok

    for label, _ in DEFINITE:
        hits = real[label]
        v.hits += len(hits)
        v.say(f"{len(hits):5}  {label}" + (f"   HIT at {', '.join(hex(o) for o, _ in hits[:5])}" if hits else ""))

    if not judging_runs:
        v.say(f"{len(runs):5}  {RUN40_LABEL} (distinct; not judged -- no trace and no public list)")
    else:
        bad = untraced_of(runs)
        v.hits += len(bad)
        detail = ""
        if bad:
            offs = [hex(o) for o, m in real[RUN40_LABEL] if m in bad][:5]
            detail = f"   UNTRACED {len(bad)}: classes {sorted(klass(m) for m in bad)} at {', '.join(offs)}"
        v.say(f"{len(runs):5}  {RUN40_LABEL} (distinct), {len(runs) - len(bad)} traced to public inputs{detail}")
        if trace_roots is not None:
            for root in trace_roots:
                n = sum(1 for r, w in where.items() if w == root and r in runs)
                if n:
                    v.say(f"         {n} found verbatim under {root}")
        v.report = {"v": 1, "image_sha256": hashlib.sha256(data).hexdigest(), "anchor": anchor,
                    "public_runs": sorted(sha(r) for r in runs - bad)}

    v.say("VERDICT: " + ("UNTRUSTWORTHY -- a control failed; nothing above is evidence" if v.control_failed
                         else f"HIT -- {v.hits} finding(s); this image must not be published or flashed" if v.hits
                         else "CLEAN"))
    return v


def exit_code(v: Verdict) -> int:
    return 3 if v.control_failed else 1 if v.hits else 0


def selftest() -> int:
    """Each rule must be able to fire. Plants never print their content."""
    anchor = "scopes.valarsystems.com"
    base = b"\0".join([b"bootloader", anchor.encode(), b"hello world string", b"x" * 40]) + b"\0"
    public = {sha(b"x" * 40)}
    cases = [
        ("clean", base, 0),
        ("name DEVICE_KEY_SECRET", base + b"DEVICE_KEY_SECRET=\0", 1),
        ("name CLOUD_FEED_KEY", base + b"-DCLOUD_FEED_KEY\0", 1),
        ("64-hex key", base + b"0123456789abcdef" * 4 + b"\0", 1),
        ("github token", base + b"ghp_" + b"A1b2" * 8 + b"\0", 1),
        ("bearer", base + b"Bearer " + b"Zz9_" * 8 + b"\0", 1),
        ("untraced 40-char run", base + b"Q7" * 20 + b"\0", 1),
        ("no anchor", base.replace(anchor.encode(), b"elsewhere.example.com"), 3),
        ("63-hex is not a key", base + b"a" * 63 + b"\0", 0),
    ]
    bad = 0
    for name, data, want in cases:
        got = exit_code(scan(data, anchor, public_runs=public, name=name))
        ok = got == want
        bad += not ok
        print(f"  {'PASS' if ok else 'FAIL'}  {name}: exit {got}, want {want}")
    # Trace path: a run present in a root file is traced; one absent is not.
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        with open(os.path.join(td, "lib.h"), "wb") as f:
            f.write(b'const char* s = "' + b"x" * 40 + b'";')
        got = exit_code(scan(base, anchor, trace_roots=[td], name="traced"))
        ok = got == 0
        bad += not ok
        print(f"  {'PASS' if ok else 'FAIL'}  trace finds a run in a public input: exit {got}, want 0")
        got = exit_code(scan(base + b"Q7" * 20 + b"\0", anchor, trace_roots=[td], name="untraced"))
        ok = got == 1
        bad += not ok
        print(f"  {'PASS' if ok else 'FAIL'}  trace refuses a run in no public input: exit {got}, want 1")
    print("SELFTEST " + ("PASSED" if not bad else f"FAILED ({bad})"))
    return 1 if bad else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Refuse a firmware image that carries a secret.")
    ap.add_argument("image", nargs="?")
    ap.add_argument("--anchor", help="a string that must be present in this image (control)")
    ap.add_argument("--trace", action="append", help="public build input to trace 40-char runs to (repeatable)")
    ap.add_argument("--public-runs", help="a scan report CI published for this image's release")
    ap.add_argument("--emit", help="write the scan report (image sha256 + traced-run hashes) here")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.image or not a.anchor:
        ap.print_usage()
        return 2
    with open(a.image, "rb") as f:
        data = f.read()
    public = None
    if a.public_runs:
        with open(a.public_runs, encoding="utf-8") as f:
            public = set(json.load(f).get("public_runs", []))
    v = scan(data, a.anchor, trace_roots=a.trace, public_runs=public, name=os.path.basename(a.image))
    print("\n".join(v.lines))
    if a.emit and exit_code(v) == 0 and v.report:
        with open(a.emit, "w", encoding="utf-8") as f:
            json.dump(v.report, f, indent=1)
            f.write("\n")
        print(f"report: {a.emit}")
    return exit_code(v)


if __name__ == "__main__":
    sys.exit(main())
