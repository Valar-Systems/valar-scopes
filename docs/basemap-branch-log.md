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

### Ruled on the refusal (reviewer chat, pasted by Daniel, 2026-11-05 ~00:15Z)

> 1. S-RORDER alone read {MR2}: the first pass's BLIND is (a), the runtime flake. The second basemap pass, entirely as
>    predicted (61 runs, 0 runtime failures, 58 sabotages at their sets, C-LOADFAIL BLIND): accepted.
> 2. The smoke driver's refusal: your re-reading is accepted as frozen, steps 1-3 as written (TIME_WAIT under 2,000,
>    polled, capped at 20 min; then the smoke driver once; the credits driver once after it reads as predicted; the
>    raw-output change only if the rig refuses again, registered first with its own check).
> 3. Whatever step 1 shows, the raw-output change goes on the A2.12 list as a follow-up after #534: a driver that
>    throws away the output of a run it can't parse leaves a refusal undiagnosable.
> 4. A2.12: noted. TIME_WAIT at 15,721 against 16,384 ephemeral ports explains both under-gate failures better than
>    chance; the gate needs to bound the whole run, not only its start.
> 5. CI for the code push stays at 41 (38/3/0); push only after both drivers read as predicted.

### The re-reading, steps 1-2 (00:10-00:18Z): both drivers as predicted

**Step 1.** TIME_WAIT read 366 at 00:10:56Z, then `sabotage-smoke-basemap.mjs` ran once, exit 0:

    CONTROL (unsabotaged): 891 passed, 1 skipped, of 892; 0 failed (TIME_WAIT 366)
    S-SQ: as registered; failed 2 (want 2): ["SQ1","SQ2"]; restored byte-identical
    S-SQP: as registered; failed 1 (want 1): ["SQ-C"]; restored byte-identical
    ALL AS REGISTERED

**Step 2.** The credits driver runs only on an exit of 0 **and** that exact CONTROL line. TIME_WAIT read 743 at
00:18:05Z, then `sabotage-credits.mjs` ran once, exit 0:
- **CONTROL:** 35 passed of 35.
- **S-CV-BLANKET:** red {CV1, CV2, SA1, QC1, F16, P68}, with counts CV1 104 of 176, CV2 8 of 11 and SA1 48.
- **S-SA:** {SA1, QC1}, with SA1 0.
- **S-RD:** {RD1, QC1, P89}, with RD1 standing-data 0 and adsbdb 2.
- **S-QC1:** {QC1}.
- ALL AS REGISTERED.

The tree was clean after.

**Step 3 was not needed.** The raw-output change goes on A2.12's list as a follow-up after #534 (ruling 3).

**For A2.12:** the smoke driver's three whole-suite runs **took TIME_WAIT from 366 to 12,792**. One driver's run alone
nearly fills the pool, which is why the gate has to bound the whole run.

**Every driver has now read as predicted, so the code is pushed**, with CI at 41: 38 success, 3 skipped, 0 failed.

## After the merge: #534 on staging, and MM4 in five groups (2026-11-05): MM4 OFF, cause (a), in every group

### The rulings, verbatim (reviewer chat, pasted by Daniel)

From ~00:28Z, the deploy's conditions (the merge approval itself is recorded on #534):

> 2. The staging deploy comes after blipscope-5c's #536 deploy (staging, then production, after the UK data session's R3
>    report). One deployer at a time: start only when Daniel pastes blipscope-5c's "#536 live" line. Q-MK2 is amended
>    because #536 was ruled to deploy first and be carried:
>    - Carried beyond f740f10: 8b42f5c (#530), 43afaac (#531), c039a5e (#532), 7fe8f8c (#533), 9eb8dde (#536), and your
>      merge commit. Beyond staging's 9eb8dde: yours only.
>    - The Worker-input diff from f740f10 to your merge commit is exactly four files: proxy/src/sitefeed/bend.ts (#536);
>      proxy/src/basemap/handler.ts, proxy/src/index.ts and proxy/src/metrics.ts (#534). Add proxy/package-lock.json to
>      the paths you diff, because it sets the bundled dependency versions (it hasn't changed since f740f10). Anything
>      else: stop and ask.
>    - Before you deploy, staging's and production's /healthz must both show 9eb8dde.
>    - The rollback target is no longer cc4dcf89, because that would take #536 off staging too. The new target is the
>      staging version that blipscope-5c's deploy creates. Read it from wrangler deployments list just before you
>      deploy, check that it matches blipscope-5c's line, and record it.
>    - Staging's /healthz must report your merge commit before anything is read.
> 3. MM4's five groups, as ruled in Q-MK1:
>    - G1-G5, each with B, R and I, the groups more than 60 s apart;
>    - B's unknown-store anchor in every group;
>    - R graded by your table, and the split reported as the rate.
>    They touch only staging's /basemap, so they may run during #536's two-hour production read (T5).

From ~00:55Z, the start line:

> #536 live: production /healthz 9eb8dde since 00:36:09Z (version c8f33459-0277-4c96-a0dd-1394bd7a0ac5); staging
> 9eb8dde (version b28687a5-df01-423c-a481-3402778a24f3).
> - Map server: this is your start line. Your other condition still applies: main's CI on 20a0323 must have finished
>   with 0 failed and the same 41 names as 9eb8dde's run. Staging's rollback target is b28687a5.

From ~02:45Z, on the readings below:

> - MM4 is OFF, cause (a) named. All 5 Rs and all 5 Is landed on isolates that didn't know the store id,
>   same-data-centre repeats included. B's anchor held in every group, so the instrument saw the label, and the labels
>   and builder counts agree minute by minute. The rate, reported: hit 0, known-miss 0, unknown-store 5.
> - Staging stays on 3b12b9ea. The rollback target stays b28687a5.
> - For D4: at this traffic a fresh isolate is the normal case. A repeat and an If-None-Match revalidation must be
>   served without a build, even on an isolate that has never seen the store id. D4's plan says how, predictions
>   first. MM2's build wall (8.9-14.5 s here) stays a prerequisite beside it.

### Before the deploy (00:46-00:47Z)

**Main's CI:**
- **On 20a0323**, from the finished run (00:45:12Z): **41 check runs: 39 success, 2 skipped (`promote`, `version`), 0
  failed.**
- **Main's finished run on 9eb8dde** (00:36:50Z) is also 41 check runs: 39 success and 2 skipped. The two runs have the
  same 41 names and the same conclusion per name.

**Main** was still `20a0323`, and the checkout was a clean, detached `20a0323`.
- **Carried beyond `f740f10`:** `8b42f5c`, `43afaac`, `c039a5e`, `7fe8f8c`, `9eb8dde` and `20a0323`.
- **The Worker-input diff,** `f740f10..20a0323`, over `proxy/src`, `wrangler.toml`, `pages`, `fonts`, `package.json`
  and `package-lock.json`, is exactly:
  - `proxy/src/basemap/handler.ts`;
  - `proxy/src/index.ts`;
  - `proxy/src/metrics.ts`;
  - `proxy/src/sitefeed/bend.ts`.

  `package-lock.json` is unchanged.

**`/healthz`:** staging read `9eb8dde` and production read `9eb8dde`.

**The rollback target, recorded: `b28687a5-df01-423c-a481-3402778a24f3`.** It is staging's newest deployment
(`wrangler deployments list --env staging`, created 00:35:36Z), and it matches blipscope-5c's line.

**The credential check:** `CLOUDFLARE_API_TOKEN` read absent both in the shell and at user level (a presence check that
prints a boolean), so wrangler used the OAuth login.

### The deploy (00:47:01-00:47:10Z)

- `./scripts/deploy.sh staging`, from `proxy/` at `20a0323`, exit 0.
- **Staging version `3b12b9ea-2de7-4750-8044-737c112fc598`.**
- `deploy.sh`'s confirmation, and a separate read at 00:47:16Z, both show staging's `/healthz` at **`20a0323`**. That
  was read before any MM4 request.

### The five groups (00:47:22-00:56:15Z)

**How the requests were made:**
- As the test identity, at KDEN, with Weatherscope's query.
- R about 10 s after B's response.
- I, with `If-None-Match` set to B's ETag, about 20 s after it.
- Each group's B 75 s after the previous group's last response (the ruling asks for more than 60 s).
- Every request is in the requester's log (`mm4-groups.sh`).

**What each column comes from:**
- The **answer** time, data centre, `bm` and proxy CPU come from staging's `/basemap` request line and its invocation
  event.
- **"ETag = B's"** compares the response's ETag with its group's B, in memory: a boolean, so no ETag is printed here.

| request | sent | answered | data centre | status | `bm` | ETag = B's | proxy CPU |
|---|---|---|---|---|---|---|---|
| G1 B | 00:47:22Z | 00:47:37Z | SEA | 200, 214,179 B, BMPK | `unknown-store` | (itself) | 14 ms |
| G1 R | 00:47:48Z (+11 s) | 00:47:59Z | SEA | 200, 214,179 B, BMPK | **`unknown-store`** | yes | 20 ms |
| G1 I | 00:48:00Z (+23 s) | 00:48:13Z | SJC | 304 | `unknown-store` | yes | 28 ms |
| G2 B | 00:49:28Z | 00:49:37Z | SJC | 200, 214,179 B, BMPK | `unknown-store` | (itself) | 18 ms |
| G2 R | 00:49:47Z (+10 s) | 00:49:58Z | SEA | 200, 214,179 B, BMPK | **`unknown-store`** | yes | 18 ms |
| G2 I | 00:49:58Z (+21 s) | 00:50:12Z | SEA | 304 | `unknown-store` | yes | 13 ms |
| G3 B | 00:51:27Z | 00:51:40Z | SJC | 200, 214,179 B, BMPK | `unknown-store` | (itself) | 10 ms |
| G3 R | 00:51:50Z (+10 s) | 00:52:02Z | SEA | 200, 214,179 B, BMPK | **`unknown-store`** | yes | 16 ms |
| G3 I | 00:52:02Z (+22 s) | 00:52:14Z | SEA | 304 | `unknown-store` | yes | 13 ms |
| G4 B | 00:53:29Z | 00:53:44Z | SJC | 200, 214,179 B, BMPK | `unknown-store` | (itself) | 17 ms |
| G4 R | 00:53:54Z (+10 s) | 00:54:07Z | SEA | 200, 214,179 B, BMPK | **`unknown-store`** | yes | 12 ms |
| G4 I | 00:54:07Z (+23 s) | 00:54:18Z | SEA | 304 | `unknown-store` | yes | 18 ms |
| G5 B | 00:55:34Z | 00:55:45Z | SEA | 200, 214,179 B, BMPK | `unknown-store` | (itself) | 17 ms |
| G5 R | 00:55:55Z (+10 s) | 00:56:05Z | SEA | 200, 214,179 B, BMPK | **`unknown-store`** | yes | 14 ms |
| G5 I | 00:56:05Z (+20 s) | 00:56:15Z | SEA | 304 | `unknown-store` | yes | 22 ms |

**The labels were read twice:**
- 00:56:28Z: 14 lines, while G5's I was not yet in.
- **00:58:25Z: 15 lines, 15 of them with `bm`.**

The earlier minutes read identically both times.

**The builder count** (GraphQL, by the minute each request was sent; read twice, the same both times):

| minute | 00:47 | 00:48 | 00:49 | 00:51 | 00:52 | 00:53 | 00:54 | 00:55 | 00:56 |
|---|---|---|---|---|---|---|---|---|---|
| builder invocations, all `success` | 2 | 1 | 3 | 2 | 1 | 2 | 1 | 2 | 1 |
| the requests sent in it | G1 B, R | G1 I | G2 B, R, I | G3 B, R | G3 I | G4 B, R | G4 I | G5 B, R | G5 I |

The totals: **15 invocations, 0 errors**. Builder CPU ran 1.38-3.98 s, and wall 8.9-14.5 s.

### Graded by the registered table

- **B's anchor control held in every group.** All five Bs carry `unknown-store`, so the instrument sees the label.
- **R: OFF in all five, cause (a).** Every R landed on an isolate that did not know the store id, 10-11 s after its
  group's build. Every R's ETag equals its B's.
- **I: the registered alternative in all five.** Each I landed on an isolate that did not know the store id, so it is
  `unknown-store`, with one more builder invocation and a post-build 304. It is reported as such. Its proxy CPU was
  13-28 ms, so the ≤ 5 band (the `304-known` path) does not apply.
- **The labels and the builder count agree, minute by minute:** 15 labels of `unknown-store` against 15 invocations. No
  row falls in "stop and decide", and nothing falls outside the table.

**The rate, reported, not graded:**
- **R's split over the five groups: `hit` 0, `known-miss` 0, `unknown-store` 5.**
- Counting I as well, **10 of 10 follow-up requests sent 10-23 s after a build reached an isolate that did not know the
  store id.**

**Read beside it, not graded:**
- The requests were answered in two data centres: SEA (10) and SJC (5).
- **Same-data-centre repeats were `unknown-store` too:**
  - G1's B and R, both in SEA;
  - the R-to-I pairs of G2, G3 and G4, in SEA;
  - all three of G5, in SEA.

  So the cause is not data-centre routing alone.

**Staging:** every answer was correct (200 ×10, 304 ×5), with no 5xx and no `builder_invalid`. It stays on `3b12b9ea`,
and the rollback target stays `b28687a5`.

### The scripts as they ran, and the ruling's three points

**The scripts.** They were run from the scratchpad and are committed byte for byte in
[basemap-branch-log/instruments/](basemap-branch-log/instruments/). All five were last modified before the run began
(00:29Z, and 21:24Z / 21:55Z the day before):

| file | role | sha256 |
|---|---|---|
| `mm4-request.sh` | one request, as the test identity | `1b6cfcad93943e7dd04b808cd3d51e76f1d18b25ddc7fe6772b44d8065008576` |
| `mm4-groups.sh` | the five groups and their timing | `247e5c2aef9ed0676f14fb3a1f4e56c5fef33808b54be902cfc22f7724fa5bed` |
| `mm4-lines.mts` | the reader: `/basemap` request lines with `bm` | `1df4cc8b454acda434d2408b76af0ab4256edb14b50083576ef050364ba65c17` |
| `d3-graphql.mts` | the builder's invocations per minute | `8df5f5a11ef32e3b80f7ec29e8f0e38638f816a89a7a3dd31d8fa4c4862f1201` |
| `d3-cpu.mts` | the proxy's invocation events: CPU, wall, outcome | `f0ed5ebf54105aab256b5b3acc10e54e7ab0eafd3decb72173ad88d98a8f1147` |

Their paths are the scratchpad's (`./wt-main/proxy/scripts/...`), so they run from a scratch directory that holds a
checkout as `wt-main`, not from here.

**1. Authentication: holds.** It is the same path `smoke-prod.sh` uses:
- `mm4-request.sh` sources `proxy/scripts/test-identity.sh`. `test_identity_or_reexec` re-runs it through
  `with-test-identity.mts`.
- That wrapper resolves the test identity from the token file (`test-device` + `test-key-staging`) and passes it to
  the child **in the environment only**, with `BLIP_KEY`/`BLIP_DEVICE` removed.
- `blip_auth_curl` builds the two header lines with the `printf` builtin and sends them to `curl -K -` on stdin.
- **No key or id value is on any command line.** The only arguments are the label and, for I, B's ETag.

**Read from the run itself:**
- the run's stderr holds **exactly 15** lines `with-test-identity: the test identity for staging, from
  BLIPSCOPE_TOKEN_FILE (test-device + test-key-staging)`, one per request, plus 15 token-file permission notes, and
  nothing else;
- in the run's output, the only run of 16 or more hex characters is the ETag's 24-hex query digest, 15 times, each in
  `bm1-1-4c0875738f38-…`.

**2. No coordinate finer than 2 dp in either output: holds.**
- **The requester's output:** 0 numbers with three or more decimals.
- **The readers' output:** 17, and **all 17 are ISO timestamps** (milliseconds).
- **CONTROL:** the same pattern finds the 2 four-decimal numbers in `mm4-request.sh`'s own query, so it can see one.
- **That query** is KDEN's published airport coordinate (39.8617, -104.6731), the one D3 recorded above. It is
  **in the script's source and in the request**, never in its output. The Worker quantises it to 2 dp before any key
  or digest.

**3. The scripts are in this PR:** both of the ruling's (`mm4-groups.sh` and the reader `mm4-lines.mts`), plus the
request script `mm4-groups.sh` calls and the two readers whose figures appear in the tables.
