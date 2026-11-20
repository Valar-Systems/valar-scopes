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

- **The weights, per row family** (`blipscope_proxy` holds several kinds of row; the reviewer, 00:29:41Z):
  - **Request rows:** `sum(_sample_interval x double4)`, over rows whose `index1` (= `blob1`) is a `KNOWN_ROUTES`
    route. The proxy samples its own cache hits at 0.1 and writes the weight in `double4`: 10 when sampled, 1
    otherwise (`proxy/src/metrics.ts:238-241`).
  - **`err5xx` rows** (`doubles: [status]` only, no `double4`) are excluded by that filter, so a failed request is
    never counted twice.
  - **`enrich_gap` rows** write `doubles: [1]`, a constant that is not a sampling weight (`metrics.ts`,
    `recordEnrichGap`). They are weighted by `_sample_interval` alone.
  - Never `count()`.
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

### Step 2b's read and its grades (2026-11-13 00:33Z; window `[2026-11-06T00:00:00Z, 2026-11-13T00:00:00Z)`)

**The instrument:** `tools/bill/kv_model_read.py` (read-only; the `analytics-read` token from the token file, never
printed; counts only).
- `tools/token_file.py` knows no KV-read purpose, so the leaderboard's two key counts (`lb:dev:` 6,
  `lb:firsttype:` 290) came from `wrangler kv key list --remote` on production's namespace, piped into a counter.
  Both counts were non-zero, which shows the remote store was read.
  - **Corrected 2026-11-13:** no key name reached the SCREEN, but every name reached DISK. wrangler 4.35.0 appends
    every message it logs, including the key list it prints, to its own debug log in `~/.wrangler/logs/`
    (`src/utils/log-file.ts`; there is no off switch, and `WRANGLER_LOG_PATH` only moves the file). This run left
    the 6 `lb:dev:` names, which are device ids, in one file there. Removing it is the owner's call.
  - **Ruled (the reviewer, 00:44:37Z):** a key count from now on goes through a reader that prints only the count.
    The reader sets `WRANGLER_LOG_PATH` to a `.log` file in a fresh temp dir it creates and removes on exit. Its two
    controls: the name-bearing files in the default folder are counted before and after and must match, and the temp
    dir must be gone after exit. The reader's sha goes here. No key list runs until it exists.
- **(iv), confirmed by the schema introspection:** `kvOperationsAdaptiveGroups`' dimensions are `actionType`, `date`,
  `datetime` (and its 5-minute, 15-minute, hour and minute forms), `namespaceId`, `responseStatusCode` and `result`.
  There is no key dimension, so KM3 is graded as a claim about the model.

**What was read:**
- Production `ENRICH_KV`: **1,696,591 reads, 76,264 writes, 1,248 lists**.
- The account: 1,697,293 reads and 141,955 writes, so staging holds about 65 k of the writes.
- Requests by group (`sum(_sample_interval x double4)`): `/blips` 314,680; `/enrich` 164,688; pages and `/other`
  3,775; `/airports` 849; leaderboard submit 577; `/photo` 555; `/config` 531; `/wx` 427; enroll 46; leaderboard
  pages 39; Weatherscope 3.
- `/enrich`'s `ac:` cache: HIT 146,740, MISS 7,239, SKIP 10,704, plus an empty state. Gaps: type 2,576, name 12,
  photo 8,106. Distinct devices: 22.

**Grades, as registered:**

| row | result | grade |
|---|---|---|
| **KM1'** | measured 1,696,591 inside [387,668, 4,385,954]; the band's width is 193% of its midpoint | **pass, weak:** consistent with the source to within **x_min = 8.55 reads a `/blips` request**, not a validated model |
| **KM1''** | the hybrid 2,080,509 against the measured 1,696,591: **+23%** | **FAIL** (+/-20%) |
| **KM2'** | measured 76,264 inside [923, 615,622] | pass, weak |
| **KM2''** | the hybrid 178,098 against the measured 76,264: **+134%** | **FAIL** |
| **KM3** | `fw:` is 100% of the low end and 13% of the high end | **FAIL:** no family is over half at both ends |
| escape | six routes the table did not name: `/credits` (27 requests, 1 KV read each), `/healthz` (91), and `/missileer/{archive,leaderboard,log,sources}` (340); the last two kinds read no KV | **reported, not folded in:** 27 reads of 1.7 M |

**What the misses say. These are explanations, not re-grades:**
- **KM1''** is over by about 384 k. Its largest unrecorded midpoint is revocation's 0.5 a request, about 241 k on its
  own; with a 60 s isolate cache, the true figure is likely near 0. The others are midpoints too: the route key and
  its mirror, the `ap:` pair, and the `ovr:`/`pa:`/`mil:` chain.
- **KM2''** comes almost entirely from two midpoints: the leaderboard's T at 145 of 290 (about 84 k), and the route
  key's writes at 0.5 a `/enrich` request (about 82 k). The true values are far lower.
- **The measurement settles what KM3 could not.** Every device request reads `fw:` exactly once, and device requests
  total about 482 k, so **`fw:` is about 28% of production's reads**. Nearly all the rest belong to `/enrich`: about
  **7.4 reads per `/enrich` request**, inside its registered [1, 16], if revocation is near 0. So lever (b) has two
  targets: `fw:` (one read a request, cacheable per isolate against `REFRESH_MS`) and `/enrich`'s lookup chain.
  The design (step 4) is registered with its own predictions.

## Step 4a: lever (b1), the `fw:` isolate cache (design; predictions frozen 2026-11-13 ~00:55Z; nothing built or deployed)

**The ruling on what step 2b's fails mean** (the reviewer, 00:36:48Z):
- KM1'''s registered "if not" is "a missed read path". A missed path pushes the measurement ABOVE the model, but the
  reading is BELOW it, so the escape branch was taken. The reviewer decided it:
  1. **`fw:` does not rest on the model.** It is one read per device request, from the source (`proxy/src/index.ts:384`
     to `fleet.ts:107`), times the measured requests. It is designed now, below.
  2. **`/enrich`'s chain is not designed from a midpoint.** Its design starts with an instrumentation proposal:
     Analytics Engine counts per branch (route cache hit or miss, the `ovr:`/`pa:`/`mil:` depth, revocation's
     isolate-cache misses). It is registered first, and its deploy is the owner's call. "About 7.4 reads per `/enrich`
     request" holds only if revocation is about 0, and is stated as that, not as a finding.
  3. **(c) waits the same way,** for measured write paths (the leaderboard's T, the route key's writes).
- **Order:** this design first. Step 3, which is reading only, runs while it is in review.

### What `fw:` costs now

- **Where the read is.** `recordFleetFirmware` (`proxy/src/fleet.ts:92`) runs on every request that passes auth and
  the per-key limit with both `X-Blip-Device` and `X-Blip-FW` set. It always issues one `ENRICH_KV.get("fw:<dev>")`.
- **When it writes.** Only when the record is new, the firmware changed, or the stored `lastSeen` is older than
  `REFRESH_MS` (1 h).
- **Its share.** In step 2b's window, the device routes made 482,349 requests against 1,696,591 production reads:
  **28.43%**. That is an upper bound. The 482,349 is the device routes' total, not filtered on both headers being
  present or on the key limiter's 429s. The exact baseline is the first thing the post-deploy read prints.
- **Per board.** On `docs/wx-account-cost.md` section 6's 401,000 KV reads a board a month: **about 114,000 `fw:` reads
  a board a month.**

### The change: a memo in front of the read, per isolate

```ts
// Per isolate: device -> the fw and lastSeen of the STORED record, as this isolate last read or wrote it.
const now = Date.now();
const m = memo.get(dev);
if (m && m.fw === fw && now - m.lastSeen < REFRESH_MS) { meta.fwKvRead = 0; return; }   // no KV call at all
meta.fwKvRead = 1;
ctx.waitUntil(/* today's read / compare / write, unchanged; then memo.set(dev, { fw: stored fw, lastSeen: stored or written lastSeen }) */);
```

- **What it remembers.** `memo.lastSeen` is the STORED `lastSeen`, not the request's time. So the memo expires
  exactly when the stored record's refresh falls due, and the read at that moment does the write today's code would
  have done.
- **Its bound.** The map is cleared at 4,096 entries; 600 boards are far below that.
- **Testable as two isolates.** It is built as `makeFleetRecorder()` with its own memo, so a test can hold two
  "isolates" over one fake KV.
- **What it does not add.** No log line, and no Analytics Engine data point (see the instrument below).

**What the KV read establishes, and what the memo does with each** (CLAUDE.md, "when you add a second path"):

| the read establishes | under the memo | consequence |
|---|---|---|
| the stored `fw` equals the reported one | it equals what THIS isolate last saw stored; another isolate may have written a change since | **The A-B-A case:** a device reports v19, then v20 on isolate B, then v19 again on isolate A within the hour. The v20-to-v19 transition is recorded LATE, by at most `REFRESH_MS`, when A's memo expires. It is never lost: the next read anywhere sees the stored v20 and appends it. Its `at` is the late time |
| the stored `lastSeen` is fresh | it is as of this isolate's last read or write; another isolate's later refresh is not seen | the memo expires early, never late: an extra read, never a missed write |
| the record exists | assumed for up to `REFRESH_MS` | a row deleted by hand is re-created up to an hour later, not at the next request |
| no write unless new, changed or stale | unchanged: a write still follows a read that found one of the three | KV writes unchanged, apart from the late A-B-A write |

`lastSeen` as liveness (`scripts/reconcile-fleet.py`) is unchanged: it is still at most `REFRESH_MS` plus the gap to the
device's next request.

**The cost, at the place it is paid: the A-B-A lateness lands on the `fw:` table's readers** (the reviewer, 00:55:29Z).
A device rolled back from v20 to v19 can show v20 in the table for up to an hour, if the isolate it returns to still
remembers v19 and another isolate recorded the v20. Every reader in the tree (found by grepping for `fw:`, `fleetKey`
and `FleetRecord`):

| reader | what it reports from `fw:` | what up to 1 h of A-B-A lateness changes |
|---|---|---|
| `scripts/reconcile-fleet.py`, the Instrument A block | the firmware spread; the `lastSeen` ages; the change-entry count (Run 1's baseline: "look for a NEW entry") | the spread can count a rolled-back device under the version it left, for up to 1 h. The rollback's change entry arrives up to 1 h late: counted late, never lost. The ages do not change |
| `scripts/reconcile-fleet.py`, "Behind version.txt" (`ota_lines`) | devices below the published version, by their row's `fw` | a device rolled back below the published version can read "not behind" for up to 1 h |
| `proxy/scripts/activation-coverage.ts` | `lastSeen` only | nothing |
| `scripts/ota-latest-probe.sh`, `scripts/ota-download-count.sh` | no `fw:` read of their own; both point at Instrument A, which is read through `reconcile-fleet.py` | as `reconcile-fleet.py` |
| `proxy/src/enrich.ts` (a comment), `proxy/test/publish-guard.test.ts` (`fw:1` as a key name) | no read of the table | nothing |

**Where the reader sees it:** the code PR adds one line to `reconcile-fleet.py`'s Instrument A block, per CLAUDE.md's
premise rule. In substance it says: since the `fw:` memo deployed (#688 step 4a), a device that returns within the hour
to a firmware it reported before can show the one it left for up to 1 h, so a "still on v20" row during a rollback may
be up to an hour behind.

**Ruled on the design** (the reviewer, 00:55:29Z):
- **Accepted:** the memo keyed to the stored `lastSeen`; C1-red, and C4 as an instrument independent of the counter.
- **The budget line lands with the code PR.** A hand-added row would be overwritten, and a rebuild now would churn every
  other figure for an estimate that is not deployable yet. The account totals below cover the owner's rule meanwhile.
  `build_budget.py`'s scenarios are shared with uk-data's work, so blipscope-50 is told before the code PR touches them.
- **One deploy** is recommended: C1 and C1-red prove the counter's wiring off production, and C4 catches an inverted or
  misplaced counter in production.
- **Whether the memo deploys at all is the owner's call.** It trades up to 1 h of `fw:` lateness, in the table above,
  for the saving. The question goes to him once the code PR is green, with the cost table, and not before.

### The instrument: one double, appended

- **The field.** `RequestMetric` gains `fwKvRead` (0 or 1), set synchronously at the decision above, so `record()`
  sees it.
- **On request rows, as `double5`, appended** (`metrics.ts:233`'s append-only rule). It adds no data point, so it
  costs nothing in Analytics Engine.
- **No collision.** Every query that reads `double5` to `double8` today belongs to the usage family
  (`dashboard/src/analytics.ts:302-305`, `proxy/scripts/usage-stats.ts:73-184`). The code PR shows each is scoped by
  `blob1 = 'usage'` before it lands.
- **The count.** `fw:` reads = `SUM(_sample_interval * double4 * double5)` over request rows; `double4` carries the
  1:10 sampling of hit rows.
- **Its denominator** is the requests that reached the decision: the same sum without `double5`, over rows with
  `blob5 != ''` and `blob6 != ''`.
- **r** = `fw:` reads per device request. Before the change r = 1, by the source, less the key limiter's 429s.

### Controls, before anything is believed

Each request's `waitUntil` is settled before the next in the vitest controls.

| control | where | must |
|---|---|---|
| **C1:** the counter cannot drift from the reads | vitest, with a spy on `ENRICH_KV.get` | over a scripted sequence (20 requests with one fw inside the hour, then a fw change, then the clock past `REFRESH_MS`), the spy's `fw:` gets equal the sum of `fwKvRead`, and both are **3** |
| **C1-red** | the same test, with the memo check planted out (the plant asserted to apply) | fails with gets = **22** |
| **C2:** a change is never lost | vitest: two recorders (two isolates) over one fake KV, the A-B-A sequence | the stored `changes` holds both transitions; the second's `at` is no later than A's memo expiry |
| **C3:** the bound | vitest | after 4,097 distinct devices the memo holds at most 4,096 |
| **C4:** two instruments agree | production, after the deploy | GraphQL's production reads per device request drop by (1 - r) within the before-to-before spread. A counter that is inverted, or wired to the wrong branch, disagrees here |

### Predictions (frozen now; graded on the first read after the owner's deploy)

- **The window:** 24 h, starting 1 h after the deploy.
- **The spread:** two 24 h windows before the deploy, read the same way. Their difference in production reads per
  device request is the spread that F2 and C4 are read against.

| row | predicted | if not |
|---|---|---|
| **F1** | **r <= 0.05**: at most one read per device per isolate per hour, plus the reads in flight while an isolate first sees the device | **0.05 < r <= 0.5:** the lever is real, but a device's requests spread over many isolates. The read prints the implied isolates per device-hour (r x the device's requests an hour) and the account totals at the measured r. **r > 0.5:** the memo rarely hits (isolates short-lived, or many); the named alternative below is designed instead |
| **F2** | production KV writes per device request unchanged, within the spread | the memo touches a write path this design missed: stop and find it |
| **F3** | Workers Logs events per request unchanged (no line is added) | stop: the change logs something it was not meant to |
| escape | r = 0 or r > 1; C4 disagrees beyond the spread; anything the rows above do not name | the instrument is unreliable at this scale: stop and report, with no reading |

**Why r <= 0.05 and not a range.** A device's requests come from one address, so one colo, and a Worker at this volume
runs on few isolates there. That is a belief, not a measurement, which is why F1 carries three branches.

### The account at the predicted r and at the middle branch

KV reads only; every other line is unchanged. The saving is N x 114,000 x (1 - r) reads a month at $0.50 a million,
while the total stays above the free 10 M. The bill is `docs/wx-account-cost.md` section 6's (Read P, November, no
Weatherscope, without the ten).

| boards | KV reads line now | at r = 0.05 | at r = 0.5 | the account now | at r = 0.05 | at r = 0.5 |
|---|---|---|---|---|---|---|
| 17 (today) | $0 | $0 | $0 | $5.29-$5.55 | $5.29-$5.55 | $5.29-$5.55 |
| 50 | $5.04 | $2.33 | $3.62 | $15.07-$16.15 | $12.36-$13.44 | $13.64-$14.72 |
| 200 | $35.13 | $24.30 | $29.43 | $117.54-$123.99 | $106.71-$113.16 | $111.84-$118.29 |
| 600 | $115.38 | $82.89 | $98.28 | $393.65-$413.11 | $361.16-$380.62 | $376.55-$396.01 |

**Today's allowance, the near-term reason** (section 1). The account's KV reads are at 72% of the free 10 M, and `fw:`
is 28.43% of production's. At r = 0.05 that falls to about **53%**; at r = 0.5, about **62%**. It moves the crossing.
It changes nothing on the bill below 10 M.

**The budget line** (the owner's rule, `docs/account-budget.md` section 0). `tools/budget/build_budget.py` assembles
`docs/account-budget.json` from fresh reads, so a hand-added row would be lost at the next build. The proposed line, in
the code PR:
- the proxy gains a state `fw-memo`. Its rows are its `r-7d` rows under a read `proj-fw-memo`, with `kv_read` x
  (1 - 0.2843 x 0.95);
- every scenario names the proxy's state (`now`), and one new scenario, "today + fw memo", names `fw-memo`.

Whether it lands there or with this design is the reviewer's decision.

### What it needs, and whose call each part is

- **The code PR:** `fleet.ts` (the memo, `makeFleetRecorder`), `metrics.ts` (`fwKvRead`, `double5`), tests C1-C3 with
  C1-red's plant, and `build_budget.py`'s rows. About 0.5 d.
- **The deploy is the owner's call,** with a slot from the reviewer and the deployer lock confirmed first.
- **The two pre-deploy windows** for the spread can be read at any time before it, read-only.

### Named alternatives, not chosen

- **The Cache API, per colo, on a synthetic URL.** It survives isolate churn within a colo and is not a KV operation;
  its billing is read from Cloudflare's documentation before it is designed. It adds a cache round trip to every
  device request and brings its own staleness rules. It is F1's fallback, not the first choice, because the memo makes
  no network call at all.
- **Liveness from Analytics Engine.** Request rows already carry `dev` and `fw`, so this would drop the read entirely.
  It changes `reconcile-fleet.py`'s source and gives up the change log beyond Analytics Engine's retention. It also
  reverses a recorded choice: `fleet.ts`'s header picked KV on purpose, for a small current-state table with a change
  log, readable with the deploy's own credential. Not proposed here.

### CI for this push (predicted before it)

Docs only, against main `e03e6c4d1903` (unmoved since the registration): **32 = 30 / 2 / 0**, as section 6.

## Step 3: lever (a), the documentation read (2026-11-13 ~01:00Z; reading only)

**Read:** Cloudflare's documentation, through its documentation search, 2026-11-13 ~00:58Z. Each page is cited by
heading; one short quote, the rest paraphrased.

| section 3's question | what the documentation says | where |
|---|---|---|
| Does head sampling keep or drop a request's logs whole? | **Whole.** The rate decides which requests are logged, and for a logged request "All logs within the context of the request are collected." A single line cannot be exempted | Workers Logs, "Head-based sampling" |
| Does `head_sampling_rate` affect `wrangler tail`? | **Not stated, either way.** The configuration reference describes the setting under the persisted Workers Logs. The real-time logs page says real-time logs do not store Workers Logs, and that a busy Worker's real-time logs can enter a sampling mode of their own and drop messages. Neither page says whether the head rate applies to a tail | Wrangler configuration, "Observability"; Real-time logs, "Limits" |

**Graded against section 3:**
- **(a1)'s premise is confirmed.** Keeping `evt:"breaker"` and `evt:"upstream_skip"` unsampled under head sampling
  means moving them out of Workers Logs, which is (a2).
- **The tail question is OUTSIDE its registered pair** (affected, or unaffected). The documentation is silent, so the
  answer is stated as neither. Settling it takes an observation: a tail on staging at a rate below 1, with its lines
  counted against Analytics Engine's request rows for the same minutes. That is a staging deploy (a slot from the
  reviewer, the deployer lock confirmed first). It is proposed here, not run.

**What the documentation adds that section 2 did not name** (named alternatives; not designs yet):
- **(a3) `invocation_logs = false`.** Each invocation writes one invocation log (the request, the response and their
  metadata), and this setting turns it off while every `console.log` line stays (Workers Logs, "Invocation Logs").
  That removes one of the proxy's 3.175 events per invocation with no sampling at all, so the breaker and skip lines
  stay unsampled by construction. Its cost is whatever reads the invocation log's fields today (status, path, timing);
  step 2a's inventory has to say so per reader before it is designed.
- **(a4) Choosing lines in code.** The proxy writes one `evt:"req"` line per request (`proxy/src/metrics.ts:229`), and
  Analytics Engine already holds the same request as a row. Writing it only for errors, or sampling it in code, keeps
  every breaker and skip line, since each is a line of its own. Its cost: the Logs readers that count `req` lines.
- **From 2026-12-01 the unit is bytes.** Workers Logs moves to Cloudflare Observability pricing. On Paid, ingestion
  beyond 50 GB a month is $0.25 a GB, and storage beyond 10 GB-month is $0.10 a GB-month (Observability, "Pricing", its
  worked example). So a lever's December saving is its share of BYTES, not of events, and the events differ in size.

### The next read, registered before it runs: the proxy's Workers Logs events by kind

**What it reads.** One whole hour of the proxy's Workers Logs events, from the Workers Observability API, the way
`tools/budget/logs_bytes.py` does. Each event is counted and sized ("API bytes", as that tool defines them) by kind:
- the invocation log;
- `evt:"req"`;
- each other `evt` value the source writes (`upstream`, `upstream_skip`, `breaker`, `enrich_gap`, `tn_miss`, `lb_clamp`,
  `incoherent_row`, `hold`, `error`, `nws`, `missileer_unconfigured`, `provision_mint`, `activation_admin`, `bm_build`);
- the plain-text lines (`route_stale`, `enroll`);
- anything else, as one "unnamed" bucket.

The proxy's invocations in the same hour come from the analytics API. Read-only; counts and byte totals only; never an
event, a value or a field; the token file's `observability-read` and `analytics-read`, never printed. The tool is
committed with its sha before it runs. It refuses a sampled hour, as `logs_bytes.py` does.

| row | predicted | if not |
|---|---|---|
| **E1** | invocation logs per proxy invocation: **1.00** (+/-0.01); the documentation says one per invocation | the event model is not what the documentation says: stop |
| **E2** | `evt:"req"` lines per invocation: **0.95 to 1.00**. `record()` runs once per fetch request (`proxy/src/index.ts:453`), and non-fetch invocations write none | a request path skips or repeats `record()`: find it |
| **E3** | everything else totals about **1.18** per invocation (3.175 measured, less E1 and E2), and **`upstream` is the largest part of it, at 0.8 or more** | the rest is something the source reading did not weight: report its kinds before any design |
| **E4** | by bytes, the invocation log is the largest kind: **over 40%** of the proxy's Logs bytes | (a3) is worth less than its event share suggests; (a4) is weighed by its own bytes |
| escape | an "unnamed" bucket over 1% of events; kinds summing more than 10% off the measured events per invocation; or a sampled hour | the instrument is unreliable at this scale: stop and report, with no reading |

**Approved** (the reviewer, 01:00:04Z) as registered, under "reading only", on three conditions: classify server-side
where the query can, and otherwise hold events in memory only (no event body, key or value reaches disk, a cache,
stdout or stderr); no wrangler anywhere; the hour is the one ending 01:00Z, `[2026-11-13T00:00Z, 01:00Z)`.

**The reader:** `tools/bill/logs_by_kind.py`, blob **`7e927b41dbaa`**, committed here before it runs.
- **Where it classifies: in memory.** The telemetry API's server-side grouping is not in the documentation found in
  step 3, and no tool in the tree uses it. Byte sizes cannot be computed server-side at all. So events are held in the
  process's memory only, as `tools/budget/logs_bytes.py` on main already does. Nothing is written; the output is
  counts, byte totals, kind names from the reader's own lists, and the counts of `$metadata.type` values shaped
  `[a-z-]{1,30}`.
- **Error paths print the exception's type only** (and an HTTP status), never its message or a traceback, so no event
  content can reach stderr that way.
- **No wrangler.** Tokens: `observability-read` and `analytics-read` from the token file, through `tools/token_file.py`.
- **Run twice, at least 60 s apart.** The two reads must agree before either is graded, because a just-closed hour can
  still be filling. If they differ, that is the moving-target signature: report it, and read again later.

### The events-by-kind read and its grades (the hour `[2026-11-13T00:00Z, 01:00Z)`; reader blob `7e927b41dbaa`)

**Two reads, 01:02:50Z and 01:04:08Z (65 s after the first ended), agree on every figure.** The hour was static, so
it is graded. Both came back unsampled (ABR 1). The proxy only; 2,159 other services' events skipped. stderr held the
token helper's permission note only.

The proxy: **13,889 events, 4,333 invocations (3.205 events an invocation), 24,388,908 API bytes.**

| kind | events | per invocation | bytes | byte share | bytes an event |
|---|---|---|---|---|---|
| invocation log | 4,938 | 1.140 | 15,371,711 | **63.0%** | 3,113 |
| `evt:"req"` | 4,349 | 1.004 | 4,583,395 | 18.8% | 1,054 |
| `evt:"upstream"` | 2,649 | 0.611 | 2,519,494 | 10.3% | 951 |
| `evt:"upstream_skip"` | 1,175 | 0.271 | 1,145,529 | 4.7% | 975 |
| `evt:"breaker"` | 362 | 0.084 | 353,620 | 1.4% | 977 |
| `route_stale` (text) | 338 | 0.078 | 342,119 | 1.4% | 1,012 |
| `evt:"enrich_gap"` | 48 | 0.011 | 45,883 | 0.2% | 956 |
| `evt:"hold"` | 30 | 0.007 | 27,157 | 0.1% | 905 |

`$metadata.type`: `cf-worker` 8,951, `cf-worker-event` 4,938. No `unnamed` or unknown `evt` event.

**Grades, as registered:**
- **E1: OFF.** 1.140 invocation logs an invocation, against 1.00 +/-0.01. Its "if not" is "stop", so the read stops
  here: nothing about (a3) is designed from it. Three alternatives fit, and the data excludes none:
  - (i) the proxy's Durable Object writes invocation logs under the proxy's service, but its invocations are not in
    `workersInvocationsAdaptive`'s `requests`;
  - (ii) other non-fetch triggers are counted differently;
  - (iii) the analytics count is an estimate.

  Separating them would take the invocation logs counted by their trigger field, which is a further read with its own
  predictions.
- **E2: OFF, by 0.004.** 1.004 `req` lines an invocation, against 0.95-1.00. 4,349 `req` lines against 4,333
  invocations is 16 more. Its denominator is the one E1 puts in question, so it is not read further.
- **E3: OFF.** The rest is 1.061 an invocation, against about 1.18. `upstream` is its largest part, as predicted, but at
  0.611, not 0.8 or more. `upstream_skip` (0.271), `breaker` (0.084) and `route_stale` (0.078) make up most of the
  remainder.
- **E4: HOLDS.** The invocation log is **63.0%** of the proxy's API bytes, against over 40%.
- **Escape: not taken.** Unnamed events 0%; 3.205 events an invocation is 0.9% off the measured 3.175; the hour was
  unsampled.

**Recorded, not read:**
- **362 `breaker` events in the hour.** That line is written only when a breaker changes state.
- **1,175 `upstream_skip` events in the hour.**
- Whether these are per-isolate breakers each changing state, or a breaker flapping, is not something this read can
  tell.

### The breaker and skip breakdown (registered 2026-11-13 ~01:15Z, before its reader exists; ahead of the bill, as a health question)

**Why first** (the reviewer, 01:06:17Z): 362 `breaker` events and 1,175 `upstream_skip` events in one hour need
explaining before anything touches those lines. A breaker really flapping on a feed is reported at once.

**What the source says the two lines mean** (`proxy/src/upstreams/types.ts:52-110`, `chain.ts:349-650`,
`weatherscope/fetch.ts:41-127`):
- **The breakers are per isolate:** a module-level `Map`. The threshold is 3 consecutive failures, and the cooldown is
  30 s (`FAILURE_THRESHOLD`, `OPEN_COOLDOWN_MS`).
- **A `breaker` line is written on each transition.** `open` is the third consecutive failure; `reopened` is any
  failure while already open (a half-open probe failing, or a call made anyway); `closed` is the first success after
  open.
- **The chain's TERMINAL feed is called even while its breaker is open** (`chain.ts:427`, `:488`). So a failing
  terminal feed writes a `reopened` on every failed request, not once per 30 s cooldown.
- **`upstream_skip` reasons:** `disabled_by_config` (adsbdb's route, `ROUTE_ADSBDB_ENABLED = "false"` in production,
  `wrangler.toml:367`), `breaker_open` (`adsb_lol_route`, `adsbdb_route`, the NWS ids), and `hold_4xx` (a feed with a
  4xx hold: adsb.fi).

**What each explanation would look like in the data:**

| explanation | `open` | `reopened` | `closed` | over the hour |
|---|---|---|---|---|
| a continuing outage of one source (on a terminal feed, one `reopened` per failed request) | about one per isolate that meets it | nearly all | about none | steady across the hour |
| isolate starts during a continuing outage | one per new isolate | the rest | about none | `open` scattered across the hour |
| **flapping** | many | some | **about as many as `open`** | open and close alternating |

**The read:** the same hour, `[2026-11-13T00:00Z, 01:00Z)`, with the same reader pattern as `logs_by_kind.py` (in
memory, counts only, no wrangler, the token file, error paths print the type only). It prints:
- `breaker` events by (id, state), with each group's count, its maximum `consecutiveFailures`, and its count in each
  10-minute slice of the hour;
- `upstream_skip` events by (id, reason), with counts and the same slices.

Ids, states and reasons are printed only from the source's own lists, and anything else as one `unknown` bucket. The
reader is committed with its sha before it runs, and it runs twice, at least 60 s apart.

| row | predicted | if not |
|---|---|---|
| **B1** | not flapping: `closed` is **at most 10%** of `breaker` events, and **at least 80%** are `reopened` | **B2:** any id with `closed` at 25% or more of its events and `open`/`closed` alternating across the slices is FLAPPING: reported to the reviewer at once, before anything else |
| **B3** | **one id carries at least 90%** of `breaker` events | the outage is spread over several sources: each is reported with its states |
| **S1** | `(adsbdb_route, disabled_by_config)` is **at least 60%** of `upstream_skip` events: every route lookup that adsb.lol's routeset leaves empty falls through to a source switched off by config | the skips are mostly something else: each (id, reason) is reported with its count |
| **S2** | `hold_4xx` skips exist for adsb.fi (the hour had 30 `hold` events) | adsb.fi's holds do not reach the chains as skips: report it |
| escape | an id, state or reason outside the source's lists; the two reads disagree; a sampled hour | the instrument is unreliable at this scale: stop and report, with no reading |

**The reader:** `tools/bill/breaker_skip_read.py`, blob **`f73b98e55a6b`**, committed here before it runs. Its 10-minute slices need an event timestamp; it counts any event without one, and prints that count.

**Read, and graded** (reader blob `f73b98e55a6b`; the hour `[2026-11-13T00:00Z, 01:00Z)`; reads at 01:10:51Z and
01:12:12Z, 66 s after the first ended, **identical**; unsampled; no event without a timestamp).

| `breaker` (id, state) | count | max consecutiveFailures | per 10-minute slice |
|---|---|---|---|
| `adsb_lol_route`, reopened | 324 | 113 | 62, 54, 61, 60, 41, 46 |
| `adsb_lol_route`, open | 25 | 3 | 4, 4, 5, 5, 4, 3 |
| `adsb_lol`, reopened | 10 | 13 | 0, 10, 0, 0, 0, 0 |
| `adsb_lol_b`, reopened | 1 | 4 | 0, 1, 0, 0, 0, 0 |
| `adsb_lol`, open | 1 | 3 | 0, 1, 0, 0, 0, 0 |
| `adsb_lol_b`, open | 1 | 3 | 0, 1, 0, 0, 0, 0 |
| any id, closed | **0** | | |

| `upstream_skip` (id, reason) | count | per 10-minute slice |
|---|---|---|
| `adsbdb_route`, disabled_by_config | 733 | 146, 130, 127, 129, 93, 108 |
| `adsb_lol_route`, breaker_open | 304 | 66, 57, 48, 52, 33, 48 |
| `adsb_fi_b`, hold_4xx | 104 | 9, 37, 46, 2, 8, 2 |
| `adsb_fi`, hold_4xx | 34 | 0, 12, 22, 0, 0, 0 |

**Grades:**
- **B1: HOLDS.** `closed` is **0** of 362; `reopened` is 335 (92.5%). **Nothing flapped.**
- **B3: HOLDS.** `adsb_lol_route` carries 349 of 362 (96.4%).
- **S1: HOLDS.** `(adsbdb_route, disabled_by_config)` is 733 of 1,175 (62.4%).
- **S2: HOLDS.** adsb.fi's holds reach the chains: 34 skips on `adsb_fi`, 104 on `adsb_fi_b`.
- **Escape: not taken.** No id, state or reason outside the source's lists; the two reads are identical; the hour was
  unsampled.

**What the shape says, by the table registered above:**
- **`adsb_lol_route` (adsb.lol's routeset) was in a CONTINUING OUTAGE for the whole hour.** It matches the "isolate
  starts during a continuing outage" row on every column:
  - `open` scattered evenly across the slices (one per isolate that met it);
  - `reopened` steady across the slices;
  - no `closed` at all;
  - one isolate saw 113 failures in a row.
- **Its effect:** with adsbdb's route switched off by config, a route lookup that misses the `rt:` mirror reaches no
  upstream that answers.
- **This is a different failure from the one CLAUDE.md records.** There the routeset answered 201 with an empty body,
  which the breaker counts as SUCCESS ("alive but empty"). Every probe in this hour FAILED.
- Why it fails, and since when, this read cannot tell.
- **Recorded, not read:**
  - `adsb_lol` and `adsb_lol_b` (the aircraft feed, both relays) had one short episode, in the 00:10-00:20 slice;
  - adsb.fi's 4xx holds were concentrated in 00:10-00:30.

## The route source's outage: two reads (registered 2026-11-13 ~01:25Z, before either reader exists)

**Why, and why first** (the reviewer, 01:15:18Z). adsb.lol's routeset failed every probe in the hour ending 01:00Z, and
production has `ROUTE_ADSBDB_ENABLED = "false"` (`wrangler.toml:367`). Line 46's comment reads `"false" once adsb.lol
routeset recovers`: the fallback was switched off on the premise that the routeset answers. Nothing is proposed before
these two reads. Turning adsbdb back on is a config change and a deploy, and it was switched off deliberately on 08-26
(`a9594ab`), so it is the owner's call, with that commit's reason read first.

**What the source records about a route** (read for this registration):
- `resolveRoute` (`proxy/src/enrich.ts:446`) runs once per `/enrich` request that carries a callsign. It is the only
  caller of `fetchRoute` (`enrich.ts:549`; nothing else in `proxy/src` calls either).
- **The KV path.** It reads the bucketed key, then the mirror `rt:<CS>`. A hit that fails the geometric check writes
  one `route_stale cs=... <verdict>` line (`not_for_position` or `reversed`) and returns no route. A hit that passes
  writes NOTHING.
- **The miss path.** `fetchRoute` (`upstreams/chain.ts:689`) asks adsb.lol's routeset. It writes an
  `upstream_skip breaker_open` while the breaker is open; otherwise it probes, writing an `upstream` line with
  `ok:true` or `ok:false`. Then, whenever that returns nothing, it asks adsbdb, which writes
  `upstream_skip disabled_by_config`. So **every lookup that missed KV and got no route writes exactly one
  `(adsbdb_route, disabled_by_config)` line.**
- **Not recorded anywhere:**
  - whether a request carried a callsign;
  - whether a KV hit came from the mirror or the 24 h runtime cache (`RT_TTL_S`);
  - a route blanked by the 2.5 s deadline (`enrich.ts:609-618`).

### (1) ONSET: adsb_lol_route's states per hour, back to Workers Logs' retention

**The read.** For every whole hour from the earliest the API returns, to the hour ending 01:00Z, the proxy's events
whose JSON `id` is `adsb_lol_route`, selected by a SERVER-SIDE filter. Counted per hour: `breaker` open, reopened and
closed; `upstream` ok:true and ok:false; `upstream_skip` breaker_open. Plus each hour's sampling level. Retention
(7 days on Workers Paid, per the documentation) is stated as the earliest hour that returns any event.

**Controls, before any hour is believed:**
- **Positive.** The filtered counts for `[00:00Z, 01:00Z)` must equal the unfiltered read's: open 25, reopened 324,
  closed 0, `breaker_open` skips 304.
- **Negative.** The same filter on an id the source never writes must return 0 events. That shows the filter is
  applied, not ignored.

| row | predicted | if not |
|---|---|---|
| **O-a** (predicted, low confidence: nothing in the record dates a change) | the outage's signature (`reopened` > 0, `closed` 0, `upstream ok:true` 0) is in EVERY hour back to the earliest retained one: onset "before <that hour>" | **O-b:** a first signature hour inside retention, after hours with `closed` or `ok:true`. The onset is dated to that hour, and the last hour with a `closed` is named |
| **O-c** (not predicted) | | signature hours interleaved with healthy hours: intermittent on an hour scale. Reported to the reviewer at once |
| escape | either control fails; more than 10% of hours sampled (a sampled hour is marked, never graded); a run of hours with no proxy events at all | the instrument is unreliable for those hours: stop and report, with no reading |

### (2) IMPACT, in the hour ending 01:00Z: how `/enrich` requests end, route-wise

**The read:**
- `/enrich` requests in `[00:00Z, 01:00Z)`, from Analytics Engine: `SUM(_sample_interval * double4)` over the two
  enrich route templates.
- The Logs counts for the same hour: the 733 `disabled_by_config` skips and 304 `breaker_open` skips already read; the
  338 `route_stale` lines, counted by verdict (the verdict printed only from the source's two values); and the routeset's
  `upstream ok:true` count, from read (1).

**Reported:**
- no route, KV miss: the `disabled_by_config` count;
- no route, KV hit withheld: the `route_stale` count, by verdict;
- route from the routeset: `ok:true` lines, as an upper bound, since an ok:true can be empty;
- route from KV (mirror or runtime cache, not separable): `/enrich` minus the above, as an UPPER bound, because
  requests without a callsign make no lookup and are not recorded.

Each is given as a count and as a share of `/enrich` requests.

| row | predicted | if not |
|---|---|---|
| **I1** | `/enrich` requests >= 733 + 338 = 1,071: by the source, each of those lines belongs to exactly one `/enrich` request | the attribution is wrong: stop |
| **I2** | the routeset's `upstream ok:true` count in the hour is **0**, consistent with no `closed` | some routes did come from the routeset: count them, and the "no route" figures stand as written |
| escape | the two reads disagree; an unknown verdict; the Analytics Engine query errors | stop and report |

**Both readers** follow the pattern above: no wrangler, counts only, held in memory, error paths print the type only,
the token file. Each is committed with its sha before it runs, and runs twice at least 60 s apart.

**The readers**, committed here before either runs: (1) `tools/bill/route_onset_read.py`, blob **`b708610e1d1b`**; (2) `tools/bill/route_impact_read.py`, blob **`3433e7efa78d`**. (1) runs its two controls first, and reads no other hour unless both pass.

### (2) IMPACT, read and graded

**The read.** Reader blob `3433e7efa78d`. The hour `[2026-11-13T00:00Z, 01:00Z)`, read at 01:24:31Z and 01:25:51Z (66 s
after the first ended). The two reads are **identical**, and the hour was unsampled.

| how a `/enrich` request ended, route-wise | count | share of the 1,994 `/enrich` requests |
|---|---|---|
| **no route: KV miss** (one `adsbdb_route disabled_by_config` line each) | 733 | **36.8%** |
| **no route: a KV route withheld as implausible** (`route_stale`) | 338 | **17.0%** (`not_for_position` 175, `reversed` 163) |
| a route from adsb.lol's routeset (`upstream ok:true`) | 0 | 0.0% |
| a route from KV (mirror or the 24 h runtime cache, not separable): an UPPER bound, since requests without a callsign are inside it | at most 923 | at most 46.3% |

The routeset's own lines in the hour: 429 `upstream` failures, no success, and 304 `breaker_open` skips.

**Grades:**
- **I1: HOLDS.** 1,994 `/enrich` requests is at least 1,071.
- **I2: HOLDS.** The routeset answered 0 times in the hour.
- **Escape: not taken.**

**What the hour says, without a reading beyond it:**
- **At least 53.8% of `/enrich` requests ended with no route** (36.8% + 17.0%).
- The 36.8% is lookups the mirror did not hold, at a time when no upstream answers.
- The 17.0% the mirror did hold but for another leg or direction. Those have nothing to do with the outage: they would be
  withheld with the routeset up too.
- **Whether the 36.8% would have had a route with the routeset up is not knowable from here.** As CLAUDE.md records,
  the routeset answered 201 with an empty body before.

#### Restated on one denominator (2026-11-13 ~07:00Z, reconciled with uk-data; the 36.8% is RETIRED)

Three readings of this hour disagreed: AE 1,994 (the table above), Logs `req` lines 4,349 (all routes, so not a
per-route count), and uk-data's 6,129 (Logs EVENTS whose URL holds `/enrich/`: 1,923 invocation and 4,206 span events,
uk-data's own defect, found by uk-data).

**One definition, one source.** The source is Workers Logs, unsampled that hour (ABR 1).
- A request is one distinct `$metadata.requestId` whose URL path contains `/enrich/`: **1,924**, uk-data's count.
- A miss is a request whose callsign is not a key of the mirror snapshot. That equals one `adsbdb_route
  disabled_by_config` skip line, and uk-data's 733 matches the 733 above exactly.

| how a `/enrich` request ended | count | share of 1,924 |
|---|---|---|
| no route: KV miss | 733 | **38.1%** |
| no route: a KV route withheld as implausible | 338 | **17.6%** |
| a route from the routeset | 0 | 0.0% |
| a route from KV (an upper bound, as above) | at most 853 | at most 44.3% |

So **at least 55.7%** of the hour's `/enrich` requests ended with no route. The grades are unchanged: I1 holds at any
denominator above 1,071, and I2 does not use one.

**Why AE read 70 more, and what is not excluded.** The table's 1,994 is an ESTIMATE, not a count:
`proxy/src/metrics.ts` writes an AE point for a HIT or STALE response (status < 400) only 1 time in 10, with weight 10.
The hour's AE points, split (counts only):
- **weight 1:** 238 rows at `_sample_interval` 1, plus 8 rows at interval 2. The estimate is 254.
- **weight 10:** 174 points. The estimate is 1,740.

If AE's 254 matches Logs, Logs holds about 1,670 HIT/STALE requests. That makes 167 ± 12 points the expectation, and
174 is +0.57 standard deviations. **The whole 70 fits the Worker's own 1:10 sampling.**

That does not EXCLUDE the two other explanations; they are simply not needed:
- **path aliasing** (a request AE files under an `/enrich` template whose URL has no `/enrich/`);
- **hour-edge skew** between AE's timestamp and the Logs event time.

The test that would separate them: Logs' non-HIT `/enrich` requests in the hour should be about 254. A count well off
that puts the gap in the unsampled class, where sampling cannot reach.

**Run by uk-data (06:55:52Z), counts only.** It split the hour's 1,924 `/enrich` requests by the `req` line's own
`cache` field and status class (1,922 carry a `req` line):
- **HIT with status < 400: 1,668.**
- **Everything else: 254**, SKIP 176 and MISS 78, all < 400, with no STALE seen. That is AE's weight-1 estimate exactly.

So the unsampled class matches to the request, which **excludes path aliasing and hour-edge skew for this hour**:
either would have moved that class. The whole 70 is the Worker's 1:10 HIT sampling (1,668 / 10 ≈ 167 expected, 174
written).

**The reason adsbdb is off, read from `a9594ab` (2026-08-26, PR #260), not from `wrangler.toml`'s line-46 comment:**
- adsbdb left the chain over a LICENSING question: the commit says there is "no written permission to use
  commercially".
- The routeset had been "broken upstream since 2026-07-08" (201 where 422 is documented).
- So the premise behind production's `false` was never "the routeset answers". Line 46's comment ("false once adsb.lol
  routeset recovers") sits on the dev `[vars]` default and describes a different decision.
- Turning adsbdb back on reopens that licensing question. It is the owner's call, and this page proposes nothing.

### (1) ONSET, read and graded

**The read.** Reader blob `b708610e1d1b`, run 01:24:31Z-01:28:48Z, ONCE. A second read was not run: the read covers a
closed past week, both controls passed, and the owner's 01:17Z rule scales read-only reads down.

**The controls, both PASS:**
- **Positive.** The filtered hour ending 01:00Z gave open 25, reopened 324, closed 0 and `breaker_open` 304: exactly the
  unfiltered read's, with 0 events that were not the target.
- **Negative.** The unwritten id returned 0 events.

**Retention:** 2026-11-06T00Z returned no event at all. The first hour with events is **2026-11-06T01Z**, about 7 days
back, as the documentation says.

**Every retained hour, 2026-11-06T01Z through 2026-11-13T00Z (168 of 168, all unsampled), carries the signature:**
- `closed` **0**;
- `upstream ok:true` **0**;
- `reopened` and `upstream` failures **in every hour**.

**The daily shape.** The lines rise and fall with traffic: about 25-70 reopened an hour around 04-10Z, and 300-500
around 16-22Z, peaking at 506 on 2026-11-11T18Z and 20Z. That is the request rate, not the source changing state:
there is no `closed` anywhere.

**Grades:**
- **O-a: HOLDS** (predicted, low confidence). **Onset: before 2026-11-06T01:00Z**, the start of retention.
- **The last hour with a `closed`:** none in retention.
- **O-b and O-c: not seen.** Nothing is intermittent on an hour scale.
- **Escape: not taken.** Both controls passed; no sampled hour; no gap inside retention (the empty 2026-11-06T00Z is the
  boundary).

This is consistent with `a9594ab`'s "broken upstream since 2026-07-08". The Logs cannot date it further back.

**Ruled since** (the reviewer, 01:26:46Z): a customer-facing QUALITY finding, the steady state since 08-26 and not a new
break. Queued as P1 for the owner as a licensing question. No further route work unless he asks.

**The deploy question, with the cloud reviewer's advice** (relayed by the owner, 01:06Z; it belongs beside the cost
table above). Yes, but AFTER v20's rollout settles, which means its first week of fleet reads, not during it. The `fw:`
table and rollbacks matter most during a rollout, and no money is at stake before then: today's account is under the
free 10 M KV reads. The question still goes to the owner once the code PR is green.

### Before the `fw:` code PR: every code path that reads `fw:` or would read the memo (the cloud reviewer's item 1, 01:06Z)

**The question:** is the UPDATE OFFER (which image a board is told to fetch) one of them? If it is, it must read storage
directly, or a test must prove that a board reflashed from v20 to v19 is offered v20 on its next check. O11's pre-flight
does exactly that reflash.

**The answer, from the source: the offer is not on the Worker at all.**
- **The firmware asks GitHub directly.** It fetches `version.txt`, then `firmware-<scope><board>.bin`, from
  `OTA_RELEASE_BASE`, which is `https://github.com/Valar-Systems/valar-scopes/releases/latest/download` unless a bench
  build pins it (`src/OtaUpdater.cpp:105-129`). `proxy/src/fleet.ts`'s header says the same: "The Worker is never on
  the update path."
- **`/config`'s fleet overrides (`cfg:fleet`, `proxy/src/config.ts:54-76`) carry no OTA field.**
- **In `proxy/src`, the only read of a `fw:` key is `recordFleetFirmware`** (`fleet.ts:106-107`). The memo sits in front
  of that read and nothing else.
- **Everything else that uses a firmware value reads the REQUEST's header (`meta.fw`), not the stored record:**
  - the request row's `blob6` (`metrics.ts:239`);
  - `recordUsage` and `recordBoot` (`index.ts:392`, `:398`).

  The memo does not touch `meta.fw`.
- **Off the Worker, the readers are the ones in the cost table above** (`reconcile-fleet.py`,
  `activation-coverage.ts`). Both are reports, neither an offer.

**So a reflash from v20 to v19 is offered v20 on its next check whatever the memo holds:** the board asks GitHub, and
the Worker is not consulted.

**What the code PR adds so this stays true:** a test that pins it. Every reference to `fleetKey(` or a `fw:` key literal
in `proxy/src` must be inside `fleet.ts`; any other file fails, naming the file. A future offer path that reached for
the table or the memo would trip it the day it is written. The test's control is a planted reference in a scratch copy,
which must fail with that file named.

## Step 4c: lever (c), what production's KV writes are (registered 2026-11-18T23:08:49Z, before its reader exists; read-only, analytics only)

**Why now.** Section 1's 600-board KV-writes line ($53.83, "about 19,000 a board a month") scales one 7-day window of
production's writes by the number of boards. KM2'' failed (+134%), so the source model has not explained the measured
writes. Since then, `docs/account-budget.md` 9.4's per-day read (`tools/budget/kv_by_day.py`) has split production's
writes into:
- a background of about **1,521 a day** (the median of 10 days without an event, its row x 30 = 45,630);
- about **67,197 on each refresh-data Monday** (the whole day; the mean of 11-02 and 11-09).

If the Monday writes are the job's and not the fleet's, they do not grow with the number of boards. This read tests
that, grades the Worker's own request path against the background, and re-derives the 600-board line from the result.
**No comparison with section 1's figure is stated until the read grades it.**

**The window:** whole UTC days `[2026-11-05T00:00:00Z, 2026-11-18T00:00:00Z)`, 13 days, all ended.

**Event days, named before the read:**

| day | event |
|---|---|
| 2026-11-09 (Mon) | the refresh-data job's scheduled run (started 16:28Z): the military and airport ingests, bulk puts, both environments |
| 2026-11-13 (Fri) | the (A) route refresh, its first UTC day (`docs/route-refresh-2026-11.md`) |
| 2026-11-14 (Sat) | the (A) route refresh, its second UTC day; also three photos-ingest runs on pushes, 03:29Z-04:15Z (changed rows only) |
| 2026-11-16 (Mon) | the refresh-data job's scheduled run (started 15:53Z) |

**Background days:** the other nine: 11-05, 11-06, 11-07, 11-08, 11-10, 11-11, 11-12, 11-15 and 11-17. The daily
photo-drift job reads KV over REST and writes nothing, so it sits in every day's READS, background included.

### The request path, from the source (`proxy/src` at main `237d5d163eb4`)

`ENRICH_KV` has 16 `put` sites and no `delete`. Each family, what drives it, and the driver's measure per day d from
Analytics Engine. **Request rows** are the dataset's rows whose index1 is a route: every index1 except `err5xx`,
`enrich_gap`, `ota`, `boot` and `usage`, the five other point families (`proxy/src/metrics.ts`).

| family | sites | written when | driver per day, from request rows | writes per unit |
|---|---|---|---|---|
| `ac:` | `enrich.ts:161`, `:200` | every `/enrich` KV miss writes exactly one: a fail marker, or the built record | **A**: `/enrich` rows with blob2 = `MISS`, `sum(_sample_interval x double4)` | 1 |
| `fw:` | `fleet.ts:133` | an authenticated request carrying both a device and a firmware, when the stored row is an hour old or the firmware changed | **F**, device-hours: over the day's 24 hours, the sum of `COUNT(DISTINCT blob5)`, on rows where blob5 and blob6 are both non-empty | [0.9, 2.0]: below 1 for partial hours; above 1 when two requests read the same hour-old row at once |
| `lb:` | `leaderboard.ts:366` (`lb:dev`), `:646` (`lb:board`, rebuilt on every submit and on a stale page read), `:193` (`lb:name`, a new name), `:346` (`lb:firsttype`, a new type) | a successful submit; a page read that finds the board stale | **L**: submit rows (`/api/v1/blipscope/leaderboard`, `/v1/leaderboard`) with double1 < 300. **P**: the page routes (`/blipscope/leaderboard`, `.json`, `/:id`, and the unprefixed three) | L x [2, 3]; P x [0, 1] |
| `enr:`, `act:` | `enroll.ts:121`, `:137`, `:244`; `activation.ts:121` | an enrol request: the day counter, the refused counter, the ledger, the activation claim | **E**: `/blipscope/enroll` and `/enroll` rows, any status | [0, 4] |
| `rt:` | `enrich.ts:556` | a DEFINITIVE route from an upstream | none in Analytics Engine. The Logs showed 0 `ok:true` route probes from 11-06T01Z to 11-13T00Z, and adsbdb's route is off by config | 0, assumed; named below |
| by hand, rare | `activation.ts:100`, `:216`, `:239` (the admin tool); `provision.ts:156` (the bench mint); the dashboard's revocation edit | a person runs a tool | none | 0, assumed; named below |

**The model's band per day:** `Mlow = A + 0.9 F + 2 L` and `Mhigh = A + 2.0 F + 3 L + P + 4 E`.

**What is measured per day,** from `kvOperationsAdaptiveGroups` by date, action and namespace (`sum { requests }`):
- **W**: production's writes;
- staging's writes, any other namespace's, and every namespace's reads, deletes and lists, printed beside them;
- **B**: distinct devices, `COUNT(DISTINCT blob5)` over the day's request rows.

### Predictions (frozen now)

| row | predicted | if not |
|---|---|---|
| **KW1** | on at least 8 of the 9 background days, W lies in `[0.8 x Mlow, 1.2 x Mhigh]` | the request path is not what the source says. Each failing day is reported with its A, F, L, P and E, and the named candidates below are read for it, one at a time |
| **KW1b** | the `fw:` writes implied per device-hour, the median over the background days of `(W - A - 2 L - 2 E) / F`, is in `[0.8, 1.3]` | above 1.3: `fw:` writes more than once an hour, which matters to #689. Below 0.8: F over-counts device-hours, or a family sits below its floor |
| **KW2** | the Monday job is constant. Production's W on 11-16, less the background median, is in `[59,108, 72,244]` (65,676 +/-10%: kv-days' 67,197 less its 1,521). Staging's 11-16 writes, less staging's background median, are in `[59,106, 72,241]` (65,673.5 +/-10%) | the job's size moves week to week, so its per-event figure becomes a range read from both Mondays |
| **KR1** | production's READS on 11-09 and 11-16 lie in `[0.9 x the background minimum, 1.1 x the background maximum]` | the Monday job reads as well as writes, so section 1's reads line also carries a job share, re-derived as in KW3 |
| **KW3** | per board: `w_b = 30 x (background median of W) / (background median of B)` is in `[1,000, 4,000]` writes a month. December's ACCOUNT KV-writes line at 600 boards, `600 x w_b + 4 x (J_prod + J_stg) + 30 x (the background medians of staging and of every other namespace)`, is **under $10 a month**, at $5 a million over the free 1 M | both figures are reported side by side, and section 1's line is not replaced |
| escape | an anchor off (below); an index1 that is not a route and not one of the five families; a background day with no writes or no request rows; the two reads differing in any figure; anything no row above names | the instrument is unreliable at this scale: stop and report, with no reading |

- `J_prod` and `J_stg` are the mean, over 11-09 and 11-16, of that namespace's writes less its own background median.
- **KW3 scales linearly in boards, which is an upper bound for `ac:`.** The cache is shared across the fleet, so its
  misses grow more slowly than the number of boards. `fw:` is linear by construction: at most 24 a board a day.

**The anchors, required before any row is graded:**
- **KV:** production's writes on 11-13 and 11-14 equal kv-days' whole-day figures, **1,899** and **570,544**, within
  0.5%. Staging's 11-13 equals **628,713** within 0.5%.
- **Analytics Engine:** over `[2026-11-06, 2026-11-13)`, the `/enrich` MISS rows equal step 2b's **7,239** within 1%,
  and the distinct devices equal step 2b's **22**.

**If KW1 fails high on a day, the named candidates,** in the order they would be read. Each is a separate read with its
own predictions, and none is run now:
1. `rt:` writes after the routeset recovered: `adsb_lol_route` `ok:true` per hour, from the Logs, whose retention now
   reaches back to about 11-11;
2. a hand-run tool (activation, provision, revocation): that tool's own `evt:` line in the Logs;
3. `fw:` written twice in an hour (KW1b).

**The reader:** `tools/bill/kv_writes_read.py`, committed here with its blob sha before it runs.
- Read-only and analytics only: no wrangler, no Logs, no KV key list.
- The `analytics-read` token comes from the token file and is never printed.
- It prints counts per day and the grades. It never prints a key, a value, a device id or a hex.
- It runs twice, at least 60 s apart, and both reads must agree before anything is graded.

**What it does not do:** no deploy, no KV write, no plan or billing change. A graded KW3 changes no budget row: a change
to `docs/account-budget.json` is its own PR, for that file's owner.

**CI for this push (predicted before it):** docs only, on the PR's merge with today's main: **35 = 33 / 2 / 0**, the
same as #758's docs-only head. `tools/bill/` is in no workflow's path filter, so the reader's own push predicts the
same.

### Amendment 4c-1: the point families are seven, not five (2026-11-18T23:20:01Z, below the frozen section; ruled by the reviewer)

**What happened.** Read 1 (2026-11-18T23:18:06Z, reader blob `b9ce4ab4318a`) found ONE index1 value that was neither a
route nor one of the five families named above. That is the escape row, so the read stopped:
- read 2 was not run, nothing was graded, and no per-day figure was printed;
- the reader does not keep an unknown value, so it could not say what the value was.

**The cause, from the source.** The registration took the families from `proxy/src/metrics.ts` alone. Two more are
written outside it, at the deployed commit (`074863fc`) and on main:
- **`provision`** (`proxy/src/provision.ts:84`): blobs `provision`, a MAC hash, the operator and a device field, in
  blob1 to blob4; one double, today's mint count;
- **`sitefeed`** (`proxy/src/sitefeed/`): blobs `sitefeed` and the event; one double, 1.

Neither carries a blob5, and with one double each, double4 is 0. So neither can enter A, F, B, L, P, E or the request-row
sums. That is why this amendment is allowed; it is not a re-grade. **The fix comes from the writers' side**, and no
figure was seen.

**Ruled (the reviewer, 2026-11-18 ~23:25Z): ACCEPTED as written, with one addition.**
1. The non-route families are **seven**: the five above, plus `provision` and `sitefeed`, cited by file and line as
   above.
2. The reader adds those two names, so an index1 outside routes and the seven still fires the escape.
3. **The addition:** the reader prints the row COUNT for `provision` and for `sitefeed` separately, as information, so
   the record shows which of them produced read 1's unknown. If neither has rows and an unknown remains, the escape
   fires, as an eighth family.
4. **Read 1 is DISCARDED.** Both reads are taken fresh, after this commit, at least 60 s apart.
5. Every row, band, anchor and the rest of the escape stand exactly as frozen above.
6. **A second escape on 4c goes to the reviewer before anything else is amended.**

The reader's new blob sha is sent to the reviewer before any query.

### Step 4c's read and its grades (2026-11-18T23:23:59Z; reader blob `4c21d7e0d51e`, commit `c4deee7d7286`)

**CORRECTION to amendment 4c-1 (appended; the text above is not edited).** Its "ruled (the reviewer, 2026-11-18
~23:25Z)" was a typed time, not a read one. The ruling arrived between read 1's start (23:18:06Z) and the amendment's
own stamp (23:20:01Z).

**The reads.**
- **Read A** ran at 23:21:25Z and **read C** at 23:22:29Z, 64 s apart. They agree on every figure, so the step is
  graded on A and C.
- **Read B DISCARDED.** It ran at 23:21:37Z, only 12 s after read A, short of the registered 60 s. That was my error.
  The command that took read C refuses to run under 60 s from read A's stamp, and it refused twice before running.
- Read 1 was discarded under the amendment.
- Nothing was printed from read B.

**The instrument.**
- **Anchors, all exact:** production writes on 11-13 were 1,899, and on 11-14 570,544; staging's on 11-13 were
  628,713; the `/enrich` MISS rows over `[11-06, 11-13)` were 7,239, from 22 devices.
- **Index check:** unknown non-route index1 values: 0.
- **Information (the ruling):** `provision` had 0 rows and `sitefeed` 177,134, so `sitefeed` produced read 1's unknown.
- **Other namespaces:** 0.

**Per day** (counts only; W = writes, R = reads; A, F, L, P, E, B as registered; "rows" = request rows):

| day | kind | W prod | W staging | R prod | A | F | L | P | E | B | rows | Mlow | Mhigh | band |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 11-05 | background | 1,495 | 7 | 220,995 | 1,027 | 265 | 72 | 7 | 14 | 13 | 64,446 | 1,410 | 1,836 | in |
| 11-06 | background | 1,585 | 5 | 239,921 | 1,094 | 271 | 76 | 6 | 8 | 13 | 76,146 | 1,490 | 1,902 | in |
| 11-07 | background | 1,459 | 0 | 226,240 | 1,016 | 263 | 77 | 6 | 6 | 14 | 64,162 | 1,407 | 1,803 | in |
| 11-08 | background | 1,233 | 0 | 213,231 | 746 | 246 | 106 | 4 | 2 | 12 | 60,564 | 1,179 | 1,568 | in |
| 11-09 | refresh-data | 67,301 | 65,678 | 253,417 | 1,108 | 276 | 82 | 7 | 2 | 14 | 68,806 | | | |
| 11-10 | background | 1,405 | 0 | 228,591 | 973 | 259 | 73 | 4 | 2 | 13 | 64,676 | 1,352 | 1,722 | in |
| 11-11 | background | 1,734 | 0 | 258,731 | 1,275 | 281 | 77 | 7 | 14 | 14 | 73,506 | 1,682 | 2,131 | in |
| 11-12 | background | 1,547 | 8 | 276,460 | 1,027 | 306 | 86 | 5 | 12 | 17 | 78,768 | 1,474 | 1,950 | in |
| 11-13 | route refresh (A) | 1,899 | 628,713 | 278,703 | 1,337 | 348 | 79 | 16 | 18 | 20 | 84,578 | | | |
| 11-14 | route refresh (A) + photos | 570,544 | 3 | 352,229 | 2,037 | 370 | 83 | 3 | 10 | 17 | 94,359 | | | |
| 11-15 | background | 1,880 | 0 | 325,158 | 1,338 | 352 | 83 | 2 | 0 | 16 | 94,376 | 1,821 | 2,293 | in |
| 11-16 | refresh-data | 67,885 | 65,708 | 399,635 | 1,643 | 389 | 78 | 8 | 20 | 21 | 106,360 | | | |
| 11-17 | background | 2,196 | 0 | 416,175 | 1,585 | 416 | 85 | 0 | 0 | 19 | 109,678 | 2,129 | 2,672 | in |

Production deletes: 1 in the window (11-14). Production lists: 153-249 a day, and 1,452 on 11-14.

**Grades, as registered:**

| row | result | grade |
|---|---|---|
| **KW1** | 9 of 9 background days in band | **HOLDS** |
| **KW1b** | `fw:` writes implied per device-hour: median **1.068** (per day 0.99-1.19) | **HOLDS** |
| **KW2** | 11-16 less the background median: production **66,338**, staging **65,708** | **HOLDS** |
| **KR1** | Monday reads 253,417 and 399,635, against `[191,908, 457,793]` | **HOLDS, weakly** (below) |
| **KW3** | **w_b = 3,315** writes a board a month (background median W 1,547, B 14). December's account KV-writes line at 600 boards: **2,515,956 writes, $7.58 a month**. Section 1's line, beside it: **$53.83** | **HOLDS** |
| escape | not taken | |

**What the grades say:**
- **Production's KV writes are two things:**
  - the Worker's request path, explained day by day by the source;
  - the refresh-data job, about 66,000 writes a run in EACH environment, the same each week.
- The job does not grow with boards. Section 1's line scaled one week, Monday included, by board count. KW3 re-derives
  it from the per-board background instead.
- **Section 1's line is not replaced here,** as registered. A change to `docs/account-budget.json` is its own PR, for
  that file's owner.

**What the grades do not say:**
- **KR1's band is wide, and it cannot exclude a small Monday read share.** Background reads nearly doubled across the
  window, so the band spans 2.4x. Against its own neighbours, 11-09 read 253,417 where 11-08 and 11-10 read 213,231
  and 228,591. Its reads per request row were 3.68, against 3.52 and 3.53.
  - **Two explanations fit, and the data excludes neither:** a Monday job that also reads (a few tens of thousands);
    or traffic on 11-09 that read more per request.
  - So the reads line is NOT shown to be free of a job share. That would take a read with its own predictions.
- **KW3 has two caveats:**
  - **It is an upper bound for `ac:`.** The cache is shared across the fleet, so its misses grow more slowly than
    boards.
  - **Its denominator is distinct devices per DAY (median 14).** The bill's 17 to 22 were devices per week, so its
    per-board figure was a different quantity.
  - w_b sits in the top quarter of its registered band.

**Recorded, not graded (not a prediction of this step):**
- **Production KV reads grew across the window:**
  - from 220,995 on 11-05 to 416,175 on 11-17;
  - distinct devices went from 13 to 19 over the same days, request rows from 64,446 to 109,678, and reads per
    request row from 3.43 to 3.79.
- **At 11-17's rate, production alone reads 12.49 M in 30 days, above the free 10 M.**
  - This bears on a premise stated with #689's deploy question (step 4a, the cloud reviewer's advice): "no money is at
    stake before then: today's account is under the free 10 M KV reads".
  - November's own total is not read here: 3,689,486 production reads in these 13 days, and 11-01 to 11-04 are not
    in the window.
  - The owner's ruling on #689 is not re-opened here. The premise change goes to the reviewer.

## Step 4b2: lever (b2), the KV reads of `/enrich`'s lookup chain (registered 2026-11-18T23:29:26Z, before its reader exists; read-only, analytics only)

**Why.** Step 2b put about **7.4 KV reads on each `/enrich` request**, and `fw:` at about 28% of production's reads.
(b1), the `fw:` memo, is #689. This step measures `/enrich`'s share, and asks how much of it is in families that
change only at an ingest or a photo publish. Those families are the ones an isolate cache could hold without serving
anything stale for long. Step 4c recorded production reads at 416,175 on 11-17, which is 12.49 M in 30 days.

**The difficulty, and the instrument chosen for it.** KV analytics has no key dimension (step 2b, (iv)), so no read can
name the keys. Instead this read separates two request kinds by how they vary through the day:
- **Device requests** (mostly `/blips` polls) run flat with the number of boards.
- **`/enrich` requests** follow the aircraft overhead, which rises and falls through the day.

One least-squares fit over the background hours gives:

`R(h) = alpha x D(h) + beta x N(h) + gamma`

| term | what it is | measured from |
|---|---|---|
| **R** | production's KV reads in hour h | `kvOperationsAdaptiveGroups` by `datetimeHour`, action `read`, production's namespace |
| **D** | device requests that read `fw:`: request rows with blob5 and blob6 both non-empty, any route | Analytics Engine, `sum(_sample_interval x double4)` |
| **N** | non-SKIP `/enrich` requests: `/enrich` rows with blob2 `HIT` or `MISS` | the same |
| **alpha** | reads per device request: `fw:` once, plus the small per-route reads (`cfg:fleet`, airport tiles, the photo blob, the leaderboard's keys) | the fit |
| **beta** | the EXTRA reads per non-SKIP `/enrich` request: the chain | the fit |
| **gamma** | reads per hour that neither kind explains: revocation's per-isolate refresh, jobs, pages | the fit |

**The window and the hours.**
- The window is step 4c's: whole UTC days `[2026-11-05, 2026-11-18)`, with the same four event days excluded whole
  (11-09, 11-13, 11-14 and 11-16).
- From the nine background days, **each day's photo-drift run hour is also excluded.** That job reads production over
  REST once a day. Its runs, from GitHub's run list, were read before any analytics query: 11-05 14Z, 11-06 14Z, 11-07
  12Z, 11-08 13Z, 11-10 14Z, 11-11 14Z, 11-12 15Z, 11-15 13Z and 11-17 15Z. Each run started and ended inside that
  hour.
- That leaves **207 background hours.**

**`/enrich`'s chain from the source** (`proxy/src` at main `237d5d163eb4`). This is the reads per non-SKIP request
beyond the device request's own. It gives the families, not a band tight enough to grade beta by.

| family | when it is read | reads | changes |
|---|---|---|---|
| `ac:<hex>` | every non-SKIP request (`enrich.ts:147`) | 1 | written on every KV miss (dynamic) |
| `tn:<type>` on a miss | `buildMeta`, and the type backfill (`enrich.ts:102`, `:182`) | MISS x [0, 2] | static |
| `rt:<cs>:<bucket>` | a callsign with `trk` (`enrich.ts:484`) | [0, 1] | dynamic, but step 4c read 0 writes to it |
| `rt:<CS>`, the mirror | a callsign whose bucketed key missed, or one with no `trk` (`:486`) | [0, 1] | static (the route ingest) |
| `ap:` x 2 | a cached route with a live position (`routeVerdict`, `:298`) | [0, 2] | static (refresh-data) |
| `ovr:`, `pa:`, `mil:` | no identity, or a military-looking hex with no operator (`:673-675`) | [0, 3] | static |
| `tn:<type>` at serve | a type with no name (`:699`) | [0, 1] | static |
| photo pointers | the hex pointer always; the type pointer when there is a type and no hex pointer (`photos.ts:131`) | [1, 2] | static (photo publish) |

### Predictions (frozen now)

| row | predicted | if not |
|---|---|---|
| **KE1** | beta, extra reads per non-SKIP `/enrich` request, is in `[5.5, 9.5]` (step 2b's 7.4 by a different instrument, +/- about 25%) | the two instruments disagree about `/enrich`. Both are reported, and the design does not start from either |
| **KE2** | alpha, reads per device request, is in `[0.95, 1.5]`: `fw:` is exactly 1, and the small per-route reads add little | above 1.5, a route reads more than the source says, and it is named before the design. Below 0.95, `fw:` is not read on every device request |
| **KE3** | gamma is in `[-500, 2,000]` reads an hour | a fixed reader the model does not name: report its size |
| **KE4, the design input** | the static share of `/enrich`'s reads is at least half: `S_low = (beta - 2 - 2m) / beta >= 0.5`, where m = MISS / (HIT + MISS) over the same hours, and 2 counts `ac:` plus a bucketed-route read on every request | under 0.5, a static-family cache cannot halve the chain, and (b2)'s design must weigh caching `ac:` itself, with its staleness |
| **KE5** | the fit explains the hours: R^2 >= 0.85 | the three-term model is not the shape of the reads. Report the residual by hour of day before any design |
| escape | an anchor off (below); `corr(D, N)` over the 207 hours above 0.9, so the fit cannot separate them (INSTRUMENT); a kept hour with no KV group or no request rows; the two reads differing in any figure, or taken under 60 s apart (the reader refuses); anything no row names | stop and report, with no reading |

**The anchors, required before any row is graded:**
- **KV:** for each of the 13 days, the hourly reads summed over the day equal step 4c's daily production reads (read A,
  `_handoffs`' copy) within 0.5%.
- **Analytics Engine:** the MISS rows over `[11-06, 11-13)` equal step 2b's 7,239 within 1%. This is the same query as
  step 4c's.

**The reader:** `tools/bill/enrich_reads_read.py`, committed here with its blob sha before it runs.
- Read-only, analytics only: no wrangler, no Logs, no key list. `analytics-read` comes from the token file.
- It prints counts and the fit only: no key, value, device id or hex.
- The fit is plain least squares on three terms, written out in the reader, with no fitting library.
- It prints beta's standard error as information.
- **`grade` itself refuses two reads stamped under 60 s apart.** After tonight's read B, that rule is in the reader,
  not only in this text.

**What it does not do:** no deploy, no KV write, no design. (b2)'s design is the next registration, from these grades,
with its account total.

**CI for this push (predicted before it):** docs only: **35 = 33 / 2 / 0**.

### Step 4b2's first read: ESCAPE, INSTRUMENT (2026-11-18T23:37:34Z; reader blob `78eb51c22213`)

**The reads.**
- Read A ran at 23:34:35Z and read C at 23:35:42Z, 67 s apart.
- Read B (23:35:04Z, 29 s after A) was refused by the reader's own 60 s guard (exit 4) before anything was printed. It
  is discarded.

**Before the fit:**
- the anchors held: the hourly reads summed per day equal step 4c's daily production reads on 13 of 13 days, within
  0.5%, and the MISS anchor is 7,239;
- **corr(D, N) over the 207 kept hours is 0.953,** above the registered 0.9.

**The escape fired.** No fit was computed or printed, so there is no reading.

**What the premise got wrong:** the registration expected device requests to run flat through the day while `/enrich`
swings. Three explanations fit, and this read separates none of them:
- **(i)** D CONTAINS N: every `/enrich` request is also a device request;
- **(ii)** the `/blips` poll cadence follows activity, so the polls also rise and fall with traffic;
- **(iii)** the fleet grew over the nine days (13 to 19 boards a day), which moves both together.

**Options were put to the reviewer** (23:3xZ): (A) D excluding `/enrich`; (B) (A) plus a within-day fit; (C) no
regression; (D) an Analytics Engine count of KV gets per request (code and a deploy). Its ruling is recorded below,
when it comes. Read A and read C are not reused.

## Step 4d: KV reads and writes per board per month, and the read-overage trigger (registered 2026-11-18T23:37:34Z, before its read)

**The owner, 2026-11-18 23:30Z (relayed by the reviewer):** "cache: as ruled. Keep the read cache for after v20's first
week. Bring it back to me earlier if the projected overage for reads passes $5 a month. Have 5c put the reads per board
per month into its cost work, so I can see what each new board costs."

### The trigger: the projected KV-read overage

**At every proxy-bill read:**

`overage = max(0, R_acct(d) x 30 - 10,000,000) x $0.50 per million`

- **R_acct(d)** is the ACCOUNT's KV reads (every namespace) on the latest WHOLE UTC day.
- **If it passes $5 a month, it goes to the reviewer at once,** for the owner.
- **Baseline** (sent 23:3xZ, from step 4c's graded read A; no new query): d = 2026-11-17, R_acct = 416,190, which comes
  to **$1.24 a month**.
- $5 is reached at about 1,000,000 reads a day.

### Per board, per month

**For one whole UTC day d:**
- `R_b = 30 x production KV reads on d / B(d)`;
- `W_b = 30 x production KV writes on d / B(d)`;
- `B(d)` = distinct device ids in the day's request rows (step 4c's B, the same query).

**Priced past the free tiers:** a new board costs `R_b x $0.50/M + W_b x $5/M` a month. That is an UPPER bound on a
board's marginal cost:
- the day's fixed readers (the photo-drift job, revocation's per-isolate refresh) are spread over the boards;
- `ac:` is shared across the fleet.

Step 4b2's gamma bounds the fixed part.

**The day graded:** **d = 2026-11-18**, the first whole day after step 4c's window, read once it has closed. No
figure from that day has been read.
- **KV-writing jobs, named before the read from GitHub's run list** (read about 23:35Z): none on 11-18 so far. The
  photo-drift job ran 15:37Z, and it reads but does not write.
- The run list is read again at read time. If a KV-writing job (refresh-data, photos, routes-refresh) ran on 11-18,
  W_b is reported and not graded, and R_b is still graded.

**The bands come from step 4c's nine background days. Those days are already SEEN, so they set the band; they are not
the test.**
- Per board a day, R/B ran from 16,160 to 21,904. Over the window it RISES: 17,000 on 11-05, 20,322 on 11-15 and 21,904
  on 11-17. That is 484,800-657,120 a month.
- W/B ran from 91 to 124: 2,730-3,720 a month.

| row | predicted | if not |
|---|---|---|
| **PB1** | R_b on 11-18 is in `[480,000, 750,000]`: the seen range, with its top raised 15% for the trend | reported with its R and B. Outside on the high side means reads per board are still climbing: the next read takes 7 days |
| **PB2** | W_b on 11-18 is in `[2,500, 4,500]` | reported with its W and B |
| **PB3, the price** | a new board costs **$0.25-$0.40 a month** in KV reads and writes past the free tiers, at PB1 and PB2's values (480,000 x $0.50/M + 2,500 x $5/M = $0.25; 750,000 x $0.50/M + 4,500 x $5/M = $0.40) | reported as measured |
| escape | the anchor off (below); the two reads differ or are under 60 s apart; B(d) = 0; anything no row names | stop and report, with no reading |

**The anchor:** the same reader reads 2026-11-17 too, and must reproduce step 4c's figures for that day exactly:
production reads 416,175, writes 2,196, B 19.

**The reader:** `tools/bill/per_board_read.py`, committed with its blob sha before it runs.
- Read-only and analytics only. It reads exactly two whole days (11-17 for the anchor, 11-18 for the grade).
- It prints counts only.
- `grade` refuses reads under 60 s apart, and prints the trigger.

### Amendment 4b2-1: (A), with (B) as its pre-registered fallback (2026-11-18T23:41:25Z, below the frozen section; ruled by the reviewer)

**The ruling** (the reviewer, between 23:37:34Z and this stamp): **(A), justified from the SOURCE, not the data.**
- (B) is registered in the same amendment, now, as the fallback. It runs only if (A) escapes, so no instrument is chosen
  after a result is seen.
- **If (B) also escapes, 4b2 stops.** Then (C), step 2b's whole-window 7.4 with the family table above, becomes the
  design input, and the owner is told.
- Reads A and C of the first read are discarded. The fresh reads are taken after this amendment's sha reaches the
  reviewer, 60 s apart by the reader's guard.

**Why (i) is structural** (`proxy/src` at main `237d5d163eb4`). A non-SKIP `/enrich` request takes this path:
- `index.ts:361` authenticates it;
- `index.ts:366` calls `setDeviceAttribution`, which sets `dev` and `fw` from the request's headers
  (`metrics.ts:59-65`);
- `index.ts:395` calls `recordFleetFirmware`, its one `fw:` read;
- only then does `index.ts:429` dispatch it to `handleEnrich`;
- `index.ts:464` writes its Analytics Engine row through `record`, carrying blob5 = `dev` and blob6 = `fw`
  (`metrics.ts:248`).

So **every row counted in N is also a row counted in D,** by construction, and its `fw:` read lands in D's term. A
correlation between D and N is partly N correlating with itself. No data is needed to show it.

**(A), the fit as amended:**
- **D' = D less N's own rows:** request rows with blob5 and blob6 both non-empty, EXCEPT `/enrich` rows whose blob2 is
  `HIT` or `MISS`. N is unchanged.
- **The fit:** `R(h) = alpha x D'(h) + beta' x N(h) + gamma`. Here beta' is the `/enrich` request's own reads: its `fw:`
  read plus the chain. So the chain is **beta = beta' - alpha**.
- **The gate:** `corr(D', N)` over the same 207 hours, at most 0.9.
- **Graded on beta, as frozen:** KE1 (beta in `[5.5, 9.5]`), KE2 (alpha), KE3 (gamma), KE4 (S_low with this beta), KE5
  (R^2 >= 0.85).
- **Information:** N's rows with an empty blob6. Those requests do no `fw:` read, so beta' overstates for them by alpha;
  the count is expected to be near 0.

**(B), the fallback: it runs ONLY if (A)'s gate fires.**
- **A within-day fit.** On each kept day, R, D' and N are centred on that day's kept-hour means. The fit is
  `R~ = alpha x D'~ + beta' x N~`, with no intercept. Each day's gamma is
  `gamma_d = mean R - alpha x mean D' - beta' x mean N`.
  - This removes (iii), the fleet's growth across days.
  - (ii), a poll cadence that follows activity, is what remains, and the gate is what tests it.
- **The gate:** corr of the centred D' and N, at most 0.9.
- **Rows:** KE1, KE2 and KE4 as in (A); **KE3:** every day's gamma_d in `[-500, 2,000]`; **KE5:** the within-day R^2
  (variance explained of the centred R) at least **0.70**.
- **If (B)'s gate fires:** ESCAPE. 4b2 stops, and (C) is the design input.
- **A limit of (B), named before it runs** (seen on the reader's synthetic control, not on data): gamma_d inherits
  alpha's error times that day's level of D'. Where D' barely moves within a day, alpha is loosely fixed, and the
  per-day gammas drift with the fleet's size. Under (B), KE3 is therefore read alongside alpha's standard error.

**The reader's synthetic controls, run on the amended grader before this commit:**
- a planted truth with separable D' and N goes to (A): beta 6.904 against 7.0;
- a planted fleet growth that pushes corr(D', N) to 0.971 routes to (B): beta 7.059 against 7.0;
- D' collinear with N within each day ends in the (B) escape;
- reads 20 s apart are REFUSED, an anchor off on one day and an empty hour each ESCAPE, and each plant turns only its
  own row.

**Unchanged:** the window, the 207 hours, the anchors, the 60 s refusal, and every other escape row.

The reader's new blob sha goes to the reviewer before any query.

### Step 4b2's read under amendment 4b2-1, graded (2026-11-18T23:44:48Z; reader blob `df0f7d33632b`)

**The reads.** Read A ran at 23:42:59Z and read B at 23:44:07Z, 68 s apart, and they are identical. The wrapper that
started read B refused four times before 65 s had passed. The first read's A and C are not reused.

**The instrument:**
- the anchors are exact: 13 of 13 days within 0.5%, and the MISS anchor is 7,239;
- N's rows with an empty fw number 51, so beta' overstates by alpha for about 0.02% of N;
- **(A)'s gate: corr(D', N) = 0.865,** under 0.9, so (A) ran and (B) did not.

**The fit, (A), over 207 hours:**
- alpha **1.270** (se 0.120); beta' **6.488** (se 0.168); gamma **1,718.3** an hour (se 143.8); R^2 **0.9782**;
- the totals over those hours: R 2,286,581, D' 428,044, N 213,846; the MISS share m is 0.0448;
- **by term:** `/enrich` requests (beta' x N) are **60.7%** of production's reads, and the chain alone (beta x N)
  **48.8%**. Other device requests (alpha x D') are **23.8%**, and gamma **15.6%**.

| row | result | grade |
|---|---|---|
| **KE1** | beta = beta' - alpha = **5.218**, against `[5.5, 9.5]` | **FAILS** |
| **KE2** | alpha **1.270**, in `[0.95, 1.5]` | **HOLDS** |
| **KE3** | gamma **1,718.3** an hour, in `[-500, 2,000]` | **HOLDS** |
| **KE4** | S_low = (5.218 - 2 - 0.0896) / 5.218 = **0.600**, at least 0.5 | **HOLDS** |
| **KE5** | R^2 **0.9782**, at least 0.85 | **HOLDS** |
| escape | not taken | |

**KE1 is OFF, and its registered consequence applies:** the two instruments disagree about `/enrich`. Both are reported,
and (b2)'s design does not start from either number.

**Named alternatives for the gap.** None is excluded by this read.
- **(1) Step 2b's 7.4 counted gamma as `/enrich`.** It divided every read but `fw:` by `/enrich` requests. With gamma's
  15.6% folded back in, the fit's own figure is (beta' x N + gamma x 207) / N - alpha = 6.488 + 1.663 - 1.270 =
  **6.88**, inside KE1's band. So part of the difference is a matter of definition, not a contradiction.
- **(2) alpha above 1** (1.270): device requests read more than `fw:` alone. The fit may have moved part of the chain
  into alpha. corr(D', N) is 0.865, below the gate but not low, so alpha and beta' still trade off.
- **(3) gamma is real and large:** about 41,000 reads a day that track neither kind. Revocation's per-isolate refresh
  is one candidate: one read a minute per live isolate, so 1,718 an hour is about 29 isolates. This read cannot
  confirm that.

**What (b2)'s design may take from this, and what it may not:**
- **It may take KE4:** at least 60% of the chain's reads (at this beta; more at 2b's) are static families: tn:, the
  route mirror, ap:, ovr/pa/mil and the photo pointers. A cache over them is the target.
- **It may take the term shares** as the scale: `/enrich` is 61% of production's reads, other device requests 24%, and
  a fixed hourly floor 16%.
- **It may not take** a reads-per-request figure from either instrument as the saving's basis. The design states a
  range that spans both.

**The trigger at this read:** unchanged, because no new whole day has been read. Step 4c's 11-17 gives **$1.24 a month**.

## Step 5: lever (e), `/wx`'s F1 (`latest` cached per colo): the colo-occupancy read (registered 2026-11-19T00:01:15Z, before its reader exists; read-only)

**What F1 is** (`docs/wx-cost.md`, "The fixes"):
- Today every `/wx` poll reads R2's `latest` (`proxy/src/wx/handler.ts:26`), a 304 included.
- F1 holds `latest` in the Cache API, which is shared per colo, for T seconds.
- A poll that finds it saves one Class B read. A board alone in its colo polls every 60 s, so it gains only if T is
  longer than one poll.

**So the design turns on one quantity this account has never read: how many boards share a colo.**

**The read:**
- One closed hour of the proxy's Workers Logs `evt:"req"` lines, through the Workers Observability API, as step 3's
  readers did.
- **Every board's colo**, from each line's `colo` field (`proxy/src/index.ts:453`).
- **The board key:** until Thursday's logs deploy, production's line carries `dev` (`metrics.ts:229` at the deployed
  `074863fc`). After it, the line carries the label `devl`.
- The reader keys on whichever the line carries, IN MEMORY ONLY.
- **It prints only a histogram:** how many colos hold k boards, for each k. Never a colo code, a label, an id or a
  line. Nothing is written but those counts.

**The model, fixed now:**
- Assume every active board polls `/wx` every P = 60 s (WD3 e), as all will once weather ships, at a random phase with
  +/-2 s of jitter.
- **A simulation, seeded and repeated 200 times per colo size,** counts the polls that find a live cache entry. That
  gives the hit rate per k, weighted by the measured histogram.
- **T is set off the 60 s boundary on purpose:**
  - **T = 55 s:** shorter than one poll, so a lone board never hits;
  - **T = 115 s:** just under two polls, so a lone board hits every other poll.

| row | predicted | if not |
|---|---|---|
| **F1-K** | at least half of the hour's active boards are ALONE in their colo | the fleet shares colos more than assumed: F1's case is stronger than `wx-cost.md` says |
| **F1-H55** | the fleet-weighted hit rate at T = 55 s is at most 25% | sharing is high: even a short hold pays |
| **F1-H115** | at T = 115 s it is in `[50%, 75%]` | reported with the histogram |
| **F1-CTL** (the model's control, run before the data) | a lone board (k = 1) simulates to 0% at T = 55 s and 50% (+/-2%) at T = 115 s; k = 10 at T = 55 s simulates above 75% | the simulation is wrong: stop before any data |
| escape | a sampled hour; more than 5% of the hour's `req` lines without a colo or a board key; the two reads differ or are taken under 60 s apart; anything no row names | stop and report, with no reading |

**The trade the design will have to state** (not graded here): a hold of T delays a new frame by up to T. Frames
arrive about every 2 minutes, so at T = 115 s a board can show a frame one cycle late. That is visible, and it makes
T the owner's call.

**At fleet scale:** 600 boards share colos far more than today's 20 do. So today's histogram is a LOWER bound on F1's
saving, and it says so.

**The reader:** `tools/bill/colo_occupancy_read.py`, committed with its blob sha before it runs. Read-only; no
wrangler; the token file's `observability-read`, never printed; error paths print the exception type only.

**When:** after step 4d's read. The hour is the last closed hour at that time. It runs before Thursday's PC quiet
(16:25Z): a multi-page reader is not allowed during it.

CORRECTION (append-only): keys on devl only per the reviewer's 2026-11-19 ruling; a dev line refuses (reader :121-123).
CORRECTION (append-only, 2026-11-20T04:57:42Z from date -u): **When** moved to SATURDAY 2026-11-21, by ruling (the logs deploy moved there). F1 starts at the later of 14:00Z and map-server's row-5 END line (rollback end 14:30Z at worst), on the orchestrator's start word. It runs: scan, read A, read B at least 60 s later, grade. The hour is the last closed hour at the start (13:00-14:00Z when the scan reads clean); the fallback is the 14:00Z hour from 15:05Z; the hard end is 16:30Z. V19S's two E2:08 boots in that window need no avoidance: F1 counts distinct boards per colo, E2:08 is one board with one `devl` either way, and a boot's extra lines sit far under the 5% escape.

## Step 4e: lever (c), what the refresh-data job writes (registered 2026-11-19T00:14:57Z, before its reader exists; read-only, GitHub only)

**Why.** Step 4c showed that production's KV writes are the Worker's request path, plus the refresh-data job at about
**66,000 writes a run in EACH environment** (11-16: production 66,338, staging 65,708 above background). The job's two
ingests explain that number differently:
- `ingest-mildb.ts` bulk-puts EVERY row, every run. Its header says "~17k rows".
- `ingest-airports.ts` diffs by shard hash: "do not blind-upsert 34k keys on every run" (`:363`).

So about 49,000 of each run's writes are not accounted for, and lever (c)'s design cannot start until they are.

**The read.** No Cloudflare call, only GitHub:
- the two runs step 4c measured: run `42655120486` (2026-11-09) and `43901417755` (2026-11-16);
- each one's job logs, from the GitHub API, per environment's job;
- **counts only:** the rows each ingest says it bulk-put, and the diff's own summary of shards written and skipped.

The logs hold no key or value, only counts and shard letters. The reader prints counts per ingest, per environment,
per run.

| row | predicted | if not |
|---|---|---|
| **JW1** | `ingest-mildb` bulk-puts **15,000-20,000** rows, per environment, per run | the header's "~17k" is stale: report the count |
| **JW2** | `ingest-airports` writes the rest: about **45,000-52,000**, per environment, per run. Its shard diff does NOT skip the shards (an `ap:` or tile family rewritten whole each run) | if airports writes about 0, the ~49k is a third writer in the job: name it from the log before any design |
| **JW3** | per environment, per run, the two ingests sum to step 4c's measured job writes within 2% (11-16: 66,338 production, 65,708 staging) | the logs and KV analytics disagree: the instrument, stop |
| escape | a log that has expired or is unreadable; a count line missing from a run that succeeded; anything no row names | stop and report, with no reading |

**What it is for:** lever (c)'s design.
- If JW1 and JW2 hold, both ingests write whole families every week.
- The design is a shard diff for mildb (airports' own method, already in the tree), plus the reason airports' diff
  does not skip.
- That takes the account's ~530,000 job writes a month (4 Mondays x 2 environments x ~66,000) towards 0, and every
  change stays a CI script change, not a Worker deploy.

### Step 4e's read and its grades (2026-11-19T00:23:41Z; reader blob `57666a9f5c88`)

**The reads.** Read A ran at 00:21:44Z and read B at 00:23:01Z, 77 s apart, and they are identical. All four jobs
succeeded, and stderr was empty. Counts only, from each job's log held in memory.

| run | environment | `mil:` rows | `ap:` keys (shards written / changed) | sentinel | `apt:` tiles | sum | step 4c measured | diff |
|---|---|---|---|---|---|---|---|---|
| 11-09 | production | 17,616 | 38,652 (35 / 35 of 36) | 1 | 9,400 | 65,669 | 65,754 | -0.1% |
| 11-09 | staging | 17,616 | 38,652 (35 / 35 of 36) | 1 | 9,400 | 65,669 | 65,678 | -0.0% |
| 11-16 | production | 17,637 | 38,656 (35 / 35 of 36) | 1 | 9,402 | 65,696 | 66,338 | -1.0% |
| 11-16 | staging | 17,637 | 38,656 (35 / 35 of 36) | 1 | 9,402 | 65,696 | 65,708 | -0.0% |

**Grades:** **JW1 HOLDS** (mildb 17,616-17,637). **JW2 HOLDS** (airports 48,053-48,059). **JW3 HOLDS** (the sums sit
within 1.0% of step 4c's measured job writes). Escape not taken.

**What the logs say that the registration did not predict** (recorded, not graded):
- **The `ap:` diff ran WITH its prior meta** ("no previous meta" absent in all four jobs). Even so, it counted **35 of 36
  shards as changed** in both weeks, and rewrote about 38,650 keys each time. Its key total moved by 4 between the weeks.
- **Two explanations fit, and these counts separate neither:**
  - (i) the upstream genuinely edits a few rows a week, spread across most shards. At about 1,000 keys a shard, one
    edited row rewrites its whole shard;
  - (ii) the shard hash covers something that changes every run, even when the rows do not.

  Telling them apart needs the source of `shardHash` and two weeks' inputs. That belongs to the fix PR's registration,
  not to this read.
- **`mil:` is a blind upsert,** as its header says: every row, every run.
- **`apt:` tiles are a blind upsert too:** about 9,400 a run.

**So lever (c)'s fix** (a later PR with its own registration, after Thursday's logs step and Friday's route run) has
three parts:
- a key-level diff for `mil:`;
- the same for `apt:`;
- the cause of `ap:`'s 35/36, found and fixed: either a row-level diff, if (i), or a stable hash, if (ii).

Together these are about **65,700 writes a run in each environment, about 525,000 a month** across the two environments
(four Mondays). Today that is about half the account's free million.

### Step 4d's read and its grades (2026-11-19T00:34:34Z; reader blob `56917f86af58`)

**First, an attempt that produced nothing.** A background job started read A at 00:31:03Z and read B 70 s later, and
both exited 1. **The cause was not seen, because the job discarded their stderr.** That was my error, and the job
gives no reading.
- Its grade found no file to grade (exit 2, the rig).
- Before the reads, it confirmed the run list again: **no KV-writing job (refresh-data, photos, routes-refresh) ran on
  2026-11-18.**

**The reads graded:**
- Read A ran in the foreground at 00:32:32Z, and read B at 00:33:37Z, 65 s later, behind a wait that would not start
  under 65 s.
- stderr was kept: only the token file's permission note.
- The two reads agree.

**The anchor:** 2026-11-17 reads exactly as step 4c read it: production reads 416,175, writes 2,196, boards 19.

**2026-11-18:** production reads **410,808**, writes **2,471**, boards **21**; the account's reads 411,421.

| row | result | grade |
|---|---|---|
| **PB1** | R_b = 30 x 410,808 / 21 = **586,869** reads a board a month, in `[480,000, 750,000]` | **HOLDS** |
| **PB2** | W_b = 30 x 2,471 / 21 = **3,530** writes a board a month, in `[2,500, 4,500]` | **HOLDS** |
| **PB3** | **a new board costs $0.311 a month** in KV past the free tiers: reads $0.293 + writes $0.018 | **HOLDS** |
| escape | not taken | |

**The trigger:** 411,421 x 30 = 12,342,630 reads, so the projected read overage is **$1.17 a month**. That is not past $5.

**What PB1 means at fleet scale** (arithmetic from the graded figures, not a new grade):
- 600 x 586,869 = **352.1 M reads a month**, which is **$171 a month** past the free 10 M.
- That is an UPPER bound, for the reason the registration gave. Step 4b2's fixed floor (gamma, about 16% of reads)
  is spread over the boards here, and `ac:` is shared across the fleet.
- Section 1's $115 used 401,000 reads a board a month, from a different week and a weekly denominator. It stays beside
  this figure, and is not replaced here.

## Step 4b2-D: lever (b2)'s design, an isolate cache over `/enrich`'s static families (registered 2026-11-19T00:34:34Z; nothing built or deployed)

**What it starts from** (step 4b2, as graded):
- `/enrich` requests are about **61%** of production's KV reads.
- **At least 60%** of the chain's reads are families that change only at an ingest or a photo publish (KE4).
- KE1 is OFF: the two instruments disagree about reads per request, 5.2 (step 4b2's fit) against 7.4 (step 2b's
  whole window). **So the saving below is stated as a range spanning both, and the design measures the number itself**
  (the instrument below).

**The change** (`proxy/src`; the same shape as #689's `fw:` memo):
- **One module-level cache per isolate,** in front of the static families only, keyed by the KV key.
- It holds a HIT and a MISS alike: a key that is absent is a KV read too.

| family | read at | TTL in the isolate | why that long |
|---|---|---|---|
| `tn:<type>` | `enrich.ts:102`, `:182`, `:699` | 1 h | changes only when a type name is added by hand |
| `rt:<CS>`, the mirror | `enrich.ts:486` | 1 h | the route ingest is weekly |
| `ap:<code>` | `enrich.ts:208` (`routeVerdict`, twice a request) | 1 h | refresh-data, weekly |
| `ovr:`, `pa:`, `mil:` `<hex>` | `enrich.ts:673-675` | 1 h | refresh-data and hand edits |
| the photo pointers, type and hex | `photos.ts:131` | **10 min** | a photo publish should show within minutes, not an hour |

- **Never cached:** `ac:<hex>` (written on every miss, with short negative TTLs that the 2026-08-12 fix depends on);
  the bucketed `rt:<cs>:<bucket>` (a runtime cache of its own); `fw:` (#689's own memo); anything outside `enrich.ts`
  and `photos.ts`. A test pins this list: a read of an uncached family must reach KV every time.
- **The bound:** at most 8,192 entries per isolate, the oldest dropped first. Values are short JSON, about 200 B each
  at most, so about 1.6 MB, against an isolate's 128 MB.

**The instrument, specified with the writer** (CLAUDE.md, "an instrument that fires into a void"):
- Two doubles are APPENDED to the request point: `double5` = KV reads this request made, and `double6` = reads the
  cache answered. Append-only: no existing blob or double moves.
- **The reader:** `tools/bill/enrich_reads_read.py` gains one line: reads per non-SKIP `/enrich` request, weighted as
  every reader here weights them, `sum(_sample_interval x double4 x double5) / sum(_sample_interval x double4)`, and the
  same for `double6`. It prints at every proxy-bill read, beside the trigger, so it is read where
  a person already looks.
- This is also what settles KE1: a measured count per request, not a fit.

**Predictions** (graded on the first whole UTC day after the deploy, which is the owner's call; it goes with #689,
after v20's first week, not before):

| row | predicted | if not |
|---|---|---|
| **BD1** | KV reads per non-SKIP `/enrich` request, from `double5`: in `[1.5, 3.5]`, down from the range the two instruments left (5.2 to 7.4) | the static families hit less than KE4 implies, so per-isolate reuse is low: the Cache API (per colo) is the named next step |
| **BD2** | cache answers per non-SKIP request, `double6`: at least 2.0 | as BD1 |
| **BD3** | `double5 + double6` per request is within 15% of that range (5.2 to 7.4): the cache moved reads, it did not change what the chain asks for | the instrument miscounts: stop before any saving is claimed |
| **BD4** | production's daily KV reads per board (`R_b`, step 4d's method) fall by `[20%, 40%]` from the 7 days before the deploy | reported with the shares |
| **BD-STALE** (a stated cost, not graded) | after an ingest, an isolate can serve a static row up to 1 h old; after a photo publish, up to 10 min | |
| escape | a family outside the table answered from the cache; `double5`/`double6` missing on any request row; anything no row names | roll back to the version before (named at the deploy), and report |

**The account, December, 600 boards** (from step 4d's graded `R_b`, 586,869 reads a board a month, and the trigger's
formula):
- **Today's design point:** 600 x 586,869 = 352.1 M reads, which is **$171 a month** past the free 10 M. This is an upper
  bound (step 4d).
- **With this cache, at BD4's band:** 211-282 M reads, which is **$100-$136**.
- **With #689's `fw:` memo as well:** `fw:` is about 24-28% of reads (step 4b2's other-device term, step 2b's share).
  The two together remove about **44-68%**: 113-197 M reads, which is **$51-$94** a month.
- **The ranges are wide,** because both instruments disagree (KE1) and the per-isolate reuse has never been measured.
  The instrument above is what narrows them after the deploy.

**Controls, red first, before the code PR** (vitest; never during Thursday's PC quiet):
- each family hits within its TTL and misses after it;
- a cached MISS is a MISS until its TTL;
- the 8,193rd entry drops the oldest;
- an uncached family reaches KV on every read;
- a KV write within the TTL is NOT seen. That is the staleness, stated as a test, so nobody mistakes it for a bug.
- **The sabotage driver:** each control is turned red by its own plant.

**What it needs, and whose call each part is:** the code PR (a reviewer's merge) is mine. The deploy is the owner's,
with #689. Changing a TTL is a code change with its own registration.

## Step 5, amendment F1-R: the F1 reader makes a `dev` line unreachable (registered 2026-11-19T13:24:03Z, before the change exists; nothing runs before the logs step's new slot)

**Why** (the reviewer, 2026-11-19). The reader at blob `af55507b9ad7` refuses a `dev` line only after it has fetched it.
- Its query carries no filter at all (`"parameters": {}`, `colo_occupancy_read.py:107-108`). Every event of the hour, from
  every Worker on the account, comes into the process.
- `:121-123` then refuses on the first req line that holds `dev`.
- So any hour holding one old-version line pulls device ids into memory before the refusal. That includes an hour after
  the logs deploy, if the window starts before the new version reached 100%.
- The change makes such a line unreachable. `:121-123` stays as the backstop.

**Read from the artifact at this freeze.** One read-only call to the telemetry keys endpoint, at 2026-11-19T13:12:39Z
(names only, never values; outside every window, and the reviewer recorded it as fine). Hour 2026-11-19T12:00Z-13:00Z;
one leaf, `$metadata.service eq blipscope-proxy`.
- 156 keys came back.
- `evt`, `dev`, `colo` and `$metadata.service` are present as bare names. There is no `source.*` form.
- `devl` is absent, because production (`fcc33692`) still writes `dev`.
- The filters below use exactly those names.

**What the API documents** (Cloudflare API reference, Workers Observability, telemetry query; read 2026-11-19):
- A filter leaf's `operation` includes `eq`, `exists` and `DOES_NOT_EXIST`.
- `parameters.filterCombination` is `"and"` or `"or"`, default `"and"`. A group is
  `{kind: "group", filterCombination, filters}`.
- `view: "calculations"` takes `{operator: "count"}`.
- The Query Builder page lists "Exists" and "Does not exist" for string fields.
- A peer session saw two bare leaves act like OR on the keys endpoint (2026-11-17). So every query here states
  `filterCombination: "and"`, and the live controls below test it rather than trusting the default.

**The change** (`tools/bill/colo_occupancy_read.py`). One function builds every filter set, so the read and the live
controls share one query form.
1. **Scope, on every query:** `$metadata.service eq blipscope-proxy` AND `evt eq req`.
2. **The pre-flight, a count only:** `view: "calculations"`, `count`, over the hour, scope AND `dev exists`.
   - A nonzero count: `REFUSED (pre-flight, exit 5)` BEFORE any events request. No line is fetched.
   - A failed count: RIG (exit 2), with no events request either.
   - This also refuses a mixed hour (the deploy reaching 100% inside it), so a reading never comes from a part-old
     hour, where the filter would silently drop the boards that spoke before 100%.
   - What it touches: one number, the count of the hour's req lines that carry the `dev` key, tested on the server.
     No id and no line come back. **The reviewer rules whether this is allowed under the devl-only rule.** The named
     alternative is no pre-flight, the filter alone. That fetches no `dev` line either, but a mixed hour then reads LOW
     with no signal: a dropped row that looks like data.
3. **The events query:** scope AND `dev DOES_NOT_EXIST`, `filterCombination: "and"`. A `dev` line is excluded on the
   server even if the count missed it.
4. **The backstop, unchanged:** `:121-123`'s refusal of a fetched req line that holds `dev`, now worded
   `REFUSED (backstop, exit 5)`.
5. **New output:** `preflight dev count C`, and `fetched N event(s), M with dev`. Every event the API returned is
   counted before any local test, so a fetched `dev` line is counted, not just refused.

After the deploy no new line carries `dev`, so the filter drops nothing the old reader kept. The measured population
(the hour's req lines from `blipscope-proxy`) is the same, and the frozen rows F1-K, F1-H55, F1-H115, F1-CTL and the
escape (`:1407-1413`) are unchanged.

**When `devl` first appears is measured, not assumed.** The `scan` step: count only, in the new slot, before the
first read.
- From the last closed hour backward, one hour at a time, it makes two counts per hour: scope AND `dev exists`, and
  scope AND `devl exists`. Calls are at least 60 s apart.
- It stops at the first hour (going back) where `devl` counts 0, which is before the deploy, or after 6 hours.
- It prints counts only: one row per hour (`HH:00Z dev C1 devl C2`), then two lines:
  - `devl first appears in the hour starting X`, or `before X` if 6 hours were not enough;
  - `first clean hour X`: the first hour, going forward, with `dev` 0 and `devl` above 0.
- No count here is graded; they set the hour.
- An hour with `dev` above 0 AFTER a clean hour means the old version came back (a rollback): stop and report.

**The hour.** As frozen (`:1425`), the last closed hour at the read, and now only if the scan found it clean. If it is
not clean, the read waits for the next closed hour, which the pre-flight checks again. If the pre-flight refuses an
hour the scan called clean, the next closed hour is taken, ONCE. A second refusal: stop and report, with no reading.

**The plant, red first.** Offline; synthetic data only; no network.
- A fake API (in the plant harness, not the reader) answers from a fixed event list and RECORDS every event it returns.
  It implements the timeframe, the `eq`/`exists`/`DOES_NOT_EXIST` leaves, `filterCombination` and groups, `events`
  paging (`limit`, `offset`), and `calculations` `count`.
- **The fake's own controls run first:** an impossible `evt` value AND the service counts 0; the scope alone counts
  exactly the plant's req lines.
- **The plant's hour** H = 2000-01-01T00:00Z (synthetic). It holds:
  - three labelled req lines, two in colo `ZZ1` and one in `ZZ2` (synthetic codes and labels);
  - one unattributed req line (a colo, no label);
  - one non-req event, and one event from another service;
  - **`D-in`**: a req line with `dev = "0000000000000000"` (the example id) inside H;
  - **`D-before`**: the same, 1 s before H.
- The harness runs the CURRENT reader (`af55507b9ad7`, from `git show`) and the NEW reader against the same fake, with
  the token read replaced. The fetched counts are the fake's record, so both readers are measured by one instrument.

| row | run | predicted | if not |
|---|---|---|---|
| **R-RED** | the current reader, H with `D-in` | the fake returned 7 events, **1 with dev**; exit 5 after the fetch. That is the defect, shown | it did not plant: stop |
| **R-PRE** | the new reader, H with `D-in` | 1 count call, **0 events requests, 0 events returned**; exit 5 from the pre-flight | the change does not hold: stop |
| **R-CLEAN** | the new reader, H without `D-in` (`D-before` still present) | 1 count call (0), then 4 events returned, **0 with dev**; exit 0; req lines 4, no colo 0, no label 1, histogram `{"1": 1, "2": 1}` | stop |
| **R-BACKSTOP** (the control that the counter can see a dev line) | the new reader, H with `D-in`, against a fake told to IGNORE filters and answer every count with 0 | 7 events returned, **1 with dev**; exit 5 from the backstop | if it reads 0 with dev, the counter is blind and R-PRE's 0 proves nothing: stop |
| **R-SCAN** | `scan` on three synthetic hours ending at H: H-2 has one `dev` line and no `devl`; H-1 has one `dev` line and two `devl` lines; H has `devl` lines only (R-CLEAN's three labelled lines) | rows: H dev 0 devl 3; H-1 dev 1 devl 2; H-2 dev 1 devl 0, then it stops. `devl first appears in the hour starting H-1`; `first clean hour H`; 6 count calls, **0 events requests, 0 events returned** | stop |

`D-before` is outside H's timeframe, so the timeframe alone already keeps it out of BOTH readers. It is in the plant as
a control on the timeframe, not as a test of the change. The in-hour line is the one only the change excludes.

**Live controls.** Count only (`view: "calculations"`, never `events`); built by the read's own function; against
production; before the first F1 read in the new slot.
- The hour L = 2026-11-19T12:00Z: closed, before the deploy, with production writing `dev`. It stays readable until
  2026-11-26.
- Four calls, at least 60 s apart.

| row | count | predicted | if not |
|---|---|---|---|
| **LC-1** | scope | > 0 | the key form `evt eq req` does not match: stop, F1 does not run |
| **LC-2** | scope AND `dev exists` | > 0 and <= LC-1 | the pre-flight is blind to a `dev` line: stop |
| **LC-3** | scope AND `dev DOES_NOT_EXIST` | < LC-1 | the operation is ignored: stop |
| **LC-4** | service AND `evt eq __f1r_impossible__` | 0 | the leaves combine as OR, not AND: stop |
| LC-SUM (recorded, graded only if L is unsampled) | LC-2 + LC-3 | = LC-1 | reported |
| escape | anything no row names | | stop and report; F1 does not run |

**Order.**
1. The change is committed with its blob sha. A reviewer reads it.
2. R-RED, R-PRE, R-CLEAN, R-BACKSTOP and R-SCAN run offline, in that order, red first. Not during Thursday's PC quiet.
3. In the logs step's new slot, outside any PC quiet: LC-1 to LC-4, then `scan`, then the two reads at least 60 s apart,
   then the grade.
4. F1 runs as frozen at `:1378-1426`, with this amendment.

The code commit and the plant are mine; the reviewer reads both. Nothing deploys.

CORRECTION (append-only): a failed count is BLIND exit 6 per F1-R-1; F1-R's RIG/exit 2 for it is superseded.

ADDED (append-only, before any row graded it): scan: 0 = last closed hour clean; 3 = not clean yet / no devl / rollback, each with its line.

## Step 5, amendment F1-R-1: a count is a value only at ABR 1 (registered 2026-11-19T13:27:50Z, the reviewer's condition on F1-R; append-only, nothing above is edited)

**Why** (the reviewer, 2026-11-19, from map-server's finding the same day). The logs API applies QUERY-TIME sampling.
- That sampling is `statistics.abr_level`. It does not show in `sampleInterval`, which is INGEST sampling.
- Above ABR 1, real events can read 0. map-server saw staging's 7 read 0, and production's two plants each read 0.
- As F1-R was written, a sampled pre-flight or scan could read `dev` 0 and call a mixed hour clean. That is the silent
  gap F1-R exists to close.

**The rule:** map-server's rule (i), `logsfix26.design.md` (sha256 `ebc916cd19f4`, local-only-20261119), applied to
every query this reader makes: the pre-flight, the scan, LC-1 to LC-4, and every events page.
- **A query is a VALUE only if** all of these hold:
  - HTTP 200;
  - a `statistics` block is present;
  - `statistics.abr_level` is PRESENT and == 1 (an absent `abr_level` is not taken as 1);
  - for a count, every `aggregates[].sampleInterval` is 0 or 1, as the reviewer worded it. An empty `aggregates`
    (a count of 0) has no sampleInterval to fail.
- **Anything else is BLIND.** A BLIND count is never 0, never a pass and never OFF, and its number is never printed as
  a value.
- **A "0" is exact only from a value** (ABR 1 on THAT query).
- **Bounded re-ask, chosen on resolution only.** A query that is not a value is re-asked up to 2 more times, 30 s apart.
  The first attempt that is a value is taken. The choice never looks at the count.
  - Every attempt is recorded: status, `abr_level`, sampleInterval, and the count only on attempts that are values.
  - A query with no value after 3 attempts stays BLIND.
  - The 30 s is a module constant; only the plant harness sets it to 0.
- **What BLIND stops:** a new exit, **6 BLIND**: "a query never reached ABR 1; F1 does not run on it". F1 does not run
  on a BLIND pre-flight, a BLIND scan count, a BLIND LC row or a BLIND events page.
  - An events page that is not a value is re-asked like a count; after 3 attempts the read stops BLIND, and its partial
    histogram is discarded and never written.
- **The frozen escape's "a sampled hour"** (`:1413`) now means: any page whose `abr_level` is absent or not 1. The
  reader at `af55507b9ad7` accepted an absent one (`:168`, `[]`); that acceptance goes.
- **Recorded in the result file:** per query, every attempt's four statistics keys, as map-server's `logsoff26s`
  does, so a reading can be re-graded without a re-run.

**LC-1 to LC-4 are graded only at ABR 1,** each one a value by the rule above. LC-SUM is now GRADED, because all three
of its counts are values: LC-2 + LC-3 = LC-1 exactly. If not, the counts disagree at full resolution (map-server's F5
class): stop, and F1 does not run.

**The plant gains one row, red first:**

| row | run | predicted | if not |
|---|---|---|---|
| **R-ABR-RED** (the sabotage) | the new reader with its ABR gate removed by a byte-asserted plant; H with `D-in`; the fake answers the PRE-FLIGHT with `abr_level` 100 and a count of 0 (a sampled zero), and everything else at ABR 1 | it goes on past the pre-flight and reads the mixed hour: 4 events returned, 0 with dev, exit 0, histogram `{"1": 1, "2": 1}`. That is the silent gap, shown | the plant did not plant: stop |
| **R-ABR** | the new reader, intact; the same fake | 3 pre-flight attempts, all at `abr_level` 100, each recorded without a count; **0 events requests, 0 events returned**; exit **6 BLIND** | the gate does not hold: stop |
| **R-ABR-ABSENT** | the same, but the fake OMITS `abr_level` on the pre-flight | as R-ABR: 3 attempts, exit 6, 0 events requests | an absent level is being taken as 1: stop |
| **R-ABR-RECOVER** | the fake answers the pre-flight's first attempt at ABR 100 and its second at ABR 1, with the true count 1 | 2 attempts; the second is the value; pre-flight count 1; exit 5 from the pre-flight; 0 events requests | the re-ask does not hold: stop |

R-RED, R-PRE, R-CLEAN, R-BACKSTOP and R-SCAN are unchanged; their fake answers at ABR 1 unless a row says otherwise.
The order of section F1-R's **Order** stands, with these four rows run after R-SCAN.

ADDED (append-only, 2026-11-19T13:55:22Z, before it ran; the reviewer's ruling (4) on F1-R-1's recording clause): R-REC, red first. `lc` on a synthetic LC hour (scope 5, dev 1, no dev 4) whose LC-2 first answers at ABR 100 with count 0, then at ABR 1 with the true count. R-REC-RED, the reader at blob `74556db07510`: exit 0, and no `attempts LC-2:` line and no record file. R-REC, the fixed reader: exit 0; printed `attempts LC-2: 1: status 200 abr 100 (not a value); 2: status 200 abr 1 count 1 (value)`; the JSON holds the same two attempts, `[1, 100, not a value, no count]` and `[2, 1, value, 1]`. R-REC-SCAN-RED and R-REC-SCAN, the same for `scan` on R-SCAN's hours: 0 `attempts scan` lines and no record against `74556db07510`; against the fixed reader 6 lines, and a record of 6 attempts, each a value on its first try, with R-SCAN's three rows.
