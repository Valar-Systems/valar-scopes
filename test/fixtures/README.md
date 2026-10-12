# Fixtures

## `game-config.json` / `game_config_fixture.h`

**Generated. Do not edit either file by hand — regenerate.**

They are a snapshot of the Missileer game server's published config, plus a
table of deviation figures produced by **that server's own formatter**. The
device's host suite ([`../host/test_game_format.cpp`](../host/test_game_format.cpp))
grades `src/game/GameFormat.cpp` against them.

### Why the strings are not typed

Blipscope must render a sortie's deviation as the same string the leaderboard
renders — `0.4`, not `+0.34 s`. That is a contract between two repositories, and
this project's standing rule is that a check takes its input from the *other*
side of the contract. A table somebody transcribed from reading the server would
agree with itself forever, and would go stale the first time the server moved.

So `emit-device-fixture.ts` fetches the deployed `/config` over the wire and
calls `formatSortieDeviation` for every row. Nothing in the table is anyone's
reading of anything.

It also **asserts coherence before it emits**: the fetched config and the server
checkout producing the strings must agree, or it refuses to write. A fixture
assembled from two disagreeing sources is worse than no fixture — every test
against it passes, against a state that never existed.

### Regenerating

```sh
cd ../valar-eam-feed
npx tsx scripts/emit-device-fixture.ts \
  --out    ../Blipscope/test/fixtures/game-config.json \
  --header ../Blipscope/test/fixtures/game_config_fixture.h
```

Then run `bash test/host/run.sh`. If the suite now fails, the server changed
something the device renders and that is the finding — not a fixture to force
green.

### The two halves, and why they are separate

| | what it does | where |
|---|---|---|
| **snapshot test** | *does the device still implement what we recorded?* | `test/host/run.sh`, offline, every commit |
| **drift alarm** | *is what we recorded still true?* | `device-fixture-drift.yml` in valar-eam-feed, scheduled |

The network check is deliberately **not** in the host suite. A suite that
sometimes fails for a reason nobody in the room controls becomes a retry, then a
skip, then nothing — and the end state is invisible, because a skipped test and
a passing test read the same at a glance.

### What is deliberately absent

The `.json` carries the scoring outputs (`buckets`, `shack`, `miss_m`) as an
archive. **The generated `.h` does not**, and must not: rail 3 — the device does
not invent scoring, and it does not carry the curve's answers either. The header
holds constants and expected strings, nothing the firmware could start
computing.

### Provenance

Every regeneration stamps `source_url`, `fetched_at`, `config_epoch`, and
`server_commit`. `server_commit` is currently **NULL** with its reason recorded:
the deployed `/status` carries no commit field yet. That is the honest value —
not a blank, and not a guess.

## `derivation_fixture.h` / `derivation.json`

**Generated. Do not edit either file by hand — regenerate.**

Both are emitted together, **here**, by
[`../../scripts/emit-derivation-fixture.ts`](../../scripts/emit-derivation-fixture.ts).
Ruled 2026-10-12: the generator and the JSON belong in valar-scopes beside the
header, so this repo is the single source of the vectors. The script and the
JSON were moved from valar-eam-feed (PR #187).

The header holds the server's own output for the class and T derivation. The
script produces it by calling valar-eam-feed's `src/game/derive.ts` (spec:
valar-eam-feed `docs/game-derivation.md`) from a checkout of that repo. That
is the function POST /votes re-derives with, and the server refuses any vote
whose class or T disagrees (`derivation_mismatch`).
[`../host/test_derive.cpp`](../host/test_derive.cpp) grades
`src/game/Derive.cpp` against it, with whole-value equality on every field.
The JSON carries the same cases, each with its `covers` reason and the
server's whole `Derivation`, for a server test suite to read.

Its cases were chosen at the edges where a port can disagree:

- a class draw exactly on a cumulative boundary, which falls into the next bucket;
- T at minS and at maxS−1;
- the minute ceiling, both on a boundary and 0.5 s / 59.5 s past one;
- a non-ASCII msg_id.

valar-eam-feed's test suite asserts that coverage by property
(`test/game-derivation.test.ts`).

The parameters are the server checkout's defaults, and they're emitted into
the header beside the cases. On the device they come from `/config` at runtime.

[`../host/test_game_agreement.cpp`](../host/test_game_agreement.cpp) reads the
same cases for what leaves the device: the POST /votes body built from each
derivation has to carry the server's class name (`class_wire`) and its
rendering of T. The server's suite commits the same cases with the same class
and T (valar-eam-feed `test/game-derivation.test.ts`). That is enrollment-open
checklist item 5, at the wire.

### The execution window section (`kWindowCases`)

The same header carries the shared execution-window vectors (ruled 2026-10-11,
"ResolveTurn -> T ± 1 s"): T itself, 0.5 s early, each edge, 1 ms outside each
edge, and 1.5 s late. The JSON's `window` section carries the same cases. Both
edges count, because the design doc's §4 ruling 1 says `|dev| <= 1.0 s`.

**These have a different source of truth from the derivation cases.** The
verdicts are the RULING's, written down in the emitter (`WINDOW_VECTORS`).
Neither implementation produced them. `test_game_agreement.cpp` grades
`DrillMachine::ResolveTurn` and `DeviationMsForServer` against them, and
valar-eam-feed's votes tests grade `inExecutionWindow` and `executeVote`
against the same cases. Every instant is a whole millisecond. Which side
decides a key a fraction of a millisecond outside an edge is an open question
(design doc "Timings", W2), so no case answers it.

### Regenerating

From the valar-scopes root, with a valar-eam-feed checkout beside it:

```sh
npx --yes tsx scripts/emit-derivation-fixture.ts --server ../valar-eam-feed \
  --out test/fixtures/derivation.json --header test/fixtures/derivation_fixture.h
# drift check (exit 1 on drift, 2 if the script itself could not run):
npx --yes tsx scripts/emit-derivation-fixture.ts --check --server ../valar-eam-feed \
  --out test/fixtures/derivation.json --header test/fixtures/derivation_fixture.h
```

- `--server` defaults to `../valar-eam-feed`. The script imports only
  `src/game/config.ts` and `src/game/derive.ts` from it, and they need nothing
  but `node:crypto`, so that checkout needs no `npm install`.
- It prints the server checkout's commit, and says when `src/game` there has
  uncommitted changes. The commit is not written into either file, so `--check`
  does not drift just because the server's HEAD moved.
- **A window verdict changes here**: edit `WINDOW_VECTORS` in the script, then
  regenerate. **A derivation change starts on the server**: a changed
  `derive.ts` shows up here as DRIFT, and the port is re-graded against the
  regenerated header.
