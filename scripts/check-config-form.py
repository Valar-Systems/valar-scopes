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


# ---------------------------------------------------------------------------
# THE SECTION VOCABULARY: four writers, one set of names.
#
# showSection(name) turns a section on by matching `name` against data-sec. It
# establishes that a name was REQUESTED; it establishes nothing about whether any
# section CARRIES it. Four independent places produce names that reach it:
#
#   1. C++   startSection -> %START_SECTION% -> body[data-start]   (the landing tab)
#   2. HTML  data-sec="..."                                        (what exists)
#   3. HTML  data-go="..."                                         (the nav buttons)
#   4. JS    const valid = [...]                                   (what a #hash may name)
#
# Rename a group in one and the others keep compiling, keep passing, and keep
# looking right in the editor. The worst case is silent and lands on the worst
# customer: a FIRST-RUN device has no location, so startSection is the "no
# location" literal, and if that name matches nothing every section is toggled
# off and the page renders BLANK on first boot.
#
# This is the "second path" family -- a guard that runs on every path and checks
# a narrower question than its name suggests. The fix is not a comment next to
# the literal; it is this, which fails CI.
SEC_ATTR_RE = re.compile(r"""\bdata-sec\s*=\s*['"]([^'"]*)['"]""", re.I)
GO_ATTR_RE = re.compile(r"""\bdata-go\s*=\s*['"]([^'"]*)['"]""", re.I)
START_ATTR_RE = re.compile(r"""\bdata-start\s*=\s*['"]([^'"]*)['"]""", re.I)
VALID_ARR_RE = re.compile(r"""\bvalid\s*=\s*\[([^\]]*)\]""")
STR_LIT_RE = re.compile(r"""['"]([A-Za-z0-9_.-]+)['"]""")
# Only ASSIGNMENTS. `return startSection;` and a lambda capture list both mention
# the name and neither decides its value -- matching those would drag in whatever
# string literals happened to sit before the next semicolon.
ASSIGN_RE = re.compile(r"\bstartSection\s*=\s*([^;]*);")


def start_section_literals(text: str) -> list[str]:
    """Every string literal that can reach %START_SECTION%.

    Returns [] if it cannot find an assignment at all -- the caller must treat
    that as REFUSE, never as pass. "Found no bad names" and "could not look" are
    the same output otherwise, and the second is the likelier shape of a broken
    probe.
    """
    lits: list[str] = []
    for m in ASSIGN_RE.finditer(text):
        lits += STR_LIT_RE.findall(m.group(1))
    return lits


def check_section_vocabulary(html: str, start_names: list[str], label: str) -> list[str]:
    problems: list[str] = []
    if not START_ATTR_RE.search(html):
        return problems  # this edition has no nav/landing machinery at all

    groups: set[str] = set()
    for m in SEC_ATTR_RE.finditer(html):
        groups.update(m.group(1).split())
    gos = {m.group(1).strip() for m in GO_ATTR_RE.finditer(html) if m.group(1).strip()}

    if not groups:
        problems.append(
            "this page sets data-start but carries NO data-sec section, so "
            "showSection() turns every section off and the page renders blank."
        )
        return problems

    for n in sorted(set(start_names)):
        if n not in groups:
            problems.append(
                f'the landing section can be "{n}", which no data-sec carries. '
                f"A device that picks it renders a BLANK page. Sections are: "
                f"{sorted(groups)}"
            )
    for n in sorted(gos):
        if n not in groups:
            problems.append(f'nav button data-go="{n}" matches no data-sec -- tapping it blanks the page')
    for n in sorted(groups):
        if n not in gos:
            problems.append(f'section group "{n}" has no nav button, so nothing can reach it')

    vm = VALID_ARR_RE.search(html)
    if vm:
        valid = STR_LIT_RE.findall(vm.group(1))
        for n in valid:
            if n not in groups:
                problems.append(f'the #hash allow-list names "{n}", which no data-sec carries')
        for n in sorted(gos):
            if n not in valid:
                problems.append(f'nav group "{n}" is missing from the #hash allow-list, so #{n} silently does nothing')
    return problems


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



# The vocabulary rule gets its own cases: it spans FOUR writers, and each one is a
# separate way to render the page blank. Every case below is a rename that
# compiles, passes every other check, and looks right in the editor.
SELFTEST_VOCAB = [
    ("landing section names a group nothing carries (BLANK on first boot)",
     ["collection", "display"], '<body data-start="%START_SECTION%"><nav class="side"><button class="navb" data-go="collection">C</button><button class="navb" data-go="location">L</button></nav><section class="sec" data-sec="collection"></section><section class="sec" data-sec="location"></section><script>const valid = [\'collection\',\'location\'];</script></body>', "renders a BLANK page"),
    ("nav button points at no section",
     ["collection"], '<body data-start="%START_SECTION%"><nav class="side"><button class="navb" data-go="collection">C</button><button class="navb" data-go="location">L</button><button class="navb" data-go="alerts">A</button></nav><section class="sec" data-sec="collection"></section><section class="sec" data-sec="location"></section><script>const valid = [\'collection\',\'location\',\'alerts\'];</script></body>', "blanks the page"),
    ("section group no nav button can reach",
     ["collection"], '<body data-start="%START_SECTION%"><nav class="side"><button class="navb" data-go="collection">C</button></nav><section class="sec" data-sec="collection"></section><section class="sec" data-sec="location"></section><script>const valid = [\'collection\'];</script></body>', "has no nav button"),
    ("#hash allow-list names a group nothing carries",
     ["collection"], '<body data-start="%START_SECTION%"><nav class="side"><button class="navb" data-go="collection">C</button><button class="navb" data-go="location">L</button></nav><section class="sec" data-sec="collection"></section><section class="sec" data-sec="location"></section><script>const valid = [\'collection\',\'location\',\'follow\'];</script></body>', "allow-list names"),
    ("nav group missing from the #hash allow-list",
     ["collection"], '<body data-start="%START_SECTION%"><nav class="side"><button class="navb" data-go="collection">C</button><button class="navb" data-go="location">L</button></nav><section class="sec" data-sec="collection"></section><section class="sec" data-sec="location"></section><script>const valid = [\'collection\'];</script></body>', "missing from the #hash allow-list"),
    ("data-start set but the page carries no sections at all",
     ["collection"], '<body data-start="%START_SECTION%"><nav class="side"></nav></body>',
     "renders blank"),
]

SELFTEST_VOCAB_OK = [
    ("all four vocabularies agree", ["collection", "location"], '<body data-start="%START_SECTION%"><nav class="side"><button class="navb" data-go="collection">C</button><button class="navb" data-go="location">L</button></nav><section class="sec" data-sec="collection"></section><section class="sec" data-sec="location"></section><script>const valid = [\'collection\',\'location\'];</script></body>'),
    # An edition with no nav at all must not be flagged -- EAM/Space/Seismic and
    # friends have single-screen forms and no landing machinery. If this starts
    # failing, the rule has grown teeth it was never meant to have.
    ("an edition with no data-start is none of this rule's business",
     ["collection"], '<body><input name="lat"></body>'),
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
    for label, starts, html, expect in SELFTEST_VOCAB:
        problems = check_section_vocabulary(html, starts, label)
        hit = any(expect in p for p in problems)
        print(f"{'ok  ' if hit else 'FAIL'}  detects: {label}")
        if not hit:
            bad += 1
            print(f"        expected a problem containing {expect!r}, got: {problems}")
    for label, starts, html in SELFTEST_VOCAB_OK:
        problems = check_section_vocabulary(html, starts, label)
        print(f"{'ok  ' if not problems else 'FAIL'}  allows: {label}")
        if problems:
            bad += 1
            print(f"        expected no problems, got: {problems}")
    # The extractor must be able to REFUSE. A source with no assignment has to
    # come back empty so main() can exit 2 rather than certify nothing.
    blind = start_section_literals("int main() { return 0; }")
    print(f"{'ok  ' if not blind else 'FAIL'}  refuses: no startSection assignment yields no literals")
    if blind:
        bad += 1

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

    # The section vocabulary spans the C++ side too, which is not inside any
    # CONFIG_HTML literal -- so it is read here rather than inside check().
    if rendered:
        starts = []          # taken per page from the rendered data-start below
    else:
        cpp_text = open(SRC, encoding="utf-8", errors="replace").read()
        starts = start_section_literals(cpp_text)
        if not starts:
            print(
                "could not find a startSection assignment in %s -- REFUSING to "
                "certify the section vocabulary. This is not a pass: 'found no bad "
                "names' and 'could not look' produce the same output otherwise."
                % SRC,
                file=sys.stderr,
            )
            return 2

    failed = 0
    total = 0
    for label, html in pages:
        page_starts = starts
        if rendered:
            m = START_ATTR_RE.search(html)
            page_starts = [m.group(1)] if m else []
        problems = check(html, label, rendered) + check_section_vocabulary(html, page_starts, label)
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
        print(f"{failed} of {len(pages)} page(s) FAILED -- see each '-' line above.")
        print("The rule is either the single-whole-form POST or the section vocabulary.")
        return 1
    print(f"all {len(pages)} page(s) hold the whole-form + section-vocabulary invariants.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
