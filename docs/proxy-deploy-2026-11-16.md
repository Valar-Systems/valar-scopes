# The proxy deploy of 2026-11-16: plan and predictions, before the deploy

**The owner's go (2026-11-14 03:32Z, relayed verbatim):** "Proxy deploy: go, on Monday Nov 16, not before. Keep it out
of S1, S2, S4 and my Sunday sitting (they use the proxy), and not on the same day as #19's merge."

**The slot** (the reviewer, 2026-11-14): **Monday 2026-11-16, 16:00-17:00Z.** It is clear of the other 11-16 windows
that use the proxy: map-client's S2b (from 00:00Z) and map-server's FT-HR (04:00-07:30Z). The uk-data session deploys,
under its own deploy claim, and runs the checks below.

## 1. What the deploy carries, read from git at plan time

Production's `/healthz` reports `aa40aa43` (read 2026-11-14 03:34:50Z), and staging's reports `b7184243`. Main was
`5b8ee36d6ce9` at plan time (read with `git ls-remote`, 03:34:50Z), and `77acc072` (#721) when the table was last
updated (`git ls-remote`, 2026-11-14 15:38:09Z; production still `aa40aa43`, read twice, 15:29:33Z and 15:30:35Z). These are the commits from `aa40aa43` to main that touch `proxy/`,
from `git log aa40aa43..origin/main -- proxy/`:

| commit | PR | what changes in the deployed Worker | its check after the deploy |
|---|---|---|---|
| `9d0a148b` | #706 | `src/fleet.ts`: a comment only | none of its own (C6: nothing else changes) |
| `e9aa69a1` | #707 | nothing: `vitest.config.mts` is test configuration | none of its own (C6) |
| `26f486ab` | #711 | `src/sitefeed/bend.ts`: the snapshot centre becomes `BEND`, the public 2-dp value, which is about 0.46 km from the old one | C3 |
| `9ca8736d` | #703 | `src/routeshealth.ts`: `ROUTES_MAX_AGE_DAYS` 14 -> 10 (and `smoke-prod.sh` in FAIL mode, which is not deployed) | C2, C5 |
| `5b8ee36d` | #712 | `src/photolicense.ts`: the credits page gains "camera location and serial metadata are removed." Its photo and manifest files are ingest inputs, not part of the Worker | C4 |
| `ce726004` | #713 | nothing: `photos/manifest.json` and `photos/src/su95.jpg` are ingest inputs. No Worker source reads them (`git grep` over `proxy/src` and `wrangler.toml`: 0 references; the one `manifest.json` match is the basemap's R2 key pattern) | none of its own (C6). Landed after the plan was written, at 2026-11-14 ~04:00Z |
| `353ef1fd` | #715 | nothing: `photos/manifest.json` and `photos/src/c700.jpg` are ingest inputs, as #713's are. An esbuild bundle of `src/index.ts` at `77acc072` has 0 inputs under `photos/`, and `wrangler.toml` binds no assets | none of its own (C6). Landed after the plan |
| `b2233f0c` | #720 | `src/wx/handler.ts` only: `/healthz` gains `wx.<id>.source` for each OPERA product, and it reads `null` until the ten OPERA ingest Workers are deployed (section 1a). The fallback itself, `src/wx/ingest.ts`, is not in this Worker, and the PR's scripts and tests are not deployed | C7. Landed after the plan. Its squash subject is stale ("no code yet"; `docs/wx-opera-backup-fallback.md` section 6) |

`77acc072` (#721) changes `docs/` only, so it is not in the table.

**Re-registered 2026-11-16, before the run (the reviewer's ruling, after D0.2's early read at 15:10Z).** Two more
commits touch `proxy/`. Both were already on main at `b282699a`, so the eight-row list was short before #727 merged:

| commit | PR | what changes in the deployed Worker | its check after the deploy |
|---|---|---|---|
| `620a215d` | #708 | `src/routeshealth.ts`: `/healthz`'s routes block gains an OPTIONAL `sourceCommittedAt`. It is emitted only when `meta:routes` holds it in the writer's exact form (`YYYY-MM-DDTHH:MM:SSZ`), and omitted otherwise, never coerced (RH9s). **Predicted ABSENT on both after the deploy.** `meta:routes` was read 2026-11-16 15:12Z through the helper, printing one boolean each: neither production nor staging holds the key. The controls: each read exited 0 and parsed with a `builtAt`. Its 20 scripts and 2 tests are not in the Worker (below) | C2 |
| `7606224c` | #723 | nothing: 8 files, all under `proxy/scripts/` | none of its own (C6) |

**No Worker build reads #723's 8 files or #708's 22 non-`src` files (24 unique). Derived, not asserted:**
- the build's entry is `wrangler.toml`'s `main` (`src/index.ts`), with no `[[rules]]` and no build command;
- bundling it in memory with the checkout's own esbuild (a metafile, `write: false`) gives 57 inputs, all under `src/`,
  with 0 under `scripts/`, `test/` or `photos/`, and none of the 24;
- the control: `src/routeshealth.ts` is an input, so the method can see a file that is read.

`074863fc` (#727) changes 0 files under `proxy/`, so the proxy tree there equals `b282699a`'s.

**The radar fallback, re-checked from the same ten commits (the owner, 2026-11-16 15:13Z: confirm that the radar
fallback, if carried, stays inert until the radar's own deploy).**
- **Which commits:** only `b2233f0c` (#720) carries radar or OPERA fallback code. The method: each commit's `proxy/` files
  matched against `/wx|opera|radar/i`, and its added lines against `/opera|fallback|backup host|operaListing/i`. #720
  matches 6 files and 90 lines; the other nine match 0 and 0.
- **The gates, cited from main `074863fc`:**
  - the fallback lives in `src/wx/ingest.ts`, imported only by `src/wx/ingest-worker.ts:8` and
    `src/wx/ingest-dev-worker.ts:15`. It is not one of the proxy bundle's 57 inputs (above);
  - the proxy's only use is `src/wx/handler.ts:52`: `source` is the `latest` record's `"primary"` or `"backup"`, else
    `null`;
  - both proxy environments bind one bucket, `blipscope-wx` (`wrangler.toml:213` staging, `:419` production), so each
    gate below is one reading that holds for both.
- **Gate A, live (2026-11-16 15:15Z, through the helper):** `deployments list --config wx-ingest-opera.wrangler.toml
  --env <id> --json` for all ten. Every newest deployment predates #720's merge (2026-11-14 10:14:49Z), so the code
  that could write `source` is not deployed. The controls: each read exited 0 and parsed.
- **Gate B, live and anchored (15:18:51-15:19:08Z, through the helper):**
  - the anchor first: `wx/cref/latest` read (node exit 0) and parsed;
  - then `r2 object get blipscope-wx/wx/<id>/latest --remote --pipe` for all ten. Each exited 1 with "The specified key
    does not exist.", a positive not-found;
  - booleans only: no record body was stored or printed;
  - so `source` stays `null` (C7) until the radar deploy (Wed 11-18 10:00Z).
- **Two instrument stops on the way, recorded:**
  - gate B's first form had no anchor (BLIND);
  - its second took the parser's exit code, not node's: `PIPESTATUS` read outside the command substitution;
  - neither reading is used. The run above is the third, with its capture form proven red-first (`false` reads 1,
    `true` reads 0).

### 1a. #720 in this deploy: inert until the radar deploy (the owner's condition)

The owner, relayed 2026-11-14: *"If Monday's proxy deploy carries the merged radar fallback code, it must stay inert
until the radar deploy. Confirm that from the carried-commit list."* It does carry `b2233f0c`. The readings below were
taken 2026-11-14 15:29-15:38Z.

- **The fallback does not run in the proxy.**
  - `operaListing`, the backup host and the OPERA User-Agent live in `src/wx/ingest.ts`.
  - Only the ingest Workers' entries import that file: `src/wx/ingest-worker.ts:8` and `src/wx/ingest-dev-worker.ts:15`
    (`git grep` over `proxy/src`).
  - The proxy's entry is `src/index.ts` (`wrangler.toml` `main`). Nothing it imports reaches either of those entries.
  - **Read from a build too.** An esbuild bundle of `src/index.ts` at `77acc072` (packages external, so our code only) has
    57 inputs, and `src/wx/ingest.ts` is not one of them. The backup host, `operaListing` and the OPERA User-Agent each
    occur 0 times in it.
  - **The control:** `src/wx/ingest-worker.ts`, bundled with the same flags, holds all three, and the proxy bundle holds
    `handler.ts`'s new `source` line once.
  - **The alternative is excluded:** wrangler's bundle is not esbuild's own, but `wrangler.toml` declares no rules,
    aliases or extra modules that could add a source file the entry does not import.
- **`/healthz`'s `source` reads `null` for every OPERA product until the radar deploy.**
  - `wxHealth` (`src/wx/handler.ts`) sets `source` only when `p.source === "opera"`. It takes the `latest` record's
    `source` when that is `"primary"` or `"backup"`, and `null` otherwise.
  - Only the ten OPERA ingest Workers write an OPERA `latest`. cref's Worker checks cref alone (`CREF_WORKER`), and the
    MRMS Worker builds only an MRMS product.
  - Both environments' proxy reads the bucket those ten write, `blipscope-wx` (`[[env.staging.r2_buckets]]` and
    `[[env.production.r2_buckets]]`).
  - **None of the ten can write `source` yet.** Each was last deployed on 2026-11-11 between 15:00 and 16:24Z (the
    newest of each Worker's `deployments list`, read through the helper). That was before OPERA's primary emptied
    (about 20:10Z that day), so before the fallback existed.
  - Today, `/healthz` shows 10 OPERA products on each environment, and none carries the key. Read on production at
    15:32:52Z and 15:33:53Z, and on staging at 15:35:20Z and 15:36:22Z.
- **Nothing else in #720 changes this Worker.**
  - Its other proxy files are not deployed: `scripts/sabotage-wx-opera-fallback.py`, `scripts/wx-products-lines.sh`
    and its selftest, `test/modules.d.ts`, `test/wx-opera-fallback.test.ts` and `vitest.config.mts`.
  - `handler.ts`'s other change, `LatestRecord.source`, is a type.
  - `wx-products-lines.sh` is smoke's. Its OPERA lines now end `source=<value>`. Those lines are information, never a
    verdict (C7).
- **So the radar deploy is the ten ingest Workers alone**, through `tools/wx/ingest_deploy.sh`. After this deploy the
  proxy side is live and needs nothing more.

**On the day, the list is read again** (D0.2). A proxy-touching commit that is not in this table stops the deploy
until it is planned. A deploy never carries a change nobody predicted.

## 2. On the day: the order

| step | when | what | predicted |
|---|---|---|---|
| D0.1 | 15:55Z | the wake fires; `date -u` read | between 15:55 and 16:00Z on 11-16 |
| D0.2 | before 16:00Z | `git ls-remote` main, and the carried list from `git log aa40aa43..<main> -- proxy/` | the ten commits above (the eight, plus #708 and #723 as re-registered on 11-16), or more that touch only files no Worker build reads. Anything else: stop |
| D0.2b | with D0.2 | #696 (map-server's basemap branch, HELD from merge: its `/credits` line names TIGER/Line 2025 while the served store is 2024; the reviewer, 2026-11-15) is not carried. Three reads: no carried subject names `(#696)`; `TIGER/Line 2025` appears 0 times in `proxy/` at main's sha, with the control that `TIGER/Line 2024` is still in `photos/credits.html` and `src/photolicense.ts`; and #696 is OPEN with `ahead_by` equal to its commit count | all three true. #696 merged, a 2025 hit, or the 2024 control missing: stop |
| D0.3 | before 16:00Z | the owner's other conditions: no S1, S2, S4 or Sunday sitting is on (asked of the reviewer), and valarscopes-site PR #19 (the live scope on /blipscope/) has not merged on 11-16: `gh pr view 19 --repo Valar-Systems/valarscopes-site --json mergedAt` | both true. If either is not, no deploy: report |
| D0.4 | before 16:00Z | the rollback targets, from `deployments list --json` through the helper, ids only | production `69072b90-575a-4984-b704-3e101cd0e36a`, staging `1f1a0fae-7674-43d5-9499-e3b35c00ba68` (read 11-14 03:37-03:38Z). A different id means someone deployed since: write the new id down, and report before going on |
| D0.5 | before 16:00Z | `/healthz` on both, twice, at least 60 s apart | production: commit `aa40aa43`, `routes.maxAgeDays` 14. Staging: commit `b7184243`, `routes.maxAgeDays` 14. The `builtAt` of each is written down; it carries into C2 |
| D0.6 | 16:00Z | the baseline: `smoke-prod.sh` against production, and `BASE=<staging>` against staging, as the test identity, whole output to a file | each run's verdict list is saved by name. Today's FAILs, if any, are the baseline, not the deploy's |
| D0.7 | 16:00Z | the control for C4: `/credits` on both | the new sentence is ABSENT on both (so C4 can fail) |
| D0.7b | 16:00Z | the control for C7: `/healthz` on both, twice, at least 60 s apart, counts only | 10 OPERA products on each, and 0 carry a `source` key (so C7 can fail). Smoke's D0.6 OPERA lines end `source=-` |
| D0.8 | before D1 | the deploy checkout, made LF (the owner, 2026-11-16 15:02Z: "eol: yes"): `git worktree add --no-checkout --detach <path> <main sha>`, then `git -C <path> -c core.autocrlf=false -c core.eol=lf reset --hard <main sha>`. Then a byte count of CRLF over `proxy/src` and `proxy/scripts` (every `.ts`, `.mts`, `.mjs`, `.js`, `.json`, `.html`, `.toml`, `.sh` and `.py` file, `node_modules` excluded), with a planted control | the control (a planted file with two CRLF lines) reads 2, and both folders read 0 CRLF. Any other reading: stop, no deploy |
| D1 | about 16:10Z | the deploy claim (`--covers proxy:staging,proxy:production --until 17:00Z`), then `deploy.sh staging` from D0.8's checkout, with `npm ci` in `proxy/` | exit 0, with the stamp confirmed by `/healthz` |
| D2 | after D1 | the checks on staging: C1, C2, C4, C5, C7 | as below. Any failure: roll staging back, stop, and production does not start |
| D3 | about 16:30Z | `deploy.sh production`, from the same checkout | exit 0, with the stamp confirmed |
| D4 | after D3 | the checks on production: C1-C7 | as below. **Any failure that the baseline does not hold: roll back at once, then report** |
| D5 | by 17:00Z | release the claim and report | |

**Why D0.8 (added 2026-11-16, before the deploy).** `.gitattributes` begins `* text=auto`. A text file with no `eol`
attribute is checked out in `core.eol`, whose default on Windows is CRLF, whatever `core.autocrlf` says. The stored
blobs are LF, and CI builds from LF on Linux. Read without touching any tree, with `git cat-file --filters`,
`proxy/src/index.ts` is 0 CRLF as a blob, 467 in a default checkout, still 467 with `core.autocrlf=false`, and 0 with
`core.eol=lf` added. TS template literals normalise CRLF, so the exposure is files read as raw bytes. Whether an earlier
deploy was affected is a question, not a finding. D0.8 makes the deployed source the bytes CI tested. No git config
changes anywhere.

## 3. The checks after each deploy

**C1, the build stamp.** `/healthz` `commit` is main's sha from D0.2, read twice at least 60 s apart.

**C2, the route-age line** (#703's D-B1 and D-B3, `docs/route-age-alert.md` section 5.4):
- `maxAgeDays` is 10;
- `builtAt` is unchanged from D0.5;
- `rows` 628956, `stale` false, no `why`;
- no `sourceCommittedAt` key on either environment (#708's field: `meta:routes` holds none, read 2026-11-16 15:12Z).
- `ageDays` is computed by script from that `builtAt` at the moment of the read. With the seals as they stand on 11-14:
  - production (`2026-11-14T01:52:29.338Z`): 2 from 11-16 01:52:29.338Z to 11-17 01:52:29.338Z;
  - staging (`2026-11-13T17:44:25.919Z`): 2 until 11-16 17:44:25.919Z.
  Both read 2 inside the slot.

**C3, the site feed** (production only). The snapshot is reachable only through the `BendFeed` service binding, so
the reads come from the consumer's side. valarscopes-site's PREVIEW Workers bind PRODUCTION's `BendFeed`, with the same
`SNAPSHOT_RULES` as the site (the reviewer, 2026-11-14).
- **The trigger:** as soon as C1 passes on production, uk-data sends the site session the deploy's completion time,
  from this run's ledger. The site session runs both reads after 16:30Z, on that signal, and sends both booleans with
  stamps to uk-data and the reviewer.
- **(1), on #11's preview:** `/live`'s centre equals the 2-dp `BEND` (from `bend.ts` at the deployed sha), as a
  boolean.
- **(2), on #19's preview:** `/blips` answers 200 with a `t` NEWER than the deploy's completion time, AND `/diag` is not
  503 `not_bend` or `radius`.
- A new centre appears at the first accepted write after the deploy. That is within the 30 s gate plus the next device
  tile refresh, so `t` newer than the completion time is what proves the read saw the new code's snapshot.
- **Predicted: both true.** A refusal (`/diag` 503 `not_bend` or `radius`), a centre that is not `BEND`, or a `t` that
  is not newer than the completion time is a failure: roll back.
- If the site session cannot run the reads on the day, C3 is reported **"not run"**, never passed.
- **valarscopes-app's code prediction (a prediction, NOT a reading):**
  - `validate()` (valarscopes-app `src/live.ts:98`) refuses only beyond `maxCentreOffsetKm`, which is 1 km for bend
    (`src/places.ts:22`), so a 0.46 km move is ACCEPTED.
  - The side effect: a screen at its maximum range can lose up to about 0.46 km of coverage on its far edge.
  - The app has no read-only path on production: the centre is never exposed, and acceptance shows only as `/blips`
    rows for a paired screen, and none is paired. So the app's side is predicted from its code only.

**C4, the credits page.** `/credits` contains `camera location and serial metadata are removed.` exactly once. The
existing NOAA MRMS and OPERA lines are still there; smoke-prod.sh checks those.

**C5, the smoke.** `smoke-prod.sh`, whole, against the deployed environment (#703's D-B2):
- the route-age line reads `RESULT: PASS -- built <builtAt>, 2 d old (limit 10 d)   (mode fail)` and counts in PASS;
- every other verdict, by name, equals the D0.6 baseline.
- A verdict that PASSED in the baseline and fails now is a failure of the deploy: roll back.
- A verdict that failed in both is reported and is not the deploy's.

**C6, nothing else changed.** C5's comparison is also #706's, #707's, #713's and #715's check. They change no
behaviour, so no verdict may move other than the route-age line's limit.

**C7, #720's field, inert** (section 1a). On each environment after its deploy, `/healthz` is read twice, at least 60 s
apart, and counted (no values but these):
- 10 OPERA products, each carrying `source`, and each `source` exactly `null`;
- no non-OPERA product carries the key;
- smoke's OPERA lines (C5's run) end `source=null`, where D0.6's ended `source=-`. Those lines are information, not
  verdicts, so C5's verdict comparison is unchanged.

| C7 reads | meaning | action |
|---|---|---|
| all 10 `null`, as predicted | the field is deployed and inert | none |
| the key absent from all 10 | this Worker is not `b2233f0c`'s descendant: C1 fails too | roll back (C1's failure) |
| any `"backup"` or `"primary"` | an OPERA ingest Worker was deployed with the fallback since 11-14 15:38Z, outside the radar deploy's slot | not this deploy's failure. Stop the checks, report at once, no rollback |
| anything else (another value, or a count other than 10) | not predicted | stop, roll back, and decide |

## 4. The rollback

The targets are D0.4's ids. The command goes through the helper, and a rollback is never refused:

    node scripts/wrangler-run.mjs rollback <version-id> --env production
    node scripts/wrangler-run.mjs rollback <version-id> --env staging

After a rollback: `/healthz` shows the old commit again, read twice. A labelled line goes into the rollback log by hand.
Then the report, with the failing check's output (counts and verdicts, nothing a check prints about a device).

## 5. Outcomes

| outcome | meaning |
|---|---|
| every check as predicted, both environments | the deploy is done. #703's D-B rows are read as predicted |
| D0.2 finds an unplanned proxy change | no deploy until it is planned |
| D0.2b finds #696 carried, a 2025 hit, or the 2024 control missing | no deploy. Report |
| D0.8 reads any CRLF, or its control does not read 2 | no deploy. Report |
| D0.3 not met | no deploy. Report; the slot moves |
| D0.4's id differs | write it down and report before deploying |
| any check fails that the baseline does not hold | roll back at once, then report |
| C3 "not run" | the deploy stands on C1, C2, C4, C5, C6 and C7. Report C3 as not run, with the reason |
| anything else | stop, roll back if production changed, and decide |

## 6. The run (2026-11-16), as read: AS PREDICTED, both environments

**Deployed, `/healthz` reports `074863fc` on staging and production,** read by the confirm step and then by C1. The
deploy sha was main `074863fc` (#727), with CI finished on it (36 of 36, 33 success and 3 skipped). The deployer lock was
confirmed free by the reviewer at about 15:30Z. The claim was taken at 15:31:03Z and released at 16:36:46Z.

| step | read | result |
|---|---|---|
| D0.8 | 15:31:10Z | the checkout was made in the two-step LF form at `074863fc`; the control read 2; `proxy/src` 0 CRLF (69 files), `proxy/scripts` 0 (132); porcelain 0 before and after `npm ci` |
| D0.1 | 15:55:06Z | the wake |
| D0.2 | 15:55Z | main `074863fc`; the ten carried commits, set-equal to the table; the proxy diff from `b282699a` empty (#727 has 0 `proxy/` files) |
| D0.2b | 15:55Z | 0 carried subjects name `(#696)`; `TIGER/Line 2025` in 0 `proxy/` files, with the 2024 control once in each file; #696 open, 54 commits, ahead 54 |
| D0.3 | 15:55Z | #19 `mergedAt` null. The reviewer's half: clear (no S1, S2, S4 or owner sitting in the hour) |
| D0.4 | 15:55Z | production `69072b90-575a-4984-b704-3e101cd0e36a`, staging `1f1a0fae-7674-43d5-9499-e3b35c00ba68`, as planned |
| D0.5 | 15:55:13Z, 15:56:16Z | production `aa40aa43`, staging `b7184243`, each `maxAgeDays` 14, each `builtAt` as section 3's C2 names; identical across the two reads |
| D0.6 | 16:00:12-16:01:09Z | smoke 39 PASS, 0 FAIL, 0 SKIPPED on each; the route-age line read `limit 14 d`; OPERA lines `source=-` x10 |
| D0.7, D0.7b | 16:00:12Z, 16:00:13Z and 16:01:16Z | the C4 sentence 0 on each; 10 OPERA products with 0 `source` keys, and 0 non-OPERA with the key, on each |
| D1 | 16:10:09-16:11:07Z | `deploy.sh staging` exit 0, version `4464d8ab-15e8-4970-9c7d-97e26bcda9bd`, confirmed `074863fc` on its third poll |
| D2 | 16:11:36-16:13:07Z | C1, C2 and C7 read twice, identical: `maxAgeDays` 10, `ageDays` 2, `builtAt` unchanged, rows 628956, not stale, no `why`, **no `sourceCommittedAt`**; 10 OPERA products, each `source` null. C4: 1. C5: 39/0/0, the route-age line exactly as registered, OPERA `source=null` x10 |
| D3 | 16:30:09-16:30:48Z | `deploy.sh production` exit 0, version `2b92e9f1-73c8-4cb3-b2b0-b0090f6102a2`, confirmed `074863fc` |
| D4 | 16:31:22-16:32:59Z | as D2, on production; C6: no verdict moved |
| C3 | 16:35:06-16:35:08Z (the site session) | (1) TRUE: `/live`'s centre equals `BEND` at `074863fc`. (2) TRUE: `/blips` 200 with a `t` newer than the completion (16:30:48Z), and `/diag` not 503 `not_bend` or `radius` |
| D5 | 16:36:46-16:39:35Z | the claim released; the background end check PASS on every run ledger |

**C5's comparison key, a normalisation applied AFTER D2's read** (a defect in the key, not a re-grade; stated before
D4 and pre-registered for it):
- **The defect:** one smoke check names a live aircraft hex taken from that run's `/blips` response, so its name differs
  between runs, and D2's by-name diff read "differing 2" for that one check, PASS in both runs.
- **The normalisation:** the diff compares names after `re.sub(r"/v1/enrich/[0-9a-f]{6}\b", "/v1/enrich/<hex>", name)`,
  applied to the name only, on both sides. Normalised on the saved files, D2 read 39 against 39, differing 0.
- **Re-proved on saved files before D4:** a list against itself reads 0; one verdict flipped reads 1; the live hex
  changed alone reads 0; and the verdict flipped on the live-hex line itself reads 1.

**Instrument stops before the run, each recorded and none of their readings used.** The radar gate B took three runs:
the first had no anchor, the second captured the parser's exit code instead of node's, and the third is the reading in
section 1. D0.8's form came from four setup stops the night before, the last of them resolved by the owner at 15:02Z.

**A MISS in this PR's CI prediction.** Predicted 33 check-runs (31 success, 2 skipped); read 34 (32 success, 2 skipped),
finished, twice at least 60 s apart.
- **The extra run:** `build (blipscope-s3-128-mapbench)`. #727 added that matrix row in `skus.yml`, where `firmware.yml`'s
  matrix lives.
- **Why it was missed:** the prediction read only the workflow file.
- **The alternative excluded:** #729, which touches four `test/host` files only.
- **What it means:** CI is green, and the registered total was wrong by one row.

**For the reviewer and the owner (the site session's finding, not this deploy's to fix):** the proxy's 2-dp `BEND` and
the site's public 2-dp value round one longitude tie in opposite directions. They are about 0.8 km apart, each inside the
1 km rule.
