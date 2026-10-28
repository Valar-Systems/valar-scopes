# BMPK fixtures

The format these test is [docs/basemap-package-format.md](../../../../docs/basemap-package-format.md) (BMPK v1).
Their truth table is `docs/weatherscope-stage3.md` A10.11, the PK family.

- **`syn-min.bmpk`:** the base package. Every value in it is written out as data in
  [`tools/weatherscope/basemap/synth.py`](../../../../tools/weatherscope/basemap/synth.py), so another builder can
  reproduce it byte for byte (the map server's ME1).
  - origin (0, 0), a test point;
  - R 97,000,000 mm, ε 100,584 mm;
  - one 4-vertex water ring, one river with one name, one class-2 road;
  - one peak, "Mount Test" at 1,000 m;
  - one place, "Test city" with population 1,234;
  - one N = 4 terrain level;
  - a two-line provenance.
- **`syn-unknown.bmpk`:** `syn-min` plus an unknown section type 9 (3 bytes). It is valid, and the type is skipped.
- **`syn-order.bmpk`:** A10.11's LN fixture, at the 60 mi view's own radius.
- **`neg-*.bmpk`:** 25 packages, each `syn-min` with one mutation. Each must be refused with its own reason (A10.11's table).

Regenerate, or check that every committed byte matches:

    python -I tools/weatherscope/basemap/synth.py test/fixtures/wx/basemap [--check]

**The checks behind them:**
- The generator asserts that each mutation took, and that the CRC is right wherever it was recomputed.
- The reference validator (`validate.py`, written from A10.11's pinned order) gives each fixture its frozen reason.
- A validator with the CRC check removed accepts `neg-crc`, so the check can fail.

`bend.bmpk`, its manifest and its licence file come from 3.5b's step 2.
