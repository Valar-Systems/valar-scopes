# The route mirror's age on /healthz (registration, predictions first, 2026-11-13)

**For:** the reviewer. **Status:** frozen before any code. **Rigour:** full: this reaches production. Deploy only on
the reviewer's slot.

## 0. Why

The `rt:` mirror is standing-data commit `dff309eb` (2026-08-25), built into production KV at 2026-08-26T17:25:54Z.
Its upstream commits daily, and nothing has refreshed it since ([route-sources.md](route-sources.md), sections 2.1 and 6).

The ingest writes `meta:routes` "so /healthz correctly reports stale" (`ingest-routes.ts:43-47`). **But no file in
`proxy/src` reads it.** That is a written claim with nothing behind it, and an instrument that fires into a void.

The reviewer accepted this proposal at 2026-11-13 ~06:47Z:
- N = 14 days;
- the alert is printed by `reconcile-fleet.py` and judged by `smoke-prod.sh`;
- the coupling: WARN until an approved refresh has landed, then FAIL.

**Why N = 14:**
- The upstream commits daily, but the value of a refresh decays slowly. 79 days of staleness cost about 9% of distinct
  misses ([route-sources.md](route-sources.md), section 6).
- A weekly refresh can miss one run before this fires, and it fires on the second.

## 1. What gets built

| file | what |
|---|---|
| `proxy/src/routeshealth.ts` (new) | `routesHealth(env)` and `resetRoutesHealth()`, plus `ROUTES_MAX_AGE_DAYS = 14`, the rule's ONE home |
| `proxy/src/index.ts` | `handleHealth` adds `routes: await routesHealth(env)`. Nothing else in the body changes |
| `proxy/test/routes-health.test.ts` (new) | RH1-RH8 (section 2) |
| `proxy/scripts/routes-age-verdict.sh` (new) | `routes_age_verdict <body file> <mode>`: one line, `PASS`, `WARN` or `FAIL`, from the `/healthz` body |
| `proxy/scripts/routes-age-verdict.selftest.sh` (new) | SV1-SV6 (section 3) |
| `proxy/scripts/smoke-prod.sh` | a new block `===== /healthz routes mirror age =====` that reads the body already fetched. It sets `ROUTES_AGE_MODE=warn`, so a WARN never changes the exit code (the wx-lines precedent). It also updates the route-source comment |
| `scripts/reconcile-fleet.py` | `report_route_mirror(env)`, called from `report_instruments` beside the OTA reader. It prints one line every run, and a warning line when stale |
| `scripts/reconcile-fleet.selftest.py` | RC1-RC4 (section 3) |
| `.github/workflows/workers.yml` | one step: `bash scripts/routes-age-verdict.selftest.sh` |
| `proxy/scripts/ingest-routes.ts` | lines 43-47 corrected to name the reader that now exists. Comment only |

**The field, exactly:**

```
"routes": { "builtAt": "<ISO>" | null, "ageDays": <int> | null, "rows": <int> | null,
            "stale": <bool>, "maxAgeDays": 14, "why"?: "absent" | "unparsable" | "future" | "unreadable" }
```

- `ageDays = floor((now - builtAt) / 86,400,000 ms)`.
- `stale = (now - builtAt) > 14 days`, in milliseconds, not floored days.
- **Never a made-up age.** A missing key, an unparsable value, or a `builtAt` in the future reads
  `builtAt`/`ageDays`/`rows` null, `stale: true`, and a `why`.
  - A KV read that throws reads `why: "unreadable"`. "Absent" and "cannot read" never collapse into one branch:
    the ingest's own rule, `ingest-routes.ts`, near line 330.
  - A future `builtAt` is never clamped to 0. A clamp would turn evidence of a wrong meta into the most reassuring
    possible age.
  - `rows` is reported only when it is a non-negative integer. Otherwise it is null.
- **`maxAgeDays` is in the body,** so smoke and reconcile read the rule from the Worker and never restate it.
- **Isolate cache:** the parsed `builtAt`/`rows` (or the reason it has none) is kept for 10 minutes, and the age is
  computed per response. A read that throws is NOT cached, so the next call retries.
  - The value is about 50 KB, with the shard map. Only the fields above leave the Worker.
- `routesHealth` never throws. `/healthz` answers 200 whatever KV does.

## 2. The Worker rows (`proxy/test/routes-health.test.ts`)

| | planted in the test's KV | must read |
|---|---|---|
| RH1 | `builtAt` = now − 3 d, rows 619,103 | `{builtAt, ageDays: 3, rows: 619103, stale: false, maxAgeDays: 14}`, no `why` |
| RH2 | now − 20 d | `ageDays: 20, stale: true` |
| RH3 | now − 14 d + 60 s, then (after reset) now − 14 d − 60 s | `stale: false`, then `stale: true`. Both read `ageDays: 13`, then `14` |
| RH4 | no `meta:routes` key | nulls, `stale: true, why: "absent"` |
| RH5 | the value `not json`, and separately `{"rows":5}` (no `builtAt`) | nulls, `stale: true, why: "unparsable"`, both |
| RH6 | `builtAt` = now + 1 d | nulls (`ageDays` null, NOT 0), `stale: true, why: "future"` |
| RH7 | a stub `ENRICH_KV` whose `get` throws, then the real KV with RH1's value | `why: "unreadable"` and status 200, then RH1's answer without a reset (the throw was not cached). Plus a cache check: after RH1, a changed value is NOT seen until `resetRoutesHealth()` |
| RH8 (control) | RH1's value | the top-level `commit`, and the `wx` and `basemap` fields, are present exactly as before |

**Predicted on today's code** (the test file lands before the implementation): **RH1-RH7 red** (there is no `routes`
field), **RH8 green**. After the code: **8 of 8 green**, and every existing proxy test unchanged.

**Sabotage rehearsals, each red first, in a copy:**
- S1: the threshold at 15 days. **RH3 red only.**
- S2: the throw cached. **RH7 red only.**
- S3: a future `builtAt` clamped to age 0. **RH6 red only.**
- S4: `routes` dropped from `handleHealth`. **RH1-RH7 red, RH8 green.**

## 3. The readers

**`routes-age-verdict.sh`**, graded by its selftest (each case run in `warn` and in `fail` mode):

| | body | warn mode | fail mode |
|---|---|---|---|
| SV1 | `stale: false`, age 3 | `PASS -- built <ISO>, 3 d old (limit 14 d)` | the same |
| SV2 | `stale: true`, age 79 | `WARN -- STALE: built <ISO>, 79 d old (limit 14 d)` | `FAIL -- ...` with the same text |
| SV3 | nulls, `why: "absent"` | `WARN -- no age: absent` | `FAIL -- no age: absent` |
| SV4 | no `routes` field at all | `WARN -- /healthz has no routes field (a Worker before this change)` | `FAIL -- ...` |
| SV5 | not JSON / empty | `WARN -- /healthz body unreadable` | `FAIL -- ...` |
| SV6 (control) | SV2's body | the line names no field outside `routes`, and never prints the body |

- **Sabotage SV-S1:** a verdict that ignores `stale`. **SV2 red in both modes**; SV1 and SV3-SV6 green.

**`reconcile-fleet.py`'s `report_route_mirror`**, graded by RC rows in its selftest, with `/healthz` replaced by a fixture:
- RC1: fresh. One line, `Route mirror (rt:): built <ISO>, 3 d old, 619103 rows (limit 14 d)`, and **no** warning.
- RC2: stale. The same line, plus **one** warning naming the age and the limit.
- RC3: no `routes` field. One warning: `/healthz has no routes field`.
- RC4: `/healthz` unreachable. One warning: `could not read /healthz`. Never a silent pass.

**Sabotage RC-S1:** the warning branch removed. **RC2-RC4 red**, RC1 green.

## 4. CI (predicted before each push; main `b7e7fdd9`)

- **This registration commit,** docs only: **33 = 31/2/0.** That is #693's read at `b83ee4a1`, on the same main.
- **The code commits** (`proxy/**` and `scripts/reconcile-fleet*.py`, both in `workers.yml`'s filter; no new job):
  **35 = 33/2/0.** That is #683's read at `278022c1`, a proxy PR on a main with the same job set.
- The new selftest step is in an existing job, so the count does not move.

## 5. The deploy (on the reviewer's slot only; staging, then production)

The values were read 2026-11-13 under the narrowed hold, with wrangler's log in a temp dir that was then removed.

| row | where | predicted |
|---|---|---|
| D-S1 | staging `/healthz` `routes` | `builtAt` `2026-08-25T23:31:26.817Z`, `rows` 619103, `stale` true, `maxAgeDays` 14, `ageDays` = floor(t − builtAt), at the read's own time t (79 before 11-13T23:31:26Z, 80 after) |
| D-P1 | production `/healthz` `routes` | `builtAt` `2026-08-26T17:25:54.476Z`, `rows` 619103, `stale` true, `maxAgeDays` 14, `ageDays` = floor(t − builtAt): 78 before 11-13T17:25:54.476Z, 79 from then, 80 from 11-14T17:25:54.476Z. **Corrected 2026-11-13 16:45Z, before any read** (ruled by the reviewer). This was a derivation error: the numbers had been written as "79 before, 80 after", one day too many. The formula is unchanged. A read within 60 s either side of 17:25:54Z is not graded; it is re-read at or after 17:27:00Z against 79 |
| D-P2 | production `/healthz` `commit` | the deployed sha, by `healthz-commit.sh` |
| D-P3 | `smoke-prod.sh` against production | the new block reads `WARN -- STALE: built 2026-08-26T17:25:54.476Z, <ageDays> d old (limit 14 d)`. PASS/FAIL/SKIPPED and the exit code are **identical** to the same script's run before the deploy |

- The rollback target is production's current version id, re-read and recorded before the deploy.
- **Any off reading:** roll back first, then report.

**Not in this PR:** the switch to `ROUTES_AGE_MODE=fail`. It ships only after an approved refresh has landed and
been verified. If the owner declines the refresh, the mode stays `warn` until it is ruled.

**Side finding (P3, the queue):** staging's `meta:routes` carries no `ruleRev`, while production's carries 3. The two
mirrors are on different rule revisions.

## 6. Escape

- Anything outside RH, SV, RC, S, CI or D as written: stop and report. No re-registration without a ruling.
- A crash is an instrument failure, never a red.
- A count off from its own list: report both.
