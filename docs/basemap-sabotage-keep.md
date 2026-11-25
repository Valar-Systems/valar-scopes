# The basemap sabotage driver keeps the evidence of a run it cannot grade

**Status:** the keep PR. Its predictions were frozen before any code was written (below, "Predictions"); the readings follow
them and do not edit them.

## Why

[proxy/scripts/sabotage-basemap.mjs](../proxy/scripts/sabotage-basemap.mjs) plants each sabotage, runs the basemap tests, and
grades the failing cases against a frozen set. Until this change, `runTests()` deleted each run's temporary directory, which
holds vitest's JSON report, **before** the caller graded the run. A run that came out wrong could say so, but not why.

That happened twice in one PR (#797, [basemap-cap-equality.md](basemap-cap-equality.md)):

- **RUN 2:** S-PRIMARYDROP also failed ME2, once. Eight later runs did not reproduce it, and the report that would have said why
  was gone.
- **RUN 3:** S-RBMAP read "the Workers runtime failed partway". The runtime's own error was gone too.

A guard that can tell you a result is untrustworthy should also be able to tell you why.

## The rule

- **A run whose line is not as predicted keeps its evidence.** That means OFF-PREDICTION, BLIND, UNREADABLE, or a baseline that
  failed. The run's directory (`r.json`, `stdout.txt`, `stderr.txt`) moves under one `bm-sab-kept-<UTC stamp>` directory in
  the OS temp directory, named after the entry (or `baseline-<set>`).
- **Its line names the directory:** `... [kept: <dir>]`. The path is printed on the line that reports the failure, because
  that is where anyone reading the failure already is.
- **Every other run's directory is removed, as before.** That includes a control's predicted "AS PREDICTED, BLIND". A clean
  pass leaves nothing behind.
- **The kept directory is outside the git tree,** so a pass leaves the tree as it found it.

## The selftest, and CI

`node scripts/sabotage-basemap.mjs --selftest-keep` proves both directions on the real Workers pool, using the cheap `mv` set
(one file, `-t MV`):

- **RED:** S-VCRC graded against a deliberately wrong expect (an empty set) must read OFF-PREDICTION. Its line must name a kept
  directory holding all three files, and the kept `r.json` must parse with exactly {MV/PK-crc} failing.
- **CONTROL:** S-VCRC graded against its true expect must read AS PREDICTED. It must keep nothing, and its run directory must
  be gone.
- No kept root may appear except RED's. RED's root is then removed, and its absence checked.

CI runs it as a step in `workers.yml`'s proxy test job, after `npm test`. The whole driver takes about an hour and stays local;
the keep rule is the part that must not silently regress.

## Predictions (frozen before any code; mirror/keep27.pred, registered 2026-11-25)

The tree is main at 0b998272 (#797's squash).

- **`--selftest-keep`:**
  - exit 0, stderr empty;
  - the mv baseline reads 31 cases in 1 file;
  - RED reads OFF-PREDICTION and names its kept directory;
  - CONTROL reads AS PREDICTED with nothing kept;
  - every check passes, and the tally reads 3 runs;
  - afterwards nothing is left in the temp directory and nothing in the tree has changed.
- **Red first of the selftest,** each on a copy of the driver:
  - with `settle()` never keeping, the RED checks fail;
  - with `settle()` never removing, the CONTROL check fails.
- **Red first of the CI step,** ruled: the step's own command, run against the real driver with the keep disabled, must fail.
- **`--selftest-dups`:** exit 0, as on main.
- **A full driver pass on the change:**
  - the four baselines read 12/1, 31/1, 203/20 and 15/1;
  - all 94 entries are AS PREDICTED, with C-LOADFAIL BLIND as its control requires;
  - no line carries `[kept:`;
  - 98 runs, no runtime failure, exit 0;
  - against #797's RUN 3, every line is equal except RUN 3's S-RBMAP line and its tally.
- **CI:** 39 runs = 37 success / 2 skipped / 0 failed. test (proxy): 86 files and 1,201 tests, as on main. Its log shows the new
  step with every keep check PASS.

**Outside the set.** The instrument-failure rule still applies first: a runtime failure carries no verdict, and one re-run is
allowed. The difference now is that such a run keeps its evidence, and the evidence is read and reported. Any other
difference stops the work and is reported.

## Readings

### P1 to P3 (2026-11-25, 01:00-01:03Z): AS PREDICTED

- **`--selftest-keep`:** 7 of 7, 3 runs, exit 0.
  - RED kept its three files, and its `r.json` failed exactly {MV/PK-crc}.
  - CONTROL kept nothing.
  - Nothing was left in the temp directory. The count that says so was first shown to see a planted directory.
- **Red first of the selftest,** each on a copy of the driver:
  - with `settle()` never keeping, the three keep checks failed (4 of 7);
  - with `settle()` never removing, only CONTROL's removal check failed (6 of 7), and exactly its two run directories were left
    behind.
- **Red first of the CI step:** the step's own command, run against the real driver with the keep disabled in place, exited 1
  (4 of 7). The driver was then restored from a byte copy.
- **`--selftest-dups`:** exit 0.

### P4, the full pass (2026-11-25, 01:03:31-02:07:13Z): AS PREDICTED

- The four baselines, each 0 failing: [ingest] 12 cases in 1 file; [mv, -t MV] 31 in 1; [all] **203 in 20**; [sea] 15 in 1.
- All 94 entries AS PREDICTED, with C-LOADFAIL BLIND (115 baseline cases not run) as its control requires. No line carries
  `[kept:`.
- 98 runs, no runtime failure, exit 0. The temp directory held nothing afterwards.
- Against #797's RUN 3, every line is equal except S-RBMAP's and the tally's. Here S-RBMAP reads AS PREDICTED {MN7} and the tally
  "0 partway".
