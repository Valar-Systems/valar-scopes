# The basemap cap's bound is `<=`: a plant that proves the tests catch `<`

**Status:** the S-STRICT PR. Its predictions were frozen before anything ran (below, "Predictions"); the readings follow them and do
not edit them.

## What this is

`fitBudget()` in [proxy/src/basemap/budget.ts](../proxy/src/basemap/budget.ts) accepts a package when its non-terrain size is
**at most** the device's cap:

```ts
return lastBytes <= capBytes ? s : null;
```

Equality is not a corner nobody reaches. The ladder fills to the byte, and in the cap check of 2026-11-22, 1 of 22 references
landed exactly AT its device's cap. A strict `<` would serve that reference a coarser rung (or a smaller disc) for no reason.

The server half's equality was already tested in pairs: MB1/MB2, MB5/MB4, MB8a/MB8b and MN2 each pair "served at exactly the cap"
with "one byte under, not at that rung". What was missing was a **plant that shows those tests catch a strict bound**: a rule
nobody has seen reject anything is untested. This PR adds that plant to
[proxy/scripts/sabotage-basemap.mjs](../proxy/scripts/sabotage-basemap.mjs), and one named row.

## The named row: MB8c

RD20's cap measures the package **less its TERRAIN bodies** (their directory entries count). So a build that lands at its cap
exactly serves:

> served total == capBytes + sum over its TERRAIN sections of (8 + n * n)

MB8a shows that only in numbers: A8 at 1,322 serves 1,346, which is 1,322 + (8 + 4 * 4). MB8c builds the same fixture at the same
cap and asserts the identity in words, with every figure read from the package (each TERRAIN section's `n`, and its values holding
`n * n` bytes). Its control: the package has at least one TERRAIN section, since without one the identity is MB1's, not this row's.

## The plant on a copy of H6 (2026-11-22)

Ruled 2026-11-22: its own PR on main after #696 merges, so nothing moved #696's head before its staging run; the sabotage run first,
locally, on a copy of H6's tree (not the branch; nothing pushed; no network).

- **Predicted** red set: {MB1, MB5, MB8a, MN2}.
- **Read**: {MB1, MB4, MB5, MB8a, MN2, T-PROV-b}, so OFF-PREDICTION; the copy restored byte-equal and shown green.
- **The two misses**, each an exact-equality case:
  - MB4: at H6 it reads "A at 623 ... D2 builds it at 49.664 km", 200 with totalBytes 623 at cap 623. The prediction's case
    list had been taken from main's older test file, where MB4 was a 422.
  - T-PROV-b: provB at cap 458 is served at rung 2 with non-terrain bytes exactly 458 (read offline before the set changed; the
    control at cap 457 moves to rung 4, 445 B).
- **Amended by ruling** to the observed six, then re-run once: AS PREDICTED.
- **The lesson**, one line: a prediction for a tree is derived from THAT tree.

## Predictions (frozen before RUN 1)

The tree is main at e59c8fca. Between H6 and that main, nothing the driver plants in or grades changed (the only proxy test changes
are device-label-lines.test.ts and leaderboard.test.ts, outside its sets), so H6's reads stand where one exists.

No CI job runs the sabotage driver, and a new row moves older plants' failing **sets**, not only counts. So the whole driver runs
twice: on clean main first, then on the changed tree. Drift already on main is then never read as this change's.

**RUN 1, the control pass** (`node scripts/sabotage-basemap.mjs`, all 93 entries, clean main):
- no duplicate-id refusal;
- baseline[all]: 0 failing, 202 cases seen in 20 files (H6's read); the other baselines as read (none on record), and RUN 2 must
  equal them;
- every entry AS PREDICTED against its set in the file, C-LOADFAIL BLIND;
- 97 vitest runs (93 + 4 baselines); exit 0; the tree clean afterwards.

**The change:** MB8c, S-STRICT with {MB1, MB4, MB5, MB8a, MB8c, MN2, T-PROV-b}, and MB8c re-registered into the two sets it joins:
S-TERRCAP {MB8a, MB8b, MB8c} and S-TNOMARGIN {MT3b, MB8a, MB8b, MB8c}.

**Why exactly those three.** MB8c and MB8a build the same thing: A8 at cap 1,322, with one terrain level whose `n` (4) comes from
the query. Its TERRAIN body is 24 bytes under every plant, since none touches the TERRAIN encoder. So MB8c is red exactly when that
build is not a 200 or its total is not 1,346, and either turns MB8a red too. Conversely, every field MB8a asserts beyond the total
is fixed by the rung and the content, and at the same cap a different rung or content means a different total. So MB8c's red sets
are exactly MB8a's (S-TERRCAP, S-TNOMARGIN), plus S-STRICT.

**RUN 2, the change pass:**
- `--selftest-dups`: exit 0, no duplicate (MB8c is a new id);
- `tsc --noEmit`: exit 0, no output;
- the whole driver: baseline[all] 203 cases in 20 files, the other baselines equal to RUN 1's; S-STRICT, S-TERRCAP and
  S-TNOMARGIN AS PREDICTED with the sets above; the other 91 entries exactly as in RUN 1; 98 runs; exit 0.

**Outside the set.** A pool or runtime failure, or any file-level crash before assertions, is an **instrument failure**: no
verdict, not drift, not an OFF; one re-run is allowed, a second needs a ruling. Otherwise any difference stops the work and is
reported; a set changes only by ruling.

**CI:** 39 runs = 37 success / 2 skipped / 0 failed. The proxy suite's line is derived, not read: 85 files (84 + 1 skipped) and
1,155 tests (1,154 + 1 skipped).

## Readings

### RUN 1, the control pass on clean main (2026-11-24, 11:53:41-12:56:34Z): AS PREDICTED

- No duplicate-id refusal. The four baselines, each 0 failing: [ingest] 12 cases in 1 file; [mv, -t MV] 31 in 1; [all] **202 in
  20**, as H6 read it; [sea] 15 in 1.
- All 92 graded entries AS PREDICTED; C-LOADFAIL BLIND (114 baseline cases not run), as its control requires.
- 97 vitest runs; runtime failures 0 (none with nothing run, none partway); exit 0; the tree clean afterwards.
- The port gate held the runs for 1,139 s in all; the highest TIME_WAIT at a run's start was 7,986 (gate 8,000).
- Information, not a reading: four entries graded with MD4 set aside (S-BUSSUB, S-SQUARELEN, S-PLACESORT, S-TNOMARGIN). That
  is the driver's own A1.7 rule: MD4 compares against the committed outputs, so it fires on any change to builder output, and it
  is set aside unless an entry names it.

So the driver carried no drift on main at e59c8fca. Main has since moved by nine PRs (#794, #795, #755, #790, #791, #792, #793,
#796, #798). The driver's reach, taken from the driver itself: it plants in `proxy/src/basemap/`, three files in
`proxy/scripts/basemap/` (ingest-lib, sea-pieces, uscontain), the attribution doc, `src/photolicense.ts` and `src/metrics.ts`
(S-MKAE), and its set "all" also runs through `src/index.ts` and `src/metrics.ts`, because `basemap-endpoint.test.ts` calls the
proxy's own dispatch (`test/helpers.ts`). Two of the nine reach it: #792 changed code in `src/metrics.ts` (its route labels), and
#798 changed one comment each in `src/metrics.ts` and `src/index.ts`. An earlier version of this paragraph listed the reach
without those three files; it held for the first five PRs only because none of them touched those files. So the whole driver ran
again on this branch with main merged in: RUN 3, below.

### RUN 2, the change pass (2026-11-24, 17:55:46-18:57:42Z): one entry OFF

- `--selftest-dups`: exit 0, no duplicate (its planted copy was still caught). `tsc --noEmit`: exit 0, no output, with
  `basemap-build.test.ts` in its file list.
- The driver: baseline[all] 0 failing, **203 cases in 20 files**; S-STRICT, S-TERRCAP and S-TNOMARGIN AS PREDICTED with MB8c;
  C-LOADFAIL BLIND (115 baseline cases not run); 98 runs; no runtime failure.
- **One OFF:** S-PRIMARYDROP failed {MB4, ME2, T-D2, T-D2c} against {MB4, T-D2, T-D2c}. In RUN 1 the same entry read as
  predicted. Exit 1.
- Not an instrument failure by the rule above (no crash, no runtime failure). Its cause cannot be read: the driver deletes each
  run's vitest report before it grades.

### The diagnosis (ruled; predictions frozen before each step)

- ME2 lives in `basemap-me2.test.ts`, which this change does not touch, and the S-PRIMARYDROP plant is byte-identical to RUN 1's.
  ME2's 1,000 stores come from a seeded generator (mulberry32), and the build path it calls reads no clock.
- ME2 alone, under the driver's own plant (made from each tree's own driver; the two outputs byte-equal), twice on unchanged main
  and twice with this change, alternating: all four passed, in about 19 s of its 900 s limit, with identical tallies
  (1,000 built, 0 too_large, 0 other; largest PROVENANCE 2,281 B).
- The driver's single-entry mode on the changed tree, three times, with ME2 among the 20 files as in RUN 2: all three as
  predicted.

### RUN 2b, the graded pass (2026-11-24, 19:21:22-20:23:24Z): AS PREDICTED

- Ruled: one whole-driver re-run with RUN 2's predictions unchanged.
- The baselines, each 0 failing: [ingest] 12 cases in 1 file; [mv, -t MV] 31 in 1; [all] **203 in 20**; [sea] 15 in 1.
- All 94 entries AS PREDICTED: S-STRICT {MB1, MB4, MB5, MB8a, MB8c, MN2, T-PROV-b}; S-TERRCAP {MB8a, MB8b, MB8c}; S-TNOMARGIN
  {MB8a, MB8b, MB8c, MT3b}; S-PRIMARYDROP {MB4, T-D2, T-D2c}; C-LOADFAIL BLIND (115 baseline cases not run).
- 98 vitest runs; runtime failures 0; exit 0; afterwards the tree held exactly the three changed files.
- Against RUN 2, every line is equal except S-PRIMARYDROP's.

**RUN 2's ME2 failure is recorded as a one-off: unexplained, and not reproduced in the 9 runs after it. It is not recorded as a
pass.** Its cause is unreadable only because the driver throws each run's report away. Keeping the report of any run that reads
OFF or BLIND is accepted as a separate change, after this one.

### RUN 3, on this branch with main 80d77cea merged in (2026-11-24 23:14:20Z to 2026-11-25 00:20:00Z): AS PREDICTED, with one re-run

- Ruled after the reach finding above: the whole driver on the merged tree, with RUN 2b's predictions line for line.
- The baselines, each 0 failing: [ingest] 12 cases in 1 file; [mv, -t MV] 31 in 1; [all] **203 in 20**; [sea] 15 in 1.
- 93 of the 94 entries AS PREDICTED, among them S-STRICT, S-TERRCAP, S-TNOMARGIN, S-PRIMARYDROP (ME2 passed under its plant
  again) and S-MKAE {MK7}, the entry that plants in `src/metrics.ts`; C-LOADFAIL BLIND (115 baseline cases not run).
- **One instrument failure:** S-RBMAP read "BLIND (the Workers runtime failed partway (2 baseline cases not run))", and the
  tally "1 partway". By the rule in force for these runs, a runtime failure is no verdict and not an OFF, and one re-run is
  allowed. Its cause cannot be read, for the same reason as RUN 2's ME2: the driver deletes each run's report.
- **The re-run**, ruled as the narrow one: the driver's single-entry mode for S-RBMAP on the same tree. baseline[all] 0 failing,
  203 in 20; S-RBMAP AS PREDICTED {MN7}; 2 runs, no runtime failure; exit 0.
- So all 94 entries are graded on the merged tree: 93 in RUN 3 and S-RBMAP in its re-run. Against RUN 2b, every other line of
  RUN 3 is equal once the port-gate figures are set aside.
