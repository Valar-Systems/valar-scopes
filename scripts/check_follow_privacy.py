#!/usr/bin/env python3
"""The follow target must not leave the device, except in an ntfy body.

Spec: docs/follow-mode-consolidated.md 17.

=============================================================================
WHAT THIS IS, AND WHAT IT IS NOT

17 asks for a STRONG form -- a host test over extracted pure payload builders,
asserting a distinctive follow value appears in none of them -- and says that if
the extraction turns out large, land a source guard first so the window is
covered and SAY PLAINLY that it is the weaker check.

THIS IS THE WEAKER CHECK. It reads the source, not the artifact, which is the
exact substitution CLAUDE.md's standing practice warns about: the input is a
statement of intent, and intent is the thing that was already wrong. The
extraction is genuinely large -- the leaderboard body alone pulls in Logbook,
DeviceIdentity, ArduinoJson and CloudFeed -- so it is owed, not done.

It is worth having anyway, because it found a real leak on its first honest run:
a Serial.printf of the follow target, shipped in stage 1, in a file whose own
header says the target must never be printed. 17 lists serial output among the
forbidden places, with the Wi-Fi password incident as the precedent.

=============================================================================
TWO CHECKS, AND ONLY ONE OF THEM IS AN ASSERTION

  1. PER LINE, NO ALLOW LIST. The target and an outbound sink in the same
     statement. There is no legitimate reason for that -- not even in
     SendFollowAlert, which passes the value through a title builder first --
     so there is nothing to forgive and no list to keep. This is the half that
     catches the real thing.

  2. PER FUNCTION, WITH TWO LISTS. Coarse and deliberately over-flagging: any
     function that touches the target anywhere and reaches a sink anywhere. It
     flags Initialise, which is enormous and contains both for unrelated
     reasons. That is not a false positive to be tuned away -- THE LIST IS THE
     RECORD OF WHAT SOMEBODY LOOKED AT. Whole lists, not substring absence: an
     addition fails even when innocent, and the reviewer writes down why.

=============================================================================
THE SCANNER GOT ITSELF WRONG TWICE, WHICH IS THE PART WORTH READING

Both were caught by this file's own anchor control -- the branch that fails when
the review list names functions the scanner can no longer find -- and NEITHER
was visible by reading the code.

  * BRACE DEPTH. Attributing a line to a function by counting { and } looks
    obvious and does not survive braces inside string literals, of which this
    codebase has many. The counter drifted, never returned to zero, and the scan
    reported TWO functions touching the target instead of twelve. Had the check
    only asked "is anything on a deny list?", that would have read as a very
    clean pass. Attribution is now by column-0 definition, full stop.

  * A LITERAL BACKSPACE WHERE A WORD-BOUNDARY ESCAPE WAS MEANT. A regex written
    through a shell heredoc lost a backslash, so the pattern compiled with
    chr(8) on the end. grep showed the line looking perfectly correct, because a
    terminal renders a backspace by not rendering it. The compiled pattern
    disagreed with the source that was supposed to produce it -- this repo's own
    standing practice, arriving inside a tool written to enforce another one.
    Word boundaries are avoided here now; the pattern anchors on "=" instead.

Usage:
    python scripts/check_follow_privacy.py
    python scripts/check_follow_privacy.py --selftest
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The value itself. `followHomeCode` is derived from the device's own configured
# location rather than from what the customer typed, so it is not the target.
# followSessionTarget is NOT a substring of followTarget ("followSession" +
# "Target"), so it needed adding explicitly -- and it was invisible to this
# scanner for the whole of the session-follow build until the review list's
# churn prompted a look. A gesture-set target is exactly as sensitive as a
# configured one: §17 is about the VALUE leaving the device, and says nothing
# about how it got there.
TARGET_TOKENS = ("followTarget", "followSessionTarget")

# Anything that puts bytes on a wire or a console. Serial is on the list because
# 17 names it: "serial output (the Wi-Fi password incident is the precedent)".
SINK_RE = re.compile("|".join([
    r"\bQueueNtfyPost\s*\(",
    r"\bEnrichRequest\b",
    r"->url\b",
    r"->lbBody\b",
    r"\bserializeJson\s*\(",
    r"\bhttp\.(?:GET|POST|addHeader)\s*\(",
    r"\bmqtt\.(?:Publish|publish)\s*\(",
    r"\bSerial\.(?:print|printf|println)\s*\(",
    r"\baddHeader\s*\(",
    # A header appended to a vector rather than set on a client. Added
    # 2026-08-29, when usage telemetry merged into this branch and brought a new
    # outbound path the sink list did not know: CloudFeed builds its request
    # headers as `h.push_back({ "X-Blip-Usage", usage })`, which matches none of
    # the shapes above.
    #
    # Found by planting the leak rather than by reading: a probe adding
    # `h.push_back({ "X-Blip-Follow", followTarget })` DID fail the build -- but
    # on the review list (an unlisted function touched the target), with the sink
    # count still reading 0. That is one line of defence, not two, and the
    # remaining one fails open: a reviewer who adds the function to ALLOWED_TOUCH
    # with an entirely plausible reason ("it only assembles headers") would then
    # be shipping the target to the backend with a green check.
    r"\bpush_back\s*\(\s*\{",
]))

# A top-level definition, at column 0. Anything nested -- a lambda, a loop body
# -- is attributed to its enclosing function, which is the right granularity: a
# lambda that leaks is the enclosing function leaking.
FUNC_RE = re.compile(
    r"^(?:[A-Za-z_][\w:<>,&*\s]*?\s)?([A-Za-z_]\w*(?:::[A-Za-z_]\w*)?)\s*\([^;]*$"
)

# An inline member definition inside a class body: indented, body opens on the
# same line. HEADERS ONLY -- applied to a .cpp this matches every indented
# `if (x) {` and steals attribution from the enclosing function.
INLINE_RE = re.compile(
    r"^\s+(?:[A-Za-z_][\w:<>,&*\s]*?\s)?([A-Za-z_]\w*)\s*\([^;]*\)\s*(?:const\s*)?\{"
)

# `if (x) {` is shape-identical to an inline member definition, and the first
# version of this file duly reported two functions named "if" and "switch"
# leaking the target. Funny once -- it matters because a check whose output is
# nonsense gets read as noise, and so does the next real finding.
CONTROL_KEYWORDS = {"if", "for", "while", "switch", "catch", "do", "else",
                    "return", "case"}

# The member's own declaration is not a use of it.
DECL_RE = re.compile(r"^\s*(?:String|bool|const\s+String)\s+followTarget\s*=")

# ---------------------------------------------------------------------------
# THE REVIEW LIST: every function permitted to touch the target at all.
# Written from the scan, not from memory. The first version was written from
# memory and got five of thirteen entries wrong, which the anchor control caught.
ALLOWED_TOUCH = {
    # --- on-device only ----------------------------------------------------
    "AircraftManager::Initialise",           # reads the config key
    "AircraftManager::MatchesFollow",        # identity match against the contacts
    "AircraftManager::FollowedAircraft",
    "AircraftManager::DrawFollowLocalFace",  # drawn on the customer's own glass
    "AircraftManager::DrawFollowWaitingFace",# C4's nudge names the tail so the
                                             # owner can check it against the
                                             # config page; panel only.
    "AircraftManager::DrawFollowHud",
    "AircraftManager::DrawRadar",            # the followed-contact ring
    "EffectiveFollowTarget",                 # inline in the header: the ONE place
                                             # that decides which target is in
                                             # force. Concentrating it here is why
                                             # UpdateFollowTrack, FollowRouteView
                                             # and FollowScreenVisible no longer
                                             # touch a target at all.
    "AircraftManager::SetSessionFollow",     # the gesture. Reviewed 2026-08-28:
                                             # stores to RAM only -- never NVS --
                                             # and its serial line prints the
                                             # LENGTH, never the value.
    "AircraftManager::ClearSessionFollow",   # ditto; prints a precomputed bool.
    "AircraftManager::BenchSessionFollow",   # FOLLOW_BENCH only -- absent from every
                                             # shipping image, verified by grepping
                                             # the blipscope-s3-128 ELF. Sets a canned
                                             # target; prints a precomputed length.
    "FollowSessionActive",                   # inline in the header: an emptiness
                                             # test, never the value.
    "<file scope>",                          # the member DECLARATIONS in
                                             # AircraftManager.h. followTarget's
                                             # own declaration was already
                                             # excluded by DECL_RE; the session
                                             # one is a String with a comment
                                             # block above it and lands at file
                                             # scope instead. A declaration is
                                             # not a use.
    "AircraftManager::RecordFrameUs",        # the [follow] health line: prints the
                                             # STATE and counters, never the target
    # --- serves the value back to the OWNER'S OWN BROWSER -------------------
    # The config page is served by this device over the LAN to the person who
    # typed the value, so they can see and edit it. That is not the disclosure
    # §17 is about -- the value reaching US or a third party -- and a settings
    # page that cannot show a setting is not a settings page.
    "ConfigurationWebServer::Initialise",
    # --- THE one sanctioned outbound use (C3) ------------------------------
    "AircraftManager::SendFollowAlert",
}

# Of those, the ones that ALSO reach a sink somewhere in the same function.
# Every entry is a "somebody read this function and it is fine" record; the
# per-line check above is what actually guards them.
ALLOWED_SINK = {
    "AircraftManager::SendFollowAlert",    # the point of the exception
    "AircraftManager::Initialise",         # huge; logs plenty, never the target
    "AircraftManager::RecordFrameUs",      # the [follow] line: state + counters only
    "ConfigurationWebServer::Initialise",  # the config page, to the owner's browser
    # Both set/clear the session target AND print a serial line. Read
    # 2026-08-28: the target cannot reach either printf. SetSessionFollow prints
    # id.length() and the two ROUTE CODES; ClearSessionFollow prints a bool
    # computed on the line before. The per-statement check above is what actually
    # guards them -- it already rejected an earlier draft of the clear line that
    # put followTarget.isEmpty() inline in the printf.
    "AircraftManager::SetSessionFollow",
    "AircraftManager::ClearSessionFollow",
    "AircraftManager::BenchSessionFollow",
}


CONTROL_HEAD_RE = re.compile(r"^(?:if|for|while|switch|else if)\s*\(.*\)$")

STRING_RE = re.compile(r'"(?:[^"\\]|\\.)*"')


def scan(text, header=False):
    """-> ({function: (touches, sinks)}, [(lineno, text)] same-STATEMENT leaks)

    STATEMENTS, NOT LINES, and the difference is the whole check.

    The first version matched per line and its selftest planted the violation on
    one line, so it passed. Rehearsed against THE ACTUAL BUG -- the stage-1
    Serial.printf, whose format string and whose `followTarget.c_str()` sit on
    consecutive lines because that is how a printf with four arguments gets
    wrapped -- it reported zero leaks and a clean pass.

    That is this repo's signature failure arriving inside the tool written to
    prevent it: the assertion was written against the shape the author imagined
    the failure would take, and the real shape walked straight through. It is
    the launch gate's `#charlie-retired` again, in Python.

    So lines are joined until the parentheses balance, and a leak is the target
    and a sink inside one logical statement. String literals are blanked before
    counting parens -- a `"("` inside a format string would drift the counter
    exactly the way brace-counting drifted, which is the OTHER bug this file has
    already had.
    """
    out, leaks = {}, []
    current = "<file scope>"
    pending, pending_line, depth = "", 0, 0

    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split("//", 1)[0]

        # Attribution is per LINE and unaffected by the joining below.
        if line[:1] not in (" ", "\t", "", "#", "/", "*"):
            m = FUNC_RE.match(line)
            if m:
                current = m.group(1)
        elif header:
            m = INLINE_RE.match(line)
            if m and m.group(1) not in CONTROL_KEYWORDS:
                current = m.group(1)
        if DECL_RE.match(line):
            continue

        if not pending:
            pending_line = lineno
        pending += " " + line.strip()
        depth += 1  # lines accumulated, used only as the runaway backstop

        # FLUSH ON A STATEMENT TERMINATOR, not on paren balance.
        #
        # Paren balance was the obvious choice and was the THIRD time this file
        # drifted: `#if defined(X)`, an unbalanced paren inside a block comment,
        # any of it leaves the counter open and the rest of the file joins into
        # one statement. The scan then reported ONE function touching the target
        # instead of eleven -- caught, again, by the anchor control below.
        #
        # `;` `{` `}` and a blank line end a statement in C++ far more reliably
        # than parens balance, and every brace resets, so a miss costs one
        # statement rather than a file. The line cap is the backstop for whatever
        # is left.
        stripped = line.rstrip()
        terminated = (not stripped
                      or stripped.endswith((";", "{", "}", ":"))
                      or stripped.lstrip().startswith("#")
                      # A braceless `if (target...)` guarding a sink on the next
                      # line is a normal, correct shape -- it is how the [follow]
                      # health line is written -- and joining the two would
                      # report every one of them as a leak. The CONDITION is its
                      # own statement; the body is the thing being judged.
                      or CONTROL_HEAD_RE.match(stripped.lstrip()))
        if not terminated and depth < 8:
            continue

        depth = 0
        stmt, pending = pending, ""
        touch = any(t in stmt for t in TARGET_TOKENS)
        sink = bool(SINK_RE.search(stmt))
        if touch and sink:
            leaks.append((pending_line, stmt.strip()))
        if touch or sink:
            prev = out.get(current, (False, False))
            out[current] = (prev[0] or touch, prev[1] or sink)
    return out, leaks


def analyse(root):
    touching, sinking, all_leaks = set(), set(), []
    for sub in ("src", "include"):
        for dirpath, _dirs, files in os.walk(os.path.join(root, sub)):
            for f in files:
                if not f.endswith((".cpp", ".h")):
                    continue
                path = os.path.join(dirpath, f)
                with open(path, encoding="utf-8", errors="replace") as fh:
                    per, leaks = scan(fh.read(), header=f.endswith(".h"))
                for fn, (touch, sink) in per.items():
                    if touch:
                        touching.add(fn)
                        if sink:
                            sinking.add(fn)
                for ln, txt in leaks:
                    all_leaks.append("%s:%d  %s"
                                     % (os.path.relpath(path, root), ln, txt[:90]))
    return touching, sinking, all_leaks


def report(touching, sinking, line_leaks=(), quiet=False):
    fails = []

    # 1. THE ASSERTION. No allow list, by design.
    if line_leaks:
        fails.append(
            "the follow target appears in the same statement as an outbound sink:\n    "
            + "\n    ".join(line_leaks)
            + "\n  17: it may leave the device ONLY in an ntfy body, and there is no"
              "\n  allow list for this one."
        )

    # 2. THE REVIEW RECORD, compared as whole lists.
    extra = sorted(set(touching) - ALLOWED_TOUCH)
    if extra:
        fails.append(
            "these functions touch the follow target and are not on the review list:\n    "
            + "\n    ".join(extra)
            + "\n  On-device use is fine -- add it to ALLOWED_TOUCH with a reason, so the"
              "\n  list keeps saying what was looked at."
        )

    # THE ANCHOR CONTROL. A shrinking list is not an improvement: it is the shape
    # of a broken scanner, and this one has been broken twice. Both times this
    # branch is what said so.
    missing = sorted(ALLOWED_TOUCH - set(touching))
    if missing:
        fails.append(
            "the review list names functions the scanner no longer finds:\n    "
            + "\n    ".join(missing)
            + "\n  Either they were renamed or removed (update the list), or the scanner"
              "\n  is broken -- which looks exactly like the code getting safer."
        )

    leaked = sorted(set(sinking) - ALLOWED_SINK)
    if leaked:
        fails.append(
            "these functions touch the follow target AND reach an outbound sink:\n    "
            + "\n    ".join(leaked)
            + "\n  Read the function. If the target cannot reach the sink, add it to"
              "\n  ALLOWED_SINK with a note saying so."
        )

    if not quiet:
        print("  functions touching the follow target: %d" % len(touching))
        print("  same-statement target+sink leaks:     %d" % len(line_leaks))
    return fails


def selftest():
    """Plant violations and require the check to FAIL.

    In memory, not on disk: an interrupted run must not leave a booby-trapped
    tree behind. The launch gate learned that one the expensive way.
    """
    state = {"ok": True}

    def expect(cond, why):
        if cond:
            print("  ok  " + why)
        else:
            print("  SELFTEST FAIL: " + why)
            state["ok"] = False

    # 1. THE REAL BUG THIS FOUND, reproduced: the target printed to serial.
    _per, leaks = scan(
        'void AircraftManager::Initialise()\n'
        '{\n'
        '    Serial.printf("[follow] target=%s", followTarget.c_str());\n'
        '}\n')
    expect(len(leaks) == 1,
           "the target printed to serial is a same-statement leak")
    expect(any("same statement" in f for f in report(set(), set(), ["x"], quiet=True)),
           "a same-statement leak fails the check, with no allow list")

    # 1b. THE REAL SHAPE, WRAPPED ACROSS LINES. This case exists because the
    #     first version of this file passed against it: a printf with four
    #     arguments wraps, the format string and the target land on different
    #     lines, and a per-LINE check sees neither statement as a leak. Verbatim
    #     from the stage-1 commit, indentation and all.
    _per, leaks = scan(
        'void AircraftManager::Initialise()\n'
        '{\n'
        '        if (!followTarget.isEmpty())\n'
        '            Serial.printf("[follow] target=\\"%s\\" track=%d active=%d\\n",\n'
        '                          followTarget.c_str(), (int)followDrawTrack,\n'
        '                          (int)followTrack.Active());\n'
        '}\n')
    expect(len(leaks) == 1,
           "the REAL bug -- a printf wrapped across lines -- is caught")

    # 1c. And the paren counter must not be fooled by a bracket inside a format
    #     string, which is the trap brace-counting already fell into once.
    _per, leaks = scan(
        'void AircraftManager::Thing()\n'
        '{\n'
        '    Serial.printf("a ( b");\n'
        '    int x = 1;\n'
        '    Serial.printf("%s", followTarget.c_str());\n'
        '}\n')
    expect(len(leaks) == 1,
           "a stray '(' inside a string literal does not swallow the next statements")

    # 2. A leak split across two statements. The per-line check CANNOT see it,
    #    and saying so is the point -- the per-function check is what covers it.
    per, leaks = scan(
        'bool AircraftManager::QueueLeaderboardSubmit()\n'
        '{\n'
        '    doc["following"] = followTarget;\n'
        '    serializeJson(doc, req->lbBody);\n'
        '}\n')
    expect(not leaks,
           "a two-statement leak is invisible per-line (stated, not hidden)")
    expect(per.get("AircraftManager::QueueLeaderboardSubmit") == (True, True)
           and any("outbound sink" in f for f in report(
               {"AircraftManager::QueueLeaderboardSubmit"},
               {"AircraftManager::QueueLeaderboardSubmit"}, quiet=True)),
           "... and the per-function check catches it")

    # 3. An innocent on-device use must NOT read as a leak. A check that fires on
    #    honest work is one people learn to bypass, which costs more than it saves.
    per, leaks = scan(
        'void AircraftManager::DrawSomethingNew(BandCanvas& b)\n'
        '{\n'
        '    b.drawString(followTarget, 0, 0);\n'
        '}\n')
    fails = report({"AircraftManager::DrawSomethingNew"}, set(), leaks, quiet=True)
    expect(not leaks and not any("outbound sink" in f for f in fails),
           "an on-device use is not reported as a leak")
    expect(any("not on the review list" in f for f in fails),
           "... but it is flagged for review")

    # 4. THE ANCHOR CONTROL ITSELF. A scanner that finds nothing must fail.
    expect(any("no longer finds" in f for f in report(set(), set(), quiet=True)),
           "a scanner that sees nothing is reported as broken, not as safe")

    print("  SELFTEST PASSED" if state["ok"] else "  SELFTEST FAILED")
    return 0 if state["ok"] else 2


# ---------------------------------------------------------------------------
# CHECK 3: EVERY TARGET-SHAPED MEMBER IS CLASSIFIED, OR THE BUILD FAILS.
#
# THE HOLE THIS CLOSES, AND HOW IT WAS FOUND. `followSessionTarget` was added on
# 2026-08-28 and was INVISIBLE to checks 1 and 2 for the whole of that build --
# it is not a substring of "followTarget" ("followSession" + "Target"), so a
# gesture-set target went unscanned. It was noticed by luck, while reading
# unrelated churn in the review list.
#
# Checks 1 and 2 scan for KNOWN NAMES, so anything newly named is absent from
# them by construction, and absence is exactly what they cannot report. This one
# inverts that: it enumerates the DECLARATIONS and requires each to have been
# classified. A new `String followWhatever` is a build failure until somebody
# says which kind of thing it is.
#
# WHAT IT STILL DOES NOT CLOSE, stated so it is not mistaken for complete: it is
# keyed on the `follow` prefix, so a target-shaped member named
# `sessionAircraftId` would escape it. The form that closes that is a TYPE --
# wrap the value so only code with access can read it, and let the compiler be
# the guard. That is a real refactor and is filed rather than done.
DECL_SCAN_FILE = os.path.join(ROOT, "src", "AircraftManager.h")
MEMBER_DECL_RE = re.compile(r"^\s*String\s+(follow\w*)\s*=", re.M)

# Each member, and which kind of thing it is. "target" means §17 applies.
MEMBER_KINDS = {
    "followTarget": "target",         # the configured one, from the config page
    "followSessionTarget": "target",  # the gesture-set one; RAM only, never NVS
    # Not targets, and the reasons are load-bearing rather than decorative:
    "followHomeCode": "not-a-target",   # derived from the device's OWN configured
                                        # location, not from what the customer
                                        # typed about an aircraft
    "followRouteOrigin": "not-a-target",  # an airport code off the enrich path
    "followRouteDest": "not-a-target",
}


def check_declarations():
    """Returns (failures, found) -- every follow* String member must be classified."""
    fails = []
    try:
        with open(DECL_SCAN_FILE, encoding="utf-8", errors="replace") as fh:
            src = fh.read()
    except OSError as e:
        return ([f"cannot read {DECL_SCAN_FILE}: {e}"], set())

    found = set(MEMBER_DECL_RE.findall(src))

    # The anchor control, same shape as check 2's: finding nothing means the
    # regex or the path broke, not that the members vanished.
    if not found:
        return ([
            "the declaration scan found NO follow* members at all. That is a "
            "broken scan, not a clean codebase -- check DECL_SCAN_FILE and "
            "MEMBER_DECL_RE before believing anything else in this run."
        ], found)

    unclassified = sorted(found - set(MEMBER_KINDS))
    if unclassified:
        fails.append(
            "these follow* members are declared but not classified:\n    "
            + "\n    ".join(unclassified)
            + "\n  Add each to MEMBER_KINDS as 'target' or 'not-a-target' WITH A"
              "\n  REASON. If it is a target, it must also be in TARGET_TOKENS --"
              "\n  that is the whole point: a new target-shaped member is"
              "\n  invisible to the name-based checks until someone says so."
        )

    missing = sorted(set(MEMBER_KINDS) - found)
    if missing:
        fails.append(
            "MEMBER_KINDS classifies members that no longer exist:\n    "
            + "\n    ".join(missing)
            + "\n  Either they were renamed (update this) or the scan is broken."
        )

    # And the classification must agree with what checks 1 and 2 actually scan.
    for name, kind in MEMBER_KINDS.items():
        if kind == "target" and name not in TARGET_TOKENS:
            fails.append(
                f"{name} is classified 'target' but is not in TARGET_TOKENS, so "
                f"checks 1 and 2 do not look at it. That is exactly the "
                f"followSessionTarget hole."
            )
    return (fails, found)


def main():
    if "--selftest" in sys.argv:
        return selftest()
    touching, sinking, line_leaks = analyse(ROOT)
    fails = report(touching, sinking, line_leaks)
    decl_fails, decl_found = check_declarations()
    print(f"  follow* members declared, all classified: {len(decl_found)}")
    fails = list(fails) + decl_fails
    if fails:
        print("\nFOLLOW PRIVACY CHECK FAILED (spec 17):\n")
        for f in fails:
            print("  " + f + "\n")
        return 1
    print("  ok  the follow target reaches exactly one outbound sink: the ntfy body")
    return 0


if __name__ == "__main__":
    sys.exit(main())
