#!/usr/bin/env python3
"""Enforce the invariants that keep the config page a SINGLE whole-form POST.

WHY THIS EXISTS. On 2026-08-02 a partial POST to /save silently cleared every
checkbox on the page, because "absent means false" is the only way a browser can
turn one off. It cost most of a day: the wiped render toggles made a frame-budget
measurement compare a full renderer against a gutted one, and the wrong
conclusion was written into a code comment as fact. #130 fixed it with a hidden
`cfg-form` marker.

The sidebar layout re-opens that exact door. Sections that LOOK independent are
one keystroke away from becoming independent <form>s -- at which point each Save
posts a subset, the marker rides along saying "this is the whole form", and every
toggle outside the visible section is silently cleared again. The failure would
be invisible in the browser and identical to the bug we already paid for.

So the layout rule is written down AND enforced:

  1. Exactly one <form> per page, and it is id="cfg".
  2. The hidden cfg-form marker is inside it.
  3. Every NAMED control lives inside it. A named control outside the form is
     never submitted -- a setting that silently does nothing.
  4. Nothing inside it is `disabled`. CSS visibility does NOT remove a field from
     FormData, but `disabled` does; that is the one attribute that can turn a
     hidden section into a partial POST. Dim with opacity instead.

Two modes, because each catches what the other cannot:

  (no args)      Parse every CONFIG_HTML literal out of the source. Runs in CI on
                 every commit, covers all editions, needs no hardware.
  --url HOST     Fetch the rendered page from a real device and check the same
                 invariants on real output, plus that no %PLACEHOLDER% survived.
"""
from __future__ import annotations

import argparse
import re
import sys

SRC = "src/ConfigurationWebServer.cpp"

# Tags that carry a value into a form submission.
CONTROL_RE = re.compile(r"<(input|select|textarea)\b[^>]*>", re.I)
NAME_RE = re.compile(r"""\bname\s*=\s*['"]([^'"]+)['"]""", re.I)
FORM_OPEN_RE = re.compile(r"<form\b[^>]*>", re.I)
SCRIPT_RE = re.compile(r"<script\b.*?</script>", re.I | re.S)


def strip_scripts(html: str) -> str:
    """Remove <script> blocks.

    Required, not cosmetic: the collection view builds markup inside JS string
    literals, so a naive scan would treat `'<input name=...'` in a template
    string as a real control and report a phantom violation.
    """
    return SCRIPT_RE.sub("", html)


def check(html: str, label: str, rendered: bool) -> list[str]:
    problems: list[str] = []
    body = strip_scripts(html)

    opens = list(FORM_OPEN_RE.finditer(body))
    closes = [m.start() for m in re.finditer(r"</form>", body, re.I)]

    if len(opens) != 1:
        problems.append(
            f"expected exactly 1 <form>, found {len(opens)}. Sections must be CSS "
            f"visibility inside ONE form, never separate forms -- see the header."
        )
        return problems  # everything below assumes a single form
    if len(closes) != 1:
        problems.append(f"expected exactly 1 </form>, found {len(closes)}")
        return problems

    form_tag = opens[0].group(0)
    start, end = opens[0].end(), closes[0]
    inside = body[start:end]
    outside = body[:opens[0].start()] + body[end:]

    if not re.search(r"""\bid\s*=\s*['"]cfg['"]""", form_tag, re.I):
        problems.append(f'the <form> is not id="cfg": {form_tag[:90]}')

    # 2. the marker, and it must be INSIDE
    if not re.search(r"""name\s*=\s*['"]cfg-form['"]""", inside, re.I):
        problems.append(
            "the hidden cfg-form marker is missing from inside the form. Without "
            "it /save treats the body as partial and can never clear a checkbox."
        )

    # 3. named controls outside the form are never submitted
    for m in CONTROL_RE.finditer(outside):
        nm = NAME_RE.search(m.group(0))
        if nm:
            problems.append(
                f'named control "{nm.group(1)}" sits OUTSIDE the form -- it will '
                f"never be submitted, so that setting silently does nothing."
            )

    # 4. disabled is the one attribute that can shrink a POST
    for m in CONTROL_RE.finditer(inside):
        tag = m.group(0)
        if re.search(r"\bdisabled\b", tag, re.I):
            nm = NAME_RE.search(tag)
            problems.append(
                f'control "{nm.group(1) if nm else tag[:50]}" is disabled inside '
                f"the form. A disabled field is dropped from FormData, which turns "
                f"a whole-form POST into a partial one. Dim with opacity instead."
            )

    # 5. the cloud source is always NAMED, even in a build that cannot serve it.
    #
    # Source mode only, and deliberately so: a rendered page comes from exactly one
    # branch of the #ifdef, so a correct cloud build has no disabled option and
    # asserting it there would fail on a healthy device. In source mode BOTH
    # branches are in the text, which makes this precisely a check that the #else
    # branch still names the cloud.
    #
    # Why it earns a rule: omitting the option was the old behaviour, and it made
    # the most consequential property a binary has invisible on the one page a user
    # reads. A no-cloud build then looks identical to a cloud build someone
    # configured for OpenSky. See the block comment at the data-source select.
    if not rendered:
        sel = re.search(
            r"""<select[^>]*\bname\s*=\s*['"]data-source['"].*?</select>""",
            inside, re.I | re.S,
        )
        if sel and not re.search(
            r"""<option[^>]*value\s*=\s*['"]cloud['"][^>]*\bdisabled\b""", sel.group(0), re.I
        ):
            problems.append(
                "the data-source select has no DISABLED cloud option. The no-cloud "
                "branch must still list Blipscope Cloud and say it is absent, or a "
                "build without the feed is indistinguishable from one set to OpenSky."
            )

    if rendered:
        left = re.findall(r"%[A-Z0-9_]+%", body)
        if left:
            problems.append(f"unsubstituted placeholders survived: {sorted(set(left))[:6]}")

    return problems


def source_pages() -> list[tuple[str, str]]:
    """Every CONFIG_HTML raw-string literal in the source, with a usable label."""
    text = open(SRC, encoding="utf-8", errors="replace").read()
    marker = 'static const char CONFIG_HTML[] PROGMEM = R"('
    pages, pos, n = [], 0, 0
    while True:
        i = text.find(marker, pos)
        if i < 0:
            break
        j = text.find(')";', i)
        if j < 0:
            print("unterminated CONFIG_HTML literal -- refusing to guess", file=sys.stderr)
            sys.exit(2)
        body = text[i + len(marker): j]
        # Name it by its <title>, which is what distinguishes the editions.
        t = re.search(r"<title>([^<]+)</title>", body, re.I)
        n += 1
        pages.append((t.group(1).strip() if t else f"CONFIG_HTML #{n}", body))
        pos = j + 3
    return pages


# A checker that has never failed is not a checker; it is a comment that costs
# CPU. Each case below is a real way the sidebar could break the whole-form POST,
# and the self-test asserts the rule fires rather than that the page passes.
SELFTEST = [
    (
        "two forms (a section became its own form)",
        '<form id="cfg"><input type="hidden" name="cfg-form" value="1"><input name="lat"></form>'
        '<form id="net"><input name="mqtt-host"></form>',
        "exactly 1 <form>",
    ),
    (
        "marker missing",
        '<form id="cfg"><input name="lat"></form>',
        "cfg-form marker is missing",
    ),
    (
        "named control outside the form",
        '<form id="cfg"><input type="hidden" name="cfg-form" value="1"></form><input name="airports" type="checkbox">',
        "OUTSIDE the form",
    ),
    (
        "disabled field inside the form",
        '<form id="cfg"><input type="hidden" name="cfg-form" value="1"><input name="lat" disabled></form>',
        "is disabled inside",
    ),
    (
        "form is not id=cfg",
        '<form id="settings"><input type="hidden" name="cfg-form" value="1"></form>',
        'not id="cfg"',
    ),
    (
        "data-source select that never names the cloud",
        '<form id="cfg"><input type="hidden" name="cfg-form" value="1">'
        '<select name="data-source"><option value="opensky">OpenSky Network (cloud)</option>'
        '<option value="local">My own ADS-B receiver</option></select></form>',
        "no DISABLED cloud option",
    ),
]

# And one that must PASS: a hidden section is legal and must not be flagged,
# because CSS visibility does not remove a field from FormData. If this ever
# starts failing, the checker has become stricter than the rule.
SELFTEST_OK = [
    (
        "hidden section still submits",
        '<form id="cfg"><input type="hidden" name="cfg-form" value="1">'
        '<section style="display:none"><input name="mqtt-host"></section>'
        '<section><input name="lat"></section></form>',
    ),
    # Rule 5 must not collide with rule 4: a disabled <option> is legal and is the
    # whole point of the no-cloud branch. Rule 4 only scans <input|select|textarea>
    # because only those shrink a POST -- if that ever widens to any tag, this case
    # fails and says so, instead of the two rules quietly contradicting each other.
    (
        "a disabled OPTION is legal (it removes no field from FormData)",
        '<form id="cfg"><input type="hidden" name="cfg-form" value="1">'
        '<select name="data-source">'
        '<option value="cloud" disabled>Blipscope Cloud &mdash; not in this firmware build</option>'
        '<option value="opensky" selected>OpenSky Network (cloud)</option>'
        '<option value="local">My own ADS-B receiver</option></select></form>',
    ),
]


def selftest() -> int:
    bad = 0
    for label, html, expect in SELFTEST:
        problems = check(html, label, rendered=False)
        hit = any(expect in p for p in problems)
        print(f"{'ok  ' if hit else 'FAIL'}  detects: {label}")
        if not hit:
            bad += 1
            print(f"        expected a problem containing {expect!r}, got: {problems}")
    for label, html in SELFTEST_OK:
        problems = check(html, label, rendered=False)
        print(f"{'ok  ' if not problems else 'FAIL'}  allows: {label}")
        if problems:
            bad += 1
            print(f"        expected no problems, got: {problems}")
    print()
    print("self-test PASSED" if not bad else f"self-test FAILED ({bad} case(s))")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", help="check a live device instead of the source, e.g. http://192.168.86.32")
    ap.add_argument("--selftest", action="store_true", help="prove the checker catches each violation")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if args.url:
        import urllib.request
        html = urllib.request.urlopen(args.url.rstrip("/") + "/", timeout=20).read().decode("utf-8", "replace")
        pages = [(f"rendered from {args.url}", html)]
        rendered = True
    else:
        pages = source_pages()
        rendered = False
        if not pages:
            print("no CONFIG_HTML literals found -- has the file moved?", file=sys.stderr)
            return 2

    failed = 0
    total = 0
    for label, html in pages:
        problems = check(html, label, rendered)
        total += len(html)
        # The size goes in the normal output so the trend is visible in every CI
        # run rather than measured by hand when someone gets suspicious. Each page
        # is PROGMEM, so these bytes are flash 1:1. The radar page went
        # 28,085 -> 34,542 B in a single PR without anyone noticing at the time.
        size = f"{len(html):>7,} B"
        if problems:
            failed += 1
            print(f"FAIL  {size}  {label}")
            for p in problems:
                print(f"        - {p}")
        else:
            print(f"ok    {size}  {label}")

    print()
    print(f"CONFIG_HTML total: {total:,} B of flash across {len(pages)} page(s)")
    if failed:
        print(f"{failed} of {len(pages)} page(s) violate the single-whole-form rule.")
        return 1
    print(f"all {len(pages)} page(s) hold the single-whole-form invariants.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
