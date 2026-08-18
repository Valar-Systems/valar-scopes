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
