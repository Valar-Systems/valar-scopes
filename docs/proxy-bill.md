# The proxy's bill: Workers Logs sampling and KV (registration; nothing is read, built or deployed yet)

**The brief** (the owner, relayed 2026-11-11 ~05:25Z, `docs/wx-account-cost.md` section 7): "The proxy's bill (Logs
sampling, KV): blipscope-5c, after v19 is promoted." v19 was promoted on 2026-11-12. The reviewer's conditions
(2026-11-12):
- Head sampling must keep `evt:"breaker"` and `evt:"upstream_skip"` unsampled, or move them to Analytics Engine with
  a control. Those two lines are how a route or breaker failure becomes visible at all (CLAUDE.md, "a justification is
  a claim about code you are not editing").
- `docs/wx-cost.md`'s F1-F3 join as lever (e): F1, the Cache API on `latest`, measured; F3 is the owner's visible
  call; F2 is not proposed.
- Every estimate states the WHOLE ACCOUNT's monthly bill against the owner's allowance, and adds its line to
  `docs/account-budget.json` (the owner's rule, `docs/account-budget.md` section 0).

**What this page decides:** nothing about deploys. It registers what gets measured, in what order, with predictions
frozen before each read. Each lever then gets its own PR, predictions first. Whether any lever deploys, and the
observability it costs, are the owner's calls.

## 1. Where the money is

At fleet scale the account's bill is the radar's own traffic, not weather (`docs/wx-account-cost.md` section 6). At
600 boards, November prices, no Weatherscope, without the ten:

| line | $/month | what drives it |
|---|---|---|
| Workers Logs | $166.45 | sampling rate 1 on the proxy: 3.1 events per request, about 1,714 B per event |
| KV reads | $115.38 | about 3.5 per proxy request, 401,000 a board a month |
| KV writes | $53.83 | production's namespace, about 19,000 a board a month; what they are has not been read |
| requests | $25.61 | the fleet's poll cadence: a product setting, not a lever here |
| R2 Class B | $16.63-$25.93 | mostly `/wx` (`docs/wx-cost.md`) |
| CPU | $8.91-$19.08 | |
| **total** | **$393.65-$413.11** | the $5 subscription included |

Workers Logs moves to GB pricing on 2026-12-01: about $353-$372 at 600, the same order.

**Today's account** (`docs/account-budget.md` section 6.1, the 7 days ending 2026-11-12 21:00Z):
- KV reads are at **72%** of the free 10 M, and **83%** over the last 24 hours. That is the next allowance to cross,
  at today's fleet, before any growth.
- KV writes are at 61%, Workers Logs events at 42% (November), and Workers CPU is already over (120%).

So one lever, the KV reads, is near-term and not only a fleet-scale saving.

## 2. The levers, and what each needs read before it is designed

| lever | the change it would be | what must be known first | read by |
|---|---|---|---|
| **(a) Logs sampling** | `head_sampling_rate` below 1 on `[env.production.observability]` (`proxy/wrangler.toml` line ~400, today 1.0) | (i) every tool in this repo that reads Workers Logs or tails the proxy, and what it assumes about the rate; (ii) which events must survive sampling, starting with the reviewer's two | section 3 |
| **(b) KV reads** | a cache in front of the hottest keys (the Cache API, or an isolate cache with a stated TTL) | which keys the 3.5 reads per request are: by route, from the source, then weighted by Analytics Engine's per-route counts | section 4 |
| **(c) KV writes** | fewer or batched writes on production's namespace | which code paths write: from the source (16 `put` sites in `proxy/src`), weighted the same way | section 4 |
| **(e) `/wx` F1-F3** | F1, the Cache API on `latest`; F3, the owner's | `docs/wx-cost.md` as it stands | its own PR |

## 3. Lever (a): who reads Workers Logs today (an inventory, read before any design)

A sampling rate below 1 changes what every one of these tools sees: a count read from Logs becomes a sample.

- **Workers Logs readers found by a first search of `scripts`, `proxy`, `tools`** (2026-11-13 ~00:20Z):
  - `tools/wx/`: `r3p.py`, `r3_reads.py`, `r3_minute_join.py`, `r3_5b.py`, `logver.py`, `m_clock.py`, `s_clock.py`,
    `s_drive.py`, `s_reads.py`, `share_range.py`, `account_cost_read.py`;
  - `tools/budget/`: `budget_read.py`, `logs_bytes.py`.
- **Widened before step 2 reads anything** (amendment (3), the reviewer, 00:19:28Z):
  - also searched: `.github/`, `dashboard/`, and every runbook in `docs/` that tells a person to read Workers Logs or
    tail the proxy;
  - CLAUDE.md's own "the first production tail after the deploy";
  - `proxy/scripts/confirm-deploy.sh` and `proxy/scripts/smoke-prod.sh`, each read for what they observe;
  - **every `wrangler tail` user** gets its one line too.
- **A named alternative to settle in step 3, not an assumption:** whether `head_sampling_rate` affects `wrangler
  tail` at all. It is stated from Cloudflare's documentation, with the passage cited. If tail is unaffected, the tail
  users keep working at any rate, and only the Logs readers change.
- **The proxy's own event lines:**
  - `evt:"breaker"`, at `proxy/src/upstreams/types.ts:85`;
  - `evt:"upstream_skip"`, at `proxy/src/upstreams/chain.ts:350` and again at `proxy/src/weatherscope/fetch.ts:42`.

  None of them is in Analytics Engine.

**Before (a) is designed, each reader gets one line:** what it reads (counts, or individual events), whether it is
proxy traffic or another Worker's, and what a rate of r does to it. A tool reading another Worker (an ingest) is
untouched, because the setting is per Worker.

**The condition, in either form:**
- **(a1) The two lines stay unsampled.** As understood so far, head sampling keeps or drops an invocation's logs
  whole, so a single line cannot be exempted. If Cloudflare's documentation confirms that (read in step 3), keeping
  them unsampled means moving them out of Workers Logs, which is (a2).
- **(a2) The two lines move to Analytics Engine,** with a control. A deliberately tripped breaker, on staging, must
  appear as an Analytics Engine row exactly once per transition, as `test/` already asserts for the log line, and be
  read back by the same query a person would run.
  - **That staging control is a deploy** (amendment (4)). When step 3 reaches it, it needs a slot from the reviewer
    and the deployer lock confirmed first, before any `deploy.sh` runs, dev or staging included.

The design picks (a2) unless the reading in this section finds a reason not to.

## 4. Levers (b) and (c): the KV model (built from the source, checked against the measured total)

**The model:** for each route in `KNOWN_ROUTES` (`proxy/src/metrics.ts:86`, which now includes the `/wx/<product>`
routes, so they are counted separately and not in the mixed `/other`), the KV gets and puts the handler performs, read
from the source, including any per-isolate caching. Then:

- KV reads a month = the sum over routes of (gets per request x that route's monthly requests, from Analytics Engine);
- the same for puts.

**The window and the counts** (amendment (2)):
- The per-route weights and the measured KV side come from **the same 7-day window**, stated by its start and its
  end (UTC).
- Analytics Engine samples at volume, so a route's request count is **`sum(_sample_interval)`, never `count()`**.

**The measured side is the PROXY's namespace, not the account** (amendment (1)):
- `docs/account-budget.md`'s 7.226 M is account-wide. Other Workers (the ingests, the weather server, basemap-builder)
  and staging's `ENRICH_KV` count toward it, while the model covers only the production proxy. A proxy model checked
  against an account total could pass or fail for reasons outside the model.
- So KM1 and KM2 are graded on **production `ENRICH_KV`'s own operations**, from the GraphQL KV operations dataset
  split by namespace id. The account total is printed beside them.
- If the per-namespace split cannot be read, that is said. Then either the model covers every Worker that binds KV,
  or KM1 and KM2 read **INSTRUMENT**.

**The check that makes the model usable** (frozen now, graded when it is built):

| row | predicted | if not |
|---|---|---|
| **KM1** | the model's KV reads are within **+/-20%** of production `ENRICH_KV`'s measured reads, in the same window | the source reading has missed a read path: find it before designing (b) |
| **KM2** | the model's KV writes are within **+/-20%** of production `ENRICH_KV`'s measured writes, in the same window | the same, for (c) |
| **KM3** | one or two key families account for **more than half** of the reads | if none does, no single cache moves the line, and (b)'s design says so |
| escape | anything the rows above don't name | stop and report |

**What is already known from the source** (2026-11-13 ~00:20Z): `ENRICH_KV` is the only KV namespace the proxy
binds. Its operations are spread over `enrich.ts` (14 references), `leaderboard.ts` (12), `enroll.ts` (7),
`activation.ts` (5), `revocation.ts` (4), `basemap/handler.ts` and `photos.ts` (3 each), `airports.ts`, `fleet.ts` and
`provision.ts` (2 each), and `config.ts` (1).

## 5. Order and size

| step | what | size |
|---|---|---|
| 1 | this registration | 0.25 d |
| 2 | section 3's inventory, one line per reader; section 4's model, graded KM1-KM3 | 1 d |
| 3 | (a)'s design: the Analytics Engine move with its control, then the sampling rate, with the account total at 50 / 200 / 600 boards | 1.5 d |
| 4 | (b)'s and (c)'s designs from the model, each with its account total | 1.5 d |
| 5 | (e): F1, measured | 1 d |
| | **total** | **about 5.25 d** |

## 6. CI for this registration (predicted before the push)

Docs only, from main `e03e6c4d1903`'s workflows: **32 = 30 / 2 / 0**:
- firmware 30 (28 pass; version and promote skip);
- device-ids 1;
- no-traffic-captures 1.

The path-filtered workflows (account-budget, workers, web-renderer, print-cards, provisioner) do not fire on
`docs/proxy-bill.md`.

## Results

### Step 2a: the Workers Logs and tail inventory (2026-11-13 ~00:30Z, read-only, from the tree at `3eda7b1f7451`)

Searched: `scripts/`, `proxy/` (scripts and src), `tools/`, `dashboard/`, `.github/`, the docs, and CLAUDE.md. A
sampling change on the proxy reaches a reader only if it reads **the proxy's** logs. The setting is per Worker, so a
reader of an ingest is untouched.

| reader | reads | Worker | what a proxy rate r does |
|---|---|---|---|
| `tools/budget/budget_read.py` | event counts | the proxy, and every other Worker | the proxy's Logs events become a sample: scale by 1/r, or read Logs volume from billing |
| `tools/budget/logs_bytes.py` | event bytes | the proxy, and `blipscope-wx-ingest` | the proxy's bytes per event are unchanged; its event count is a sample |
| `tools/wx/account_cost_read.py` | event counts and bytes | the proxy, and `blipscope-wx-ingest-opuk` | as `budget_read.py` |
| `tools/wx/logver.py` | version ids per product line | any Worker named as its argument; used on the ingests | none, unless pointed at the proxy |
| `tools/wx/r3p.py`, `r3_reads.py`, `r3_minute_join.py`, `r3_5b.py` | the ingests' product lines, CPU | `blipscope-wx-ingest*` (`r3_5b.py` also `blipscope-wx`) | none |
| `tools/wx/share_range.py` | product lines | `blipscope-wx-ingest-opuk` | none |
| `tools/wx/m_clock.py`, `s_clock.py`, `s_reads.py` | product lines | `blipscope-wx-ingest-dev` | none |
| `tools/wx/s_drive.py` | a `wrangler tail` | `blipscope-wx-ingest-dev` | none |
| `proxy/scripts/confirm-deploy.sh` | `/healthz` only | the proxy | none: no logs read |
| `proxy/scripts/smoke-prod.sh` | HTTP responses only | the proxy | none: no logs read |
| CLAUDE.md (lines ~588, ~607): a person's production `wrangler tail` | individual events | the proxy | **depends on step 3's answer: whether head sampling applies to tail** |
| docs runbooks (about 20, e.g. `docs/basemap-deploy.md`, `docs/wx-server.md`) | tails and Logs reads | mostly the ingests and the basemap Workers | each checked per Worker in step 3; a proxy tail depends on step 3's answer |

- **False positive, excluded:** `scripts/wx-fb-check.py`. Its "tail" is the radar sweep beam's.
- **`.github/` and `dashboard/`** read no Workers Logs and run no tail.
- **So a proxy sampling rate below 1 changes three tools' numbers** (`budget_read.py`, `logs_bytes.py`,
  `account_cost_read.py`), all of them counts that can be scaled. It also changes every person's proxy tail, if
  sampling applies to tail.

### Step 2b: the KV model, from the source (in progress)

The per-request floor, read from `proxy/src/index.ts` and its imports:
- **Every authenticated device request reads `fw:<device>` once** (`recordFleetFirmware`, `proxy/src/index.ts:384`
  and `proxy/src/fleet.ts`, inside `waitUntil`). It writes back at most hourly per device (`REFRESH_MS` = 1 h), or on
  a firmware change.
- **Revocation** reads one aggregate key per isolate per 60 s (`proxy/src/revocation.ts`, `CACHE_TTL_MS`).
- **Activation's** `act:<id>` read is on the enrolment and activation paths only, not on every request.
- **The rate limiter** touches no KV.

The route handlers' own reads (enrich, config, airports, leaderboard, photos, basemap) come next. Then the per-route
weights and production `ENRICH_KV`'s measured operations, in one stated window, and the grading of KM1-KM3.

## Amendment (5), the range model (the reviewer, accepted ~00:23Z with conditions (i)-(iv); written before the read)

**Why the change.** `/enrich`'s KV reads are branchy (about 1 to 16 a request, from cache hits and data completeness
that the source does not contain), so section 4's "model within +/-20%" of a fixed count cannot be graded honestly.

### The table: KV reads and writes per request, by route, from the source (`proxy/src` at `3eda7b1f7451`)

**Terms that apply to every authenticated device request** (after `limitByKey`, `proxy/src/index.ts:384`):
- `fw:<device>`: **1 read** (`recordFleetFirmware`).
- **Revocation** (`cfg:revoked`, `proxy/src/revocation.ts`): at most once per request, and only when the isolate's
  60 s cache is stale, so **[0, 1] read**. The isolate count is not in the source, so it stays a range per request;
  it needs no per-time term, because it can never exceed one read per request.

| route (Analytics Engine `index1`) | reads per request [low, high] | the branches | writes per request [low, high] |
|---|---|---|---|
| `/api/v1/blipscope/blips`, `/v1/blips` | **[1, 2]** | the floor only; the handler reads no KV | the `fw:` write, below |
| `/api/v1/blipscope/config`, `/v1/config` | **[2, 3]** | + `cfg:fleet` (`config.ts`, not cached) | 0 |
| `/api/v1/blipscope/airports`, `/v1/airports` | **[1, 32]** | + 0 on a Cache API hit; up to `MAX_TILES` = 30 tile reads on a miss (`airports.ts`) | 0 |
| `/api/v1/blipscope/enrich`, `/v1/enrich` | **[1, 16]** | low: a non-ICAO hex returns before any KV. High: `ac:<hex>` 1; on an `ac:` miss, up to 2 `tn:` (`buildMeta`, `resolveMeta`); the route key 1, its mirror on a miss 1, 2 `ap:` for plausibility; `ovr:` -> `pa:` -> `mil:` up to 3, only when identity or operator is missing; `tn:<type>` 1 when the name is missing; `resolvePhoto` 1-3 (hex, type, base type) | `ac:` on a miss [0, 1]; the route key on a miss [0, 1] |
| `/api/v1/blipscope/photo`, `/v1/photo` | **[2, 3]** | + the photo's bytes (`photos.ts`) | 0 |
| `/api/v1/blipscope/wx/<product>` (13 routes) | **[1, 2]** | the floor only (R2, not KV) | 0 |
| `/api/v1/weatherscope/{alerts,conditions,forecast}` | **[0, 2]** | the floor, if the request carries a device id and firmware; no KV of their own | 0 |
| leaderboard **submit** (`POST /api/v1/blipscope/leaderboard`) | **[3 + T, 3 + T + L_dev + L_type]** | `lb:dev` 1, `lb:firsttype` per claimed type T, then `buildBoard`: one read per `lb:dev:` row (L_dev) and per `lb:firsttype:` row (L_type), plus 2 lists (a separate operation) | `lb:dev` 1, `lb:firsttype` [0, T], `lb:board` 1 |
| leaderboard **pages** (`/blipscope/leaderboard`, `.json`, `/leaderboard`, `/v1/leaderboard`) | **[1, 1 + L_dev + L_type]** | `lb:board` 1; `buildBoard` only if `lb:board` is missing; not authenticated, so no floor | 0, or 1 when rebuilt |
| `/blipscope/enroll`, `/enroll` | **[0, 7]** | `enroll.ts`'s reads (rare) | [0, a few] |
| `/`, `/blipscope/support`, `/other` | **[0, 2]** | pages: none. `/other` also holds rare admin, provision and activation paths, and the basemap store read (`basemap/handler.ts`, `cacheTtl` 60 s): [0, 1] | [0, a few] |

**Writes that are per time, not per request:**
- `fw:<device>`: at most once per device per hour (`REFRESH_MS`), plus once per firmware change. Over a window of H
  hours: **[0, N_dev x H + changes]**, where N_dev is the distinct devices in the window (Analytics Engine's `blob5`).
- **Every other write sits in a row above.** No term is unbounded: L_dev and L_type are key counts, read in the
  window as counts only.

### The checks, graded after one read (one stated 7-day window)

- **The weights:** each route's requests are `sum(_sample_interval x double4)`, because the proxy samples its own
  cache hits at 0.1 and writes the weight in `double4` (`proxy/src/metrics.ts:238-241`). Never `count()`.
- **The measured side:** production `ENRICH_KV`'s reads and writes, from `kvOperationsAdaptiveGroups` filtered by
  its `namespaceId`. The account total is printed beside them.

| row | the check | what a pass proves |
|---|---|---|
| **KM1'** | measured reads inside **[0.8 x sum(low), 1.2 x sum(high)]** | The band's width and its midpoint are printed. A missed read path of x reads per `/blips` request shows only if measured > 1.2 x sum(high), so x_min = (1.2 x sum(high) - measured) / R_blips is printed. A pass is reported as **"consistent with the source to within x_min a /blips request"**, not as a validated model |
| **KM1''** | a HYBRID, graded at **+/-20%** of measured reads: **point rates where Analytics Engine records the branch, and the range's midpoint only where nothing does** | Point rates, all from the same window: `/enrich`'s `ac:` HIT/MISS (`blob2`, `enrich.ts:149/152`; SKIP at :585); its `enrich_gap` rows (`type`, `name`, `photo`, so `resolvePhoto`'s depth where the gap is `photo`). Ranges remain for `/airports` (its Cache API state is not recorded: only `blips.ts` and `enrich.ts` set `meta.cache`), the route key and its mirror (not recorded), the `ovr:`/`pa:`/`mil:` chain (not recorded) and revocation's [0, 1]. If a recorded branch turns out not to be readable, KM1'' says so, and that branch falls back to its range |
| **KM2'** | measured writes inside [0.8 x sum(low), 1.2 x sum(high)], with the `fw:` per-time term | as KM1': the width and x_min for writes |
| **KM2''** | the hybrid for writes, graded at +/-20%: `ac:` writes = `/enrich` misses (recorded); `fw:` = N_dev x H as the bound | as KM1'' |
| **KM3** | **a claim about the MODEL:** a key family passes as "over half the reads" only if its share is above half at **both** ends of the range | `kvOperationsAdaptiveGroups` documents `namespaceId`, `actionType` and `date`, and no key or key prefix (developers.cloudflare.com/kv/observability/metrics-analytics/, read 2026-11-13: "This dataset consists of the operations made to your KV namespaces"). The read's first step is a schema introspection of the dataset (no data). If it shows a key dimension, KM3 is measured instead |
| escape | anything the rows above don't name | stop and report |

**My expectation, written so it can be wrong:** the `fw:` floor (one read per device request, and `/blips` is most of
the requests) is the family that passes KM3 at both ends.
