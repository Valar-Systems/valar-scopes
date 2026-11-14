# The proxy deploy of 2026-11-16: plan and predictions, before the deploy

**The owner's go (2026-11-14 03:32Z, relayed verbatim):** "Proxy deploy: go, on Monday Nov 16, not before. Keep it out
of S1, S2, S4 and my Sunday sitting (they use the proxy), and not on the same day as #19's merge."

**The slot** (the reviewer, 2026-11-14): **Monday 2026-11-16, 16:00-17:00Z.** It is clear of the other 11-16 windows
that use the proxy: map-client's S2b (from 00:00Z) and map-server's FT-HR (04:00-07:30Z). The uk-data session deploys,
under its own deploy claim, and runs the checks below.

## 1. What the deploy carries, read from git at plan time

Production's `/healthz` reports `aa40aa43` (read 2026-11-14 03:34:50Z), and staging's reports `b7184243`. Main is
`5b8ee36d6ce9` (read with `git ls-remote`, 03:34:50Z). These are the commits from `aa40aa43` to main that touch `proxy/`,
from `git log aa40aa43..origin/main -- proxy/`:

| commit | PR | what changes in the deployed Worker | its check after the deploy |
|---|---|---|---|
| `9d0a148b` | #706 | `src/fleet.ts`: a comment only | none of its own (C6: nothing else changes) |
| `e9aa69a1` | #707 | nothing: `vitest.config.mts` is test configuration | none of its own (C6) |
| `26f486ab` | #711 | `src/sitefeed/bend.ts`: the snapshot centre becomes `BEND`, the public 2-dp value, which is about 0.46 km from the old one | C3 |
| `9ca8736d` | #703 | `src/routeshealth.ts`: `ROUTES_MAX_AGE_DAYS` 14 -> 10 (and `smoke-prod.sh` in FAIL mode, which is not deployed) | C2, C5 |
| `5b8ee36d` | #712 | `src/photolicense.ts`: the credits page gains "camera location and serial metadata are removed." Its photo and manifest files are ingest inputs, not part of the Worker | C4 |

**On the day, the list is read again** (D0.2). A proxy-touching commit that is not in this table stops the deploy
until it is planned. A deploy never carries a change nobody predicted.

## 2. On the day: the order

| step | when | what | predicted |
|---|---|---|---|
| D0.1 | 15:55Z | the wake fires; `date -u` read | between 15:55 and 16:00Z on 11-16 |
| D0.2 | before 16:00Z | `git ls-remote` main, and the carried list from `git log aa40aa43..<main> -- proxy/` | the five commits above, or more that touch only files no Worker build reads. Anything else: stop |
| D0.3 | before 16:00Z | the owner's other conditions: no S1, S2, S4 or Sunday sitting is on (asked of the reviewer), and valarscopes-site PR #19 (the live scope on /blipscope/) has not merged on 11-16: `gh pr view 19 --repo Valar-Systems/valarscopes-site --json mergedAt` | both true. If either is not, no deploy: report |
| D0.4 | before 16:00Z | the rollback targets, from `deployments list --json` through the helper, ids only | production `69072b90-575a-4984-b704-3e101cd0e36a`, staging `1f1a0fae-7674-43d5-9499-e3b35c00ba68` (read 11-14 03:37-03:38Z). A different id means someone deployed since: write the new id down, and report before going on |
| D0.5 | before 16:00Z | `/healthz` on both, twice, at least 60 s apart | production: commit `aa40aa43`, `routes.maxAgeDays` 14. Staging: commit `b7184243`, `routes.maxAgeDays` 14. The `builtAt` of each is written down; it carries into C2 |
| D0.6 | 16:00Z | the baseline: `smoke-prod.sh` against production, and `BASE=<staging>` against staging, as the test identity, whole output to a file | each run's verdict list is saved by name. Today's FAILs, if any, are the baseline, not the deploy's |
| D0.7 | 16:00Z | the control for C4: `/credits` on both | the new sentence is ABSENT on both (so C4 can fail) |
| D1 | about 16:10Z | the deploy claim (`--covers proxy:staging,proxy:production --until 17:00Z`), then `deploy.sh staging` from a fresh detached checkout of main's sha, with `npm ci` in `proxy/` | exit 0, with the stamp confirmed by `/healthz` |
| D2 | after D1 | the checks on staging: C1, C2, C4, C5 | as below. Any failure: roll staging back, stop, and production does not start |
| D3 | about 16:30Z | `deploy.sh production`, from the same checkout | exit 0, with the stamp confirmed |
| D4 | after D3 | the checks on production: C1-C6 | as below. **Any failure that the baseline does not hold: roll back at once, then report** |
| D5 | by 17:00Z | release the claim and report | |

## 3. The checks after each deploy

**C1, the build stamp.** `/healthz` `commit` is main's sha from D0.2, read twice at least 60 s apart.

**C2, the route-age line** (#703's D-B1 and D-B3, `docs/route-age-alert.md` section 5.4):
- `maxAgeDays` is 10;
- `builtAt` is unchanged from D0.5;
- `rows` 628956, `stale` false, no `why`.
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

**C6, nothing else changed.** C5's comparison is also #706's and #707's check. They change no behaviour, so no verdict
may move other than the route-age line's limit.

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
| D0.3 not met | no deploy. Report; the slot moves |
| D0.4's id differs | write it down and report before deploying |
| any check fails that the baseline does not hold | roll back at once, then report |
| C3 "not run" | the deploy stands on C1, C2, C4, C5 and C6. Report C3 as not run, with the reason |
| anything else | stop, roll back if production changed, and decide |
