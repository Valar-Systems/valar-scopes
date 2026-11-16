# adsbdb off by default, and its type backfill on a switch of its own (registered 2026-11-16, before the code)

## Why

**The owner's ruling (2026-11-16, relayed verbatim):** "adsbdb types: licensed out too. adsbdb credits PlaneBase for
aircraft data, and PlaneBase's terms are personal use only (commercial export needs their permission). Keep the type
fill-in off; the FAA registry becomes the replacement, proposed first thing after v20."

Production is already safe today. Both production and staging bind `ROUTE_ADSBDB_ENABLED = "false"`
(`proxy/wrangler.toml`, read in the 2026-11-16 deploy's own output). A read-only audit on 2026-11-16 found no adsbdb or
PlaneBase path live: the proxy, KV, the firmware launch gate and the ingests were all checked. Two defects remain in the
code, and this change closes them.

1. **The code defaults ON.**
   - `chain.ts` enables the route source on `ROUTE_ADSBDB_ENABLED !== "false"`, and the type backfill and the route fetch
     return null only on `=== "false"`.
   - The top-level `[vars]` set `"true"`.
   - So an environment without the variable, or a deploy of the top-level Worker, would turn adsbdb ROUTES and adsbdb TYPE
     data back on.
2. **The type backfill rides the ROUTE switch.**
   - `fetchAircraftMetaAdsbdb` returns null on `ROUTE_ADSBDB_ENABLED === "false"`, commented "same master switch as the
     route source".
   - That coupling is how #260 (`a9594ab0`) switched the type backfill off on 2026-08-26 while stating that type was NOT
     affected.
   - A future re-enable of adsbdb routes would silently re-enable the unlicensed type data.

## The change

- **The route source is off unless the variable is exactly `"true"`.** `ROUTE_ADSBDB_ENABLED === "true"` enables it;
  absent, empty, `"false"` or anything else leaves it off.
- **The type backfill gets its own switch, `TYPE_ADSBDB_ENABLED`, off unless it is exactly `"true"`.** It no longer
  reads the route switch. The reason sits beside the check: PlaneBase-derived; personal use only per its terms; owner
  ruling 2026-11-16.
- **Every environment in `proxy/wrangler.toml` sets both to `"false"`.** That covers the top level, staging and
  production, with the reason on the type line. The top level changes from `"true"`.
- **Stale comments fixed:**
  - `types.ts` said "default on";
  - `upstreams/adsbdb.ts` described flipping the route switch;
  - the top-level `wrangler.toml` line said "false once adsb.lol routeset recovers".
- **Tests that exercise adsbdb set the switch they need to `"true"` explicitly.** The type backfill tests set
  `TYPE_ADSBDB_ENABLED`, and the route fallback tests set `ROUTE_ADSBDB_ENABLED`. No assertion changes.

No deploy of its own. This lands in main and reaches production in the next proxy deploy's plan, as a named carried
commit with its own check: the type switch reads off.

## Red-first rows (a new `proxy/test/adsbdb-off.test.ts`), predicted before the code

| row | the test | against main's code |
|---|---|---|
| **AD1** | with BOTH variables ABSENT, a feed record with a registration and no type: the response's `t` is empty, AND the adsbdb `/v0/aircraft/<hex>` interceptor is still pending afterwards. The test consumes it itself, so a backfill that ran fails here | **red**: the route variable is absent, so the backfill runs and `t` reads the type |
| **AD2** | with the route variable ABSENT, routeset's empty 201: `o` and `d` are empty, AND the adsbdb `/v0/callsign/<cs>` interceptor is still pending | **red**: the fallback runs |
| **AD3** | the test pool's own default `env` (the top-level `[vars]`) reads `ROUTE_ADSBDB_ENABLED === "false"` and `TYPE_ADSBDB_ENABLED === "false"` | **red**: the top level is `"true"`, and the type variable does not exist |
| **AD4** | DECOUPLED: `TYPE_ADSBDB_ENABLED = "true"` with `ROUTE_ADSBDB_ENABLED = "false"` still backfills the type | **red**: the type variable is ignored, and the route `"false"` stops the backfill |
| **AD5** | the control: `ROUTE_ADSBDB_ENABLED = "true"` with the type variable absent still falls back for the route | **green** before and after (not a red-first) |

**Predicted:**
- **Red-first:** run against main's code with only the new test file added, the new file reads red exactly
  **{AD1, AD2, AD3, AD4}**, with AD5 green.
- **The existing tests are unchanged in that run and stay green.** Their explicit `"true"` overrides come with the code
  commit, because `TYPE_ADSBDB_ENABLED` is not in `Env` until then.
- **After the code:** the new file reads 5 of 5. `enrich.test.ts` and `enrich-ttl.test.ts` stay green with no assertion
  changed. The whole proxy suite stays green in CI.

**Can AD1 fail? One plant, after the code:** the old coupling restored in `fetchAircraftMetaAdsbdb`, meaning the new
`TYPE_ADSBDB_ENABLED` check replaced by `if (env.ROUTE_ADSBDB_ENABLED === "false") return null;`.
- The run covers `adsbdb-off.test.ts` alone. Predicted red exactly **{AD1, AD4}**: with both variables absent the
  backfill runs again (AD1), and the route `"false"` stops it (AD4).
- The existing type-backfill tests (`enrich.test.ts` "backfills the type from adsbdb…", and `enrich-ttl.test.ts`'s
  reg-only and legacy-heal rows) would also go red under the plant. They set only the type switch, and the pool's
  route variable is `"false"`. They are not in this run, so they are named here rather than predicted as part of its
  set.
- Restored from a byte copy, with its sha256 checked.

**Outside the set:** any other red, or a regression in an existing row, is a stop.

## Addendum (2026-11-16, the reviewer's condition; registered before its code): the type switch on /healthz

**Why.** This defect lived about three months because no instrument could express "type backfill off". Meanwhile
/healthz showed the route breakers green beside it. A switch that is off for a licence reason must be visible where a
person already looks. A per-lookup log line was considered and declined: about 460 lines a day by the measured flow,
and not asked for.

**The change.** `feedHealth()` (`src/upstreams/chain.ts`, the source of /healthz's `upstreams` block) gains ONE entry,
for the type backfill's own breaker id:
- `{"id":"adsbdb_ac","enabled":<TYPE_ADSBDB_ENABLED === "true">,"state":<its breaker>,"reason":"licence"}`;
- the `reason` is present when the switch is off;
- nothing else in the block changes, and there are no per-request lines.

**The row and its predictions:**

| row | the test | before this code (at `d5822849`) |
|---|---|---|
| **AD6** | `/healthz` under the pool's default env: `upstreams` holds exactly one entry with `id` `adsbdb_ac`, and it reads `enabled` false and `reason` `"licence"` | **red**: no such entry |

- **After the code:** AD6 green, and AD1-AD5 as predicted above.
- **Plant P-AD6:** the new entry removed from `feedHealth()`. Run on `adsbdb-off.test.ts` alone, it is predicted red
  exactly **{AD6}**, and restored from a byte copy, with its sha256 checked.
- **Smoke is unaffected.** `smoke-prod.sh`'s "/healthz upstream posture" check reads only the `adsb_fi` and
  `airplanes_live` entries (by `grep -oE` on their ids), so no smoke verdict can move. No other test pins the block's
  shape or length. The scripts that read it (`healthz-commit`, `routes-age-verdict`, `wx-products-lines` selftests) read
  fixed fixtures.
- **For the next proxy deploy's plan:** its C1/C2 predictions include this entry on both environments, reading
  `"enabled":false,"reason":"licence"`. That is the "the type switch reads off" check.

## CI, predicted before the push

The count is derived from what CI expands (`skus.yml`'s rows and the workflow files):
- #723, which touched `proxy/**` and `docs/`, read 36 = 34 success + 2 skipped before #727;
- #727 added one firmware build matrix row in `skus.yml`, with no other workflow change since.
- **Predicted: 37 check-runs = 35 success + 2 skipped (promote, version) + 0 other.**
- The proxy suite in the workers workflow runs the new file.
- Any other total or split is a stop.

## Record (2026-11-16), and the re-registrations it forced

**Run 1, red-first at `30824fef` (21:10:51-21:10:53Z, `adsbdb-off.test.ts`, one vitest run): OFF. A MISS, recorded as
read and not re-graded.** Five rows failed against the predicted four.
- **AD1 to AD4 were red, each by exactly its registered mechanism:**
  - AD1: `expected 'E75L' to be ''`, so the backfill ran with both variables absent;
  - AD2: `expected 'ZRH' to be ''`, so the fallback ran;
  - AD3: `expected 'true' to be 'false'`;
  - AD4: `expected '' to be 'E75L'`, so no backfill ran under the route's `"false"`.
- **AD5's own body passed.** Its log shows `adsbdb_route ... ok:true`. Its one failure is the shared `afterEach`
  `fetchMock.assertNoPendingInterceptors()` reporting `GET .../v0/aircraft/ad0004` pending, which is AD4's mock.
- **The cause is this registration's defect.** It predicted the rows as if they were isolated, but a red row that leaves
  an interceptor unconsumed fails every LATER row's `afterEach`.
- **The alternative, AD5's mechanism broken, is excluded:** its body's assertions passed, and a broken AD5 would fail on
  `ZRH`/`JFK`, not on another row's hex.

**The cascade, derived row by row (the reviewer's condition, before any later run).**
- **Rows that register interceptors:** AD1 (feed, `/v0/aircraft`), AD2 (feed, routeset, `/v0/callsign`), AD4 (feed,
  `/v0/aircraft`) and AD5 (feed, routeset, `/v0/callsign`). AD3 and AD6 register none.
- **Which red rows leave a mock pending:**
  - a RED AD1 or AD2 consumes its adsbdb mock, because the backfill or fallback ran, and fails before its own drain, so
    nothing is left pending;
  - a RED AD4 leaves `/v0/aircraft/ad0004` pending, because no backfill ran. It is the only cascading row;
  - a RED AD6 uses no mocks.
- **Unmatched requests cannot cascade.** With network connect disabled, an unmatched request throws inside the upstream
  chain and is caught there. The `afterEach` checks only for pending mocks.
- **File order** is AD1, AD2, AD3, AD4, AD5 at `30824fef`, and AD1, AD2, AD3, AD4, AD6, AD5 at `819a89e0` and
  `93b65f0b`.

**Re-registered, before each runs:**

| run | at | predicted red | why |
|---|---|---|---|
| R1b, AD5 alone (`-t "AD5"`), a NEW run and not a re-grade | `30824fef` | **none**: AD5 green, nothing pending in its `afterEach` | AD5 runs alone, so nothing earlier can leave a mock |
| R2, the AD6 addendum | `819a89e0` | **{AD6}** | the code is present, so AD4 is green and leaves nothing; AD6 uses no mocks |
| R3, green | `93b65f0b`, `adsbdb-off`, `enrich` and `enrich-ttl` | **none** | nothing red, so nothing left pending |
| P-ADC, the old coupling restored | `93b65f0b` plus the plant, `adsbdb-off` alone | **{AD1, AD4, AD6, AD5}** | AD1 and AD4 for their own reasons; AD6 and AD5 by the CASCADE from AD4's pending `ad0004` mock, their bodies green |
| P-AD6, the entry removed | `93b65f0b` plus the plant, `adsbdb-off` alone | **{AD6}** | AD6 uses no mocks and AD4 is green, so no cascade |

**Outside the set:** anything else is a stop. A second OFF on AD5's row goes to the owner. A cascade-proof `afterEach`
(draining pending mocks after asserting) is deferred to a later PR with its own red-first, because changing it now
would change the rows being read.
