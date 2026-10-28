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

**`bend.bmpk`** is the stored Bend package (3.5b step 2, docs/weatherscope-stage3.md A12), built from real sources:
- **beside it:** `bend.manifest.json` (every file read, with its URL, sha256, bytes and vintage; F1's `builder`,
  F2's `states` and `counties`) and `bend.LICENSE.txt` (the sources' terms, the SRTM credit and its §3.15 notice);
- **the origin** lives only in the package's binary header. No tool prints it, and no file records it;
- **how it was made:** fetch the sources into a directory outside every repo, then build, with the tool committed
  (it refuses a dirty tool directory, so `builder:` names the code that ran):

      python -I tools/weatherscope/basemap/make_package.py fetch --origin-from <proxy/src/sitefeed/bend.ts> --sources <dir>
      python -I tools/weatherscope/basemap/make_package.py build --origin-from <proxy/src/sitefeed/bend.ts> --sources <dir> --out test/fixtures/wx/basemap/bend.bmpk

- **the determinism check** rebuilds at the package's own recorded `builder` commit, in a temporary worktree, and
  compares bytes (exit 0 identical, 1 different, 2 could not run):

      python -I tools/weatherscope/basemap/make_package.py check test/fixtures/wx/basemap/bend.bmpk --sources <dir>

- **the licence check** (with its control: the notice deleted from a copy must turn it red):

      python -I tools/weatherscope/basemap/check_licence.py test/fixtures/wx/basemap/bend.LICENSE.txt --selftest
