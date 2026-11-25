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
AMENDMENT F1-S (append-only, 2026-11-20T10:22:11Z from date -u): the orchestrator's rulings for Saturday's run (~10:20Z). Nothing above is edited.
- **F1-CTL runs AGAIN Saturday, before the data** (`ctl`). A control from another day does not prove Saturday's environment; 11-19's HOLD is history.
- **LC-1 to LC-4 RUN in the slot** (`lc`), as the order says (:1713). "No LC re-run" was scoped to 11-19. This slot is where the recording fix (R-REC) is proven live.
- **One new folder,** `C:\Github\_handoffs\blipscope-5c\f1-20261121\`. Its creation REFUSES if it already exists (a plain `mkdir`, no `-p`). Every output is named here, before Saturday, each command's stderr to its own `.err`:
  - `ctl.out`
  - `lc.json`, `lc.out`
  - `scan.json`, `scan.out`. The paced `scan` runs in the BACKGROUND, registered in the bg ledger, with bgcheck at the end.
  - `readA.json`, `readA.out`
  - `readB.json`, `readB.out`
  - `grade.out`
  - the fallback's `readA2.json`, `readA2.out`, `readB2.json`, `readB2.out`, `grade2.out`
  - `bgcheck.out`
- **A BLIND (exit 6) on the 13:00Z hour's reads is an instrument outcome, not a red.**
  - ONE fallback is pre-registered: the 14:00Z hour's reads, from 15:05Z (:1429), into the `*2` files.
  - A second BLIND means STOP and report to the orchestrator, with no third attempt.
- **map-server's row-5 END line reaches 5c only inside the orchestrator's start word,** quoted with its stamp.

AMENDMENT F1-D (append-only, registered 2026-11-22T05:15:35Z from date -u, before any of it runs): F1's DATA half, Sunday 2026-11-22. Nothing above is edited. Why: Saturday's CTL and LC ran and held (`f1-20261121/`: CTL HOLDS; LC-1 3695, LC-2 3659, LC-3 36, LC-4 0; LC-SUM exact; all at ABR 1), but production was not deployed, so the data half was rebooked to the first full closed hour after production's logs deploy reaches 100%. The owner granted the logs exception (03:52Z); the deploy runs today, T0 06:00Z, END ~08:55Z, worst 09:10Z, hard end 09:45Z (the orchestrator, 05:11:40Z). F1 reads production's Workers Logs only: no deploy, no proxy request, no wrangler. Reader blob `50db3cb8cb2a`, unchanged.
- **The key: the END line, as the orchestrator relays it, with its stamp and production's `/healthz` commit `<D>`.** No END line, or an END that says no production deploy happened: F1 does not run today, and a new slot is asked.
- **The hour H0 = the first whole hour that starts at or after the END stamp.** An END at or before 09:00:00Z gives H0 = 09:00Z; an END in (09:00Z, 10:00Z] gives H0 = 10:00Z (the orchestrator's "likely 10:00-11:00Z"). H0 is passed EXPLICITLY to `scan` and `read`, so a slipped start cannot change the hour. An END after 10:00Z is outside the logs step's own hard end (09:45Z): stop, and ask.
- **The windows.** `ctl` and `lc` run after the END line (they read no hour of today's). `scan` starts at H0 + 1 h + 5 min. **HARD END 12:20Z,** before KBOS's PC quiet (from 12:25Z): nothing of F1 runs after it, and a step that cannot finish by then does not start.
- **bgcheck before EVERY start;** each background start is registered in the bg ledger. Today's known outcome is VOID exit 4 on exactly five pids: DefenderSessionHelper 13848, ctfmon 13932, logioptionsplus_appbroker 16340, MSI.TerminalServer 20184, taskhostw 59348, with a cmdline search of 0. Any other pid: stop, and it goes to the orchestrator.
- **The folder** `C:\Github\_handoffs\blipscope-5c\f1-20261122\`, by a plain `mkdir` that refuses if it exists. `f1-20261121\` is not touched. File names as F1-S, each command's stderr to its own `.err`: `ctl.out`; `lc.json`, `lc.out`; `scan.json`, `scan.out` (in the background, registered, bgcheck after); `readA.json`, `readA.out`; `readB.json`, `readB.out`; `grade.out`; the fallback's `readA2.json`, `readA2.out`, `readB2.json`, `readB2.out`, `grade2.out`; `bgcheck.out`; and `stamps` (each step's start and exit, from `date -u`).

| row | when | predicted | if not |
|---|---|---|---|
| **D-FIELDS** | after the END line, read-only git at `<D>` | `<D>` is on origin; at `<D>`, `proxy/src/metrics.ts` `record()` strips `dev` from the console line and logs `{evt: "req", ...}` (main `cbd999db594f`, `:237-238`); the line carries `colo` (`index.ts:453`) and `devl` (`index.ts:370-371`, deterministic: the first 8 hex of SHA-256 over the id, `devlabel.ts`); `wrangler.toml` production `head_sampling_rate = 1.0` (`:409`) | any of these differs: STOP before any query, report |
| **D-CTL** | first | the same line as 2026-11-21, exactly (the simulation is seeded): `k=1 T=55 0.000`, `k=1 T=115 0.500`, `k=10 T=55 0.900 -> HOLDS`, exit 0 | stop before any data (`:1412`) |
| **D-LC** | after D-CTL | LC-1 to LC-4 and LC-SUM HOLD as frozen (`:1717-1723`, F1-R-1), AND each value equals 2026-11-21's exactly: **3695, 3659, 36, 0**. L (2026-11-19T12:00Z) is a closed hour, readable until 11-26 | a value that differs from 11-21's: the instrument read one closed hour two ways. STOP, F1 does not run, report both readings. A BLIND: as F1-R-1 (exit 6), STOP |
| **D-SCAN** | `scan scan.json H0`, in the background, at H0 + 1 h + 5 min | the H0 row reads `dev 0`, `devl` > 0; going back, it stops at the first hour with `devl 0`; `devl first appears in the hour starting X`, with X at or after 06:00Z (the step's T0) and at or before the END's hour; `first clean hour` at or before H0; `last closed hour H0: clean`; exit 0. At most 12 count calls, 60 s apart | exit 3 with "not clean" (`dev` > 0 in H0, the old version still serving): the fallback below. Exit 3 ROLLBACK, `devl first appears before ...`, X before 06:00Z, or `devl` 0 in H0: STOP, report. Exit 6: STOP |
| **D-READ** | read A `read H0 readA.json`; read B the same into `readB.json`, **started by a check that refuses under 65 s after read A's exit stamp** | read A: `preflight dev count 0`; `fetched N event(s), 0 with dev`; ABR `[1]`; `no colo 0`; exit 0. Read B: the same, apart from `read_at` and `attempts` | pre-flight exit 5: the fallback below. Exit 6: the fallback below. A backstop exit 5: STOP |
| **D-GRADE** | `grade readA.json readB.json` | the reads agree; F1-K, F1-H55 and F1-H115 graded as frozen (`:1409-1411`); its own escapes not taken | exit 3 ESCAPE: stop and report, no reading. Exit 4: the 65 s check failed, a rig error: STOP |
| **D-LABEL** (by hand, from `readA.json`) | after D-GRADE | `no_label` is at most 5% of `req_lines` | over 5%: the frozen escape's "or a board key" half (`:1413`) is TAKEN: no reading, report. **The grade does not check it:** `grade()` tests only `no_colo` against 5%, so this half is graded here from the result file. Expected near 1% (L's 36 of 3695 req lines carried no `dev`) |
| **D-BOARDS** (by hand, from the histogram) | after D-GRADE | boards in H0 (the sum of k x colos) at most 42, twice step 4d's 21 boards a day | above 42: the histogram is not one entry per board. Escape, no reading, report. Fewer than 10 is reported and graded as usual |
| escape | any step | anything no row names | stop and report; nothing is improvised |

- **ONE fallback in total,** whichever triggers it (a not-clean scan, a pre-flight exit 5, or a BLIND read): hour H0 + 1, reads only (its pre-flight is its `dev` check), into the `*2` files, starting at H0 + 2 h + 5 min, and only if it can end by 12:20Z (so with H0 = 10:00Z it must start by 12:08Z). A second trigger: STOP, with no third attempt.
- **After:** the record goes into this doc below F1-D, and #688 is pushed once with F1-S, Saturday's CTL and LC, and today's record. The CI prediction is stated before the push.

F1-D, ADDED (append-only, 2026-11-22T05:18:54Z from date -u, before anything runs; the reviewer's 05:17:21Z rulings and question). **Ruled:** H0 as defined above, no extra margin; D-LABEL and D-BOARDS kept; the reader's `no_label` fix comes after F1, as its own registration, red first. **The question:** the site's #19 merges at 10:00Z and turns its feed on at ~10:02Z, calling production's proxy through its `BendFeed` binding about 360 times an hour. Do those calls produce `req` lines? **Answer, from the code: no. They produce no `req` line at all, so they are neither counted in `req_lines` nor classified as `no_label`.**
- **The site makes an RPC call, not a fetch.** valarscopes-site #19 at its head `85845783c8af`: the binding is `{ "binding": "BLIPSCOPE_BEND", "service": "blipscope-proxy", "entrypoint": "BendFeed" }` (`wrangler.jsonc:20`, `:49`). Its only use in `src/` is `this.env.BLIPSCOPE_BEND.snapshot()` (`src/areafeed.ts:33`); no `.fetch()` on that binding appears anywhere in `src/`.
- **On the proxy (main `cbd999db594f`):** `BendFeed` is a named `WorkerEntrypoint` whose one method `snapshot()` returns `readSnapshot(this.env)` (`proxy/src/sitefeed/bend.ts:364-368`). `readSnapshot` reads `caches.default` and, on a miss, the Durable Object's own `fetch` (`/get`). `bend.ts` contains no `console.` call. Its only metric is an Analytics Engine point (`count()`, `:174-180`), and that is called only on the device-path writer (`:209-227`), not by the reader.
- **The `req` line has one writer:** `console.log(JSON.stringify({ evt: "req", ...line }))` (`metrics.ts:238`), inside `record()`, which is called once, at `index.ts:464`. That call is inside `export default { fetch }` (`:444-464`). An RPC call to a named entrypoint does not run the default `fetch`, and the Durable Object's `fetch` is its own class's handler.
- **The named alternative, and why the data excludes it:** Workers Logs could store an invocation event for each RPC call under service `blipscope-proxy`. Such an event carries no `evt` field, because `evt` exists only on our own console JSON lines, so the server-side scope `evt eq req` excludes it (`colo_occupancy_read.py`'s `params`, and locally `req_fields`). Also, production's invocation logs are off (`wrangler.toml:413-414`, `invocation_logs = false`), which D-FIELDS confirms at `<D>`. The scope alone is enough.
- **So H0 = 10:00Z is not exposed, and neither is a 10:00Z fallback hour.** With H0 = 09:00Z, the fallback (10:00Z) runs as registered, with the same rows.
- **The check that runs, rather than this paragraph:** D-LABEL itself. Two rows are added:

| row | when | predicted | if not |
|---|---|---|---|
| **D-FIELDS-2** | with D-FIELDS, read-only git | at `<D>`, `proxy/src/sitefeed/bend.ts` and `index.ts`'s `record(env, meta)` call site are as at main `cbd999db594f`: one `record()` call, inside the default `fetch`; `BendFeed.snapshot()` returns `readSnapshot` | a second `record()` caller, or a console line in `bend.ts`: STOP before any query, report |
| **D-SITE** | any graded hour that starts at or after 10:00Z | D-LABEL holds, with `no_label` near L's ~1% of `req_lines`, not ~10% | D-LABEL breached with `no_label` some 300-420 above ~1% of `req_lines`: the site's calls do reach the `req` line, and this reading of the code is wrong. The escape is taken, with no reading, and it is reported. A breach of any other size: the escape, reported as D-LABEL's |

The text of blipscope-5c's `f1r/f1-d-monday-1.md` (sha256 `37430c6e8ec2`) follows VERBATIM; only its `##` headings are demoted to `####` here.

F1-D, MONDAY (append-only, registered 2026-11-22T15:25:09Z from date -u, before any of it runs; the orchestrator's 15:18:54Z relay: the owner chose R1). Nothing above is edited. F1-D and F1-D ADDED stand, except for the rows re-based here. Why: the logs deploy did not run on Sunday, so F1 did not run. No `f1-20261122\` folder was made; `ls` at 15:19Z shows only `f1-20261121\`. The deploy now runs **Monday 2026-11-23**: T0 10:00Z, prep ~09:20Z, production by 12:25Z at the latest, END ~12:55Z, hard end 13:45Z. Reader blob `50db3cb8cb2a`, unchanged (#688's local head `b60c6c2b8c5b`, unpushed).

#### Re-based clock rows

- **The key is unchanged:** the END line as relayed, with its stamp and `<D>`. No END line on Monday, or one that says no production deploy happened: F1 does not run, and a new slot is asked.
- **H0 = the first whole hour that starts at or after the END stamp,** passed explicitly to `scan` and `read`:

| END stamp (Mon 11-23) | H0 | scan starts (H0 + 1 h 5 min) | fallback hour (H0 + 1) | fallback reads start (H0 + 2 h 5 min) |
|---|---|---|---|---|
| (10:00Z, 11:00Z] | 11:00Z | 12:05Z | 12:00Z | 13:05Z |
| (11:00Z, 12:00Z] | 12:00Z | 13:05Z | 13:00Z | 14:05Z |
| (12:00Z, 13:00Z]: **the expected case** (~12:55Z) | 13:00Z | 14:05Z | 14:00Z | 15:05Z |
| (13:00Z, 13:45Z] | 14:00Z | 15:05Z | 15:00Z | 16:05Z |
| after 13:45Z (the logs step's own hard end), at or before 10:00Z (T0), or not on 11-23 | stop, and ask | | | |

  One change from Sunday's wording. Sunday's stop line was "an END after 10:00Z", set against a logs hard end of 09:45Z, so an END in (09:45Z, 10:00Z] still mapped to an hour although it was past the step's own hard end. Monday's stop line is the hard end itself, 13:45Z.
- **HARD END 16:20Z.** Monday has no KBOS, so the hard end comes from F1's own worst case:
  - H0 = 14:00Z puts the fallback's reads at 16:05Z.
  - They get Sunday's 12-minute budget (start by 12:08Z for a 12:20Z end), so they must start by 16:08Z.
  - Nothing of F1 runs after 16:20Z, and a step that cannot finish by then does not start.
  - The only Monday window I hold is map-client's LOOK on E2:08 at 17:45Z. F1 touches no board, and 16:20Z is 80 minutes before a 17:40Z T-5.
  - **If the orchestrator holds a PC-quiet window that opens between the END and 16:20Z,** the hard end becomes 5 minutes before it (Sunday's 12:20Z against 12:25Z). The fallback's latest start becomes that hard end minus 12 minutes.
- **D-SCAN's T0 is Monday's.** X must be at or after 10:00Z and at or before the END's hour. X before 10:00Z: STOP, report.
- **D-LC:** L (2026-11-19T12:00Z) is still a closed hour on Monday, readable until 11-26. The four values must still equal 3695, 3659, 36 and 0.
- **D-SITE applies to every Monday graded hour.** Site #19 MERGED at 2026-11-22T10:00:04Z, head `85845783c8af` (gh, read 15:19Z), so every graded hour on Monday starts after it: H0, and the fallback hour if it runs.
  - **The named alternative, which the data does not exclude:** if the site's feed made no calls in the hour, D-SITE holds exactly as it does when the calls produce no `req` line.
  - F1 cannot see the calls: they write no `req` line, invocation logs are off, and the AE point is on the writer path.
  - So a D-SITE HOLD reads "not refuted", never "confirmed". Only a breach of the stated size discriminates.
- **The folder** is `C:\Github\_handoffs\blipscope-5c\f1-20261123\`, made by a plain `mkdir` that refuses if it exists. File names as F1-D.
- **bgcheck's known set is Sunday's, and pids do not survive a restart** (map-client's second restart is pending).
  - Monday's first bgcheck runs, read-only, before the END line (by 12:00Z).
  - Its exit and its VOID set go to the orchestrator, and F1's first start waits until that set is ruled known.
  - The cmdline search excludes this command's own ancestor chain, as Sunday's did.
  - Five matching names with new pids are still sent, not assumed.
- **D-FIELDS:** main has moved since F1-D, from `cbd999db594f` to `910d8975bdf7` (#786, #785, #788).
  - The five files D-FIELDS and D-FIELDS-2 read (`metrics.ts`, `index.ts`, `devlabel.ts`, `sitefeed/bend.ts`, `wrangler.toml`) are unchanged between the two: `git diff --stat` is empty, exit 0.
  - So F1-D's line references hold on main, and D-FIELDS still grades at `<D>`, as registered.

#### Board-counting couplings, named

**The soak's E7:94 enrols with production this week.** The reader keys a board by its `devl` per colo, across all endpoints (`colos.setdefault(c, set()).add(d)`, `colo_occupancy_read.py:318`). So while E7:94 sends to production, it is one more board in the histogram. F1 cannot separate it out: that would need its label, and so its id, which F1 neither holds nor computes.

| row | coupling | effect |
|---|---|---|
| D-BOARDS | one board more than the fleet's | the 42 line stands: it is a one-entry-per-board sanity check, not a fleet count |
| D-LABEL, D-SITE | every request that reaches the default `fetch` is a `req` line (`index.ts:464`, unconditional), and `devl` is set only after the key verifies (`:361-372`). So E7:94's enrol-page requests are `no_label`, and so is any poll whose key production has not accepted yet (a 401) | at L's volume, about 157 more `no_label` lines in the hour cross 5%. If L's hour had at most step 4d's 21 boards, its 3659 labelled lines are at least ~174 per board-hour. So one board polling unauthenticated for about 54 minutes takes D-LABEL's escape by itself, and a breach in such an hour cannot be attributed to the site |
| F1-K | one of n boards, alone or sharing a colo with any other board | it moves the alone count by -1 (it was alone), 0 (a colo of 3 or more) or +1 (it made a pair) |
| F1-H55, F1-H115 | the same board, inside the weighted rate | not separable by hand: `grade` prints only the fleet rates, not `hit_rate(k)` per k |
| all three | if E7:94 is not a radar build polling the same resource about every 60 s, it is in the histogram without being in the cache-sharing model the simulation assumes | a property of the reader for any such board: named, not solved here |

**D-SOAK, a new row.** The question, for the orchestrator's relay: did E7:94 send anything to production between H0 and the end of the last graded hour (H0 + 1 h, or H0 + 2 h with the fallback)? If so, when did it enrol? The relay is needed before the record is written. If none has arrived by then, the row below for "yes, enrolled before" applies, as the conservative case.

| outcome | then |
|---|---|
| no | no coupling; graded as registered |
| yes, enrolled before the graded hour started | D-BOARDS as registered. **F1-K's verdict stands only if a/n and each variant the histogram allows give the same result under F1-K's own test (alone >= boards / 2):** (a-1)/(n-1) if it has a k=1 colo, a/(n-1) if it has a k>=3 colo, and (a+1)/(n-1) if it has a k=2 colo. Otherwise F1-K reads "within one bench board's reach", with no verdict. This is done by hand, from the printed histogram. F1-H55 and F1-H115 are reported with "includes one bench board", with no verdict |
| yes, enrolled inside a graded hour | that hour has a known source of `no_label` lines that cannot be separated out. A D-LABEL breach is the escape, unattributed, and D-SITE gets no verdict. Otherwise as the row above |

**Recommended: keep E7:94 off production from H0 to the end of F1's last graded hour.** That is expected to be 13:00Z-15:00Z, and at worst 14:00Z-16:00Z. Then D-SOAK is "no", and nothing above applies. If the soak cannot wait, I can write a small sensitivity check before Monday 10:00Z, as its own registration, red first. It would rerun the frozen `hit_rate` over the "one board fewer" histograms, so that F1-H55 and F1-H115 get the same test as F1-K. That is the orchestrator's call, and nothing is built unless asked.

The same mechanism applies to any bench board that changes its production state inside a graded hour, including E2:08 on its LOOK day. D-SOAK's relay should cover each such board.

F1, MONDAY 2026-11-23: THE RECORD (append-only, written 2026-11-23T15:55:04Z from date -u). The run's files are in blipscope-5c's handoff folder `f1-20261123/`. **F1 has NO READING for 2026-11-23**, by the orchestrator's 15:19Z ruling: D-SITE fell outside both of its columns, so the frozen escape (`:1413`, "anything no row names") applies. The numbers below are information only, never F1's verdict.

- **The key:** END stamp 2026-11-23T13:44:00Z (relayed). H0 = 14:00Z. `<D>` = `4a02123ca992`, with production's version 80c524ea. D-SOAK = No.
- **D-FIELDS and D-FIELDS-2:** HOLD at `<D>`.
- **D-CTL:** HOLDS, byte-identical to 11-21's line.
- **D-LC:** HOLDS. 3695, 3659, 36 and 0; LC-SUM exact; every query at ABR 1.
- **D-SCAN:** as predicted.
  - H0: `dev 0`, `devl 5304`.
  - `devl` first appears in the hour starting 11:00Z (11:00Z: dev 2045, devl 455; 10:00Z: dev 2209, devl 0).
  - The first clean hour is 12:00Z, and the last closed hour, 14:00Z, is clean.
  - 10 count calls, each at ABR 1 on its first attempt; exit 0.
- **D-READ:** as predicted. Reads A and B were 74 s apart, and their output is byte-identical.
  - preflight dev 0; 5553 events fetched, 0 with dev; 3 pages, ABR `[1]`.
  - req lines 5553, no colo 0, no label 249. As a cross-check, 5553 - 249 = 5304, the scan's `devl`.
- **D-GRADE:** exit 0, with its own escapes not taken. F1-CTL HOLDS. 20 boards in 12 colos, 8 of them alone (40%); histogram k:colos `{1: 8, 2: 2, 4: 2}`.
  - **Information only:** F1-K FAILS; F1-H55 FAILS at 40.0% (limit: at most 25%); F1-H115 HOLDS at 70.0% (range: 50% to 75%).
- **D-BOARDS:** 20, at most 42. Information only.
- **D-LABEL:** `no_label` is 249 of 5553 = 4.48%, at most 5%. It held, near its edge, at about 4.5x the expected ~1% (L: 36 of 3695). That is 193.5 lines over 1%.
- **D-SITE: OUTSIDE ITS SET.** Its hold column needs ~1%, and its breach column needs D-LABEL breached by 300-420 lines over 1%. 4.48% is neither, so the escape applies and there is no reading.
- **Five sources fit the 193.5 extra lines, and none is excluded,** because the record is counts only, with no path or time per line:
  - (a) the 13:44-14:05Z lift of the production hold for the site, the app and the screen. Their statements say nothing was sent; there is no artifact.
  - (b) a board refused before its key is accepted: the per-IP limit, 401 or 403 (`index.ts:359`, `:362`, `:363`).
  - (c) the site's BendFeed calls reaching `req` lines for part of the hour.
  - (d) unauthenticated public traffic.
  - (e) a board whose key verified but whose label digest failed (`index.ts:371`, with `authPath` set at `:383`). This one was added by correction after the END line.
- **No fallback trigger fired, and nothing was run twice.** There was no second look at the 15:00Z hour; the ruling was that a look chosen after the reading would be a rescue.
- **One process note:** the cmdline search before read A went out in the same tool batch as read A's start. It returned 0, and bgcheck ran first.

CORRECTION to D-LABEL (F1-D, above; append-only, 2026-11-23T15:55:04Z): the row's "**The grade does not check it**" was true until commit `6f2032cb2997` (F1 NL-FIX, below). From that commit, `grade()` runs the board-key half of `:1413` itself: it escapes on more than 5% of `req` lines without a colo or a board key.

The text of blipscope-5c's `f1r/f1-d-site-1.md` (sha256 `78fb904958c9`) follows VERBATIM; only its `##` headings are demoted to `####` here.

F1-D SITE-1 (append-only; a PROPOSAL, text only; written 2026-11-23T15:21:54Z from date -u, after the orchestrator's 15:19Z ruling). Nothing here runs without the orchestrator's word, and no query was made to write it. Code cited at `<D>` = `4a02123ca992`, read with `git show`.

**Today stays as ruled:** F1 has NO READING for 2026-11-23 (the frozen escape, proxy-bill.md:1413). Nothing below changes that, whatever it shows. F1-K FAILS, F1-H55 40.0% FAILS, F1-H115 70.0% HOLDS and D-BOARDS 20 are numbers, information only.

#### 0. Correction to my END's list (append-only)

My END named four alternatives for the 193.5 extra `no_label` lines, (a) to (d). The code at `<D>` names a fifth, and widens (b):

- **(e) a board whose key verified, but whose label digest failed.** `index.ts:371` is `try { meta.devl = await deviceLabel(meta.dev); } catch {}`, and `meta.authPath = "device"` follows at `:383`. Its line has no `dev` (stripped by `record()`) and no `devl`, so it counts as `no_label`. It is **board-shaped**: like (b), it can hide a board from the histogram.
- **(b), widened:** a board refused before its key is accepted. That covers the per-IP limit (`:359`), 401 (`:362`) and 403 `fake_device_id` (`:363`), not only the 401.
- (c) is unchanged: `BendFeed` is a named export, "not routes" (`:441-442`), so its calls should not reach the default `fetch`. If that reading is wrong, the classifier below cannot say which route the calls would land on. They could land on `/other` 404, which is where scanner traffic lands too.

#### 1. D-SITE's columns for a future graded hour (proposal)

**The rule I'm proposing: a ceiling comes from a population that is registered before it is read, never from the hour being graded.** 4.48% is never used to set a column; a threshold tuned to today would be the rescue the ruling refuses.

| column | condition | D-SITE | F1's reading |
|---|---|---|---|
| HOLD | `no_label` share at most C | "not refuted" | as graded |
| **MIDDLE (new)** | C < share <= 5% | **no verdict** | the classifier (section 2) runs on the same hour. The reading stands only if board-shaped lines ((b) + (e)) are at most 1% of `req_lines`, which is L's entire no-key rate (36/3695), the most a working fleet has been seen to produce. Above that: **no reading**, because F1 cannot count boards it cannot label |
| BREACH | share > 5% | as F1-D ADDED; it is attributed to the site only with the site's OWN count of BendFeed calls for that hour (section 3) | no reading (frozen) |
| outside | anything else, or no C | none | no reading (frozen :1413) |

**C, the ceiling: the population, counts only.**
- **Population:** for each closed hour, two counts: `scope` and `scope AND dev DOES_NOT_EXIST AND devl DOES_NOT_EXIST`. These are count queries of the kind the scan already sends. The `devl DOES_NOT_EXIST` leaf is new, written the same way as the existing `DEV_ABSENT`. No line is fetched and no `dev` value is touched.
- **Window:** 24 closed hours straddling the site merge, 2026-11-21T22:00Z to 2026-11-22T21:00Z. The site's #19 merged at 2026-11-22T10:00:04Z and its feed was due on at ~10:02Z. This is about 48 count calls, 60 s apart, about 48 minutes. Those hours are readable until about 11-28.

**Predictions for the population, written before any query:**

| outcome | meaning |
|---|---|
| **predicted:** no step at the merge. The median no-key lines per hour for the 12 hours after 10:00Z is within +/-150 of the 12 before | the "no `req` line" reading of BendFeed is not refuted at full volume. C = the window's maximum share |
| a step of +300 or more lines per hour from the 10:00Z hour onward | (c) at full volume: the code reading is wrong. No C; report |
| any hour at 10% or more | report it, and set no C |
| 4 or fewer hours BLIND | those hours are dropped and named. More than 4 BLIND: no population, stop |
| **none of the above** (for example, a step between +150 and +300) | the instrument is unreliable at this scale: stop and decide |

A "no step" result does not separate "the feed made calls that wrote no line" from "the feed made no calls". MONDAY's named alternative stands, and only the site's own count (section 3) closes it.

#### 2. What separates (a)/(b)/(c)/(d)/(e): a `classify` mode for the reader

- **What it reuses:** the SAME events query as `read`: `scope + dev DOES_NOT_EXIST`, the same pre-flight, backstop, ABR-1 rule and observability-read token. There is **no new token and no new query shape**. The only new thing is in-memory classification of each `no_label` line.
- **What it classifies, for each `no_label` line:**
  - **route class:** `routeTemplate()`'s rule over `KNOWN_ROUTES` (`metrics.ts:91`, `:204`), taken from the proxy source in the same tree rather than transcribed. Anything unlisted is `/other`;
  - **status class:** 2xx/3xx, 401, 403, 404, 429, other 4xx, 5xx;
  - **authPath:** present or absent;
  - **time:** the five-minute bucket within the hour, from the event's timestamp.
- **What it prints: counts only.** A route-class by status table, the authPath-present count, and twelve five-minute counts. **Never** an `ep` (raw paths carry hex ids, `/v1/enrich/<hex>`), a colo, a `devl` or a line.
- **Controls:**
  - **The anchor:** the class counts must sum to the read's own `no_label` and `req_lines`. For 14:00Z that is 249 and 5553; anything else means stop.
  - **Host tests, red first:** fixture events with a known `ep`/status/time, including a raw `/v1/enrich/<hex>` path that must print only as `/v1/enrich`. A sabotaged route map must turn them red.
  - **A missing field reads BLIND:** a missing timestamp or status makes the time row (or the status table) BLIND, never 0.

**Predictions for 14:00Z** (the only hour holding the excess, readable until about 11-30), written now. A signature counts as MET when it holds at least 150 of the 249 lines, about three quarters of the excess. Uniform timing would put about 21 lines in any one five-minute bucket.

| source | signature | if it is the source |
|---|---|---|
| (a) the hold lift | time | at least 150 lines in [14:00, 14:05) |
| (b) a board refused before its key is accepted | 401/403/429 on known device routes | at least 150 such lines, in 10 or more of the 12 buckets |
| (e) label digest failed | authPath present, `devl` absent | at least 150 such lines |
| (c) site calls via `fetch` | one class-and-status cell, steady at about 30 per bucket | at least 150 in that cell, in 10 or more buckets, AND the site's own count (section 3) at or above it. If the cell is `/other` 404, the classifier alone cannot separate it from (d) |
| (d) public traffic | pages, `/healthz`, `/other`, 404 | at least 150 in those classes, with no other signature met |
| mixed | no signature reaches 150 | report the table, no attribution |
| (a) together with one class signature | both met | reported as both: for example, a session's refused requests in the lift window |
| **none of the above** | for example, the anchor fails, a row is BLIND, or two class signatures are both met | the instrument is unreliable at this scale: stop and decide |

#### 3. The other side of the contract, for (c)

A count taken from where the calls originate beats any inference made on the receiving side: **the site's own count of BendFeed calls in [14:00, 15:00)**. That count belongs to the site, from its logs, as counts only, and is not a statement. If it is 0, (c) is excluded outright, and MONDAY's "feed made no calls" alternative becomes a fact rather than a possibility. I do not know the site Worker's log setup, so this is a question for the site's session, not a query I would make.

#### Order, if ruled

1. The reader's `no_label` fix (already ruled: its own registration, red first). The middle column needs `grade()` to see `no_label` itself.
2. `classify` plus its host tests (red first), as their own registration.
3. The population read, then classify on 14:00Z. Each needs its own word; both are counts only, in the background and registered, with bgcheck before each start.

#688 is still unpushed (`b60c6c2b8c5b`); its push, with the CI prediction first, is the next step regardless.

ACCEPTED AS TEXT by the orchestrator (it re-read the sha at 15:23:24Z). Its notes:
- Today's "no reading" is unchanged.
- The correction ((e) added, (b) widened) is accepted.
- The columns are for future hours only.
- The population and `classify` each run only on its word, after their own registrations.
- `classify` run on the 14:00Z hour is a diagnostic after the escape, and every line of its output is labelled "diagnostic after F1's no-reading; changes no reading".
- The site's own BendFeed count is asked of the site by the orchestrator; this session does not query it.

The text of blipscope-5c's `f1r/f1-nolabel-fix-1.md` (sha256 `2dd057605a8c`) follows VERBATIM; only its `##` headings are demoted to `####` here.

F1 NL-FIX (registration, append-only, written 2026-11-23T15:30:51Z from date -u, before any code). This is the reader's `no_label` fix, ruled in F1-D ADDED ("comes after F1, as its own registration, red first") and placed first in the orchestrator's 15:2xZ order. Text only until accepted. It changes nothing about Monday: F1 has NO READING (the ruling), and this fix would not have escaped 4.48% either.

#### The defect

The frozen escape (proxy-bill.md:1413) reads "more than 5% of the hour's `req` lines without a colo **or a board key**".

- **What `grade()` executes:** only the colo half, at `colo_occupancy_read.py:438` (`no_colo > 0.05 * req_lines`).
- **The other half exists only as prose:** D-LABEL is a by-hand row ("**The grade does not check it**", F1-D).
- **This is the second-paths shape:** the guard exists, and one of its two clauses is never run.

#### The change (one line, plus its message)

`:438` becomes `if d["req_lines"] and (d["no_colo"] + d["no_label"]) > 0.05 * d["req_lines"]`, with the escape text "over 5% of req lines without a colo or a board key".

- **The sum is the union.** `no_colo` and `no_label` are disjoint by construction: a line with no colo `continue`s at :312-314, before the label test at :315.
- **The 5% is strict (`>`),** as frozen: "more than".
- **Nothing else moves:** `ctl`, `lc`, `scan` and `read`, and every file `read` writes, are unchanged.

#### Red first: rows added to `colo_occupancy_plant.py`, grade only

All rows are synthetic, with no network and no token. Each uses a result-file pair 70 s apart, `abr [1]` and histogram `{1:1, 2:1}` (3 boards). Every row varies only `req_lines`, `no_colo` and `no_label`.

| row | pair (req / no_colo / no_label) | OLD reader (blob 50db3cb8cb2a) | NEW reader |
|---|---|---|---|
| G-LABEL | 1000 / 0 / 51 | exit 0, F1-K printed: **this is the red** | exit 3, "ESCAPE: over 5% of req lines without a colo or a board key" |
| G-UNION | 1000 / 30 / 21 | exit 0 (red) | exit 3, same text |
| G-EDGE | 1000 / 0 / 50 | exit 0 | exit 0 (exactly 5% is not "more than") |
| G-COLO (control) | 1000 / 51 / 0 | exit 3, "without a colo" | exit 3, the new text (the colo half still fires) |
| G-SAB (sabotage) | the NEW reader with `:438` reverted by bytes; the plant asserts the anchor was replaced exactly once | | G-LABEL and G-UNION read **OFF** (exit 0), and the harness exits 1 |

#### PREDICTED

- The OLD reader's four rows read as in the table.
- With the NEW reader, the harness exits **0**, and every existing row reads exactly as it does today.
- G-SAB makes the harness exit **1**, with exactly 2 rows OFF: G-LABEL and G-UNION.
- The new reader is then restored by bytes and its sha checked.
- **CI does not exercise this:** no workflow references `tools/bill/`.
  - #688's CI count is unchanged at 36, since no path filter matches the new files.
  - The push gets its own CI prediction first.

**Outside the set:** anything else (another row OFF, a row that will not plant, an exit 2) means stop and report, with nothing re-run.

#### Not in this change

- It adds no D-SITE middle column; that is SITE-1's, and waits for its word.
- It has no `classify` mode.
- The D-LABEL row's "The grade does not check it" is corrected below the frozen text in `docs/proxy-bill.md`, never edited in place.

ACCEPTED by the orchestrator before any code. RESULT (append-only, 2026-11-23T15:55:04Z): as registered.
- **Baseline:** the unchanged plant against the reader before the fix (blob `50db3cb8cb2a`): 13 rows, 13 as predicted, exit 0.
- **Red:** the plant with the G rows, against the same reader: 17 rows, 15 as predicted, 2 OFF (G-LABEL and G-UNION: exit 0 with no ESCAPE line), exit 1.
- **Green:** against the fixed reader (blob `88052942e847`): 17 rows, 17 as predicted, exit 0.
- **G-SAB:** the fixed line reverted by bytes on a COPY (one line changed, asserted). The plant exits 1 with exactly G-LABEL and G-UNION OFF, and the working reader was never touched.
- **Commits:** `f005d1278997` (the red rows; plant blob `b3b24e1ab1fa`) and `6f2032cb2997` (the fix).
- **Outputs:** in the handoff folder `f1r/nlfix/` (red `3974ab07fede`, green `474aae2405c7`, sabotage `862ba300759e`).

CORRECTION to F1 MONDAY's record, above (append-only, 2026-11-23T16:39:01Z): the list of sources for the 193.5 extra `no_label` lines had five. There is a **sixth**, found while classify's families were being derived from source:

- **(f) Missileer requests,** which this Worker proxies to the game origin.
  - `isMissileerPath` (`missileer.ts:56-61`) is dispatched at `index.ts:190`, before the auth gate, on purpose.
  - Every default `fetch` is recorded (`:464`), so each such request is a `req` line with no `devl`.
- (f) is not board-shaped for F1, and it is not excluded either. F1's NO READING for 2026-11-23 is unchanged.

The text of blipscope-5c's `f1r/f1-classify-1.md` (sha256 `627dd8c67b72`) follows VERBATIM; only its `##` headings are demoted to `####` here.

F1 CLASSIFY (registration, append-only, written 2026-11-23T16:16:46Z from date -u, before any code; text only, on the orchestrator's word). It **runs only on the orchestrator's later word**. Running it on the 14:00Z hour is a DIAGNOSTIC after the escape: **every output line, and the result file, is labelled "diagnostic after F1's no-reading; changes no reading"**. F1 has NO READING for 2026-11-23, and nothing here can change that. The design is SITE-1's section 2 (78fb904958c9, accepted as text). Source cited at `<D>` = `4a02123ca992`.

#### 0. A correction to SITE-1's list (append-only): a sixth source, (f)

- **(f) Missileer requests,** which this Worker proxies to the game origin.
  - `isMissileerPath` (`missileer.ts:56-61`: `/missileer`, `/missileer/...`, `/api/v1/missileer/...`) is dispatched at `index.ts:190`, **before** the auth gate. That is on purpose: Missileer "must never inherit Blipscope's device-key check".
  - Every default `fetch` is recorded (`:464`), so every Missileer request is a `req` line with **no `devl`**.
- **Is (f) board-shaped?** Not for F1. A Missileer client is not a radar board polling `/wx`, so it belongs outside F1's histogram, which is where it already is. In SITE-1's MIDDLE column it counts as non-board-shaped.
- **Where else it is missing:** it was absent from my END, from SITE-1 and from the Monday record pushed in `ec0f0d62a040`. The record gets a correction below it in a later docs commit, never an edit.

#### 1. What it adds: `classify <hour start ISO> <out.json>`, a new reader mode

**What it reuses, unchanged:**
- the same pre-flight (a `dev exists` count that refuses the hour if nonzero, exit 5);
- the same events query (`scope + dev DOES_NOT_EXIST`) and paging;
- the same backstop, the same ABR-1 value rule (exit 6 on BLIND), and the same observability-read token.

No new token, no new query shape, no new field fetched. It reads exactly the events `read` reads.

**For each req line,** it counts what `read` counts (`req_lines`, `no_colo`, `no_label`). Then it classifies each `no_label` line, in memory only:

| field | classes, in this order of test |
|---|---|
| **family** (from `ep`) | `missileer` (the `isMissileerPath` rule); `healthz` (`/healthz`, `:245`); `enroll` (`/blipscope/enroll`, `/enroll`, `:276`, `:296`, above the auth gate); `api` (`apiEndpoint` non-null: `/api/v1/blipscope/`, `/api/v1/weatherscope/`, `/v1/`, `:126-147`; the auth gate applies, `:356-363`); then, for any other path, `notfound` if the status is 404 (`:356`), else `page` |
| **status** | `2xx/3xx`, `401`, `403`, `404`, `429`, `other4xx`, `5xx`; missing or non-numeric makes the status table **BLIND** |
| **model present** | `model` non-empty (`X-Blip-Model`, set before auth at `:452`, so a refused board still carries it). A boolean only; the value is never kept |
| **authPath present** | a boolean: (e)'s marker |
| **time** | the five-minute bucket, 0 to 11, from the event's top-level `timestamp` (ms), the shape the plant's fake models. A line without one makes the time rows **BLIND**, never 0 |

**The family rules' constants are parsed at run time from `proxy/src/index.ts` and `proxy/src/missileer.ts` in the same tree,** not transcribed: `API_PREFIX`, `API_LEGACY_PREFIX`, `WEATHERSCOPE_API_PREFIX`, `PAGE_PREFIX`, and Missileer's two prefixes. A constant not found exactly once is a RIG error, exit 2.

**What it prints and writes: counts only.**
- The anchor: req lines, no colo, no label.
- A family by status table.
- model-present counts per family, and the authPath-present count.
- Twelve bucket counts per family.
- **Never** an `ep` (raw paths carry hex ids), a colo, a `devl`, a `model` value or a line.
- Every printed line starts with `[diagnostic after F1's no-reading; changes no reading]`, and the out.json carries the same `label`.

**Exits:** 0 for a full table; 3 if any row is BLIND (no diagnostic); 5, 6 and 2 as `read`.

#### 2. Red first: rows added to `colo_occupancy_plant.py`

All rows are synthetic and offline. The plant writes copies of the two proxy source files beside the temp readers.

| row | run | predicted |
|---|---|---|
| **C-RED** | the reader before this change (blob `88052942e847`) given `classify` | prints usage, exit 2: **the red** |
| **C-TABLE** | the new reader on a synthetic hour holding one or more lines of every family, status and boolean: 2 `missileer` 2xx; 1 `healthz` 2xx; 1 `enroll` 2xx with model; 2 `api` 401 with model; 1 `api` 403 with model; 1 `notfound` 404; 1 `page` 2xx; 1 `api` 2xx with authPath and no `devl`; 3 labelled lines; 1 line with no colo | the printed table equals the expected table **whole** (every line compared), and the anchor equals `read`'s on the same fake: req 14, no colo 1, no label 10; exit 0 |
| **C-NOID** | the same hour, where one `api` line's `ep` is `/v1/enrich/` followed by a synthetic 6-hex aircraft address, and one Missileer `ep` carries a synthetic path segment | neither raw `ep` string appears, as bytes, in stdout or in out.json; their families are counted |
| **C-BLIND** | one `no_label` event with no `timestamp` | the time rows print `BLIND` (not 0); exit 3 |
| **C-PRE** | a `dev` line in the hour | pre-flight refuses, exit 5, 0 events requests (as R-PRE) |
| **C-SRC** | the copy of `index.ts` with `API_PREFIX`'s name changed by bytes (asserted exactly once) | RIG, exit 2; nothing printed past the error |
| **C-SAB** | a COPY of the new reader with the Missileer family test removed by bytes (asserted exactly once) | C-TABLE reads **OFF** (the 2 Missileer lines fall to `api`, `notfound` or `page`); the harness exits 1 with exactly C-TABLE OFF |

C-TABLE's anchor arithmetic: 10 `no_label` lines (2 + 1 + 1 + 2 + 1 + 1 + 1 + 1), 3 labelled lines and 1 with no colo make 14 req lines.

**PREDICTED:**
- With the new reader, all 17 existing rows plus these rows read AS PREDICTED, exit 0.
- C-RED reads exit 2 against `88052942e847`.
- C-SAB makes exactly C-TABLE OFF, exit 1.
- **CI does not run the plant;** no workflow references `tools/bill/`. Each push gets its own CI prediction first.

**Outside the set:** anything else means stop and report, with nothing re-run.

#### 3. The diagnostic run on the 14:00Z hour (only on the word)

**The run.** One background start, registered, with bgcheck before it. The calls are the same as one `read`: 1 pre-flight count and 3 events pages, all at ABR 1.

**The anchor, required before any row is read:** req lines 5553, no colo 0, no label 249, as reads A and B. Any other value means the instrument is unreliable on this hour: stop, and read no row.

**The signature table** is SITE-1's, with (f) added. A signature is MET at 150 or more of the 249 lines; uniform timing would put about 21 in any one bucket.

| source | signature | if it is the source |
|---|---|---|
| (a) the hold lift | time | 150 or more lines in bucket 0, [14:00, 14:05) |
| (b) a board refused before its key is accepted | `api` with 401/403/429 and model present, plus `enroll` with model present | 150 or more, in 10 or more of the 12 buckets |
| (c) the site's calls via `fetch` | one family-and-status cell outside `missileer`, model absent, steady at ~30 per bucket | 150 or more in that cell, in 10 or more buckets, AND the site's own count (asked by the orchestrator) at or above it. If the cell is `notfound` 404, classify alone cannot separate it from (d) |
| (d) public traffic | `page`, `healthz`, `notfound`, model absent | 150 or more, with no other signature met |
| (e) label digest failed | authPath present | 150 or more |
| (f) Missileer | `missileer` | 150 or more |
| mixed | no signature reaches 150 | the table is reported, with no attribution |
| (a) with one class signature | both met | reported as both |
| **none of the above** | for example, the anchor fails, a BLIND row, two class signatures both met, or an unknown status | the instrument is unreliable at this scale: stop and decide |

**Whatever it reads, F1 stays at no reading.** The result says which source produced the excess, for D-SITE's future columns and nothing else.

#### 4. Order

1. This registration's acceptance.
2. The code with C-RED first (two commits, red then green), and the plant output with the new reader's sha sent.
3. The push, with its own CI prediction first.
4. The diagnostic run, only on the orchestrator's word.

The population's registration follows the same way, text first.

ACCEPTED by the orchestrator, before any code; then ACCEPTED AS REGISTERED. RESULT (append-only, 2026-11-23T16:39:01Z): as registered.

- **Red:** against the reader without classify (blob `88052942e847`), 23 rows, 18 as predicted. The 5 OFF were C-TABLE, C-NOID, C-BLIND, C-PRE and C-SRC; C-RED read as predicted; exit 1. That tally was written down before the run.
- **Green:** against the new reader (blob `9b1a1610626f`), 23 rows, all 23 as predicted, exit 0.
- **C-SAB:** the Missileer family line removed by bytes from a COPY. The plant exits 1 with exactly C-TABLE OFF (Missileer total 2 -> 0), and the working reader was never touched.
- **Commits:** `d2872d06a3cb` (the red rows; plant blob `c76832061a33`) and `75d378cfbf63` (the mode).
- **Outputs:** in blipscope-5c's handoff folder `f1r/classify/`.

**A CHANGE TO `read`'S CODE, recorded as one (the orchestrator's ruling):**
- To give `read` and `classify` ONE path, `read`'s pre-flight, `dev`-absent events query, paging and backstop moved into `walk_hour`, which both walk. That way `classify` cannot skip a guard `read` establishes.
- **`read`'s reader is now blob `9b1a1610626f`, no longer `88052942e847`, for every future read run.**
- **The proof** is an identical-behaviour check: `read` with the reader before and after, on 7 fakes (clean, pre-flight refusal, backstop, BLIND pre-flight, recover, the classify hour, and a 4,100-line hour that pages 3 times). Exit, full stdout and result file were identical in 7 of 7 (`readdiff-check.out`, `7fff07532853`).
- **Its control,** against the F1-R-era reader `af55507b9ad7`, differed in 7 of 7 (`readdiff-control.out`, `a1d43fd8df65`).

The text of blipscope-5c's `f1r/f1-population-1.md` (sha256 `75213feac918`: the registration `5cf7d7288c34` plus its appended correction) follows VERBATIM; only its `##` headings are demoted to `####` here.

F1 POPULATION (registration, append-only, written 2026-11-23T16:18:56Z from date -u, before any code; text only). It **runs only on the orchestrator's later word**. It measures the ceiling C for D-SITE's FUTURE HOLD column (SITE-1, 78fb904958c9, section 1, accepted as text), from hours registered here before any of them is read. It never touches 2026-11-23's grade: F1 has NO READING that day. Its output is labelled "diagnostic after F1's no-reading; changes no reading", as classify's is.

#### 1. What it adds: `population <out.json>`, a new reader mode, count queries only

**What it counts.** For each hour it makes two count queries, in the shape the scan already sends:
- `scope`, giving req lines;
- `scope AND dev DOES_NOT_EXIST AND devl DOES_NOT_EXIST`, giving no-key lines.

No events are fetched, so no line and no `dev` value is ever seen. The `devl DOES_NOT_EXIST` leaf is new, written the same way as the existing `DEV_ABSENT`.

**What the share is.** The share is no-key lines over req lines. It counts no-colo lines too, so it is the same union NL-FIX grades: (`no_colo` + `no_label`) / `req_lines`. It is therefore the quantity a future D-SITE compares against C.

**One count shape for both eras.** The window straddles no logs deploy (that was 2026-11-23, 11:xxZ), so every window hour wrote `dev` on authenticated lines and none wrote `devl`. "No `dev` and no `devl`" is the no-key count in both eras.

**The window is fixed now and passed by the code, not by argv:** the 24 closed hours starting 2026-11-21T22:00Z through 2026-11-22T21:00Z.
- The site's #19 merged at 2026-11-22T10:00:04Z, and its feed was due on at about 10:02Z.
- So there are 12 hours before (22:00Z to 09:00Z) and 12 from the merge hour (10:00Z to 21:00Z).
- The hours are readable until about 11-28.

**Two anchor controls run first,** on hours whose counts are already known from `read` and `lc`:

| anchor | hour | must read |
|---|---|---|
| ANCHOR-PRE (the `dev` era) | L, 2026-11-19T12:00Z | req 3695, no-key 36 (LC-1 and LC-3) |
| ANCHOR-POST (the `devl` era) | H0, 2026-11-23T14:00Z | req 5553, no-key 249 (reads A and B; no colo 0) |

Any other value means the count shape does not read what `read` reads: stop, and no window hour is queried.

**Calls and pacing.** 4 anchor calls plus 48 window calls, 60 s apart (`CALL_GAP_S`), about 52 minutes, in the background and registered, with bgcheck before. Each call is a value only at ABR 1, re-asked as in F1-R-1. A window hour with a BLIND call is dropped and named.

**What it prints and writes: counts and hour stamps only.** Per hour: req, no-key and share. Then the median no-key per hour for the 12 hours before and the 12 from 10:00Z, their difference, the maximum share, and the verdict. Every line starts with `[diagnostic after F1's no-reading; changes no reading]`.

#### 2. PREDICTED, for the window

| outcome | then |
|---|---|
| **predicted:** no step. The median of the 12 hours from 10:00Z minus the median of the 12 before is within +/-150 lines/h | C = the window's maximum share; printed as `C <share>` |
| a step of +300 lines/h or more | (c) at full volume (the "no `req` line" reading of BendFeed is wrong) **OR** another source that began in that hour. The population cannot tell which, so no C; report both |
| any hour at 10% or more | no C; report that hour |
| 4 or fewer hours BLIND | those hours are dropped and named, and the medians use the hours left. More than 4: no population, stop |
| **none of the above** (for example, a step between +150 and +300, or a negative step of 150 or more) | the instrument is unreliable at this scale: stop and decide |

A "no step" result does not separate "the feed's calls wrote no line" from "the feed made no calls". MONDAY's named alternative stands, and only the site's own count closes it.

#### 3. Red first: rows added to `colo_occupancy_plant.py`

All rows are synthetic and offline; the fake answers count queries per hour.

| row | run | predicted |
|---|---|---|
| **P-RED** | the reader before this change given `population` | usage, exit 2: **the red** |
| **P-NOSTEP** | anchors as above; 24 hours of 1000 req, no-key 10 to 40 | exit 0; verdict "no step"; C = the maximum share; 52 count requests, 0 events requests |
| **P-STEP** | anchors as above; the 12 hours from 10:00Z carry +400 no-key | exit 0; verdict "step"; no C |
| **P-ANCHOR** | ANCHOR-POST answers 248 | exit 3, "stop"; **0 window hours queried** |
| **P-BLIND-2** | 2 window hours answer at ABR 100 on all three attempts | those 2 are dropped and named; the verdict is computed on the other 22; exit 0 |
| **P-BLIND-5** | 5 window hours answer at ABR 100 on all three attempts | "no population", exit 3 |
| **P-SAB** | a COPY of the new reader with the `devl DOES_NOT_EXIST` leaf removed by bytes (asserted exactly once). The fake's ANCHOR-POST hour holds labelled lines, so the copy counts them as no-key | P-NOSTEP, P-STEP and P-BLIND-2 each stop at ANCHOR-POST (exit 3) and read **OFF**. P-ANCHOR and P-BLIND-5 still read as predicted, and so do P-RED and every other row. The harness exits 1 with exactly those 3 rows OFF |

**PREDICTED:** with the new reader, every existing row, the C rows (classify's, if merged first) and these P rows read AS PREDICTED, exit 0. P-SAB makes the harness exit 1. CI does not run the plant, and each push gets its own CI prediction first. **Outside the set:** stop and report.

#### 4. Order

This waits on classify's acceptance and code. It is coded after classify, in its own red and green commits, with the plant output and sha sent, then pushed with its CI prediction first. It runs only on the orchestrator's word, and never in the same hour as another production-logs read of mine.

CORRECTION (append-only, 2026-11-23T16:40:55Z from date -u, before any code or run; found while designing the rows): P-SAB's predicted tally above is WRONG, and the reason is in its own mechanism. P-SAB stops every run at ANCHOR-POST (with the devl leaf gone, the post-deploy hour counts all its 5553 lines as no-key), so EVERY row that reaches the window reads OFF under it: P-NOSTEP, P-STEP, P-BLIND-2 **and P-BLIND-5**. A P-BLIND-5 that "still reads as predicted" would have to check only its exit code (3), which cannot tell "no population" from "stopped at the anchor" -- a row that could not fail. So P-BLIND-5 checks its exit, the window hours it attempted (24) and its "no population" line, and P-SAB's corrected prediction is: **exactly 4 rows OFF (P-NOSTEP, P-STEP, P-BLIND-2, P-BLIND-5); P-ANCHOR (exit 3, stopped at ANCHOR-POST, 4 count requests) and P-RED, and every other row, still as predicted; the harness exits 1.** Nothing else in the registration changes.

ACCEPTED as text by the orchestrator; the correction above was ACCEPTED as a pure append (lines 1-63 still hash to `5cf7d7288c34`); then ACCEPTED AS REGISTERED. RESULT (append-only, 2026-11-23T17:44:00Z): as the corrected text says.

- **Red:** against the reader without population (blob `9b1a1610626f`), 29 rows, 24 as predicted. The 5 OFF were P-NOSTEP, P-STEP, P-ANCHOR, P-BLIND-2 and P-BLIND-5; P-RED read as predicted; exit 1. That tally was written down before the run.
- **Green:** against the new reader (blob `ac94f99a83d0`), 29 rows, all 29 as predicted, exit 0.
  - P-NOSTEP: both anchors hold; step +12.0; "verdict: no step; C 0.0330"; 52 count requests and 0 events requests.
  - P-STEP: step +412.0, no C.
  - P-ANCHOR: stopped at ANCHOR-POST after 4 count requests.
  - P-BLIND-2: 2 hours dropped and named.
  - P-BLIND-5: 24 hours attempted, then "no population".
- **P-SAB:** the `devl` leaf removed by bytes from a COPY. The plant exits 1 with exactly P-NOSTEP, P-STEP, P-BLIND-2 and P-BLIND-5 OFF, as corrected; P-ANCHOR reads as predicted. The working reader was never touched.
- **Commits:** `2489e7b5022a` (the red rows; plant blob `2039b96cbcb1`) and `974a92fe3952` (the mode).
- **Outputs:** in blipscope-5c's handoff folder `f1r/population/`.

**`read`'S READER, recorded again (the orchestrator's ruling):**
- **`read`'s reader for every future read run is now blob `ac94f99a83d0`.** Its `read` code is the same as `9b1a1610626f`'s, which the CLASSIFY entry above made the reader.
- **The identity run.** The identical-behaviour check for THIS commit compared `9b1a1610626f` (before) against `ac94f99a83d0` (after): 7 of 7 identical.
- **Its first output did not name its pair.** That output (`f1r/population/readdiff-check.out`, `7fff07532853`) is byte-identical to classify's, so its bytes alone cannot say which pair it proved. This entry records the pair.
- **The re-run names it.** The check was re-run with the pair printed on its first line: "readdiff: read with reader 9b1a1610626f (before) against reader ac94f99a83d0 (after); plant 2039b96cbcb1", 7 of 7 identical (`readdiff-check-2.out`, `35c793efca6e`).
- **Standing from now on:** every identity run prints the two blob shas it compared on its first line.

CLASSIFY's DIAGNOSTIC RUN on the 14:00Z hour, RESULT (append-only, written 2026-11-24T09:12:14Z from date -u; graded 2026-11-23T18:08:54Z against section 3 of CLASSIFY above) [diagnostic after F1's no-reading; changes no reading]: inside the set, (b).

- **Go:** the orchestrator, relayed 18:07:35Z: the 14:00Z hour on reader `ac94f99a83d0`, through the gate, to end by 18:45Z.
- **Gate:** bgcheck VOID on the ruled set; PASS at 18:07:58Z.
- **Read:** 18:08:01Z to 18:08:11Z, exit 0. 4 queries (1 pre-flight count and 3 events pages), each a value on its first attempt at ABR 1. The pre-flight `dev` count read 0. Every printed line carries the label.
- **Anchor: HOLDS.** req lines 5553, no colo 0, no label 249, equal to reads A and B.
- **Outputs,** in blipscope-5c's run folder `f1-20261123/`: `classify.out` `cd1e50113690`, `classify.json` `b06592ee4013`.

| source | signature | read | met? |
|---|---|---|---|
| (a) the hold lift | 150 or more lines in bucket 0 | 22 | no |
| (b) a board refused before its key is accepted | `api` with 401/403/429 and model present, plus `enroll` with model present: 150 or more, in 10 or more buckets | `api` 401 with model 247, in 12 of 12 buckets (19 to 22 each); `enroll` 0 | **MET** |
| (c) the site's calls via `fetch` | one model-absent cell outside `missileer`, steady, 150 or more | the largest such cell is 1 (`notfound` 404) | no |
| (d) public traffic | `page`, `healthz`, `notfound`, model absent | 1 (`notfound` 404) | no |
| (e) label digest failed | authPath present | 0 | no |
| (f) Missileer | `missileer` | 1 | no |

The 249 lines are 247 `api`, 1 `missileer` and 1 `notfound`. `api`'s buckets read 22 20 20 22 20 20 22 19 20 22 20 20.

**Graded:** one class signature met and no other row: inside the set. **Result: (b),** a device sending X-Blip-Model, refused 401 on api-family routes all hour. **F1 stays at NO READING;** this names the source of the excess for D-SITE's future columns and nothing else. It does not say whose device: nothing identifying is read.

CLASSIFY on the 17:00Z hour, on the orchestrator's ruling (18:10:28Z), registered before the run and graded after it. The text of blipscope-5c's `f1r/f1-classify-17z-prediction.md` (sha256 `eb8579aef020`: the prediction `9de4d25bc30b`, stamped 18:10:05Z, plus its appended GRADED line) follows VERBATIM.

F1 CLASSIFY on 2026-11-23T17:00Z, PREDICTION (written 2026-11-23T18:10:05Z from date -u, before the run; the orchestrator's ruling) [diagnostic after F1's no-reading; changes no reading]: reader ac94f99a83d0 through gate.sh, started only after 0edebf88f5ff's CI read settles and only if it can end by 18:45Z, counts only, every line labelled; CONTROL (no known anchor for this hour): exit 0, pre-flight dev 0, every query ABR 1, no BLIND row, family totals summing to the run's own no_label; OUTCOMES on (b) (api 401/403/429 with model present): **(b) MET again** = at least 150 such lines in at least 10 of 12 buckets = the device is STILL refused; **(b) not met** = at most 10 such lines = it STOPPED before 17:00Z; **OUTSIDE** = anything else (11 to 149 lines, or at least 150 in fewer than 10 buckets, i.e. it changed inside the hour; or a control off) = reported as counts with no attribution, stop and decide.
GRADED (append-only, 2026-11-23T18:29:05Z from date -u) [diagnostic after F1's no-reading; changes no reading]: run 18:28:21Z-18:28:30Z through the gate (extras none: 6768 has closed), exit 0, pre-flight dev 0, 5 queries all ABR 1, no BLIND, 21/21 lines labelled, family totals 2+1+0+218+1+10 = 232 = no_label (req 6701). (b) = api 401 with model 218, in 12 of 12 buckets: **(b) MET again = still refused**, by the rule as frozen. NOT CAPTURED BY THE RULE, reported as counts only: buckets 0-9 read 22 20 20 21 20 20 22 20 20 22 (mean 20.70, var/mean 0.043, the same shape as 14:00Z's 0.065), then buckets 10-11 (17:50-18:00Z) read 6 and 5. So the refusals did not stop by 18:00Z, but their rate fell by about three quarters in the last 10 minutes. The rule set has no outcome for a rate change with the count still above 150; no verdict is improvised for it. (classify17.out e1fc4efd06cb, classify17.json a441b7dbb49b)

POPULATION's RUN, on the orchestrator's word after E0 ended (01:23:52Z). The text of blipscope-5c's `f1r/f1-population-result-1.md` (sha256 `219ce0147f44`) follows VERBATIM; only its `##` headings are demoted to `####` here.

F1 POPULATION: RESULT, graded against the frozen registration (`f1-population-1.md`, sha256 `75213feac918`). Written 2026-11-24T02:20:48Z from date -u, after the run; counts only. Diagnostic after F1's no-reading; changes no reading.

#### The run

- **Go:** blipscope-orchestrator-c0, after E0 ENDED at 01:23:52Z.
- **Gate:** `gate-population`, bgcheck exit 4 (VOID) on the ruled five only, with no extras. PASS at 01:27:57Z.
- **Read:** started 01:27:58Z and exited **3** at 02:19:37Z. It was registered as `bg-20261124T012758Z-9ddea229`.
- **What ran:** reader blob `ac94f99a83d0` at head `0edebf88f5ff`, with a clean tree.
- **Calls:** 52 (4 anchor calls plus 48 window calls). Every one answered on its first attempt with status 200 at ABR 1, as a value; 0 differ from that shape.
- **Outputs, in f1-20261123/:**
  - `population.out`: `718f493db5d6`
  - `population.json`: `465429ad3af0`
  - `population.err`: `16803fbd5c80` (only the token file's Windows permissions note)
- **Ledger at the end:** bgcheck VOID on the same five; neither of my tags is still running.

#### Anchors: both HOLD

| anchor | hour | read | must read |
|---|---|---|---|
| ANCHOR-PRE | 2026-11-19T12:00Z | req 3695, no-key 36 | 3695 / 36 |
| ANCHOR-POST | 2026-11-23T14:00Z | req 5553, no-key 249 | 5553 / 249 |

#### The window: 24 hours read, 0 BLIND, 0 with no req lines

| hour (Z) | req | no-key | share |
|---|---|---|---|
| 11-21 22:00 | 5071 | 2 | 0.0004 |
| 11-21 23:00 | 4266 | 5 | 0.0012 |
| 11-22 00:00 | 3438 | 2 | 0.0006 |
| 01:00 | 2777 | 1 | 0.0004 |
| 02:00 | 2543 | 1 | 0.0004 |
| 03:00 | 2575 | 2 | 0.0008 |
| 04:00 | 2154 | 2 | 0.0009 |
| 05:00 | 2139 | 236 | 0.1103 |
| 06:00 | 1893 | 255 | 0.1347 |
| 07:00 | 1975 | 249 | 0.1261 |
| 08:00 | 2522 | 251 | 0.0995 |
| 09:00 | 2610 | 248 | 0.0950 |
| 10:00 (merge hour) | 2367 | 252 | 0.1065 |
| 11:00 | 3028 | 250 | 0.0826 |
| 12:00 | 3931 | 251 | 0.0639 |
| 13:00 | 5364 | 250 | 0.0466 |
| 14:00 | 5910 | 418 | 0.0707 |
| 15:00 | 6471 | 295 | 0.0456 |
| 16:00 | 6349 | 283 | 0.0446 |
| 17:00 | 6285 | 219 | 0.0348 |
| 18:00 | 6519 | 74 | 0.0114 |
| 19:00 | 6803 | 80 | 0.0118 |
| 20:00 | 6536 | 72 | 0.0110 |
| 21:00 | 6550 | 170 | 0.0260 |

- **Medians:** no-key median for the 12 hours before 10:00Z is 3.5, and for the 12 from 10:00Z it is 250.0.
- **Step:** +246.5.
- **Maximum share:** 0.1347.

#### Graded, row by row, against section 2

| frozen row | met? |
|---|---|
| predicted: no step, within +/-150 | **NOT MET** (+246.5) |
| step of +300 or more | not met |
| any hour at 10% or more: no C; report that hour | **MET: 4 hours.** 11-22 05:00Z 0.1103, 06:00Z 0.1347, 07:00Z 0.1261 and 10:00Z 0.1065 |
| 4 or fewer BLIND hours: drop and name them | 0 BLIND; nothing dropped |
| **none of the above** (for example, a step between +150 and +300) | **MET** (+246.5) |

**VERDICT: OUTSIDE THE SET, as registered. The instrument is unreliable at this scale: stop and decide. No C.** The reader printed the same verdict and exited 3. The 10% row is met as well; under either row there is no C.

#### Observation: counts only, NOT a finding

- **Where the rise is.** No-key rises between 04:00Z (2) and 05:00Z (236) on 11-22. That is five hours BEFORE the merge hour where the registration split the medians. From 06:00Z to 13:00Z it reads 248–255 every hour.
- **Why the step lands in no row.** The split straddles a rise at another hour. Seven before-hours read 1–5 and five read 236–255, so the before median is 3.5 and the step is +246.5.
- **A matching magnitude.** The 06:00Z–13:00Z plateau of 248–255 is close to ANCHOR-POST's 249 (11-23 14:00Z) and to classify's 247 api 401s with a model (11-23 14:00Z).
- **Alternatives, none eliminated:**
  - (b), the refused device, starting on 11-22 around 05:00Z;
  - another source at a similar cadence.

  This mode reads counts only (no events, no line contents), so it cannot separate them.
- **Not interpreted:** 14:00Z reads 418, 18:00Z–20:00Z read 72–80, and 21:00Z reads 170. These are recorded and not interpreted.

RECORDED by the orchestrator (re-read 02:21:28Z: `population.out` `718f493db5d6`, `population.json` `465429ad3af0`, result `219ce0147f44`, registration still `75213feac918`): OUTSIDE THE SET, as registered, with no C. The observation stays an observation, with both alternatives open.

F1 FAMILY COUNT. The orchestrator APPROVED a classify of 11-22 04:00Z-06:00Z as a NEW registration, because it bears on a question the owner holds: whose device is refused, and since when. The text of blipscope-5c's `f1r/f1-famcount-1.md`, lines 1-109 (sha256 `ff8d7f596c0c`, the registration as accepted), follows VERBATIM; only its `##` headings are demoted to `####` here. Its AMENDMENT 1 (lines 111-134; the whole file is now `036e18f087ee`) is copied further down, after the first run's result, in the order things happened.

F1 FAMILY COUNT (registration, append-only, written 2026-11-24T02:26:06Z from date -u, before any code; text only). Approved in principle by blipscope-orchestrator-c0 as "the classify of 11-22 04:00Z-06:00Z". It **runs only on the orchestrator's word, after it has read this text.** The question is the owner's: is the 11-22 05:00Z rise in no-key lines (population, `f1-population-result-1.md` `219ce0147f44`: 04:00Z 2, then 05:00Z 236) the api-401-with-model family seen on 11-23? All output is labelled `[diagnostic after F1's no-reading; changes no reading]`. It is counts only and identifies no device.

#### 0. Why this is not `classify`, and why the guard stays

**`classify` cannot read an 11-22 hour.**
- It walks an hour through `walk_hour`. `walk_hour`'s pre-flight counts `dev exists` lines and refuses the hour if the count is nonzero: exit 5, with 0 events requests. That is classify's registration line 14 and row C-PRE.
- Every 11-22 hour is in the `dev` era. At 05:00Z, req is 2139 and no-key is 236, so 1903 lines carry `dev`. As coded, classify would refuse at the pre-flight.
- **The guard is not relaxed, and I do not propose relaxing it.** It keeps device ids out of the process.

**So this mode makes count queries only (`view: calculations`), never `view: events`.**
- No line and no `dev` value can enter the process, by construction, not by a filter. It can therefore read a `dev`-era hour, as the population read did.
- Each plant row asserts 0 events requests.

#### 1. What it adds: `famcount <out.json>`, a new reader mode

**THE SHAPE S** (the api-401-with-model family, written as count leaves):
- `scope` AND `dev DOES_NOT_EXIST` AND `devl DOES_NOT_EXIST` (population's no-key, unchanged);
- AND `status` equals 401 (a number leaf);
- AND `model` exists;
- AND `ep` starts with P, for each P of the API prefixes.

**How the prefixes are set.**
- The prefixes are PARSED from `proxy/src/index.ts` by classify's `route_rules()`, never transcribed: today `/api/v1/blipscope/`, `/api/v1/weatherscope/` and `/v1/`.
- S is the sum of three count queries, one per prefix.
- The code asserts that no prefix is a prefix of another, nor of Missileer's `/missileer` and `/api/v1/missileer/`, nor of `/healthz` and the two enrol paths; otherwise it is a RIG error with nothing asked. That is what makes the sum equal classify's `api` family. classify places Missileer first, so the api family is exactly "starts with one of the three".

**Where it can differ from classify, and the anchor that would catch it.**
- `model exists` counts an empty-string model. classify counts a non-empty one only.
- The API token for starts-with is taken from the API reference at coding time. The plant's fake implements that same token, so **only the live anchor K4 proves production honours it**:
  - a rejected token is not a value, so the run goes BLIND and stops;
  - a silently ignored leaf reads 3 x 232 = 696 at K4, so the run goes OFF and stops.

**THE CALLS, in this order.** The hours are fixed in the code, not argv. Calls are 60 s apart (`CALL_GAP_S`). Each is a value only at ABR 1, re-asked as in F1-R-1.

| step | hour | query | must read |
|---|---|---|---|
| K1 | 11-23 17:00Z (classify17's hour) | no-key | 232 |
| K2 | same | no-key AND 401 | 218 |
| K3 | same | no-key AND model exists | 218 |
| K4 | same | no-key AND api (3 calls, summed) | 218 |
| K5 | same | S (3 calls, summed) | 218 |
| H | 11-23 14:00Z (H0) | S (3 calls, summed) | 247 |
| E1 | 11-22 05:00Z | `dev exists` AND model exists AND api (3 calls, summed) | at least 1 and at most 1903 |
| E2 | same | `status` equals 200 | at least 1 and at most 2139 |
| W | 11-22 04:00Z, 05:00Z, 06:00Z | no-key (must re-read population's 2 / 236 / 255), then S (3 calls) | see section 2 |
| O | 11-22 05:00Z, in 12 five-minute buckets | no-key | the 12 must sum to 236 |

**Why these are the right anchors.**
- K1 to K5 come from classify17's own counts: no_label 232, of which api 218, all 401 and all with a model; the other 14 lines had no 401 and no model.
- So every leaf is load-bearing at 17Z. Dropping the 401 leaf, the model leaf or the api leaf moves its count from 218 to 232, or to 696 for an ignored api leaf.
- E1 and E2 prove the same leaves read in the `dev` era. A leaf that read 0 on 11-22 would otherwise turn into a false NOT.
- E1 is a count of keyed lines: a number only, as LC-2 and the scan already made.

**Totals.** 40 calls, about 40 minutes, plus 30 s for each re-ask.

**Stops.**
- A call that is not a value after its re-asks makes the run BLIND: exit 6, no verdict.
- Any must-read that is OFF (K1 to K5, H, E1, E2, W's no-key re-read, or O's sum) stops the run at that step, names the step, and queries nothing after it: exit 3.
- What it prints and writes: counts, hour and bucket stamps, the verdict, and the step it stopped at, if any. Never an `ep`, `model`, colo, `dev` or `devl` value.

#### 2. PREDICTED

These rows are graded on 05:00Z and 06:00Z, with K(h) the hour's no-key re-read.

| row | condition | then |
|---|---|---|
| **IS** | S(05Z) is at least 213 **and** S(06Z) is at least 230, i.e. at least 90% of 236 and of 255 | the 05:00Z rise is the api-401-with-model family |
| **NOT** | S(05Z) is at most 23 **and** S(06Z) is at most 25 (at most 10%), with E1 and E2 holding | it is not that family |
| **OUTSIDE** | anything else: between the bands, the two hours disagreeing, S(h) above K(h) in any hour, no onset bucket of 10 or more, or any stop | the instrument cannot answer at this scale: **stop and decide** |

**My prediction, stated so it can be wrong: IS.**
- The reason is a magnitude only: 05:00Z–13:00Z holds 248–255 against 11-23's 249 and 247.
- S(04Z) is reported, and predicted to be 2 or fewer.

**Onset ("since when").** The first five-minute bucket of 05:00Z with 10 or more no-key lines is reported as the onset, to 5 minutes. It grades nothing else.

**What IS would NOT show, named now so it cannot be improvised later.**
- WHOSE device. That is by design: nothing identifying is read.
- That it is the SAME device as 11-23's. The shape matches; the identity is not observed. The alternative, another device refused in the same way, stays open, and only an identity-bearing source could close it. Nothing here reaches one.

#### 3. Red first: rows added to `colo_occupancy_plant.py`

All rows are synthetic and offline. The fake gains the starts-with operation and a synthetic 17Z, H0 and 11-22 window built as events, so the reader's filters are exercised. Every stop row checks its exit, **the step it stopped at, and the number of count requests made**, never the exit alone (the P-BLIND-5 lesson). Every F row checks 0 events requests.

| row | run | predicted |
|---|---|---|
| **F-RED** | the reader before this change, given `famcount` | usage, exit 2: **the red** |
| **F-IS** | anchors as above; S 233 / 252 at 05Z / 06Z | exit 0; "IS"; 40 count requests |
| **F-NOT** | S 3 / 4; E1 and E2 holding | exit 0; "NOT" |
| **F-MID** | S 120 / 252 | exit 3; OUTSIDE |
| **F-ANCHOR** | K4's api leaf ignored by the fake (696) | exit 3; stopped at K4 after 6 count requests; 0 window hours |
| **F-ERA** | S 3 / 4, but E1 reads 0 | exit 3; stopped at E1; never "NOT" |
| **F-STABLE** | 05Z no-key re-reads 235 | exit 3; stopped at W 05Z |
| **F-SUM** | the onset buckets sum to 235 | exit 3; OUTSIDE (O) |
| **F-BLIND** | one W call at ABR 100 on all three attempts | exit 6; no verdict |
| **F-SAB** | a COPY of the new reader with the 401 leaf removed from its ONE definition by bytes, asserted exactly once | K2 reads 232 at 17Z, so every F row that reaches K2 stops there and reads OFF: **exactly 8 OFF** (F-IS, F-NOT, F-MID, F-ANCHOR, F-ERA, F-STABLE, F-SUM, F-BLIND). F-RED and all 29 existing rows read as predicted; the harness exits 1 |

**PREDICTED:** with the new reader, all 29 existing rows and these F rows read AS PREDICTED, and the harness exits 0. F-SAB makes the harness exit 1 with exactly the 8 rows named. **Outside the set:** stop and report.

The identity check (readdiff) is re-run for this commit with the pair printed on line 1: `read`'s code must be unchanged.

#### 4. Order and time

1. **Text.** This text, then the orchestrator's acceptance.
2. **Code.** The red commit, then the green commit, each with its plant output and sha sent.
3. **Push.** Pushed with its CI prediction first; CI read twice, at least 66 s apart.
4. **Run.** Only on the orchestrator's word: through the gate (bgcheck first), registered, never in the same hour as another production-logs read of mine (the population ended 02:19:37Z).
5. **Time.** It must END before 07:00Z, so it does not start after 06:10Z. If it cannot start by then, it waits until FXP's window has closed (after 08:00Z), on the orchestrator's word. The 11-22 hours stay readable until about 11-28.
6. **Result.** Written to `f1r/` as a separate file; this registration is never edited, only appended to.

ACCEPTED as text by the orchestrator (re-read 02:27:09Z: `ff8d7f596c0c`, 109 lines, written before any code). RESULT of the code (append-only): as registered.

- **Red:** the tally was written down before the run (`f1r/famcount/red-prediction.md`, `da342f2611c0`). Against the reader without famcount (blob `ac94f99a83d0`): 38 rows, 30 as predicted; exactly F-IS, F-NOT, F-MID, F-ANCHOR, F-ERA, F-STABLE, F-SUM and F-BLIND OFF; F-RED and the 29 existing rows as predicted; exit 1.
- **Green:** against the new reader (blob `a71303d95e6d`), 38 rows, all 38 as predicted, exit 0.
- **F-SAB:** the 401 leaf removed by bytes from its ONE definition, on a COPY. Exactly the same 8 OFF, each stopped at K2; exit 1. The working reader was never touched.
- **`read` unchanged:** `read` with reader `ac94f99a83d0` (before) against `a71303d95e6d` (after), the pair printed on line 1: 7 of 7 identical (`readdiff-check.out`, `36d848578d03`).
- **Commits:** `e80cbf655fc8` (the red rows; plant blob `117cfb9873a6`) and `b58485ef0d55` (the mode).
- **Outputs:** in blipscope-5c's handoff folder `f1r/famcount/` (red `0442774107b0`, green `689c6790b279`, sabotage `8ca31c7c49ec`).
- **CI at `b58485ef0d55`:** AS PREDICTED, two reads identical: 36 check-runs, 34 success and 2 skipped, 0 failures. The first read attempt died on a gh network error: an instrument failure with no reading. It was read again after a gh probe answered; CI itself was not re-run.

**The first run,** on the orchestrator's word (start at 03:20:00Z or later). The text of blipscope-5c's `f1r/f1-famcount-result-1.md` (sha256 `33dcc30cc2f9`) follows VERBATIM; only its `##` headings are demoted to `####` here.

F1 FAMILY COUNT: RESULT, graded against the frozen registration (`f1-famcount-1.md`, sha256 `ff8d7f596c0c`). Written 2026-11-24T03:23:34Z from date -u, after the run; counts only. Diagnostic after F1's no-reading; changes no reading.

#### The run

- **Go:** blipscope-orchestrator-c0, on the condition that the clock read 03:20:00Z or later. The registered wait ended at 03:20:02Z, and the chain re-checked 03:20:00Z–06:10:00Z itself.
- **Gate:** `gate-famcount`, bgcheck exit 4 (VOID) on the ruled five, no extras. PASS at 03:20:31Z.
- **Read:** started 03:20:31Z, exited **3** at 03:22:33Z, registered as `bg-20261124T032031Z-0b3995fb`. Head `b58485ef0d55`, reader `a71303d95e6d`.
- **Calls:** 3, every one a value on the first attempt (status 200, ABR 1).
- **Outputs, in f1-20261123/:**
  - `famcount.out`: `f1c6572b3ccb`
  - `famcount.json`: `956cb8341acd`
- **Ledger at the end:** bgcheck VOID on the same five; neither of my tags is still running.

#### What it read

| step | hour | query | read | must read | |
|---|---|---|---|---|---|
| K1 | 11-23 17:00Z | no-key | 232 | 232 | HOLDS |
| K2 | 11-23 17:00Z | no-key AND 401 | 218 | 218 | HOLDS |
| K3 | 11-23 17:00Z | no-key AND model exists | **232** | 218 | **OFF: stop** |

Nothing after K3 was queried: no H0, no 11-22 hour, no bucket.

#### Graded

**VERDICT: OUTSIDE (stopped at K3), as registered.** The instrument cannot answer at this scale: stop and decide. Neither IS nor NOT can be read. The reader printed the same verdict and exited 3.

#### Why K3 read 232: the registration's named divergence, with the source behind it

- **Named in advance.** Section 1 of the registration says: "`model exists` counts an empty-string model. classify counts a non-empty one only." K3 is the anchor written to catch exactly this.
- **The source.** `proxy/src/metrics.ts:9` declares `model: string` (required). `proxy/src/index.ts:452` sets `model: request.headers.get("X-Blip-Model") ?? ""`. So **every** req line carries a `model` key, empty when the header is absent. `model exists` is therefore true for every line, which predicts K3 = K1 = 232: what was read.
- **The alternative.** The API's `exists` misbehaving is not needed to explain the reading, and this read does not exclude it.
- **Why the plant did not catch it (my defect).** `fam_set`'s 14 non-api no-key lines carry NO `model` key, where the existing `classify_set` writes `model=""` on such lines, as the source does. The fake was built from the shape I imagined, not from the source. The plant therefore exercised a world production is not in. The live anchor caught it, which is what it was there for.

#### Not done

Nothing was re-run and nothing is amended. Any change waits on the orchestrator's decision.

RECORDED by the orchestrator (re-read 03:24:10Z, registration still `ff8d7f596c0c`): OUTSIDE, stopped at K3, as registered. The live anchor caught a plant that modelled a world production is not in. Option (1) was ACCEPTED as an append-only amendment and counts as the ONE re-run allowed after an instrument failure; options (2) and (3) were declined.

The text of AMENDMENT 1, lines 111-134 of the same file, follows VERBATIM.

AMENDMENT 1 (append-only, written 2026-11-24T03:24:57Z from date -u, before any code; accepted in principle by blipscope-orchestrator-c0 as its option (1) after the first run stopped at K3, f1-famcount-result-1.md 33dcc30cc2f9). Everything above stands except what this changes.

**The one change: the model leaf.**
- `FC_MODEL` becomes `model neq ""` (string) in place of `model exists`, in its ONE definition. K3, K5, H, E1 and W's S all take it from there.
- Why this is classify's test: every req line carries `model`, because `proxy/src/index.ts:452` sets it to the X-Blip-Model header or "". So "not the empty string" means non-empty, which is what classify counts. How `neq` treats an absent key does not arise for a req line.
- Only K3 read LIVE as 218 proves the leaf. The plant cannot.

**The plant's fake now mirrors the source.** `fam_set` writes `model=""` on EVERY line without a model (17Z's 14 others, H0's 2, 04Z's and 05Z's and 06Z's `/credits` lines, and the keyed `/credits` lines), as `classify_set` and the source do. The fake gains `neq`. No other row's data changes.

**PREDICTED, plant:**
- **RED** (the corrected fake; reader a71303d95e6d, which still says `exists`, as the new reader; ac94f99a83d0 as the pre-famcount reader): 38 rows, 30 as predicted.
  - **Exactly 8 OFF:** F-IS, F-NOT, F-MID, F-ANCHOR, F-ERA, F-STABLE, F-SUM and F-BLIND. Each one's got-line shows it stopped at K3, with K3 reading 232 against 218: production's failure, reproduced offline.
  - F-RED and the 29 existing rows read as predicted. The harness exits 1.
- **GREEN** (the new reader): 38 of 38, exit 0, with every F row's expectation unchanged. K3 reads 218, E1 100, E2 123.
- **F-SAB** (FC_STATUS emptied on a copy, the same driver): exactly the same 8 OFF, each at K2, exit 1.
- **readdiff** of a71303d95e6d against the new reader, with the pair on line 1: 7 of 7 identical.

**CI:** pushed with its prediction first, read twice.

**THE RUN:**
- On the orchestrator's word after CI.
- Not before 04:22:33Z, which is one hour after the first run ended at 03:22:33Z. Not after 06:10Z.
- The same 40 calls and rows as above.
- **This is the ONE re-run allowed after an instrument failure. If it stops at any must-read again, the result goes to the owner, not to a third attempt.**

RESULT of AMENDMENT 1's code (append-only): as amended.

- **Red:** the corrected fake, against the reader that still says `exists` (blob `a71303d95e6d`): 38 rows, 30 as predicted; exactly the same 8 F rows OFF, each stopped at K3 with 232 against 218, production's failure reproduced offline; exit 1.
- **Green:** against the new reader (blob `ba636810808b`), 38 of 38, exit 0.
- **F-SAB:** the same driver, on a COPY: the same 8 OFF, each at K2; exit 1.
- **`read` unchanged:** `read` with reader `a71303d95e6d` (before) against `ba636810808b` (after), the pair printed on line 1: 7 of 7 identical (`readdiff-check-a1.out`, `c7c2e8aa936d`).
- **Commits:** `9ec91d4f9d27` (the red: `fam_set` writes `model=""` and the fake gains `neq`; plant blob `04897ada1b66`) and `f92023f9874b` (the leaf).
- **Outputs:** in `f1r/famcount/` (red `9a3c005c1727`, green `9ff5030da3c2`, sabotage `ca0877d42e20`).
- **CI at `f92023f9874b`:** AS PREDICTED, two reads identical: 36 check-runs, 34 success and 2 skipped, 0 failures.

**The re-run,** on the orchestrator's word (start at 04:22:33Z or later, ONE attempt). The text of blipscope-5c's `f1r/f1-famcount-result-2.md` (sha256 `745d033a083c`) follows VERBATIM; only its `##` headings are demoted to `####` here.

F1 FAMILY COUNT: RESULT 2 (the one re-run, under AMENDMENT 1), graded against the frozen registration (`f1-famcount-1.md`, sha256 `036e18f087ee`; lines 1-109 `ff8d7f596c0c`). Written 2026-11-24T05:03:35Z from date -u, after the run; counts only. Diagnostic after F1's no-reading; changes no reading.

#### The run

- **Go:** blipscope-orchestrator-c0, on the condition that the clock read 04:22:33Z or later. The registered wait ended at 04:22:35Z, and the chain re-checked 04:22:33Z–06:10:00Z itself.
- **Gate:** `gate-famcount2`, bgcheck exit 4 (VOID) on the ruled five, no extras. PASS at 04:23:20Z.
- **Read:** started 04:23:20Z, exited **3** at 05:02:48Z, registered as `bg-20261124T042320Z-c590f7f4`. Head `f92023f9874b`, reader `ba636810808b`.
- **Calls:** all 40, every one a value on the first attempt (status 200, ABR 1).
- **Outputs, in f1-20261123/:**
  - `famcount2.out`: `3a609d4a17f0`
  - `famcount2.json`: `814b42f9359d`
  - The first run's files (`famcount.out` `f1c6572b3ccb`, `famcount.json` `956cb8341acd`) are untouched.
- **Ledger at the end:** bgcheck VOID on the same five; neither of my tags is still running.

#### Every must-read HELD

| step | hour | query | read | must read |
|---|---|---|---|---|
| K1 | 11-23 17:00Z | no-key | 232 | 232 |
| K2 | same | no-key AND 401 | 218 | 218 |
| K3 | same | no-key AND model (`neq ""`) | **218** | 218 |
| K4 | same | no-key AND api | 218 | 218 |
| K5 | same | S | 218 | 218 |
| H | 11-23 14:00Z | S | 247 | 247 |
| E1 | 11-22 05:00Z | keyed AND model AND api | 1888 | 1..1903 |
| E2 | same | status 200 | 1929 | 1..2139 |
| W | 11-22 04:00Z / 05:00Z / 06:00Z | no-key re-read | 2 / 236 / 255 | 2 / 236 / 255 |
| O | 11-22 05:00Z | 12 buckets, summed | 236 | 236 |

**K3 live read 218: the amended leaf is proven.** E1 shows the leaves read in the `dev` era.

#### The window

| hour (Z) | no-key | S (api AND 401 AND non-empty model) | share |
|---|---|---|---|
| 11-22 04:00 | 2 | 0 | |
| 11-22 05:00 | 236 | **203** | 86.0% |
| 11-22 06:00 | 255 | **248** | 97.3% |

05:00Z's no-key lines by five-minute bucket, starting 05:00: 0 1 1 0 8 **60 83** 7 12 22 22 20. **Onset (the first bucket with 10 or more): 05:25Z.**

#### Graded against section 2

| row | condition | met? |
|---|---|---|
| IS | S(05Z) at least 213 AND S(06Z) at least 230 | **NOT MET**: 203 is under 213 (06Z alone meets it, 248) |
| NOT | S(05Z) at most 23 AND S(06Z) at most 25 | not met |
| OUTSIDE | between the bands, or the two hours disagree | **MET** |

**VERDICT: OUTSIDE, as registered. The two hours disagree: stop and decide.** The reader printed the same verdict and exited 3.

**This was not a stop at a must-read.** Every must-read held, and the run reached its verdict. Amendment 1's owner clause names a must-read stop. Whether this result goes to the owner is the orchestrator's decision. No third attempt from me either way.

#### Observation: counts only, NOT a finding

- **The family dominates both hours.** 06:00Z is 97% S. 05:00Z is 86% S, with 33 of its no-key lines outside the family. 04:00Z's baseline is 2 no-key lines.
- **The rise starts at 05:25Z**, not at 05:00Z. The 05:25Z and 05:30Z buckets hold 60 and 83 lines (143 in ten minutes). From 05:45Z the buckets read 22, 22 and 20, which is about one line per 15 s, the same cadence as 11-23's.
- **What this read cannot place:** it counts S per HOUR, not per bucket. So it cannot say whether the 33 non-family lines of 05:00Z sit inside the 05:25Z–05:35Z burst or elsewhere, nor what shape they have.
- **Alternatives, none eliminated:**
  - the same refused device as 11-23's, starting at about 05:25Z on 11-22;
  - another device refused in the same way.

  Nothing identifying is read, and IS could not have separated these either.

RULED by the orchestrator (05:04:49Z; re-read 05:04:21Z: result `745d033a083c`, registration `036e18f087ee`): **STOP HERE.** Every must-read held, K3's amended leaf included. The onset, 05:25Z, is a registered output. The observation stays an observation, and no verdict is improvised.

- No per-bucket read, and no further attempt.
- The onset and the hourly family counts go to the owner as information, under the owner's open question about the refused device: chase it, or leave it.
- If the answer is chase it, a new registration starts from the offered per-bucket read, after FXP.

**`read`'s READER: NOT RULED.** `read`'s code is the same in `ac94f99a83d0`, `a71303d95e6d` and `ba636810808b` (the two identity runs above, each naming its pair). No ruling names a new blob as `read`'s reader for a future read run. The last ruling named `ac94f99a83d0`, and it stands until the orchestrator rules otherwise.

**`read`'s READER: RULED, `ba636810808b`** (the orchestrator, 2026-11-24, in its RECORDED of `8db32bf2c7b2`). Added append-only: the NOT RULED paragraph above stays as it was written, and this one supersedes it. The reader for a future read run is `ba636810808b`, proven by the CHAIN of identity runs, each naming its pair on line 1:
- `ac94f99a83d0` to `a71303d95e6d`, 7 of 7 (`36d848578d03`);
- `a71303d95e6d` to `ba636810808b`, 7 of 7 (`c7c2e8aa936d`).

`read`'s code is unchanged at each step, so the newest blob carries it. **Since the ruling,** the file moved to `21a83381b247` (FAMILY HOURS adds the `famhours` mode). Its readdiff against `ba636810808b` read 7 of 7 identical (`dd53ab4bbf84`), so `read`'s code is unchanged there too. The ruling does not name that blob: the ruled reader stays `ba636810808b` until the orchestrator rules otherwise.

F1 FAMILY HOURS. The owner ruled on the refused device: "chase it, cheapest check first, counts only, no ids." The cheapest check is his own lead from the firmware: do the family's hourly counts follow the radar's day and night cadences, and since when is it refused? The text of blipscope-5c's `f1r/f1-famhours-1.md` (sha256 `f522149ae7ac`, 185 lines, the registration as accepted) follows VERBATIM; only its `##` headings are demoted to `####` here.

F1 FAMILY HOURS (registration, append-only, written 2026-11-24T18:29:33Z from date -u, before any code and before any read of these hours as S; text only). Asked for by blipscope-orchestrator-c0 on the owner's ruling, quoted: "chase it, cheapest check first, counts only, no ids." It **runs only on the orchestrator's word, after it has read this text.** The question is the owner's: do the refused device's hourly counts follow his lead from the firmware, and since when is it refused? All output is labelled `[diagnostic after F1's no-reading; changes no reading]`. It is counts only and identifies no device: no device id, label, address, `ep`, `model` or colo value is printed or written anywhere.

#### 0. The lead and the family

**The owner's lead**, from v19 and main (`src/AircraftManager.cpp` `isNightNow` and the `src/CloudFeed.h` defaults), relayed by the orchestrator:
- the radar asks every 15 s when idle, and every 60 s at night;
- night is the sun at the board's saved location, and no location means 0,0;
- night at 0,0 ran 17:51Z-05:43Z on Nov 22 and 23.

**Sun times at 0,0,** computed here (NOAA low precision, zenith 90.833 deg): sunrise 05:43Z on 11-21 to 11-23 and 05:44Z on 11-24; sunset 17:50Z on 11-21 to 11-23 and 17:51Z on 11-24. Every transition in the window stays inside the 05:00Z or the 17:00Z hour.

**The family S** is unchanged from the FAMILY COUNT as amended (AMENDMENT 1):
- no key (`dev` and `devl` both absent);
- AND `status` 401;
- AND `model` not "";
- AND `ep` starting with one of the three API prefixes parsed from `proxy/src/index.ts`, one count query per prefix, summed.

The reader keeps ONE definition of each: `POP_NOKEY`, `FC_STATUS`, `FC_MODEL` and `fc_prefixes()`.

#### 1. PREDICTED: the owner's numbers, frozen before any read

Quoted: refusals per hour "about 60 in the 18:00Z to 04:00Z hours, about 240 in the 06:00Z to 16:00Z hours, about 110 in the 05:00Z hour and about 213 in the 17:00Z hour, every day. If the hours don't follow that, the lead is wrong; say so."

Each hour takes a CLASS from its start hour (UTC), the same every day:

| class | hours (Z) | the owner's number | "about" (the band, mine) |
|---|---|---|---|
| NIGHT | 18:00 to 04:00 (11 hours) | 60 | 52 to 68 |
| DAWN | 05:00 | 110 | 96 to 124 |
| DAY | 06:00 to 16:00 (11 hours) | 240 | 211 to 269 |
| DUSK | 17:00 | 213 | 187 to 239 |

**The band, proposed by me before any read: "about X" means X minus 12% to X plus 12%, rounded outward to whole lines.** Why 12%:
- **It must hold the excess already seen.** The family's known hours sit 2.3% to 3.3% above the owner's numbers: 248 (11-22 06:00Z) and 247 (11-23 14:00Z) against 240, and 218 (11-23 17:00Z) against 213.
  - If that excess is a constant 7 or 8 lines an hour rather than a percentage, it is 12% to 13% of 60.
  - 60 + 8 = 68 is the NIGHT band's edge. At 10%, 60 + 7 = 67 would fall outside.
- **It must exclude the alternatives named in section 3, and it does:**
  - NIGHT (52-68) excludes 0 (the device does not ask at night) and 240 (no night mode);
  - DAY (211-269) excludes 60, and 480 (two devices at 15 s);
  - DAWN (96-124) excludes 60 and 240 (no sunrise inside the 05:00Z hour);
  - DUSK (187-239) excludes 60 and 240 (no sunset inside the 17:00Z hour). 239 is one line under 240, and the day rate actually seen is 247-248.
- **In sun-time terms:** each minute moved from night to day adds 3 lines (4 by day, 1 by night). So DAWN's band of plus or minus 14 lines is about plus or minus 5 minutes of sunrise, and DUSK's plus or minus 26 lines about plus or minus 9 minutes of sunset.

**Each hour reads as one of:**
- **QUIET:** S is 5 or fewer. The population's quiet hours read 1 to 5 no-key lines.
- **IN:** S is inside its class's band.
- **HIGH:** above the band.
- **LOW:** below the band. A QUIET hour is LOW as well.

#### 2. The real start (the orchestrator's (a))

- **START is the first hour in the window whose S is inside the NIGHT band or the DAY band** (about 60 or about 240), whatever that hour's class. It may be before 11-22 05:00Z, which is why the window reaches back to 11-21 00:00Z.
- **Before START,** every hour must read QUIET, except the ONE hour directly before START, its onset hour, which can read anything. Any other hour before START that is not QUIET puts the run OUTSIDE.
- **Graded hours** are START and every hour after it to the window's end, less any BLIND hour dropped under section 4.
- **A known limit:** one hour landing in a band by chance (a short burst before a quiet spell) would be taken as START. It would show as QUIET hours after START, and those are printed.

#### 3. The verdict

For each class: the share of its graded hours that read IN.

| row | condition | then |
|---|---|---|
| **FOLLOWS** | every class has at least 2 graded hours, and in every class at least 80% of them read IN | the hours follow the lead |
| **DOES NOT FOLLOW** | in some class with at least 2 graded hours, fewer than 50% read IN | **the lead is wrong**: said so, with the class named |
| **OUTSIDE** | anything else: no START; an hour before START, other than the onset hour, that is not QUIET; more than 4 BLIND hours; a class with fewer than 2 graded hours and none under 50%; a class between 50% and 80% and none under 50%; any stop in section 4 | the instrument cannot answer at this scale: **stop and decide** |

**The order of the tests:**
1. A stop (section 4).
2. No START, an hour before START that is not QUIET, or more than 4 BLIND hours: OUTSIDE.
3. DOES NOT FOLLOW.
4. FOLLOWS.
5. Whatever is left: OUTSIDE.

**Printed for information, grading nothing:**
- the tallies per day and per class;
- every HIGH and LOW hour, by hour stamp, with its count;
- runs of QUIET hours after START;
- the same per-class shares on the hours never read before (section 5).

**What FOLLOWS would NOT show, named now so it cannot be improvised later:**
- WHOSE device. By design, nothing identifying is read.
- That the board has NO saved location. The counts see only sun times, and any place whose sunrise and sunset fall within minutes of 0,0's reads the same.
- That it is ONE device. The counts are a sum.
- WHICH firmware. Any build with the same cadences reads the same.

**What DOES NOT FOLLOW would NOT show:** why. The tallies say where the hours miss (night, dawn, day or dusk) and in which direction (HIGH or LOW), not the cause.

**My own prediction, stated so it can be wrong. It grades nothing.**
- **START is 11-22 06:00Z,** with 11-22 05:00Z (S 203, already read) as its onset hour, and every 11-21 hour QUIET. The reason: the seven hours before the 05:25Z onset read 1 to 5 no-key lines.
- **The verdict is FOLLOWS.** The reason is a magnitude only: 11-22 07:00Z to 13:00Z read 248 to 252 no-key lines, and 18:00Z to 20:00Z read 72 to 80.
- **The risk, named:** 11-22 14:00Z read 418 no-key lines and 21:00Z read 170. Both would be HIGH if S holds most of them.

#### 4. What it adds: `famhours <out.json>`, a new reader mode

**Count queries only (`view: calculations`), never `view: events`,** as in the FAMILY COUNT. No line and no `dev` value can enter the process, by construction, so a `dev`-era hour can be read. walk_hour's pre-flight is untouched. `read`, `classify`, `population` and `famcount` do not change.

**The window** is every hour from 11-21 00:00Z to the last closed hour when the run starts (`last_closed_hour()`): N hours, printed on the first line before any query. Nothing is asked, and it is a RIG error, if:
- the window does not contain 11-23 17:00Z; or
- the run starts after 11-27 00:00Z, when the oldest hour nears the 7-day retention of Workers Logs.

**THE CALLS, in this order.** Calls are 60 s apart (`CALL_GAP_S`). Each is a value only at ABR 1, re-asked as in F1-R-1.

| step | hour | query | must read |
|---|---|---|---|
| K1 | 11-23 17:00Z | no-key | 232 |
| K2 | same | no-key AND 401 | 218 |
| K3 | same | no-key AND model | 218 |
| K4 | same | no-key AND api (3 calls, summed) | 218 |
| M1 | 11-23 17:00Z | S (3 calls, summed) | 218, the FAMILY COUNT's K5 |
| M2 | 11-23 14:00Z | S | 247, its H |
| M3 | 11-22 05:00Z | S | 203, its W |
| M4 | 11-22 06:00Z | S | 248, its W |
| the window | every other hour, oldest first | S (3 calls each) | C-POP on 24 of them (below) |

M1 to M4 are window hours, read first. Their reads are also their window values.

**Why these anchors:**
- **K1 to K4** are the FAMILY COUNT's. Each leaf moves 17:00Z's count from 218 to 232, or to 696 for an ignored api leaf.
- **M1 to M4** re-read four counts already made. Workers Logs do not change, so a different number means the instrument moved.
- **C-POP** costs no call. In each of the population's 24 hours, 11-21 22:00Z to 11-22 21:00Z, S must be at most that hour's no-key read (`f1-population-result-1.md`): 2, 5, 2, 1, 1, 2, 2, 236, 255, 249, 251, 248, 252, 250, 251, 250, 418, 295, 283, 219, 74, 80, 72 and 170. S above no-key would mean the server ignored a leaf.

**Totals.** 6 + 3N calls, 60 s apart.
- If the run starts between 19:00Z and 20:00Z on 11-24, N is 91 (to 11-24 18:00Z): 279 calls, about 4 h 40 min, plus 30 s for each re-ask.
- That is long. The cost is the three API prefixes, each its own query.
- **A cheaper shape I considered and do not propose:** a day-long count per prefix, to skip a prefix that reads 0 all day. No count over 24 hours has been shown to answer at ABR 1, so it would need its own anchor and a new BLIND fallback.
- `CALL_GAP_S` is unchanged. Shortening it would be the orchestrator's ruling, not mine.

**Stops and exits:**
- **A K or M call** that is not a value after its re-asks makes the run BLIND: exit 6, no verdict.
- **K1 to K4 or M1 to M4 OFF, or C-POP failed:** the run stops at that step, names it, and queries nothing after it: exit 3.
- **A window hour** whose call is not a value after its re-asks: that HOUR is dropped and named, and the run goes on (the population's rule). More than 4 dropped hours: OUTSIDE.
- **FOLLOWS and DOES NOT FOLLOW** exit 0. OUTSIDE exits 3.
- **What it prints and writes:** counts, hour stamps, classes, the verdict and the step it stopped at, if any. Never an `ep`, `model`, colo, `dev` or `devl` value.

#### 5. What is already known, so nobody grades it as a blind prediction

- **S itself** is already read for four hours: 11-22 05:00Z 203, 06:00Z 248, 11-23 14:00Z 247 and 17:00Z 218 (`f1-famcount-result-2.md`).
- **No-key,** an upper bound on S, is already read for the population's 24 hours (above). Those counts went to the owner as information before this text.
- **So the hours never read before** are 11-21 00:00Z to 21:00Z, and 11-22 22:00Z to the window's end, less 11-23 14:00Z and 17:00Z.
- **The verdict is graded on all graded hours,** as registered. The per-class shares on the never-read hours alone are printed beside it, for information.

#### 6. Red first: rows added to `colo_occupancy_plant.py`

All rows are synthetic and offline. The fake builds the window as events: one per S line, plus the other no-key lines K1 to K4 need. The clock is pinned so the window ends at 11-24 18:00Z (N = 91). Every row checks:
- its exit;
- the verdict line;
- the step it stopped at, if any;
- the number of count requests;
- 0 events requests.

Never the exit alone.

| row | run | predicted |
|---|---|---|
| **Q-RED** | the reader before this change (blob `ba636810808b`), given `famhours` | usage, exit 2: **the red** |
| **Q-FOLLOWS** | the lead exactly: 11-21 QUIET; 11-22 05:00Z 203; then NIGHT 62, DAWN 113, DAY 247 and DUSK 218 (11-22 06:00Z 248) | exit 0; FOLLOWS; START 11-22 06:00Z; 279 count requests |
| **Q-WRONG** | as Q-FOLLOWS, but every NIGHT hour from 11-22 22:00Z reads 247 | exit 0; DOES NOT FOLLOW, naming NIGHT |
| **Q-MID** | as Q-FOLLOWS, but every third NIGHT hour from 11-22 22:00Z reads 120 | exit 3; OUTSIDE (NIGHT between 50% and 80%) |
| **Q-ANCHOR** | K4's api leaf ignored by the fake (696) | exit 3; stopped at K4 after 6 count requests |
| **Q-STABLE** | 11-22 05:00Z re-reads 202 | exit 3; stopped at M3 after 15 count requests |
| **Q-POP** | 11-22 01:00Z reads S 3 (its population no-key: 1) | exit 3; stopped at C-POP 11-22 01:00Z after 96 count requests |
| **Q-PRE** | 11-21 10:00Z reads 30, inside no band | exit 3; OUTSIDE (an hour before START not QUIET) |
| **Q-BLIND-1** | one window hour at ABR 100 on all three attempts | exit 0; FOLLOWS; that hour dropped and named; 279 count requests |
| **Q-BLIND-5** | five window hours so | exit 3; OUTSIDE (more than 4 BLIND) |
| **Q-BLIND-K** | K1 at ABR 100 on all three attempts | exit 6; no verdict; 3 count requests |

**Q-SAB:** a COPY of the new reader with `FC_STATUS` emptied by bytes, asserted to match exactly once (the F-SAB driver). K2 then reads 232, so every row that reaches K2 stops there.
- **Exactly 17 rows OFF:** the 8 F rows that F-SAB names, and 9 Q rows (all but Q-RED and Q-BLIND-K).
- The harness exits 1.

**PREDICTED, plant:**
- **RED** (the new rows on blob `ba636810808b`): 49 rows, 39 as predicted. The 10 Q rows after Q-RED read OFF (each a usage exit 2). The harness exits 1.
- **GREEN** (the new reader): 49 of 49, exit 0.
- **Q-SAB:** as above.
- **readdiff** of `ba636810808b` against the new reader, with the pair printed on line 1: 7 of 7 identical, so `read`'s code is unchanged.
- **Outside the set:** stop and report.

#### 7. Order and time

1. **Text.** This text, then the orchestrator's acceptance.
2. **Code.** The red commit, then the green commit, each sent with its plant output and sha. Then Q-SAB and readdiff.
3. **Push.** With its CI prediction first; CI read twice, at least 66 s apart.
4. **Run.** Only on the orchestrator's word: through the gate (bgcheck first) and registered. Never in the same hour as another production-logs read of mine (the last ended at 05:02:48Z on 11-24).
5. **Time.** About 4 h 40 min, starting before 11-27 00:00Z, and outside any window the orchestrator names.
6. **Result.** Written to `f1r/` as a separate file. This registration is never edited, only appended to.

ACCEPTED as registered by the orchestrator (2026-11-24 18:30Z, `f522149ae7ac`). Its cost was accepted as is: 279 calls at the unchanged 60 s gap, about 4 h 40 min. RESULT of the code (append-only): as registered.

- **Red:** the tally was written down before the run (`f1r/famhours/red-prediction.md`, `0031e4be7fba`). Against blob `ba636810808b`, which has no famhours mode: 49 rows, 39 as predicted; exactly the 10 Q rows after Q-RED OFF; exit 1.
- **Green:** against the new reader (blob `21a83381b247`), 49 of 49, exit 0.
- **Q-SAB:** `FC_STATUS` emptied by bytes on a COPY: exactly 17 OFF, the 8 F rows and 9 Q rows, every Q row stopped at K2; exit 1.
- **`read` unchanged:** `read` with reader `ba636810808b` (before) against `21a83381b247` (after), the pair printed on line 1: 7 of 7 identical (`readdiff-check.out`, `dd53ab4bbf84`).
- **Commits:** `5de2a9fcf106` (the 11 Q rows; plant blob `030856864f3e`) and `32a9f89294f8` (the mode).
- **Outputs:** in `f1r/famhours/` (red `95ccaf0642f3`, green `26079ded5d13`, sabotage `28ef0a2d582c`).
- **The push was refused once, by its own guard.** Main moved (#755 merged) between the CI prediction's read of it and the push, so nothing was pushed. The prediction was amended append-only, the guards were re-run on the new merge view, and the push went as a fast-forward of 2.
- **CI at `32a9f89294f8`:** AS PREDICTED, two reads identical: 36 check-runs, 34 success and 2 skipped, 0 failures.

**The run,** on the orchestrator's START (19:00:00Z or later; head `32a9f89294f8`, reader `21a83381b247`). The text of blipscope-5c's `f1r/f1-famhours-result-1.md` (sha256 `177ca75f7eaa`) follows VERBATIM; its `#` title is a plain line here, and its `##` headings are demoted to `####`.

F1 FAMILY HOURS: result 1 (stamped 2026-11-24T23:44:43Z from date -u; counts only, no ids)

**For:** the orchestrator. **By:** blipscope-5c. **Registration:** f1r/f1-famhours-1.md (f522149ae7ac), accepted, graded exactly as registered. This file is append-only: any correction goes below it.

#### The run

- **Started** 19:00:32Z on the orchestrator's START (date -u ≥ 19:00:00Z). The gate passed VOID on the ruled five. Tag bg-20261124T190031Z-768c175e.
- **Exited 0** at 23:43:00Z, after 279 count queries over the window 11-21 00:00Z to 11-24 18:00Z (91 hours).
- **Reader** `tools/bill/colo_occupancy_read.py` at 32a9f89294f8 (blob 21a83381b247), mode `famhours`.
- **Outputs** in f1-20261123/:
  - `famhours.out` 69ca09c085cb, 418 lines;
  - `famhours.json` c820dd4e69bc;
  - `famhours.err` 16803fbd5c80, which holds only the token file's "permissions are not checked on Windows" note.
- **End check:** bgcheck exit 4 at 23:44:40Z, VOID on exactly the ruled five (13848, 13932, 16340, 20184, 59348), nothing else.

#### Anchors and controls: all held

| check | read | must read | |
|---|---|---|---|
| K1 11-23 17Z no-key | 232 | 232 | HOLDS |
| K2 no-key AND 401 | 218 | 218 | HOLDS |
| K3 no-key AND model | 218 | 218 | HOLDS |
| K4 no-key AND api (summed) | 218 | 218 | HOLDS |
| M1 11-23 17Z S | 218 | 218 | HOLDS |
| M2 11-23 14Z S | 247 | 247 | HOLDS |
| M3 11-22 05Z S | 203 | 203 | HOLDS |
| M4 11-22 06Z S | 248 | 248 | HOLDS |
| C-POP (24 hours) | every S ≤ the population's no-key | | HOLDS |
| BLIND hours | none dropped | ≤ 4 | |

#### START and the hours before it

- **START: 11-22 06:00Z**, the first hour inside the NIGHT or DAY band.
- **Before START:** 30 hours were read. The onset hour, 11-22 05:00Z, read S 203. **No hour before START other than the onset was not QUIET** (all 11-21 hours and 11-22 00-04Z read 0).

#### The tallies (graded hours: 61 = 91 − 30 before START)

| class | about | band | graded | IN | HIGH | LOW | share IN |
|---|---|---|---|---|---|---|---|
| NIGHT | 60 | 52..68 | 23 | 10 | 10 | 3 | **43%** |
| DAWN | 110 | 96..124 | 2 | 1 | 0 | 1 | 50% |
| DAY | 240 | 211..269 | 33 | 20 | 2 | 11 | 61% |
| DUSK | 213 | 187..239 | 3 | 2 | 0 | 1 | 67% |

#### GRADED, as registered

**VERDICT: DOES NOT FOLLOW. The lead is wrong in NIGHT** (START 11-22 06:00Z; NIGHT 10/23, DAWN 1/2, DAY 20/33, DUSK 2/3 IN).
- The order of the tests ran as registered: no stop; START exists; no pre-START hour other than the onset is not QUIET; 0 BLIND.
- Then DOES NOT FOLLOW: NIGHT has at least 2 graded hours, and fewer than 50% of them read IN.
- The reader printed the same verdict and exited 0.

**What DOES NOT FOLLOW does not show (registered):** why. The tallies say where the hours miss and in which direction, not the cause.

**My own prediction (it graded nothing):**
- START 11-22 06:00Z: **right.**
- The verdict FOLLOWS: **wrong.**
- The named risk at 11-22 14:00Z (418 no-key) read S 247, IN. The HIGH hours of that day came at 15Z and 16Z instead (293 and 282).

#### Observations: counts only, NOT findings

**O1. The family STOPS inside the window.**
- 11-24 03:00Z read S 8. From **04:00Z to 18:00Z, all 15 hours read 0**. Every one of those 45 calls answered status 200, ABR 1, count 0.
- **Alternatives, none eliminated by this read:**
  - (a) the device stopped asking (power, network);
  - (b) it left the family's filter (a key, a re-enrolment, another firmware);
  - (c) production's answer to it changed, so the `status = 401` leaf no longer matches (for example, a deploy);
  - (d) the log pipeline is incomplete for those hours. ABR 1 rules out sampling, not a gap, and every anchor sits on 11-22 or 11-23, so **no anchor stands inside the stop.**
- A control that could separate (d) from the rest is the population's own no-key or total count over the same 15 hours. It was not registered and has not been read.

**O2. The stop carries the NIGHT result. This is arithmetic on the printed lines, not a second verdict.**
- NIGHT's 13 hours that were not IN include 11-24 03Z (8), 04Z (0) and 18Z (0) as LOW, and 11-24 00Z (231) and 01Z (178) as HIGH.
- On 11-22 and 11-23 alone, NIGHT reads 9/17 (53%), DAY 20/22, DAWN 1/1 and DUSK 2/2. Under the registered table that set would be neither DOES NOT FOLLOW nor FOLLOWS, but OUTSIDE.
- The registered verdict counts every graded hour, the stop included, and it stands as printed.

**O3. NIGHT hours at the DAY rate.**
- 11-23 18Z read 68, then 19Z through 11-24 01Z read **328, 248, 247, 248, 248, 231, 178**, about the DAY band's 240, at hours the lead calls night (sunset at 0,0 is about 17:50Z).
- Earlier, 11-22 21Z read 169 and 11-23 03Z read 177.
- **Alternatives, none eliminated:**
  - the board's saved location changed on 11-23, moving its night;
  - a second device with the same signature adding its lines (the counts are a sum);
  - a reboot into another cadence.
- The step at 11-23 19Z (68 → 328) is the sharpest edge in the window.

**O4. The rest of the pattern is as the lead says.**
- 11-22 07-13Z and 11-23 06-16Z read 247-248 an hour (DAY, IN).
- 11-22 18-20Z, 23Z and 11-23 00-02Z and 04Z read 67-68 (NIGHT, IN).
- 11-23 05Z read 119 (DAWN, IN).

#### What happens next

Nothing from me. **Per the registration: no re-run, no new read, no third attempt without the orchestrator's word.**
- O1's control (the population over the 15 stopped hours) is the cheapest read that would separate "the device stopped" from "the logs stopped". It is offered, not run.
- Whether the result goes to the owner, under his "chase it, counts only, no ids", is the orchestrator's decision.

RECORDED by the orchestrator (2026-11-24 23:46Z; result `177ca75f7eaa`, registration `f522149ae7ac`): DOES NOT FOLLOW, graded exactly as registered. NIGHT reads 10 of 23 IN (43%). There was no instrument failure: K1-K4, M1-M4 and C-POP hold, and 0 hours were BLIND. The prediction had START right and FOLLOWS wrong. O1-O4 stay OBSERVATIONS, each with its alternatives open. O2's 53% is arithmetic, not a second verdict, and the registered verdict stands.

- **GO** on the one read offered under O1: the population over the same 15 hours, with control hours on each side, its meaning registered first (POP-GAP, below).
- **Asked beside it,** read-only: did a production proxy deploy run on 11-24 between 03Z and 07Z?

F1 POP-GAP. The text of blipscope-5c's `f1r/f1-popgap-1.md` (sha256 `7ee3a7a57822`, 96 lines, the registration as accepted) follows VERBATIM; its `#` title is a plain line here, and its `##` headings are demoted to `####`.

F1 POP-GAP: the population across family hours' 15-hour stop (registration 1; stamped 2026-11-24T23:50:12Z from date -u, before any reader or read)

**For:** the orchestrator. **By:** blipscope-5c. **On:** its GO of 23:46Z ("the one read you offered, to separate O1's (a)-(c) from (d), counts only, predictions first"). Nothing is built or run before the orchestrator accepts this. This file is frozen: corrections go below it, append-only.

#### 0. Why

FAMILY HOURS (result 177ca75f7eaa) read **S = 0 for all 15 hours, 11-24 04:00Z to 18:00Z**, after 03:00Z read 8. Its O1 names four explanations, none eliminated:
- (a) the device stopped asking;
- (b) it left the family's filter (a key, a re-enrolment, another firmware);
- (c) production's answer to it changed (the 401 leaf no longer matches);
- (d) the log pipeline is incomplete for those hours.

No family-hours anchor sat inside the stop. **This read asks the one question that separates (d) from the rest: did the WHOLE population keep logging through the stop?**

#### 1. The population, defined in the reader's own leaves (one definition each)

| name | the filter (`params()`, scope on) | what it is |
|---|---|---|
| **T** | `$metadata.service` = the production proxy AND `evt` = `req` | every request line the Worker logs, refused or served, any status: the universe the family filter subsets |
| **N** | T AND `dev` absent AND `devl` absent (`POP_NOKEY`) | the no-key lines; the family is a subset of these |
| **K** | **T − N**, computed | the device-attributed lines. **Independent of the family by construction:** the family is no-key |
| **S** | the family, exactly as FAMILY HOURS reads it (`POP_NOKEY` AND `FC_STATUS` AND `FC_MODEL` AND each of the 3 API prefixes, summed) | read only at the 4 control hours |

Count queries only: no event is read, so no line and no `dev` value is ever seen.

#### 2. The window and the calls (fixed here)

- **The window: 11-24 02:00Z to 20:00Z, 19 closed hours.**
  - **Controls before:** 02Z, 03Z (the family read 68 and 8).
  - **The stop:** 04Z to 18Z (15 hours).
  - **Controls after:** 19Z, 20Z (the family not yet read).
- **Anchors first, each a re-read of a value already read:**
  - **ANCHOR-PRE** 11-19 12:00Z: T must read **3695** and N **36** (the population mode's own `POP_ANCHORS`);
  - **ANCHOR-POST** 11-23 14:00Z: T must read **5553** and N **249**;
  - **S-02** 11-24 02:00Z: S must read **68**;
  - **S-03** 11-24 03:00Z: S must read **8** (both from FAMILY HOURS' printed lines).
- **Then, per window hour:** T and N (2 calls), and S at 19Z and 20Z (3 calls each), which are new and grade nothing.
- **Calls:** 4 (POP anchors) + 6 (S-02, S-03) + 6 (S at 19Z and 20Z) + 38 (T and N × 19) = **54**, at `CALL_GAP_S` 60 s: **about 55 minutes**. ABR 1 required on every call, as in every F1 read.

#### 3. THE VERDICT (registered before any read)

**The order of the tests:**
1. **A STOP:**
   - any anchor not equal to its must-read → **stop, exit 3** ("the instrument does not read what it read before");
   - any BLIND anchor → **exit 6**;
   - any BLIND window hour → **stop, exit 3**. No hour is dropped here: this read IS about missing hours.
2. **LOG GAP, (d):** T reads **0 in all 15 stopped hours**, and T > 0 in all 4 controls.
3. **THE FAMILY'S OWN CHANGE, (a)-(c):** in **every** stopped hour, T > 0 AND **K ≥ 50% of the median K of the 4 control hours**.
4. **OUTSIDE:** anything else. That includes:
   - some stopped hours at T = 0 and others not;
   - K under the 50% floor in any stopped hour while T > 0;
   - any control hour with T = 0.
   - → **stop and decide**; nothing is improvised.

**Why K and not T:** T contains the family itself, so a 248-an-hour device leaving would move it. K cannot move with the family, so it measures the logs' health and nothing else. **Why 50% of the control median:** device traffic varies across the day (the population anchors read 3,695 and 5,553 total at two different hours), so the floor is set to catch a collapse, not a dip.

**Registered SUB-READING, printed and graded ONLY under (a)-(c).** It uses N over the stopped hours, against the family's own rates (about 240 an hour by day, about 60 by night, under the lead) and the non-family no-key baseline (14 at 11-23 17Z = K1 − K2; 36 at ANCHOR-PRE, before the family existed).
- **N-HIGH:** N ≥ 150 in **every** DAY-class stopped hour (06Z to 16Z, 11 hours). **Meaning:** the device still asks with NO key, but no longer matches the family filter. That is (c), a changed answer, or (b) through a firmware or path change; (a) is excluded.
- **N-LOW:** N ≤ 50 in **every** stopped hour. **Meaning:** keyless asking stopped. That is (a) stopped, or (b) now keyed; (c) is excluded.
- **NOT SEPARATED:** anything else, printed as such.

**Printed for information, grading nothing:**
- T, N and K per hour;
- the control median K and the floor;
- S at 19Z and 20Z (does the family come back?).

#### 4. What it CANNOT show

- **Under (a)-(c) with N-LOW:** whether the device stopped or got a key. A newly keyed device adds about 250 an hour to K, inside K's normal variation. Separating those needs an identifying read, which this read never makes.
- **Under N-HIGH:** whether production's answer changed (c), or the firmware's path or model changed (b). The status of those N lines would separate them, and is NOT read here.
- **Nothing identifying:** no `dev`, no label, no line, no address.

#### 5. My own prediction, stated so it can be wrong (it grades nothing)

- **(a)-(c), not (d).** A 15-hour total gap in the production Worker's logs would have blinded every F1 read in that range. FAMILY HOURS' calls for those hours all answered status 200, ABR 1. That is not proof of logs, only of a working query, which is why this read exists.
- **Sub-reading: N-LOW,** weakly held: the device stopped asking keyless. It rests on nothing but the stop's sharpness (03Z read 8, then 0).

#### 6. The deploy records (asked beside this read; records only, no Workers Logs read)

- **Asked:** did a production proxy deploy run on 11-24 between 03Z and 07Z? The owner's 03:52Z words booked one for 06:00Z.
- **Answered SEPARATELY, from the deploy records.** A deploy after the 04Z onset cannot explain the onset; it would be named, not graded.

#### 7. The build, once accepted

A new mode, `popgap`, in `tools/bill/colo_occupancy_read.py`, plus plant rows in `colo_occupancy_plant.py`, **red first**:
- Q-GAP (all stopped T 0 → LOG GAP);
- Q-OWN-NLOW;
- Q-OWN-NHIGH;
- Q-OWN-NSPLIT (→ NOT SEPARATED);
- Q-PARTIAL (some stopped hours 0 → OUTSIDE);
- Q-COLLAPSE (K under the floor → OUTSIDE);
- Q-CTL0 (a control hour T 0 → OUTSIDE);
- Q-ANCHOR (a wrong anchor → stop 3);
- Q-BLIND-A (exit 6) and Q-BLIND-W (stop 3).

`read`'s code is proven unchanged by readdiff against the ruled reader ba636810808b, as before. Then the push (prediction first), the CI read through the gate, and the run through the gate. **About 1.5-2 hours of build, then about 55 minutes of run.**

ACCEPTED by the orchestrator (2026-11-24 23:51Z, `7ee3a7a57822`, 96 lines). It accepted K as the separator, because K is independent of the family by construction: a device asking 248 times an hour cannot move it. That acceptance is half of the miss recorded below.

**The deploy records** (section 6), RECORDED by the orchestrator:
- production `/healthz` reports `4a02123ca992` (11-17), and no newer proxy commit is deployed, so no new code reached production 03Z-07Z;
- that does not exclude a same-commit redeploy, so the `wrangler deployments list` read went to map-server, the deployer with the helper. Its answer is P5 in the result below.

RESULT of the code (append-only): as registered, with one naming change.

- **The naming change,** stated in the red prediction before the code (`f1r/popgap/red-prediction.md`, `81e12fe9aa98` with its READ lines). `Q-ANCHOR` already existed (FAMILY HOURS' row), so the ten rows are PG-GAP to PG-BLIND-W, one-to-one. PG-RED is added, as each mode has had one.
- **Red:** against the reader without popgap (blob `21a83381b247`): 60 rows, 50 as predicted; exactly the 10 PG rows after PG-RED OFF; exit 1.
- **Green:** against the new reader (blob `0d5dc4ffde7c`), 60 of 60, exit 0.
- **P-SAB:** the floor test replaced by `False`, once, on a COPY: exactly PG-COLLAPSE OFF, reading THE FAMILY'S OWN CHANGE; exit 1.
- **`read` unchanged:** `read` with reader `ba636810808b` (before) against `0d5dc4ffde7c` (after), the pair printed on line 1: 7 of 7 identical (`readdiff-check.out`, `e1684806b15d`).
- **Commits:** `e4a2649d7cf2` (the 11 PG rows; plant blob `ee1d98ec2996`) and `fa7145e06f8b` (the mode).
- **Outputs:** in `f1r/popgap/` (red `b2621f07f6cf`, green `dbfd3d92ee82`, sabotage `70ae90580efe`).
- **CI at `fa7145e06f8b`:** AS PREDICTED, two reads identical: 39 check-runs, 37 success and 2 skipped, 0 failures.

**The run,** on the orchestrator's GO, through the gate (head `fa7145e06f8b`, reader `0d5dc4ffde7c`). The text of blipscope-5c's `f1r/f1-popgap-result-1.md` (sha256 `d1f75abdda52`) follows VERBATIM; its `#` title is a plain line here, and its `##` headings are demoted to `####`.

F1 POP-GAP: result 1 (stamped 2026-11-25T01:16:43Z from date -u; counts only, no ids)

**For:** the orchestrator. **By:** blipscope-5c. **Registration:** f1r/f1-popgap-1.md (7ee3a7a57822), accepted, graded exactly as registered. This file is append-only: any correction goes below it.

#### The run

- **Started** 00:21:51Z through the gate (PASS, VOID on the ruled five). Tag bg-20261125T002151Z-18dda469.
- **Preconditions held** at 00:21:31Z: head fa7145e06f8b, reader blob 0d5dc4ffde7c, porcelain 0, before 11-26 06:00Z.
- **Exited 3** at 01:15:25Z after 54 count queries. **Every query answered status 200 at ABR 1:** 0 not-a-value.
- **Outputs** in f1-20261123/:
  - `popgap.out` 7210ac34bc83, 82 lines;
  - `popgap.json` 8c14214cb38a;
  - `popgap.err` holds only the token file's Windows-permissions note.
- **End check:** bgcheck exit 4 at 01:16:43Z, VOID on exactly the ruled five, nothing else.

#### Anchors: all held, no stop

| check | read | must read | |
|---|---|---|---|
| ANCHOR-PRE 11-19 12Z | T 3695, N 36 | 3695 / 36 | HOLDS |
| ANCHOR-POST 11-23 14Z | T 5553, N 249 | 5553 / 249 | HOLDS |
| S-02 11-24 02Z | S 68 | 68 | HOLDS |
| S-03 11-24 03Z | S 8 | 8 | HOLDS |

No window hour was BLIND, and no hour read N above T.

#### The hours (T = every req line, N = no key, K = T − N)

| hour | role | T | N | K |
|---|---|---|---|---|
| 02Z | control | 2728 | 84 | 2644 |
| 03Z | control | 3097 | 13 | 3084 |
| 04Z | stop | 2292 | 7 | 2285 |
| 05Z | stop | 1780 | 2 | 1778 |
| 06Z | stop | 1673 | 15 | 1658 |
| 07Z | stop | 1703 | 4 | 1699 |
| 08Z | stop | 2001 | 2 | 1999 |
| 09Z | stop | 2653 | **337** | 2316 |
| 10Z | stop | 2161 | 8 | 2153 |
| 11Z | stop | 2677 | 4 | 2673 |
| 12Z | stop | 3532 | 0 | 3532 |
| 13Z | stop | 4731 | 9 | 4722 |
| 14Z | stop | 5152 | 8 | 5144 |
| 15Z | stop | 5978 | 4 | 5974 |
| 16Z | stop | 6260 | 6 | 6254 |
| 17Z | stop | 6589 | 9 | 6580 |
| 18Z | stop | 6212 | 4 | 6208 |
| 19Z | control | 6529 | 1 | 6528 (S 0) |
| 20Z | control | 6451 | 4 | 6447 (S 0) |

- **Controls:** K 2644, 3084, 6528, 6447; median K 4765.5; floor (50%) 2382.8.

#### GRADED, as registered

**VERDICT: OUTSIDE. K was under the floor in 7 stopped hours (04Z, 05Z, 06Z, 07Z, 08Z, 09Z, 10Z). The instrument cannot answer at this scale: stop and decide.**
- **Not LOG GAP:** T read above 0 in every stopped hour.
- **Not THE FAMILY'S OWN CHANGE:** the floor failed in 7 hours.
- The reader printed the same verdict and exited 3.
- **The N sub-reading is NOT graded**, because it applies only under (a)-(c).

**My own prediction (it graded nothing):** "(a)-(c), not (d)" did not happen as registered, because the verdict is OUTSIDE. N-LOW is not graded.

#### THE REGISTRATION'S MISS, named as mine

The floor compared each stopped hour's K with **half the median of four controls that mix two night-rate hours (02Z, 03Z: K about 2.6k-3.1k) with two day-rate hours (19Z, 20Z: K about 6.5k).** That median, 4765.5, is pulled up by the daytime pair, so the stop's own night-rate hours fall under it.
- The registration's own words ("the floor is set to catch a collapse, not a dip") named the intent. **The controls I chose did not deliver it.**
- A same-hour comparison, the day before, was considered while drafting and not taken. It would have separated time of day from a loss.
- **This is the escape hatch working as written:** a predicted set too narrow for the data. **No reading is improvised from it.**

#### Observations: counts only, NOT findings

**P1. T is above 0 in every one of the 15 stopped hours (1,673 to 6,589), and every query was a value at ABR 1.**
- So the registered LOG GAP condition (T = 0 in all 15) did not occur, and no single stopped hour read 0.
- **What it does NOT show:** whether the logs were COMPLETE. A partial loss reads as a lower T, which is exactly the dip the floor caught.

**P2. K's shape across the window is smooth:**
- it falls from 3,084 (03Z) to a minimum of 1,658 (06Z);
- then it rises steadily to about 6,500 (16Z-20Z), with no step at 04Z or at 19Z, where the family's stop begins and ends.
- **Alternatives, none eliminated:**
  - (i) the keyed fleet's own daily cycle (night-rate polling, fewer boards on at night);
  - (ii) a partial log loss that happens to follow a smooth curve.
- **What would separate them:** the same hours on another day (for example K for 11-23 04Z-18Z). Under (i) the curve repeats; under (ii) 11-24 sits below it. That is a new read, needing its own registration. It is offered, not run.

**P3. N during the stop:**
- 0 to 15 in 14 of the 15 hours. That is the non-family baseline: 14 at 11-23 17Z, 36 at ANCHOR-PRE.
- **The exception is 09Z: N 337**, while FAMILY HOURS read S = 0 there. So 337 keyless lines in that hour are not the family's filter (401, a model, an API prefix).
- **Alternatives, none eliminated:**
  - another keyless client (a crawler, or a page fetched many times);
  - the device asking keyless in a way the family filter does not match (another status, path or model).
- This read cannot place them.

**P4. The family did not return:** S read 0 at 19Z and at 20Z.

**P5. Deploys (map-server's deployments list, through the helper, with its coverage control; relayed by the orchestrator):**
- 0 deployments on 11-24 between 03:00Z and 07:00Z;
- version 80c524ea (deployed 11-23 11:47:30Z) served the whole window.
- So O1's (c) **as a deploy, redeploys included, is EXCLUDED.**
- **Not covered:** a config change made outside a deploy, such as a KV-held list (for example `cfg:` keys) that changes how a request is answered. It bears on (c) only if the device still asks keyless and is answered differently. P3's 09Z burst is the only stopped hour where keyless lines rose, and this read cannot say whose they were.

#### What happens next

Nothing from me. **No re-run and no new read without the orchestrator's word.**
- The offered next read is **P2's same-hours-another-day comparison**, registered first, with controls of matching time of day. It is the cheapest read that would separate "the logs were whole" from "a partial loss".
- Whether this goes to the owner as it stands, with the family-hours result, is the orchestrator's decision.

RECORDED by the orchestrator (2026-11-25 01:17Z; result `d1f75abdda52`): OUTSIDE, graded as registered. It was not an instrument stop: the anchors held, and 0 hours were BLIND. The escape hatch worked, and nothing was re-graded. P1-P5 stay observations with their alternatives open.

**THE MISS, recorded as two misses:**
- **blipscope-5c's:** the floor's four controls mixed two night-rate hours (02Z, 03Z) with two day-rate hours (19Z, 20Z), so their median sat above the stop's own night-rate hours. The registration named the intent ("a collapse, not a dip"), and the controls did not deliver it. A same-hour comparison on another day was considered while drafting and not taken.
- **The orchestrator's:** it accepted K as "the right separator" without checking the controls' time of day.

What carries forward: a control must match the measured hours in everything except what is being tested. These differed in time of day, and the floor measured that.

GO: THE SAME HOURS ON OTHER DAYS (POP-DAY, below), registered first, with a tolerance derived before the read from 11-22 against 11-23.

F1 POP-DAY. The text of blipscope-5c's `f1r/f1-popday-1.md` (sha256 `35221286d05e`, 103 lines, the registration as accepted) follows VERBATIM; its `#` title is a plain line here, and its `##` headings are demoted to `####`.

F1 POP-DAY: 11-24's stopped hours against the same clock hours on 11-22 and 11-23 (registration 1; stamped 2026-11-25T01:18:44Z from date -u, before any reader or read)

**For:** the orchestrator. **By:** blipscope-5c. **On:** its GO after POP-GAP result 1 (d1f75abdda52): "THE SAME HOURS ON OTHER DAYS … with a tolerance you derive BEFORE the read from 11-22 vs 11-23's own hour-by-hour spread". Nothing is built or run before it accepts this. Frozen: corrections go below, append-only.

#### 0. Why, and what is already known

POP-GAP read K (device-attributed lines) on 11-24 04Z-18Z as a smooth curve, from 1,658 to 6,580. Its two alternatives (P2) were:
- (i) the keyed fleet's own daily cycle;
- (ii) a partial log loss that follows a curve.

The same clock hours on other days separate them: under (i) the curve repeats, and under (ii) 11-24 sits below it.

**Already known, so it cannot be graded as a blind prediction:** 11-24's T and N for every one of the 15 hours (POP-GAP's printed lines). They are used here as MUST-READS (section 2), not as a prediction. **What is NOT known when this is frozen:** any value on 11-22 or 11-23 for these hours. The tolerance is a FORMULA fixed here and computed from those two days alone, so it cannot be fitted to 11-24.

#### 1. The definitions (POP-GAP's, unchanged)

- **T** = `$metadata.service` = the production proxy AND `evt` = `req`;
- **N** = T AND `dev` absent AND `devl` absent;
- **K** = T − N.

Count queries only; nothing identifying is read.

#### 2. The calls (fixed here; 94 count queries, about 95 minutes at 60 s, ABR 1 on every one)

1. **Anchors (4):** ANCHOR-PRE 11-19 12Z must read T 3695 and N 36; ANCHOR-POST 11-23 14Z must read T 5553 and N 249.
2. **R-24, 11-24 re-read (30):** T and N for 04Z-18Z, each pair MUST EQUAL POP-GAP's printed pair:

   | hour | T | N |
   |---|---|---|
   | 04Z | 2292 | 7 |
   | 05Z | 1780 | 2 |
   | 06Z | 1673 | 15 |
   | 07Z | 1703 | 4 |
   | 08Z | 2001 | 2 |
   | 09Z | 2653 | 337 |
   | 10Z | 2161 | 8 |
   | 11Z | 2677 | 4 |
   | 12Z | 3532 | 0 |
   | 13Z | 4731 | 9 |
   | 14Z | 5152 | 8 |
   | 15Z | 5978 | 4 |
   | 16Z | 6260 | 6 |
   | 17Z | 6589 | 9 |
   | 18Z | 6212 | 4 |

   **This is the "prove the artifact is holding still" read.** If 11-24's counts have moved since 00:21Z, late-arriving logs are themselves evidence, and the run STOPS rather than compare a moving target.
3. **The reference (60):** T and N for 04Z-18Z on **11-22** and on **11-23**.

#### 3. THE TOLERANCE, a formula fixed now

For each clock hour h (04Z…18Z), let a = K(11-22, h) and b = K(11-23, h).
- **The hour's own day-to-day spread:** s_h = |a − b| ÷ ((a + b) / 2).
- **τ = the MEDIAN of the 15 s_h, with a floor of 0.10.** The median is the typical day-to-day difference across these hours. The floor keeps two near-identical days from making the band narrower than ordinary noise.
- **The band for hour h:** from **min(a, b) × (1 − τ)**, rounded down, to **max(a, b) × (1 + τ)**, rounded up.
- **11-24's K(h) reads:** IN (inside the band), BELOW, or ABOVE.

#### 4. THE VERDICT (registered before any read)

**The order of the tests:**
1. **A STOP (exit 3, or 6 for a BLIND anchor):**
   - an anchor off;
   - **an R-24 pair not equal to POP-GAP's** ("the target moved");
   - a BLIND window hour (nothing is dropped);
   - N above T in any hour;
   - a reference hour with T = 0 (s_h is undefined).
2. **THE FLEET'S DAILY CYCLE, no partial loss:** 11-24's K is **IN in all 15 hours**.
3. **A PARTIAL LOSS:** 11-24's K is **BELOW in a run of at least 3 consecutive hours**, and ABOVE in none. The run's hours are named.
4. **OUTSIDE:** anything else. That includes a BELOW hour not in a run of 3, any ABOVE hour, or two separate runs. → **stop and decide.**

**Printed for information, grading nothing:**
- τ and every s_h;
- every hour's band, 11-24's K and its mark;
- **N at 09Z on 11-22, 11-23 and 11-24** (the 337 burst against the same hour on two other days);
- the reference days' own K curves.

#### 5. What each verdict CANNOT show

- **CYCLE does not show that the logs are complete.** It shows that 11-24 matches two other days. A loss that hit all three days alike would be invisible.
- **PARTIAL LOSS does not show WHOSE loss.** Fewer device-attributed lines than usual reads the same whether the logs dropped them or the devices were genuinely quiet (an outage, a network failure, many boards off). This read cannot separate those.
  - The instrument that could is a second pipeline counting the same requests: the Worker's Analytics Engine data points. That is offered, not part of this read.
- **The days differ in kind:** 11-22 is a Sunday, 11-23 a Monday and 11-24 a Tuesday, and the fleet can grow day to day. τ absorbs a difference only as large as the reference days' own. A weekday pattern stronger than the Sunday-Monday spread would read as BELOW or ABOVE.
- **Nothing identifying is read.**

#### 6. My own prediction (grades nothing)

**CYCLE**, weakly held. POP-GAP's K curve was smooth, with its minimum at 06Z and no step at the family's stop. But that is exactly the shape I could not tell apart from (ii), which is why this read exists.

#### 7. The build, once accepted

A mode `popday` in `colo_occupancy_read.py`, with plant rows red first (named PD-, so no clash):
- PD-RED (the pre-popday reader, 0d5dc4ffde7c);
- PD-CYCLE;
- PD-LOSS (a run of 4 BELOW);
- PD-ODD (one BELOW → OUTSIDE);
- PD-ABOVE (→ OUTSIDE);
- PD-TWO (two runs → OUTSIDE);
- PD-MOVED (an R-24 pair changed → stop);
- PD-FLOOR (identical reference days → τ = 0.10);
- PD-ANCHOR;
- PD-BLIND-A;
- PD-BLIND-W.

Then a sabotage (the run length 3 → 1, so PD-ODD turns OFF), readdiff against ba636810808b, the push (prediction first), CI and the run, each through the gate. **About 1.5 hours of build, then about 95 minutes of run. It must start before 11-26 06:00Z (ANCHOR-PRE's retention).**

ACCEPTED by the orchestrator (2026-11-25 01:19Z, `35221286d05e`, 103 lines). 11-24's pairs are MUST-READS, so a moved pair STOPS. τ is a formula from 11-22 and 11-23 alone, with a 0.10 floor, fixed before either is read. OUTSIDE comes last, and the limits are stated up front.

RESULT of the code (append-only): as registered. Two design choices were stated before the code, and two predictions were missed during the build; both misses are recorded (`f1r/popday/red-prediction.md`, `ef61ff43e2de` with its READ lines).

- **The two design choices:**
  - each PD row overrides the reader's `PD_R24` constant with synthetic pairs, because real events at the registered scale would cost about 60k events a row;
  - so one row is ADDED, PD-TABLE, asserting that the real `PD_R24` equals the registered fifteen pairs. That makes 12 PD rows, not the registered 11.
- **Red:** against the reader without popday (blob `0d5dc4ffde7c`): 72 rows, 61 as predicted; exactly the 11 PD rows other than PD-RED OFF; exit 1.
- **Green, first run: NOT AS PREDICTED.** 9 rows were OFF, every one stopped at ANCHOR-POST reading T 5818.
  - The cause was the plant's data. ANCHOR-POST's hour, 11-23 14:00Z, is also a reference hour, and the plant added its synthetic reference lines on top of the anchor's 5553. The reader read the fake correctly.
  - A plant-only commit fixed it: that hour's anchor lines serve as its reference lines. The outcome was re-predicted before the re-run.
- **After the fix:** red preserved (72 rows, 61 as predicted, the same 11 OFF). Green against the new reader (blob `90c5a0d5529e`): 72 of 72, exit 0.
- **P-SAB: NOT AS PREDICTED.** `PD_RUN = 3` replaced by 1 turned 3 rows OFF, not 1.
  - PD-ODD turned to A PARTIAL LOSS, the logic the sabotage targets.
  - PD-ABOVE and PD-TWO were OFF only because `PD_RUN` is printed in the OUTSIDE text. The predicted set was incomplete.
- **P-SAB-2,** pre-registered before it ran, on the logic only: the run test's `>= PD_RUN` becomes `>= 1`, and the printed text is unchanged. Exactly PD-ODD OFF, reading A PARTIAL LOSS; exit 1.
- **`read` unchanged:** `read` with reader `ba636810808b` (before) against `90c5a0d5529e` (after), the pair printed on line 1: 7 of 7 identical (`readdiff-check.out`, `0e7c3be3a760`).
- **Commits:** `3cddbc1361f2` (the 12 PD rows; plant blob `9efd4957fff9`), `64c337aff651` (the plant fix; plant blob `7fb94a4e4056`) and `bc756becf416` (the mode).
- **Outputs:** in `f1r/popday/`:
  - red `fd4bbe4f116f`, the same before and after the fix;
  - green, first run `b52076a43726`, and after the fix `91c7c67fec33`;
  - P-SAB `13b403ee440a` and P-SAB-2 `4a9150e787df`.
- **CI at `bc756becf416`:** AS PREDICTED, two reads identical: 39 check-runs, 37 success and 2 skipped, 0 failures.

**The run,** on the orchestrator's GO, through the gate (head `bc756becf416`, reader `90c5a0d5529e`; started 01:49:20Z, before 11-26 06:00Z). The text of blipscope-5c's `f1r/f1-popday-result-1.md` (sha256 `3925a774f9e8`) follows VERBATIM; its `#` title is a plain line here, and its `##` headings are demoted to `####`.

F1 POP-DAY, result 1 (stamped 2026-11-25T03:25:28Z from date -u, after the run exited)

**For:** the orchestrator. **By:** blipscope-5c. **Graded against:** f1-popday-1.md (35221286d05e), exactly as registered. Counts only; nothing identifying was read. Frozen: corrections go below, append-only.

#### Verdict

**THE FLEET'S DAILY CYCLE, no partial loss.** 11-24's K is IN its band in **all 15 hours** (04Z-18Z), with τ 0.1000. My own registered prediction (section 6, grades nothing) was CYCLE, weakly held.

#### The run

- **Start:** preconditions held at 01:48:53Z (head bc756becf416, reader 90c5a0d5529e, porcelain 0, before 11-26 06:00Z). gate-popday PASSED at 01:49:20Z (VOID on the ruled five, no extras); tag bg-20261125T014920Z-d8316145.
- **Exit:** 0 at 03:23:25Z. stderr carries only the token file's Windows-permissions note.
- **Calls:** 94 count queries, the registered number. Each answered on its first attempt with status 200 and ABR 1; 0 retries.
- **End check:** bgcheck exit 4 at 03:24:47Z, VOID on exactly the ruled five.
  - It also EXCLUDED one process, by its rule F2: a `sleep.exe` created 03:10:29Z.
  - Its parent pid is the reused pid of a ledger root of mine from 2026-11-22, long recorded dead. It is not a process this run started.
- **Files** (f1-20261123/):

  | file | sha |
  |---|---|
  | popday.out | d62b3d17d5d5 |
  | popday.json | 945e01651e45 |
  | popday.err | 16803fbd5c80 |
  | popday.reg | efc49a940298 |
  | gate-popday.out | 3d8cde64e0ac |
  | end-popday.out | 48362b0aaaae |

#### Observations, in the registered order

1. **Anchors HOLD.** ANCHOR-PRE 11-19 12Z read T 3695 and N 36. ANCHOR-POST 11-23 14Z read T 5553 and N 249.
2. **R-24 HOLDS in all 15 hours.** Every 11-24 T/N pair equals POP-GAP's printed pair, so the target had not moved since 00:21Z.
3. **No stop condition fired.** No BLIND hour, N never above T, and no reference hour at T 0.
4. **The spread.** The 15 s_h run from 0.0086 to 0.1524, with median 0.0510 (08Z). **So τ is the floor, 0.1000.**
5. **Marks:** IN ×15, BELOW ×0, ABOVE ×0.

| hour | K 11-22 | K 11-23 | s_h | band | K 11-24 | mark |
|---|---|---|---|---|---|---|
| 04Z | 2152 | 2507 | 0.1524 | 1936..2758 | 2285 | IN |
| 05Z | 1903 | 1868 | 0.0186 | 1681..2094 | 1778 | IN |
| 06Z | 1638 | 1745 | 0.0633 | 1474..1920 | 1658 | IN |
| 07Z | 1726 | 1704 | 0.0128 | 1533..1899 | 1699 | IN |
| 08Z | 2271 | 2158 | 0.0510 | 1942..2499 | 1999 | IN |
| 09Z | 2362 | 2330 | 0.0136 | 2097..2599 | 2316 | IN |
| 10Z | 2115 | 2209 | 0.0435 | 1903..2430 | 2153 | IN |
| 11Z | 2778 | 2500 | 0.1053 | 2250..3056 | 2673 | IN |
| 12Z | 3680 | 3379 | 0.0853 | 3041..4049 | 3532 | IN |
| 13Z | 5114 | 4682 | 0.0882 | 4213..5626 | 4722 | IN |
| 14Z | 5492 | 5304 | 0.0348 | 4773..6042 | 5144 | IN |
| 15Z | 6176 | 6123 | 0.0086 | 5510..6794 | 5974 | IN |
| 16Z | 6066 | 6179 | 0.0185 | 5459..6797 | 6254 | IN |
| 17Z | 6066 | 6469 | 0.0643 | 5459..7116 | 6580 | IN |
| 18Z | 6445 | 6947 | 0.0750 | 5800..7642 | 6208 | IN |

**Printed for information, grading nothing (registered):**
- **N at 09Z:** 11-22 248, 11-23 277, 11-24 337.
- **N on the reference days.** On 11-22 and 11-23, N sits near 250 in most hours: the no-key family running. On 11-24 it reads 0-15 in every hour except 09Z, the family's stop.
  - So 09Z's 337 on 11-24 is a keyless burst on a day the family was not running. On the two reference days the same hour's N is the family's ordinary rate.
  - This is POP-GAP's reading, restated; nothing new is claimed.

#### Findings

- **The finding.** POP-GAP's smooth K curve on 11-24 (04Z-18Z) is the keyed fleet's ordinary daily curve. It repeats on 11-22 and 11-23 at the same clock hours, inside a tolerance fixed before either day was read. A partial log loss that put 11-24's device-attributed lines below the band for 3 hours or more is excluded.
- **The alternative this read was built to separate**, (ii) "a partial log loss that follows a curve", is excluded **at the instrument's resolution**: anything larger than about 10% below the lower reference day, sustained for 3 hours or more. A smaller loss is NOT excluded; see the post-hoc section.
- **What this cannot show** (registered, section 5, and all of it stands):
  - CYCLE does not show the logs are complete. A loss hitting all three days alike would be invisible.
  - The days differ in kind (Sunday, Monday, Tuesday).
- **For the original question.** Together with POP-GAP (OUTSIDE: T > 0 every hour, anchors held) and the deploy read (0 deployments 03-07Z), the 04Z stop of the no-key family is **not accompanied by a drop in keyed traffic**. Keyed devices stayed within the registered tolerance of their usual daily volume through the family's stopped hours.
  - So the evidence points at the no-key family itself stopping (its sender), not at the Worker or the log pipeline dropping lines.
  - **The alternative still standing:** a change that affects only keyless requests, such as a KV config change or a routing rule matching only keyless traffic. The deploy read does not cover config. This read cannot separate "the senders stopped" from "the keyless path stopped being logged", because K does not exercise the keyless path at all.

#### Post-hoc (NOT registered; computed from popday.out after the verdict; grades nothing and changes nothing)

- **Against the two reference days' own range, without τ:**
  - 6 hours inside (04, 06, 10, 11, 12, 13Z);
  - 7 below the lower day (05, 07, 08, 09, 14, 15, 18Z), including 07-09Z consecutively;
  - 2 above the higher day (16, 17Z).
- **Extremes and sums:**
  - lowest K24/min(a,b) is 0.9263 at 08Z, which is also the hour closest to a band edge, 57 above the floor of 1942;
  - highest K24/max(a,b) is 1.0172 at 17Z;
  - summed 04Z-18Z: 11-22 55,984, 11-23 56,104, 11-24 54,975. **11-24 is 0.9809 of the two days' mean.**
- **The alternatives, unseparated.** A shortfall of about 2% overall and at most about 7% in any hour fits three explanations equally:
  - a Tuesday being slightly quieter than a Sunday and a Monday;
  - ordinary fleet churn;
  - a small partial loss below the 10% floor.

  The data does not exclude any of them. Report all three; claim none.
- **This does not change the verdict**, and it is not a reason to re-grade. The τ floor was fixed before the read precisely so a near-identical pair of reference days could not narrow the band below ordinary noise. The instrument that could resolve a loss this small is the second pipeline (Analytics Engine counts), which stays OFFERED and is not brought, per the orchestrator's rule (only on PARTIAL LOSS).

RECORDED by the orchestrator (2026-11-25 03:26Z; result `3925a774f9e8`): THE FLEET'S DAILY CYCLE, no partial loss, graded as registered. Both anchors hold, and all 15 R-24 pairs equal POP-GAP's, so the read held still. The bgcheck F2 exclusion is recorded: a reused pid, not this run's. The post-hoc 2% stays POST-HOC, with three explanations and none excluded; it is not graded. The Analytics Engine read stays offered and unrun.

**Where the chase stands after FAMILY HOURS, POP-GAP and POP-DAY** (counts only, no ids):
- **The family stopped at 11-24 04:00Z** and had not returned by 20:00Z (FAMILY HOURS O1, POP-GAP P4).
- **The logs did not stop with it.** T is above 0 in every stopped hour (POP-GAP P1). Keyed traffic over the same hours matches the same clock hours on 11-22 and 11-23, within the tolerance registered before either day was read (POP-DAY).
- **No deploy explains it.** There were 0 deployments 03Z-07Z, and one version served the whole window (POP-GAP P5).
- **So the evidence points at the family's own senders stopping.**
- **THE ALTERNATIVE THAT STANDS:** a change touching only keyless requests, such as a KV-held config. Neither the deploy read nor K can see one, so it is not excluded.
- **Open:** the owner is asked whether he touched a device near 04:00Z. His answer may close it without another read. Nothing further is registered.

**`read`'s code since the ruling.** The reader moved twice more: to `0d5dc4ffde7c` (popgap) and `90c5a0d5529e` (popday). Each readdiff against `ba636810808b` read 7 of 7 identical (`e1684806b15d`, `0e7c3be3a760`), so `read`'s code is unchanged. The ruling names neither blob: the ruled reader stays `ba636810808b` until the orchestrator rules otherwise.

CLOSED (append-only, 2026-11-25T07:26:34Z from date -u; the orchestrator's ruling): the refused device was a bench board, the photo Missileer (E7:94). The owner's account matched map-client's check of its flash and boot times, and its own console shows the 401s at the day rate, then night. No further population reads; the record above stands as written.

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
