# The branch a /basemap request took, on the proxy's log line (a D4 prerequisite: MM4)

**Status: predictions, frozen and pushed before any code.** This is the first of D4's prerequisites, as ruled when D3
closed (`docs/basemap-deploy.md`, "D3: closed").

## The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-04 ~22:30Z)

> 3. Q-D3b: MM4 stays off for D3, the miss kept, and moves to D4's prerequisites beside MM2:
>    - a PR, predictions first, that puts the branch taken on the proxy's /basemap log line (304-known, hit,
>      known-miss, unknown-store): no device id and no query, with a test per branch and a sabotage per label;
>    - under it, MM4 read again on staging, inside the TTL;
>    - D4's plan states how a request that lands on a fresh isolate is served. Today it can't hit the cache without the
>      store id, so a device that lands on a fresh isolate pays a full build (8-19 s here). That and MM2's wall time
>      are the two things D4 has to answer.

## Why: what D3 could not tell apart

D3's MM4 re-read, inside the TTL (`docs/basemap-deploy.md`, 21:57Z):
- B built.
- R, the same request 10 s after B's response, **reached the builder**.
- I, with `If-None-Match`, was a 304 at 0 ms.

The handler (`proxy/src/basemap/handler.ts:53-68`) skips the builder for a plain repeat only when **both** of these hold:
- the isolate knows the store id (`knownStore`, within `STORE_TTL_MS`);
- **and** `caches.default` holds the copy.

So R's builder call had two possible causes:
- **(a) another isolate:** R landed on one that did not know the store id;
- **(b) B's isolate, with no cached copy:** it knew the store id, and the Cache API had nothing.

No log line or invocation event carried an isolate id or the result of `cache.match`, so nothing read could separate
them. The label separates exactly these two: (a) is `unknown-store` and (b) is `known-miss`.

## The change (planned; none of it is written yet)

**`proxy/src/basemap/handler.ts`:**
- `export type BasemapBranch = "304-known" | "hit" | "known-miss" | "unknown-store"`.
- `handleBasemap` gains a fourth parameter, `log: { bm?: BasemapBranch } = {}`. It sets `log.bm` at each of the four
  branches, at the point the branch is decided.
- `503 not_configured` and `400` return before any branch, and set nothing.
- The header comment's "Nothing here logs" is corrected: the handler still writes no line of its own; the label rides on
  the proxy's existing per-request line.

**`proxy/src/metrics.ts`:** `RequestMetric` gains `bm?: BasemapBranch`.
- `record()` already spreads the metric into the `evt:"req"` log line, so the label appears there.
- The Analytics Engine point is **unchanged**: the same six blobs, four doubles and one index. Those are positional, and
  three months of retained points depend on their order (`metrics.ts`, the blob-layout comment).

**`proxy/src/index.ts`:** passes `meta` as the fourth argument.

**What the label can carry:** one of four fixed strings, typed so that nothing else can be assigned. There is no device
id, no query and no coordinate in it. (The line's existing fields are unchanged.)

| `bm` | when | the builder |
|---|---|---|
| `304-known` | the isolate knows the store id, and `If-None-Match` equals the ETag: a 304 | not called |
| `hit` | it knows the store id, and `caches.default` holds the copy: a 200 | not called |
| `known-miss` | it knows the store id, and the cache has no copy | called once |
| `unknown-store` | it does not know the store id: a fresh isolate, or past `STORE_TTL_MS` | called once |
| *(absent)* | `503 not_configured` or `400`, which return before any branch; a `401` never reaches the handler | not called |

The line's `status` still says how a builder call ended: a 200, a post-build 304, or a 404, 422, 500 or 503.

## The rows, frozen

All seven rows are in `proxy/test/basemap-endpoint.test.ts`, in a new `describe` placed **before** MP, so MP1-MP3 run over
them as well. Each row reads the `{"evt":"req",...}` line of the one request it is named for. That is exactly one line
per request: a different count is an instrument failure, not a pass. Setup requests are checked for their status only,
never their label.

| row | the request | predicted |
|---|---|---|
| **MK1** | a fresh isolate (`resetBasemapIsolate`), MW's query | 200; **`bm` `unknown-store`**; 1 builder call |
| **MK2** | MW9's build, then the same query with `If-None-Match` set to its ETag | 304; **`304-known`**; no further builder call |
| **MK3** | MW9's build, then the same query again | 200; **`hit`**; no further builder call |
| **MK4** | MW9's build, then MW's query with `r=98`: a different query (`rKm` is `round(r)`), so a different ETag and no cached copy | 200; **`known-miss`**; 1 further builder call |
| **MK5** | a 401 (no key), a 400 (`lat=91`) and a 503 (no binding) | each status as named; each has its line (CONTROL); **none carries `bm`** |
| **MK6** | every `/basemap` request line the file has logged by then (the MW rows and MK1-MK5) | every `bm` is one of the four, or absent; **each of the four appears at least once** (the CONTROL that the scan saw each label) |
| **MK7** | a fresh-isolate request through a `METRICS` spy | the dataset point has **exactly six blobs, none of them a label**. CONTROL: the same request's line carries one of the four |

**Counts.** Main's whole suite at `c039a5e`, read at ~22:40Z, is 67 files passed and 1 skipped (68), and **884 tests
passed and 1 skipped (885)**. Predicted after the change:
- **891 passed, 1 skipped (892)**, in the same 68 files. That is +7, all in one existing file.
- Every MW and MP row still passes.

**The search for text this change removes or adds, run before predicting:**
- No test or script reads the `evt:"req"` line, or any of the four labels.
- No test reads `handler.ts`'s source.
- `handleBasemap` has one caller, `index.ts:399`.
- The tests that spy on `console.log` (`leaderboard`, `provision`, `route-reversal`, `upstream-instruments`) never
  request `/basemap`.

## The sabotages, frozen

They are added to `proxy/scripts/sabotage-basemap.mjs`, set `all`. That driver grades by case id, and its `caseOf`
pattern (`^M[A-Z]…`) is why the rows are named `MK`: a `BL1` would grade as `UNNAMED`. `MK` is unused in the tree.

| sabotage | the plant | predicted failing set |
|---|---|---|
| **S-MK304** | the 304-known branch labels itself `hit` | **{MK2, MK6}** |
| **S-MKHIT** | the hit branch labels itself `304-known` | **{MK3, MK6}** |
| **S-MKKMISS** | the known-miss branch labels itself `unknown-store` | **{MK4, MK6}** |
| **S-MKUNK** | the unknown-store branch labels itself `known-miss` | **{MK1, MK6}** |
| **S-MKPRE** | a label set before the binding check and the parse | **{MK5}** |
| **S-MKAE** | `metrics.ts`: the label appended to the dataset point's blobs | **{MK7}** |

**Why each set is what it is.**
- Each label plant is a *mislabel*: another valid label. MK6's set-membership check therefore stays green; only the row
  for that branch can catch the plant, plus MK6's "each label seen" control. Every request through the branch is
  mislabelled, including MW10's (`304-known`) and MW11's (`hit`), so the label vanishes from the file.
- MK7's control accepts any of the four labels, so S-MKUNK leaves it green.
- MK1-MK4 never read a setup request's label.
- Under S-MKPRE, the four branches overwrite the early label, so MK1-MK4 and MK6 stay green.

**An existing sabotage, re-registered: S-W304LATE.** It deletes the 304 comparison.
- **Its anchor** is the line this change edits, so the anchor is updated to the labelled line.
- **Its frozen set moves from {MW10} to {MK2, MK6, MW10}.** With no comparison, MK2's request becomes a `hit` 200, and
  `304-known` is never logged.

The 1 is kept on record as the set A1.7 froze.

**Every other sabotage in the driver: unchanged.**
- **Why MK1-MK4 and MK7 hold.** They share MW9's success path. No other frozen set holds MW9, MW10, MW11 or MW16,
  except S-PETAG.
- **S-PETAG:** its MW9 red is the ETag's shape, which no MK row checks. MK2 sends back whatever ETag it was given, so it
  is still a 304.
- **S-PLOG:** adds a non-request line, which the MK rows filter out.
- **The assumption, stated:** no existing plant changes whether MW's store builds at `r=98` (MK4) while leaving `r=97`
  building. None of their sets holds MW9.

**The driver's own baselines:**
- `baseline[all]` sees **145 cases in 17 files**: main's **138 in 17**, plus MK1-MK7. Main's figure was read at
  `c039a5e` with `node scripts/sabotage-basemap.mjs S-W304LATE`, which also graded S-W304LATE {MW10} as frozen.
- `baseline[ingest]` and `baseline[mv]` are unchanged.
- **The whole driver:** every sabotage "AS PREDICTED" (C-LOADFAIL "AS PREDICTED, BLIND"), exit 0.

**The other proxy drivers:**
- **`sabotage-smoke-basemap.mjs`** runs the whole suite per sabotage: S-SQ {SQ1, SQ2} and S-SQP {SQ-C}, unchanged.
- **`sabotage-credits.mjs`:** its four sets unchanged. No file it plants or runs changes here; it is run because the
  rule is all drivers.
- **Not run, with the reason:**
  - `sabotage-basemap-mc.mjs` grades MC rows on the sealed store from a 7.2 GB ingest cache, through `tsx` scripts. No
    file it plants or reads changes here.
  - `sabotage-tistarve.mjs` is Linux-only and plants the test-identity selftest, which this change does not touch.

## CI, frozen

The same workflows as #532: `device-ids`, `firmware` and `no-traffic-captures` on any PR, plus `workers` for `proxy/**`.
- **The predictions-only push** (this doc alone) runs the docs-only set: **38 check runs: 35 success, 3 skipped
  (`promote`, `ship-alert`, `version`), 0 failed.** That is #533's set.
- **The code push** adds `manifest-drift`, `test (proxy)` and `test (dashboard)`: **41: 38 success, 3 skipped, 0
  failed.** That is #532's set.

Each is read from a finished run only.

## After the merge: MM4 read again on staging, inside the TTL (registered now; read after the staging deploy)

**The deploy.**
- Main, with this PR, to **staging only**. The commits carried beyond staging's `f740f10` are listed just before the
  deploy.
- At this writing, nothing since `f740f10` changes the proxy Worker's inputs (`proxy/src`, `wrangler.toml`, `pages`,
  `fonts`, `package.json`). So this PR is the only Worker change.
- Staging's `/healthz` must report the merge commit before anything is read.
- The rollback target is `cc4dcf89`. Production's binding stays absent.

**The requests:** as the test identity, with Weatherscope's query at KDEN, as in D3's re-read.
- **B:** builds.
- **R:** about 10 s after B's response.
- **I:** with `If-None-Match` set to B's ETag, about 20 s after B's response.

**The instrument** is the staging proxy's `/basemap` log line in Workers Logs: its `bm` field, read beside `status`,
`colo` and `ms`.
- **Its anchor control is B's line.** B's isolate cannot know a store id that expired long ago, so its line must carry
  `bm` `unknown-store`.
- **If B's line carries no `bm`, the instrument cannot see the label:** BLIND, stop.
- The builder count per minute comes from GraphQL, and the proxy's CPU from its invocation events, as in D3.

| request | predicted |
|---|---|
| **B** | 200, BMPK; `bm` **`unknown-store`**; 1 builder invocation |
| **I** | 304, `cpuTimeMs` ≤ 5; **`304-known`** with 0 builder invocations. Or, if I lands on a fresh isolate, `unknown-store` with 1 more invocation (a post-build 304): reported as such |
| **R** | the reading this PR exists for, in the table below |

| R's `bm` | means | MM4 |
|---|---|---|
| `hit` | R found the store id and the copy | as registered, if `cpuTimeMs` ≤ 10 and R adds 0 builder invocations |
| `known-miss` | **cause (b):** R's isolate knew the store id, and the Cache API had no copy | **off**, the cause named |
| `unknown-store` | **cause (a):** R landed on an isolate that did not know the store id | **off**, the cause named |
| `304-known` | impossible: R sends no `If-None-Match` | instrument failure: stop |
| absent, or any other value | the label did not reach the line | instrument failure: stop |
| a label that contradicts the builder count (for example `hit` beside a builder invocation in R's minute) | the two instruments disagree | stop and decide |
| **none of the above** | the instrument is unreliable at this scale | stop and decide |

**What one reading can and cannot say.** One group names the path **this** R took. It does not give a rate: how often a
repeat lands on another isolate is a frequency, and one sample of it is a coin flip. If the ruling wants a rate, it is N
groups, registered as N before any is sent.

**Outside the set:** stop and report.

## Ruled after the predictions, before the code push or any staging read (reviewer chat, pasted by Daniel, ~22:50Z)

> 2. #534: the predictions at 0d6af62 are accepted as frozen, S-W304LATE's re-registration included (found before any
>    run, the original set kept). Push the code only if every driver reads as predicted, as you say.
> 3. Q-MK1: read R in 5 groups, each B, R, I as registered, each group more than 60 s after the last. Each group is
>    graded by your outcome table, and B carries unknown-store in every one. The split of R's labels over the 5 is
>    reported as the rate, not graded: D4 needs how often, not just which.
> 4. Q-MK2: no need to ask the UK data session; staging isn't in R3's path. List the carried commits first. If
>    anything besides #534 has changed the proxy Worker's inputs since f740f10, stop and ask.

**The staging read, as ruled.** It replaces the single group registered above. The outcome table and the instruments
are unchanged.

- **Before the deploy:**
  - list the commits carried beyond staging's `f740f10`;
  - diff the proxy Worker's inputs (`proxy/src`, `proxy/wrangler.toml`, `proxy/pages`, `proxy/fonts`,
    `proxy/package.json`) from `f740f10` to the commit being deployed. **Anything there besides #534's own change:
    stop and ask.**
- **Five groups, G1 to G5,** each B, R and I as registered:
  - R about 10 s after B's response;
  - I, with `If-None-Match`, about 20 s after it.
- **Spacing:** each group's B is sent **more than 60 s after the previous group's last request.** By then every isolate's
  store id has expired, because an isolate learns it only from a builder response, and none is later than that last
  request.
- **Per group:**
  - **B** must carry **`unknown-store`**. That is the anchor control, in every group. A B without it is BLIND: stop.
  - **R** is graded by the outcome table above.
  - **I** is read as registered.
- **The rate:** the split of R's labels over the five (`hit` / `known-miss` / `unknown-store`) is **reported, not
  graded.**
- **The builder count:** GraphQL per minute. A group's B and R can share a minute with the next group only if the spacing
  above is broken, which is an instrument failure. Each group's minutes are read as that group's.

## The run, first pass (2026-11-04, 22:40-23:15Z): S-RORDER BLIND; stopped, code not pushed

**As predicted:**
- **Typecheck:** both configs clean.
- **The whole suite:** **891 passed, 1 skipped (892)**, 67+1 files, exit 0.
- **`sabotage-basemap.mjs`, the baselines:**
  - `baseline[ingest]`: 10 cases;
  - `baseline[mv]`: 31;
  - **`baseline[all]`: 145 cases in 17 files.**

**The first off reading** came at the driver's 16th sabotage:

    S-RORDER: BLIND (the Workers runtime failed partway (24 baseline cases not run)) [TIME_WAIT 6592 at start]

**Two explanations, and the data rules out neither:**
- **(a) the known vitest-pool-workers runtime failure** (A2.12);
- **(b) S-RORDER's plant, which reorders frame clipping, together with the new MK rows**, crashing the runtime in
  `basemap-endpoint.test.ts`. MK4 builds `r=98`, which no earlier row built. 24 is exactly that file's case count.

A static count of the other files' cases (a regex, 111 against the driver's measured 145) cannot show that no other file
also has 24.

**What was stopped, and what was not:**
- The chain was stopped, so `sabotage-smoke-basemap.mjs` and `sabotage-credits.mjs` never started.
- The basemap driver was left to finish. It holds each original file only in memory and restores it in `finally`, so a
  kill mid-plant would have left a planted file mixed into uncommitted work.

**Its remaining lines are recorded as read, not acted on.** Every one reads AS PREDICTED:
- S-W304LATE at its re-registered **{MK2, MK6, MW10}**;
- the six MK sabotages at their frozen sets;
- C-LOADFAIL BLIND.

**The driver's tally:**
- 61 runs; **runtime failures: 0 with nothing run, 1 partway**;
- highest TIME_WAIT at a run's start 7,882 (gate 8,000); held for ports 1,336 s.

**Afterwards:**
- The tree held exactly this PR's six files, with no plant text in `proxy/src`, `proxy/scripts/basemap` or
  `docs/basemap-attribution.md`.
- **A2.12's note:** a runtime failure **under** the TIME_WAIT gate means the gate alone does not keep the runtime up.

### Ruled on the stop (reviewer chat, pasted by Daniel, ~23:00Z)

> 2. #534, S-RORDER BLIND: stopping was right, and so was letting the driver finish its restores. Its later lines are
>    recorded as read, not acted on.
>    - Your re-reading is accepted as frozen: S-RORDER alone, once. {MR2} reads as (a). BLIND with 24 missing points
>      at (b). Anything else: stop.
>    - If it's BLIND again: before any further run, make the driver name the files whose cases didn't run, not just
>      the count. That's an instrument change, registered first, with its own check (a runtime failure planted in one
>      known file names exactly that file). Then re-run S-RORDER alone.
>    - A BLIND under the TIME_WAIT gate means the gate alone doesn't keep the runtime up; note it for A2.12.
>    - Then, as registered: the whole basemap driver again, plus the smoke and credits drivers, before any code push.

### S-RORDER alone, as ruled (23:22-23:23Z)

    baseline[all]: 0 failing cases (145 cases seen in 17 files) [TIME_WAIT 3168 at start]
    S-RORDER: AS PREDICTED, failing {MR2} [TIME_WAIT 6314 at start]

**{MR2} reads as (a): the runtime flake.** The driver change that names missing files is therefore not needed.

### The second pass (23:23Z-00:08Z): the basemap driver as predicted; the smoke driver's rig refused; stopped

**`sabotage-basemap.mjs`, the whole driver: AS PREDICTED throughout.**
- 61 runs, **0 runtime failures**. The highest TIME_WAIT at a run's start was 7,909; held for ports 1,492 s.
- Baselines: 10, 31 and **145 in 17 files**.
- **All 58 sabotages as frozen:**
  - S-W304LATE {MK2, MK6, MW10};
  - S-MK304 {MK2, MK6}; S-MKHIT {MK3, MK6}; S-MKKMISS {MK4, MK6}; S-MKUNK {MK1, MK6};
  - S-MKPRE {MK5}; S-MKAE {MK7};
  - C-LOADFAIL BLIND.

**`sabotage-smoke-basemap.mjs`: its rig refused before any sabotage**, exit 2:

    RIG: the unsabotaged suite must pass first (failed ?, parsed false, timed out false)

- **What the line says:** the unsabotaged whole-suite run produced no parseable `Tests` summary, and did not time out.
- **The output was not kept.** The driver discards the raw output.
- **The consequences:** S-SQ and S-SQP were not read. The `&&` chain did not start `sabotage-credits.mjs`. The tree was
  clean after.

**Two explanations, and the data rules out neither:**
- **(a) Port exhaustion during the run.**
  - The driver's gate checks for TIME_WAIT under 8,000 only **at the start**. It started straight after the basemap
    driver, whose last runs began at up to 7,909.
  - A whole-suite run (68 files) adds thousands more connections. **TIME_WAIT read 15,721 at 00:08:00Z, just after**,
    against Windows' 16,384 ephemeral ports.
- **(b) A real failure** that produced no summary. The same suite at this tree passed whole at ~22:40Z (891 / 1), but
  without the discarded output this is not excluded.
