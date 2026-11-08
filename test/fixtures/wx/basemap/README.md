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
- **`neg-*.bmpk`:** 27 packages, each `syn-min` with one mutation. Each must be refused with its own reason (A10.11's
  table, and A12.15's two: `neg-terrain-count` is TERRAIN's count 2, `kTerrainCount`; `neg-prov-count` is
  PROVENANCE's count 3 against its 2 lines, `kProvenanceCount`).

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

**`kjac.bmpk` and `ksfo.bmpk`** are the map client's fixtures (docs/blipscope-basemap-client.md): Jackson Hole and
San Francisco, each exactly as staging's `/basemap` route served it at the client's default query:

    lat=43.61&lon=-110.74   (KJAC)    lat=37.62&lon=-122.38   (KSFO)
    &r=162&eps=201168&layers=water,rivers,roads,places,terrain&levels=41239440:246,81808320:244,162275520:242

- **built by** the map server's builder at `fdf62ada9f79`, from store `4c0875738f38`. No peaks: the query asks for none.
- **beside each:** `<name>.manifest.json` (every file the build read, sorted by URL, with the builder's commit and
  the store's `predicates`) and `<name>.LICENSE.txt` (the sources' terms, the terrain credits the package's
  PROVENANCE names, and SRTM's §3.15 notice).
- **Unlike Bend's, a package's `store:` line is not the hash of the manifest beside it.** It names the store's own
  manifest (3,867,380 B), as format §7 says for a package from the route. The file beside it is the part of that
  store this build read, so each layer's `files.<layer>` and `sha256.<layer>` recompute from it.
- **how they were checked:**
  - a local build of the same query, at the same builder commit, over a copy of the store, gave the route's bytes
    exactly;
  - each layer's digest over the manifest's files equals the package's own `sha256.<layer>` line.
- **water, under the ladder they were built with:**
  - KJAC holds 1.2%, with nothing dropped;
  - KSFO holds none: its ladder went to rung 3, dropping 138 water rings (one more collapsed) and 580 secondary
    roads.

  The next store (the A+B ladder and the sea mask, docs/basemap-water.md) changes both, and these fixtures are re-cut
  from it.
