# Q-RV7: why the shipped builder holds 6 to 7 MiB more than the copy at KORD and KIND (a read before anything moves)

**A local, read-only read, registered before it runs.** #639's measurement run found the shipped builder (the release variant) reading
6 to 7 MiB above Q-DR3's scratch copy from the tile loop to the end, at two points of 13. This finds out whether that is the builder's,
and where it is made. Nothing changes in any builder, and nothing runs on staging.

## The ruling (from the orchestrator, read at 19:29Z), verbatim (its Q-RV7 part)

> Q-RV7: yes, read-only, registered and committed first.
> - (1) The first cross: the copy and the PR at KORD, one process each, in the same child family (dr-child5), the same day.
>   - Predicted, if the difference is the builder's: PR - copy is about +5.95 at the tile and ladder phases again.
>   - If it's about 0 in the same child: the old Q-DR3 copy figure at KORD is what moved (the date or the environment). Say so, and
>     re-base the copy figures.
> - (2) Then the per-tile samples, to find where the extra is made. Name the tile, and what it holds.
> - Either way, report what it means for Q-W4's memory predictions under B, with each point's figure on the SHIPPED builder.
> - Anything outside: stop and report.

## What is known (#639's record, `docs/basemap-release.md` on its branch)

The shipped builder minus Q-DR3's copy, per phase, MiB:

| | cell | tile, tiles, terrain, layers, ladder |
|---|---|---|
| KBOS and the four New York points | −1.28 to −7.41 | within 0.10 |
| **KORD** | 0.02 | **+5.94 to +5.97** |
| **KIND** | −0.73 | **+6.89 to +6.91** |
| the other 6 | within 0.03 | within 0.16 |

## The read, registered before it runs

**One process each, at KORD, at the client's query,** in `dr-child8.mts` (sha256 `e23759cae36c`). That is `dr-child7.mts` (the
`dr-child5` family, which takes "copy" and "pr") plus two outputs:
- **the per-tile trace:** every tile sample, in order;
- **the non-cell reads in request order,** with their sizes. Each tile sample matches the tile read consumed there.

**The builders:**
- the copy is `rv-hk/hk-copy`, byte-identical to Q-DR3's `dn/src/basemap`;
- the PR is `rv-hk/hk-pr`, the PR's hooked builder (`f60ae55f45d9`).

**The wrapper** is `rv-q7.sh` (sha256 `5459c9b9adbc`). It checks the artifacts, runs the copy then the PR, and prints both
traces, their difference per sample, every step over 1 MiB, and the read consumed at each step. **The child's imports were checked first:**
given an unknown point as "copy" and as "pr", it loaded every module and refused the point (exit 2).

| row | what | predicted |
|---|---|---|
| **R7-0** | both processes | the controls held, every sample settled, every object local, and KORD's 200 with Nov 8's package `b54fa953fef7` |
| **R7-1** (1) | the PR minus the copy at the ladder phase | **BUILDER: within 0.785 of +5.95** (the difference is the builder's) |
| **R7-2** (2) | the two tile traces | the same length. **Where the extra is made is not predicted:** the step's tile index, size and read are what this reads |

| outcome | meaning | next |
|---|---|---|
| **R7-1 BUILDER** (predicted) | the difference is the builder's | (2) names the tile and what it holds |
| R7-1 ZERO: within 0.785 of 0 | **the old Q-DR3 copy figure at KORD moved** (the date or the environment) | say so, and re-base the copy figures |
| R7-1 outside both bands, R7-0 off, or the traces differing in length | the instrument is suspect | **stop and report** |
| **R7-2: one step, in one read, present in the PR's trace and absent from the copy's** | that read is where the extra is made | name it, and what it holds |
| R7-2: anything else (several steps, or none) | | **reported as read** |

**For Q-W4, either way:** the report gives each point's figure on the shipped builder (#639's measurement). B's figures came from the
copy, which differs from the shipped builder by −7.41 to +6.91 MiB depending on the point and the phase.

**What this cannot see:** workerd; and KIND, which this read leaves out (the ruling names KORD). If KORD's cause is found, KIND is
checked against it in the report.

## The read (2026-11-11, 19:35:34-19:35:40Z): the difference is the builder's, and it is made before the tile loop

The registration was pushed at 19:35:29Z. The wrapper and every artifact check read as registered. The error files are empty, and
nothing ran on staging. The log is scratch (sha256 `d9109448aaaf`).

| row | read | |
|---|---|---|
| **R7-0** | both: controls held, every sample settled, every object local, 200 with `b54fa953fef7` | as predicted |
| **R7-1** | the copy 39.84 MiB, the PR 45.81, both in the ladder phase: **PR minus copy +5.97** | **as predicted: BUILDER** |
| **R7-2** | 65 tile samples each (64 tiles, then `terrain-sources.bin`). **PR minus copy is +5.98 at the first sample and stays at 5.98-6.03 through all 65** | **one step, at the first sample** |

**Where the step is, and why the tile doesn't explain it:**
- **The read consumed at the first sample is `terrain/9/127/186.png`, 59,858 B,** about 0.19 MiB once decoded. A 5.98 MiB step can't be
  that tile's own bytes.
- **The step is therefore made before the first tile sample.** It sits somewhere between the copy and the PR agreeing in the cell loop
  (29.51 against 29.56 at its largest) and the first tile.
- In that window both builders run the same code, apart from the hooks: the regions' file references, the places, the tile list, and
  the tile loop's first six reads issued.
- **The cell phase is reported only as its largest sample.** So the step could also have formed late in the cell loop, after the
  largest. This read cannot place it more finely.
- **By the registered table:** one step, at one sample, but not made by that sample's read. This is closest to "anything else",
  **reported as read: the tile is not the cause.**

**What it means for Q-W4 under B: each point's figure on the shipped builder (#639's measurement, v0, MiB):**

| KBOS | KJFK | KLGA | KEWR | Midtown | KMIA | KSFO | KSEA | KORD | KDTW | KMSP | KIND | KJAC |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 56.70 | 58.54 | 60.01 | 60.81 | 58.11 | 39.39 | 39.21 | 49.18 | **45.79** | 47.13 | 47.27 | **50.85** | 38.88 |

- **B's figures came from the copy.** At New York and KBOS, the shipped builder's ladder phase matches the copy's to within 0.10 MiB, so
  **B's largest, New York's 162 km tries at 74.2-77.3 MB, are not moved by what this read found.**
- **At KORD and KIND, the copy's B figures are probably 6 to 7 MiB low:** 48.5 and 53.6 MB from the copy, so about 54.7 and 60.8 MB on
  the shipped builder. **That is an estimate,** made by adding this offset to the copy's figure, not a measurement. Both stay well under
  New York's.
- **So Q-W4's memory predictions should be measured on the shipped builder,** which Q-W4 builds on, not carried from the copy.

**Proposed next (Q-RV8), read-only and registered first:** the same two processes at KORD, with every cell's sample traced, plus samples
after the regions' file references, after the places, and after the tile list. That places the 6 MiB step to one statement.

## The ruling (from the orchestrator, after Q-RV7; its stamp is in its ledger), verbatim

> The question this raises, and it comes first: every comparison so far has been the PR against the COPY. Is #639 a regression against
> MAIN's builder at KORD and KIND? If main reads about 39.8 at KORD and the PR 45.8, then #639 adds 6 MiB there while it saves 39.5 at
> KLGA. That has to be known before I approve a merge.
>
> Q-RV8, with a part 0 added. Read-only, registered and committed first:
> - Part 0, the decisive row: main's builder (78c2bec2's, i.e. the repo's, unhooked except the instrument) against the PR's builder, at
>   all 13, one process each, the same child, the same day.
>   - Predicted for a builder that only releases earlier: PR <= main + 0.785 at every point, in every phase from the tiles on.
>   - If PR > main + 0.785 anywhere: #639 is a regression at those points. Stop, report each, and #639 isn't approved until it's
>     explained and the owner has seen it.
>   - Anything else: report.
> - Parts 1+: your per-cell trace and the samples after the region refs, the places and the tile list, to place the step to one
>   statement.
>
> #639: approval HELD until part 0 has read, whatever its CI says.

## Q-RV8: registered before it runs

**Part 0, the decisive row:**
- **main's builder:** `rv-hk/hk-main` (main's `build.ts` at `8f1a878f518f`, whose `proxy/src/basemap/` is unchanged at `78c2bec2` and at
  main's head `192faed8b095`), with only the instrument's seven hooks (`bc22989950b4`);
- **the PR's builder:** `rv-hk/hk-pr` (`f60ae55f45d9`);
- at all 13 points, one process each, main then the PR at each point, in `dr-child8.mts` (`e23759cae36c`), the same day.
- **Predicted: PR ≤ main + 0.785 at every point, in every phase from the tiles on** (tile, tiles, terrain, layers and ladder). In the
  cell loop the PR is predicted lower, wherever main holds its reads.
- **Every point is read.** If the PR is above main + 0.785 anywhere, the run then stops and lists each such point. **#639 is not approved
  until that is explained and the owner has seen it.**
- **The answers must agree** at every point (the same package, or the same ladder), or the run stops.
- **The 96 MB band stops only the PR's builds.** Main's builds at the dense points are predicted to read over 96 MB in the cell loop
  (KLGA 99.54 MiB): that is the control, as under the owner's (a).

**Part 1, only if part 0 passes:**
- the copy and the PR at KORD, then at KIND, one process each, in `dr-child9.mts` (`281fb4b65f2d`): `dr-child8` with every cell's
  sample recorded, and accepting the two trees below;
- each tree gains three more samples, and nothing else changes: after the cell loop ("postcell"), after the regions' file references
  ("refs"), and just before the tile map ("tilelist"). The trees are `rv-hk/hk-copy3` (`41adba1fa9c1`) and `rv-hk/hk-pr3`
  (`f806da58b408`), each `build.ts` three lines longer than `hk-copy`'s and `hk-pr`'s, every other file identical;
- **reported, not predicted:** the first sample (cell by cell, then postcell, refs, tilelist and the first tile) where PR minus copy
  steps up by more than 3 MiB. That names the statement window.

**The driver** is `dr-q8.mts` (sha256 `a42e34f63b86`). **Every child's imports were checked first:** given an unknown point, each loaded
every module under each builder name and refused the point (exit 2). Nothing runs on staging.

| outcome | meaning | next |
|---|---|---|
| **part 0: PR ≤ main + 0.785 everywhere from the tiles on** (predicted) | #639 is no regression against main at any point | part 1; #639's approval is the orchestrator's |
| **part 0: PR above main + 0.785 anywhere** | **#639 regresses at those points** | **stop; each point reported; #639 held until explained and seen by the owner** |
| an answer differing, a failed control, or a PR build over 96 MB | the instrument is suspect | **stop and report** |
| part 1: one step over 3 MiB at one sample | the statement window named | report it |
| part 1: anything else | | reported as read |

## Q-RV8, read (2026-11-11, 19:41:05-19:42:19Z): part 0 as predicted at all 13. #639 is no regression against main

The registration was pushed at 19:40:45Z (`54d91e64`). The driver and both children read as registered. The error file is empty, and
nothing ran on staging. The log is scratch (sha256 `37aed0d68bca`). **p4 had started a four-env PlatformIO run at 19:40:33Z.** Every
sample follows a settled `gc()`, and main and the PR ran back to back at each point, so the load fell on both sides of each comparison.

**Part 0: main's builder against the PR's, at all 13 (MiB):**

| point | the answer, the same for both | main's largest | the PR's largest | PR minus main: cell / tile / tiles … ladder |
|---|---|---|---|---|
| KBOS | `ce5b1ea7589e` | 78.45, cell (82.3 MB) | 56.67, ladder (59.4 MB) | −31.21 / −5.59 / ≤ 0.01 |
| KJFK | #620's ladder | 95.04, cell (99.7 MB) | 58.54, ladder (61.4 MB) | −39.77 / −5.21 / ≤ 0.00 |
| KLGA | #620's ladder | 99.54, cell (104.4 MB) | 60.03, ladder (62.9 MB) | −44.33 / −5.21 / ≤ 0.03 |
| KEWR | #620's ladder | 92.93, cell (97.4 MB) | 60.81, ladder (63.8 MB) | −40.44 / −5.83 / ≤ −0.02 |
| Midtown | #620's ladder | 87.93, cell (92.2 MB) | 58.11, ladder (60.9 MB) | −37.34 / −5.23 / ≤ 0.02 |
| KMIA | `000dde33cc42` | 42.23, tile (44.3 MB) | 39.40, ladder (41.3 MB) | −8.45 / −5.09 / ≤ 0.01 |
| KSFO | `399cb059f84a` | 42.57, tile (44.6 MB) | 39.23, ladder (41.1 MB) | −9.81 / −5.66 / ≤ 0.01 |
| KSEA | `69ff5b17e2ee` | 54.06, tile (56.7 MB) | 49.20, ladder (51.6 MB) | −14.90 / −7.14 / ≤ 0.01 |
| **KORD** | `b54fa953fef7` | 54.98, cell (57.6 MB) | 45.86, ladder (48.1 MB) | −25.42 / −2.44 / ≤ 0.01 |
| KDTW | `8976db34bf1a` | 47.59, tile (49.9 MB) | 47.19, ladder (49.5 MB) | −17.86 / −3.10 / ≤ 0.06 |
| KMSP | `863f7f2955a7` | 52.93, cell (55.5 MB) | 47.28, ladder (49.6 MB) | −23.36 / −3.61 / ≤ 0.01 |
| **KIND** | `f740fbc3f99e` | 66.56, cell (69.8 MB) | 50.86, ladder (53.3 MB) | −32.86 / −3.25 / ≤ 0.00 |
| KJAC | `b3f428e5860e` | 41.38, tile (43.4 MB) | 38.89, ladder (40.8 MB) | −10.94 / −4.73 / ≤ 0.01 |

- **As predicted at all 13:** from the tiles on, the PR is within 0.06 MiB of main at every point and every phase. In the cell loop it is
  8.45 to 44.33 MiB lower, and at the first tile 2.44 to 7.14 lower.
- **The PR's largest build is lower than main's at all 13 points,** by 0.40 MiB (KDTW) to 39.51 MiB (KLGA).
- **So KORD's and KIND's 6 to 7 MiB is not the PR's.** Main reads it too. It is where Q-DR3's scratch copy reads lower than both.

**Part 1: where the copy and the PR part, sample by sample (PR minus copy, MiB):**

| KORD | KIND |
|---|---|
| cells 1-23 within 0.06; **cell 24, the last: −3.43**; then **postcell +5.90**, refs +5.90, tilelist +5.90, tile 1 +5.97 | cells 1-20 within 0.04; **cell 21: −4.31; cell 22, the last: +2.08**; then postcell +6.86, refs +6.87, tilelist +6.86, tile 1 +6.87 |

- **The step is made at the end of the cell loop:** across the last one or two cells and the read generator finishing, before any of
  the code after the loop runs.
- At that moment the PR first drops below the copy, then rises above it by 6 to 7 MiB, and stays there. Whatever the copy lets go of
  at the loop's end, or doesn't make, the PR and main both keep.
- **By the registered table:** one step over 3 MiB at one sample at each point (KORD postcell, KIND cell 22), so the window is named. It
  is the end of the cell loop. This read does not name the object.

**What it means for Q-W4 under B:** the shipped builder's figures are part 0's PR column. They are lower than main's everywhere and
within 0.06 MiB of main's from the tiles on. B's largest (New York's 162 km tries) came from the copy, which matches the shipped builder
in the ladder phase at New York. At KORD and KIND the copy's B figures are about 6 to 7 MiB low, still an estimate. Q-W4 measures on the
shipped builder.
