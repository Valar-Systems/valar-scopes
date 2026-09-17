# Fixtures for the fresh-boot acceptance step-4 assertion

Two synthetic capture logs. They exist so the assertion that guards the
2026-08-21 defect can be shown to FAIL, and to fail for the right reason --
a check nobody has watched reject anything is a check nobody can trust, and
this one spent its life rejecting a healthy board instead.

    bash scripts/fresh-boot-acceptance.sh --assert-only \
         test/fixtures/acceptance/persist-ten-minutes-late.log
    # step 4 must FAIL: "came 600s later, past the 60s bound"

    bash scripts/fresh-boot-acceptance.sh --assert-only \
         test/fixtures/acceptance/persist-before-and-after.log
    # step 4 must PASS: a persist at -30s AND one at +18s

`persist-ten-minutes-late.log` is the shape of the original defect: the book
IS written, just not until long after the owner has opened Collection and
found it empty. Ordering alone accepts it, which is why the check is bounded
in time rather than only in sequence.

`persist-before-and-after.log` is the shape that broke the OLD check: a
perfectly healthy run where something persisted before the first claim. The
old assertion took the first persist in the file and failed on this forever.
