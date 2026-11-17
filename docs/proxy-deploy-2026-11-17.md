# The proxy deploy after #736: plan and predictions, before the deploy (written 2026-11-16)

**The owner's go (2026-11-16, about 22:21Z, relayed by the reviewer; the owner's selection, verbatim):** "15:00Z Tue
(7 AM PT) (Recommended)". The option the owner selected read: "Between FT-HR and S2b, with no proxy-using window open;
~30 min; rollback always allowed. uk-data runs it after #737's CI and my OK." ("my OK" is the reviewer's.)

**The slot: Tuesday 2026-11-17, from 15:00Z, about 30 minutes.** It is a production deploy, so it keeps its time, with
no early start. The deployer lock is confirmed with the reviewer at 14:45Z (D0.2), and the checks from D0.3 on run at
15:00Z. The uk-data session deploys, under its own deploy claim, and runs the checks below.

**Why this deploy.** #736 (`00fc75d1`) turns adsbdb off unless a variable says exactly `"true"`. It also gives the
adsbdb TYPE backfill a switch of its own, off, because adsbdb's aircraft data is PlaneBase-derived and PlaneBase's
terms are personal use only (the owner's ruling, 2026-11-16; `docs/adsbdb-off-by-default.md`). Merging it changed
nothing in production. This deploy is what puts it there, and `/healthz`'s stamped commit is the proof.

**Production behaviour does not change, and the plan says why rather than assuming it.**
- Before: both environments bind `ROUTE_ADSBDB_ENABLED = "false"`, and that one value stopped both adsbdb paths. The
  route fallback returned on `=== "false"`, and so did the type backfill, which was coupled to it.
- After: the route fallback runs only on `ROUTE_ADSBDB_ENABLED === "true"`, and the type backfill only on
  `TYPE_ADSBDB_ENABLED === "true"`. Both environments bind both as `"false"` (`proxy/wrangler.toml` at `00fc75d1`).
- So neither path runs before or after. C2 reads it at runtime: `/healthz`'s `enabled` for `adsbdb_route` and
  `adsbdb_ac` is computed from exactly those two predicates.

## 1. What the deploy carries

**Re-listed at deploy time, not frozen now** (D0.3). Both environments' `/healthz` report `074863fc`, read
2026-11-16 22:05:07Z. At plan time, main is `00fc75d1` (`git ls-remote`). `git log 074863fc..00fc75d1 -- proxy/` lists two
commits:

| commit | PR | what changes in the deployed Worker | its check |
|---|---|---|---|
| `00fc75d1` | #736 | `src/upstreams/chain.ts`: the two switches, and one new `/healthz` `upstreams` entry, `adsbdb_ac`. `wrangler.toml`: `TYPE_ADSBDB_ENABLED = "false"` in every environment; the top level's `ROUTE_ADSBDB_ENABLED` goes from `"true"` to `"false"`, and staging and production already had `"false"`. `src/types.ts` adds interface fields only, and `src/upstreams/adsbdb.ts` changes comments only | C2, C3 |
| `3815844f` | #734 | nothing: its proxy files are `FEED-SOURCING.md`, a script and a test | none of its own (C5) |

**No Worker build reads the eight carried files outside `src/` and `wrangler.toml`. This is derived, not asserted
(2026-11-16, in a checkout whose tree equals `00fc75d1`'s):**
- the eight are `FEED-SOURCING.md`, `README.md`, `scripts/sabotage-wx-opera-fallback.py` and five files under `test/`;
- the build's entry, `src/index.ts`, is bundled in memory with the checkout's own esbuild. That gives 57 inputs, all
  under `src/`, and none of the eight;
- control 1: `src/routeshealth.ts` is an input, so the method sees a file that is read;
- control 2: the same check with `src/upstreams/chain.ts` as the candidate reports it as an input.

**The rule at D0.3.** The deploy goes ahead only if every commit listed then is one of the two above, or touches only
files the same bundle check proves are not inputs. Anything else means no deploy until this plan is amended and
re-registered before the run. A deploy never carries a change nobody predicted.

## 2. On the day: the order

| step | when | what | predicted |
|---|---|---|---|
| D0.1 | before the run | the owner's go and slot, above, copied verbatim on 2026-11-16 | present. Without it, nothing runs |
| D0.2 | 14:45Z, and again at 15:00Z | the deployer lock, confirmed with the reviewer (a granted slot is not the lock). The reviewer also confirms that no session's window that uses the proxy is open, and that neither the radar deploy nor the route run of 2026-11-18 is in progress. **FT-HR:** at 15:00Z, map-client is asked whether FT-HR (from 12:54Z, up to about 212 min, so to about 16:26Z at most) is still drawing through the proxy | all confirmed. If FT-HR is still drawing, the deploy waits for its end, but not past 17:50Z (S2b). If it cannot fit before then, no deploy: report to the reviewer, who goes back to the owner |
| D0.3 | from 15:00Z | `git ls-remote` for main, then the carried list from `git log 074863fc..<main> -- proxy/`, under section 1's rule. `/healthz` on both still reports `074863fc` | as section 1. A base other than `074863fc` means someone deployed since: stop and re-list from the new base |
| D0.4 | before D1 | CI at main's sha: a FINISHED run, read twice at least 60 s apart, by full sha | finished, nothing failed. Otherwise wait, or no deploy |
| D0.5 | before D1 | the rollback targets, from `deployments list --json` through the helper, ids only | one id per environment, written down |
| D0.6 | before D1 | `/healthz` on both, twice, at least 60 s apart. Written down: `commit`; `upstreams` by id, order, `enabled`, `state` and key set; the routes block; the OPERA products' `source` values | `074863fc`; the seven entries in section 3's C2 table; routes and OPERA values written down for C5 |
| D0.7 | before D1 | the baseline: `smoke-prod.sh` against production, and with `BASE=<staging>` against staging, as the test identity, whole output to a file | each run's verdicts are saved by name. Today's FAILs, if any, are the baseline's, not the deploy's |
| D0.8 | before D1 | the deploy checkout, made LF: `git worktree add --no-checkout --detach <path> <main sha>`, then `git -C <path> -c core.autocrlf=false -c core.eol=lf reset --hard <main sha>`. Then a byte count of CRLF over `proxy/src` and `proxy/scripts`, with a planted control | the control reads 2 and both folders read 0. Otherwise no deploy |
| D1 | in the slot | the deploy claim (`--covers proxy:staging,proxy:production --until <slot end>`), then `deploy.sh staging` from D0.8's checkout, with `npm ci` in `proxy/` | exit 0, with the stamp confirmed |
| D2 | after D1 | the checks on staging: C1-C5 | as below. Any failure: roll staging back and stop; production does not start |
| D3 | after D2 | `deploy.sh production`, from the same checkout | exit 0, with the stamp confirmed |
| D4 | after D3 | the checks on production: C1-C5 | as below. **Any failure that the baseline does not hold: roll back at once, then report** |
| D5 | by the slot's end | release the claim, and report | |

## 3. The checks after each deploy

**C1, the build stamp.** `/healthz` `commit` equals D0.3's main sha, read twice at least 60 s apart.

**C2, the `upstreams` block: exactly one entry more.** D0.6 is read twice; so is this.

| # | id | enabled | keys | after the deploy |
|---|---|---|---|---|
| 1 | `adsb_lol` | true | `enabled,id,state` | equal to D0.6 |
| 2 | `adsb_lol_b` | true | `enabled,id,state` | equal to D0.6 |
| 3 | `adsb_fi` | true | `enabled,id,state` | equal to D0.6 |
| 4 | `adsb_fi_b` | true | `enabled,id,state` | equal to D0.6 |
| 5 | `airplanes_live` | false | `enabled,id,state` | equal to D0.6 |
| 6 | `adsb_lol_route` | true | `enabled,id,state` | equal to D0.6 |
| 7 | `adsbdb_route` | false | `enabled,id,state` | equal to D0.6 |
| 8 | `adsbdb_ac` | **false** | `enabled,id,reason,state` | **new**: `"state":"closed","reason":"licence"` |

The table is the plan-time read (2026-11-16 22:07:13Z production, 22:07:14Z staging; both identical; every `state`
`closed`). D0.6's read on the day replaces it as the comparison.
- **Graded:** the count is 8; entries 1-7 match D0.6 in order, `id`, `enabled` and key set; entry 8 is exactly
  `{"id":"adsbdb_ac","enabled":false,"state":"closed","reason":"licence"}`.
- `adsbdb_ac`'s `state` is graded because its breaker cannot open: with its switch off, the type backfill makes no
  request.
- **Entries 1-7's `state` is read and reported, not graded.** A live breaker follows the upstream's health, not the
  deploy, and a fresh isolate starts every breaker `closed`. If one reads `open`, C4 decides: smoke's verdicts equal to
  the baseline make it information; a verdict that moved is C4's failure.

**C3, the deployed variables.** The deploy's own output for each environment lists its variables. Predicted:
`ROUTE_ADSBDB_ENABLED` `"false"` and `TYPE_ADSBDB_ENABLED` `"false"`. If the output does not print them, C3 is reported
"not observable", never passed. C2's two `enabled: false` readings still stand as the runtime reading of the same two
values.

**C4, the smoke.** `smoke-prod.sh`, whole, against the deployed environment:
- every verdict, by name, equals the D0.7 baseline;
- a verdict that passed in the baseline and fails now is the deploy's failure: roll back;
- a verdict that failed in both is reported, and is not the deploy's;
- the new entry cannot move a verdict. Smoke's "/healthz upstream posture" check reads only the `adsb_fi` and
  `airplanes_live` entries, by id.

**C5, nothing else changed.** On `/healthz`, read twice:
- `ok` is `true`;
- the routes block equals D0.6's: `maxAgeDays`, `builtAt`, `rows` and `stale`. `ageDays` moves with the clock and is
  not compared;
- the OPERA products' count and `source` values equal D0.6's. D0.6 is read inside the slot, and D0.2 confirmed that
  the radar deploy is not running in it, so those values have no reason to move.
This is also #734's check: it changes no behaviour, so nothing may move.

## 4. The rollback

The targets are D0.5's ids. The command goes through the helper, and **a rollback is never refused**:

    node scripts/wrangler-run.mjs rollback <version-id> --env production
    node scripts/wrangler-run.mjs rollback <version-id> --env staging

After a rollback, `/healthz` shows `074863fc` again, read twice, and `upstreams` has seven entries. A labelled line goes
into the rollback log by hand. Then the report, with the failing check's output (counts and verdicts; nothing a check
prints about a device).

## 5. Outcomes, registered before the run

| outcome | meaning | action |
|---|---|---|
| C1-C5 as predicted, both environments | #736 is deployed; the type switch reads off where a person looks | release the claim, report |
| D0.1-D0.8 not met | the deploy is not ready, or not allowed | no deploy; report, and the slot moves |
| C2 reads 7 entries, or no `adsbdb_ac` | this Worker is not `00fc75d1`'s descendant: C1 fails too | roll back (C1's failure) |
| C2's `adsbdb_ac` reads `enabled: true`, or has no `reason` | `TYPE_ADSBDB_ENABLED` is `"true"` in that environment, against the toml: the licence condition is broken | roll back at once, report to the reviewer and the owner |
| C2's entries 1-7 differ in `id`, `enabled`, order or keys | a change nobody predicted | roll back, report |
| C3 "not observable" | the deploy output's format changed | the deploy stands on C1, C2, C4 and C5; report C3 as not observable |
| a smoke verdict moved from the baseline (C4) | the deploy changed behaviour | roll back, report |
| **anything else** (a reading outside every row above, or an instrument that prints OFF or BLIND) | **the instrument or the prediction is unreliable** | **STOP. Roll back if production changed, report, and decide with the reviewer. No re-run until the ruling** |

## 6. Amendment, before D0: the owner moved the slot (the 15:00Z go above is SUPERSEDED)

The text above is left as frozen. This section changes only the slot and D0.2; everything else stands.

**The owner's go, superseding the 15:00Z one (2026-11-16, about 23:58Z, relayed by the reviewer; the owner's selection,
verbatim):** "23:00Z Tue (3 PM PT) (Recommended)". The option the owner selected read: "After your sitting; no bench run
open. The daylight chain FT-HR -> S2 -> S2b keeps its margin before dusk. Production behaviour is unchanged by this
deploy, so a later time costs nothing."

**Why it moved** (the reviewer). FT-HR, S2 and S2b all draw through the production proxy, and S2 must run in daylight to
match S2b. A 15:00Z deploy would have pushed S2b toward dusk.

**The slot: Tuesday 2026-11-17 from 23:00Z, about 30 minutes.** It keeps its time, with no early start.

**D0.1:** this go.

**D0.2**, replacing the row above:
- **When:** 22:45Z (the deployer lock, confirmed with the reviewer) and 23:00Z.
- **The window rule:** "No proxy-using window open" now means the owner's sitting has RELEASED. The sitting starts at
  22:00Z, lasts about 40 min, and uses a bench board that fetches through the proxy.
- **How it is read:** from the sitting's end line, or by asking at 23:00Z.
- **The FT-HR condition above is replaced by this one.**
- **If the sitting overruns:** the deploy waits for its end.
- **If the deploy cannot start by 23:45Z:** no deploy. The report goes to the reviewer, who goes back to the owner.

D0.3 onward run from 23:00Z.
