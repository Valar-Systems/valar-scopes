# Blipscope Proxy

A Cloudflare Worker that sits between the Blipscope device fleet and public ADS-B
data sources. It serves three tiny, device-friendly endpoints — `/v1/blips`
(aircraft near a point), `/v1/enrich/{hex}` (one aircraft's details, pre-joined),
and `/v1/config` (fleet tunables) — so devices make **one** TLS connection to
**one** host, receive fixed-`Content-Length` payloads measured in hundreds of
bytes, and never talk to an upstream directly. Upstreams get a failover chain,
2 s timeouts, circuit breakers, and edge/KV caching so the fleet can't multiply
load onto them.

```
device ──HTTPS keep-alive──> Worker ──> edge cache (blips, 3 s + SWR)
                                   ──> KV (enrich 30 d/24 h, fleet config)
                                   ──> adsb.lol (primary; adsb.fi / airplanes.live ship disabled)
```

## Data sources & licensing

- **Aircraft positions & metadata: [adsb.lol](https://adsb.lol).** Data is
  licensed under the [Open Database License (ODbL) 1.0](https://opendatacommons.org/licenses/odbl/1-0/),
  which permits commercial use with attribution.
  **Attribution: aircraft data © [adsb.lol](https://adsb.lol) contributors, licensed under ODbL 1.0.**
  The device config page carries the same credit line. Every upstream request
  identifies us: `User-Agent: BlipscopeProxy/1.0 (+daniel@valarsystems.com)`.
  The adapter sends an `X-Api-Key` once adsb.lol issues feeder keys
  (`ADSB_LOL_API_KEY` secret; we run a feeder).
- **Routes: adsb.lol's routeset API first, adsbdb.com as fallback.** Routeset
  (their vrs-standing-data database) is queried with a plausibility check fed
  by the aircraft's live position when the device provides one — but as
  observed live on 2026-07-08 it returns `201 Created` with an **empty body**
  even for major scheduled callsigns (its OpenAPI docs promise 200 + JSON), so
  it currently yields no data. adsbdb — the route source the firmware used for
  years — earns its place back for **callsign→route only** (aircraft metadata
  stays on adsb.lol). Empty routeset answers fall through to adsbdb; only
  definitive answers (from either source) are negative-cached. Disable the
  fallback with `ROUTE_ADSBDB_ENABLED = "false"` once routeset recovers.
- **adsb.fi and airplanes.live adapters ship DISABLED** (`UPSTREAM_*_ENABLED`
  vars) pending commercial permission from their operators. Until then the
  outage story is: circuit breaker → stale-while-revalidate serves the last
  picture (stamped with its original `t`) → devices show their stale indicator.
- **Why not adsbdb anymore:** the firmware used adsbdb.com for registration /
  type / operator / route. adsb.lol's v2 responses already carry registration
  (`r`), type code (`t`), and — where its DB knows them — a friendly description
  (`desc`) and registered operator (`ownOp`); its routeset API covers callsign →
  route. Staying inside one licensed ecosystem removes a third-party dependency
  and a third TLS host on the device. Friendly names missing upstream fall back
  to a KV table (`tn:<CODE>`, updatable without a deploy) and a baked table of
  ~150 common types. If real gaps show up in the field, adsbdb can return as a
  disabled adapter behind the same interface.
- **Photos: none in v1.** The old firmware hotlinked airport-data.com thumbnails
  via adsbdb — legally shaky for a commercial product and a third TLS host on
  the C3, so cloud mode ships photo-less on all models (changelog: known
  regression vs adsbdb mode). A pre-resized RGB565 photo endpoint stays parked
  until a licensed source exists.

## Endpoints

All `/v1/*` endpoints require the `X-Blip-Key` header and are rate-limited.
Devices also send `X-Blip-Model` (hardware slug, e.g. `c3-128`, `s3-146`) and
`X-Blip-FW` (firmware version). Responses always carry `Content-Length` (no
chunked encoding), `Cache-Control: no-store, no-transform`, and a schema
version `v` in the body. Errors are `{"v":1,"error":"..."}` with a proper
status; 429/503 include `Retry-After`.

### `GET /v1/blips?lat=&lon=&r=[&limit=]`

Aircraft near a point. `lat`/`lon` in decimal degrees, `r` in km, `limit`
optional (default 25, max 50).

```json
{"v":1,"t":1751970245,"n":2,"a":[
  ["4b1817","SWR123",473976,85517,37000,451,231,-704,4,2],
  ["aaaaaa","",474100,85600,-100000,-1,-1,-100000,0,-1]
]}
```

- `t` — unix seconds of the **upstream data snapshot** (not the serve time).
  Devices compute staleness from `t`; a stale-while-revalidate response keeps
  its original `t`, so the device stale indicator is always honest.
- `n` — row count. `a` — one array per aircraft, field order below.
- Diagnostics headers: `X-Cache: HIT|STALE|MISS`, `X-Upstream: adsb_lol|...`.
- **Cold tiles:** a first request for an uncached tile races the upstream
  against a ~3.5 s serve deadline (`BLIPS_SERVE_DEADLINE_MS`). If the upstream
  is slower (busy-basin tiles measured at ~5 s; 429-retry ladders longer), the
  device gets a fast `503 {"error":"warming"}` + `Retry-After: 5` while the
  fetch finishes in the background and caches — the next poll hits a warm
  tile. Devices keep their last picture on any 503.

| # | Field | Type | Unit / encoding | Unknown sentinel |
|---|-------|------|-----------------|------------------|
| 0 | hex | string | ICAO 24-bit address, lowercase (may carry readsb's `~` prefix for TIS-B) | never missing |
| 1 | callsign | string | trimmed | `""` |
| 2 | lat | int | degrees × 1e4 (~11 m) | never missing |
| 3 | lon | int | degrees × 1e4 | never missing |
| 4 | altitude | int | feet, barometric (geometric fallback) | `-100000` |
| 5 | ground speed | int | knots | `-1` |
| 6 | track | int | degrees 0–359 | `-1` |
| 7 | vertical rate | int | ft/min, signed | `-100000` |
| 8 | category | int | OpenSky emitter-category enum (0 = unknown … 8 = rotorcraft) | `0` |
| 9 | position age | int | seconds before `t` the position was measured — feed this to the dead-reckoner | `-1` |

Semantics (all part of the contract):

- **Airborne only.** Entries readsb reports on the ground are filtered out.
- **Positions older than 60 s are dropped** (a multi-minute dead-reckoning
  extrapolation would teleport blips).
- **Request quantization:** centre snaps to a 0.05° (~5.5 km) tile; `r` snaps
  UP to a bucket {10, 20, 40, 80, 160} km. The response may therefore include
  aircraft slightly outside the requested radius — devices clip when
  projecting. Sorted nearest-first from the **quantized tile centre**, capped
  at `limit`.

### `GET /v1/enrich/{hex}[?cs=CALLSIGN&lat=&lon=]`

One aircraft's details, pre-joined from the aircraft DB and the route DB —
one request replaces the firmware's old two adsbdb calls. Target < 512 B.

```json
{"v":1,"r":"HB-JMB","t":"A343","tn":"Airbus A340-313","op":"Swiss","o":"ZRH","d":"JFK"}
```

| Key | Meaning | Unknown |
|-----|---------|---------|
| r | registration | `""` |
| t | ICAO type designator | `""` |
| tn | friendly type name (`desc` upstream, else KV `tn:<CODE>`, else baked table) | `""` |
| op | operator / registered owner | `""` |
| o / d | route origin / destination (IATA preferred, else ICAO; first/last leg of multi-leg) | `""` |

`cs` enables the route lookup. `lat`/`lon` (the aircraft's live position, which
the device already has) enable the plausibility check: an implausible route —
callsigns get reused across legs — comes back blank rather than wrong. Without
a position, codes are returned as-is.

**Cold lookups** race a ~2.5 s serve deadline (`ENRICH_SERVE_DEADLINE_MS`):
fields that didn't resolve in time come back `""` while the lookups finish and
cache in the background — the firmware's detail card re-requests naturally and
gets the warm KV hit. Sub-second responses are therefore the warm-path norm;
only a first-ever tap on an aircraft during upstream throttling comes back
partially empty once.

### `GET /v1/config`

Fleet tunables, resolved **server-side** for the requesting `X-Blip-Model`.
Devices fetch on boot + daily and apply without reboot.

```json
{"v":1,"rev":1,"minFw":"0.0.0","enrich":"full",
 "pollActiveMs":5000,"pollIdleMs":15000,"pollNightMs":60000,
 "idleAfterMs":600000,"staleFactor":3,"upstreamState":"ok"}
```

| Key | Meaning |
|-----|---------|
| rev | config revision (bump when editing KV, devices log it) |
| minFw | if newer than the running build, the device triggers its normal daily OTA check immediately |
| enrich | `off` \| `watchlist` \| `full` — background-enrichment level (S3 default `full`, C3 default `watchlist`; taps always enrich) |
| pollActiveMs / pollIdleMs | feed cadence; idle after `idleAfterMs` without touch |
| pollNightMs | cadence while the device's solar-dim says night — the fleet's biggest cost lever (see cost model) |
| staleFactor | stale indicator threshold: data age > staleFactor × current interval |
| upstreamState | `ok` \| `degraded` \| `down` (circuit-breaker rollup, informational) |

Baked tiers: default/`c3-128` poll 5/15/60 s (C3 enriches `watchlist`-only);
`s3-146`/`s3-21` poll 2/10/45 s — the S3's near-realtime motion is a pure
server-side knob.

### `GET /healthz`

Public, unauthenticated: `{"ok":true,"upstreams":[{id,enabled,state}]}`.

## Schema evolution rule

**v1 is frozen.** Additive changes append fields to the end of the `a` arrays
or add new JSON keys; firmware ignores unknown trailing fields and unknown
keys. Anything breaking — reordering, retyping, re-uniting, sentinel changes —
bumps `v`, and firmware asserts `v` on parse. The cache key includes the schema
version, so a `v` bump can never serve mixed payloads.

## Caching design

- **/v1/blips — edge Cache API**, keyed on quantized tile + radius bucket +
  limit + schema version. Fresh TTL 3 s (`BLIPS_FRESH_TTL_MS`), then
  stale-while-revalidate: stale entries serve immediately (original `t`) while
  one background revalidation per key per isolate refreshes the tile, up to a
  10-minute storage ceiling during upstream outages. Notes: the edge cache is
  **per-colo** (each PoP warms its own copy); caching dedupes upstream load for
  co-located devices but **does not reduce our own billed Worker requests**;
  concurrent first-misses on a cold tile in one colo can briefly race the
  upstream (accepted at our tile occupancy — a Durable Object would serialize
  it if that ever matters).
- **/v1/enrich — KV.** `ac:{hex}` 30 days (24 h for unknown-aircraft negatives),
  `rt:{CALLSIGN}` 24 h (negatives too, so an unknown/implausible callsign isn't
  re-queried all day). `tn:{TYPE}` friendly-name overrides, no TTL.
- **/v1/config — KV** `cfg:fleet`, read per request (KV is edge-cached; config
  traffic is 2/device/day).

## Auth & rate limiting

- `X-Blip-Key` checked against the `BLIP_KEYS` secret — comma-separated so a
  rotation can accept old+new keys simultaneously: add the new key, roll
  firmware, remove the old key. v1 ships one shared per-build key (per-device
  keys are explicitly out of scope for now).
- Workers Rate Limiting bindings (per-colo token buckets, 60 s period):
  `RL_IP` 120/min per client IP (covers ~3 fastest-tier devices behind one NAT,
  checked **before** auth so key-guessing is throttled too) and `RL_KEY`
  6000/min per key as a fleet-wide abuse ceiling. Both return
  `429` + `Retry-After: 10`.

## Fleet config operations

Retune the fleet with one KV write — no deploy, devices pick it up within 24 h:

```sh
# current staging namespace id is in wrangler.toml
npx wrangler kv key put --binding ENRICH_KV --env staging --remote cfg:fleet \
  '{"rev":2,"minFw":"0.0.0","defaults":{"pollActiveMs":6000},"models":{"s3-146":{"pollActiveMs":2500}}}'
```

Precedence (low → high): baked base < baked model tier < KV `defaults` < KV
`models[<slug>]`. Add a friendly type name the same way:
`... put tn:A21N "Airbus A321neo"`.

## Deploy

Prereqs: Node 20+, a Cloudflare account with Workers Paid (rate limiting +
Analytics Engine), `npx wrangler login`.

```sh
cd proxy
npm install
npm test                 # vitest + workers pool (miniflare)
npm run typecheck

# one-time per environment:
npx wrangler kv namespace create ENRICH_KV --env staging
#   -> paste the id into wrangler.toml under [[env.staging.kv_namespaces]]
npx wrangler secret put BLIP_KEYS --env staging          # the device build key(s)
npx wrangler secret put ADSB_LOL_API_KEY --env staging   # optional, when issued

npm run deploy:staging
```

Staging serves at `https://scopes-staging.valarsystems.com` (custom domain; the
zone must be on the same Cloudflare account). Production later: copy the
`[env.staging]` block to `[env.production]` with its own KV namespace, secrets,
the `scopes.valarsystems.com` domain, and a lower `head_sampling_rate` (logs
cost money at fleet scale — see below).

Smoke test:

```sh
BASE=https://scopes-staging.valarsystems.com KEY=<staging key>
curl -s $BASE/healthz
curl -s -H "X-Blip-Key: $KEY" "$BASE/v1/blips?lat=47.39&lon=8.55&r=40"
curl -s -H "X-Blip-Key: $KEY" "$BASE/v1/enrich/4b1817?cs=SWR123&lat=47.4&lon=8.5"
curl -s -H "X-Blip-Key: $KEY" -H "X-Blip-Model: s3-146" "$BASE/v1/config"
```

## Observability

- **Logs:** one JSON line per request:
  `{"evt":"req","ep":"/v1/blips","status":200,"ms":4,"model":"s3-146","colo":"ZRH","cache":"HIT","upstream":"adsb_lol","upstreamMs":0}`
  plus `evt:upstream` lines per upstream attempt and `evt:error` on 500s.
  Staging keeps 100% (`head_sampling_rate = 1`); at fleet scale sample to
  1–5 % or logs become a real line item (~200 B × requests).
- **Analytics Engine** (`blipscope_proxy[_staging]`): blobs `[endpoint, cache,
  upstream, model]`, doubles `[status, ms, upstreamMs, weight]`, index
  `endpoint`. Successful HIT/STALE points are sampled 1:10 with `weight = 10`
  (bill control); everything else is 1:1 with `weight = 1` — **always
  `SUM(double4)`, never `COUNT(*)`**. Example, cache hit rate over 24 h:

```sql
SELECT blob2 AS cache, SUM(double4) AS requests
FROM blipscope_proxy WHERE timestamp > NOW() - INTERVAL '24' HOUR AND blob1 = '/v1/blips'
GROUP BY cache
```

## Cost model

**Assumptions** (change them and the arithmetic below is linear): a device's day
is 4 h active + 12 h idle + 8 h night; ~20 enrich taps + 2 config fetches/day
(“misc 22”); 30.44 days/month; Workers Paid plan: $5/mo including 10 M
requests and 30 M CPU-ms, then $0.30 per extra M requests and $0.02 per extra
M CPU-ms at ~2 ms/request; Analytics Engine $0.25 per M points past 10 M
(after the 1:10 hit sampling, points ≈ 0.11 × requests); KV $0.50/M reads past
10 M (enrich is ~40 reads/device/day — negligible even at 5 k devices).

**Requests per device per month:**

| Tier | Active | Idle | Night | /day | /month |
|------|--------|------|-------|------|--------|
| default & `c3-128` (5/15/60 s) | 4·3600/5 = 2880 | 12·3600/15 = 2880 | 8·3600/60 = 480 | 6262 | **190 615 (0.19 M)** |
| `s3-146` / `s3-21` (2/10/45 s) | 4·3600/2 = 7200 | 12·3600/10 = 4320 | 8·3600/45 = 640 | 12 182 | **370 820 (0.37 M)** |

(“/day” includes misc 22.) Fleet rows assume a 50/50 model mix ⇒ **0.281 M
req/device/month**.

| Fleet | Req/mo | Requests $ | CPU $ | Analytics $ | KV $ | **Total/mo** |
|-------|--------|-----------|-------|-------------|------|--------------|
| 100 | 28.1 M | (28.1−10)·0.30 = 5.43 | (56.2−30)·0.02 = 0.52 | 0 (3.1 M pts) | 0 | **$10.95 + $5 base ≈ $11** |
| 1 000 | 281 M | (281−10)·0.30 = 81.30 | (562−30)·0.02 = 10.64 | (30.6−10)·0.25 = 5.15 | ~0 | **≈ $102** |
| 5 000 | 1 405 M | (1405−10)·0.30 = 418.50 | (2810−30)·0.02 = 55.60 | (153−10)·0.25 = 35.78 | ~0 | **≈ $516** |

≈ **$0.10 per device per month** at 1 k+ fleet scale.

**The night-cadence knob is real money:** if night hours polled at the idle rate
instead, the blend rises to 0.337 M req/device/mo and the 5 k fleet lands at
≈ $574/mo — `pollNightMs` saves ≈ $95/mo (~17 %) at 5 k, server-side, any time.

**Upstream honesty:** the edge cache dedupes co-located devices, but a lone
device in its own tile drives ~1 upstream fetch per poll (SWR revalidation), so
adsb.lol sees roughly fleet-poll-rate traffic: ~7 req/s average at 100 devices,
~70 req/s at 1 000. Before 1 k devices, coordinate with adsb.lol (feeder key),
enable the other adapters to spread load, raise `BLIPS_FRESH_TTL_MS`, or
self-host a readsb ingest of their open beast feed. Note also that edge caching
does **not** reduce our own billed Worker requests — only upstream load.

**Shared-egress-IP throttling (observed live, launch-relevant):** Cloudflare
Workers egress IPs are shared per-colo across all Workers customers, and the
free ADS-B APIs rate-limit anonymously by IP — so our requests can be 429'd
because of *other tenants'* traffic, in bursts lasting seconds to minutes,
regardless of our own volume. Observed live from colo SEA against **both**
adsb.lol (`/v2/lat`, `/v2/hex`) and adsbdb (`/v0/callsign`) while the same
queries returned 200 from a residential IP. Mitigations already in the Worker:
retry ladders (2 attempts on `/v2/lat`, 3 on `/v2/hex` and the adsbdb route
path, linear backoff), SWR serving stale through bursts, the warming responses
above, and KV meaning any enrichment that lands once is fleet-shared for its
TTL. The real fix is the **adsb.lol API key** (`ADSB_LOL_API_KEY` secret, sent
as `X-Api-Key`): treat obtaining one via their feeder program as a **launch
requirement**, not an optimization — and enabling one failover feed (pending
permission) de-risks the rest. adsbdb has no key program; routes converge
through throttle gaps and stay cached 24 h once landed.

## Testing

`npm test` runs vitest inside workerd (`@cloudflare/vitest-pool-workers`) with
mocked upstreams (`fetchMock` + `disableNetConnect`), covering: cache keying
(tile jitter shares an entry; buckets/limits don't), SWR staleness + background
revalidation, cold-tile warming (503 + background cache fill), 429 retry
ladders on both point and hex paths, upstream failover, circuit-breaker opening
after 3 failures, 503-with-Retry-After, wire serialization (field order,
sentinels, filters, category mapping, quantization), enrich join + KV
positive/negative caching + route plausibility + the routeset-empty → adsbdb
fallback + the enrich serve deadline, per-model config resolution + KV
overrides, auth, rate limiting (stubbed bindings — the real binding isn't
emulatable locally), and routing. Rate-limit *thresholds* and custom-domain
routing are validated on staging, not in unit tests.

Operational tunables (env vars, all optional): `UPSTREAM_TIMEOUT_MS` (8000),
`UPSTREAM_RETRY_DELAY_MS` (400), `BLIPS_FRESH_TTL_MS` (3000),
`BLIPS_SERVE_DEADLINE_MS` (3500), `ENRICH_SERVE_DEADLINE_MS` (2500),
`ROUTE_ADSBDB_ENABLED` ("true"), `UPSTREAM_ADSB_FI_ENABLED` /
`UPSTREAM_AIRPLANES_LIVE_ENABLED` ("false" pending permission).

## Work queue

Accepted items, spec'd enough to pick up cold.

### Military enrichment deepening (accepted 2026-07-09)

Driver: military contacts (e.g. a US DoD `AE…`-block hex) routinely render a
near-empty detail card — MILITARY tag, hex-as-title, no type/reg/operator.
The enrich path already resolves metadata from the adsb.lol `/v2/hex` live
return (`r`/`t`/`desc`/`ownOp` in `enrich.ts buildMeta`); the gap is that
adsb.lol's airframe DB has no record for many military hexes, so the live
return carries a position and nothing else. No device firmware change in any
phase — the card already renders `r`/`t`/`tn`/`op` whenever they arrive.

1. **Cache-policy fix (P0):** `buildMeta` marks any non-null upstream return
   `found:true` — including all-empty ones — which then positive-caches for
   30 d (`AC_TTL_S`). An airframe whose DB record appears later stays blank
   for a month. Cache found-but-all-EMPTY metas at the negative TTL (1 d).
2. **Military floor (P1):** carry a compact ICAO military-allocation range
   table (`AE0000`–`AFFFFF` = US DoD, plus the other national mil blocks);
   when the hex is in a mil block and `op` resolved empty, fill `op` with
   e.g. "US military". Read `dbFlags` bit 0 too when the DB supplies it.
   Never guess types.
3. **Static airframe dataset (P2):** the tar1090/Mictronics community
   aircraft DB carries many military airframes (type/reg/operator). Load a
   mil-block slice into KV (`ac:` pre-seed, or a `mil:<hex>` side table
   consulted when live fields come back empty). **License review required
   before shipping** (attribution per the adsb.lol ODbL credit pattern
   already on the device config page).
4. **Callsign color (P3, optional):** static prefix table (RCH → Air
   Mobility Command, …) filling `op` when a callsign is broadcast and
   nothing else resolved. Military photos (by-hex photo APIs) are a separate
   licensing/attribution decision — explicitly not part of this item.

Tests per phase in the existing vitest suite: empty-meta TTL, mil-block
floor, KV side-table hit, prefix fill.

### Stock photo library for cloud mode (accepted 2026-07-09)

Driver: cloud mode is deliberately photo-less (`AircraftManager.cpp` blanks
`photoUrl` — "no licensed source"; the BYO builds' adsbdb/planespotters
thumbnails can't be re-hosted). A self-hosted, owner-curated stock library is
the licensing-clean photo path for cloud devices — and for military types the
obvious sources (official DoD imagery) are public domain.

- **Sourcing:** per the playbook in
  [blipscope-military-photo-sourcing.md](../blipscope-military-photo-sourcing.md)
  — two layers, one machinery. **Military tiers (hand-curated):** Wikimedia
  Commons FIRST (its API's `extmetadata` carries machine-readable
  license/author/credit, so the harvest script auto-fills manifest rows),
  DVIDS/.mil manually for gaps; every .mil-hosted photo passes the
  **credit-line test** ("U.S. Air Force photo by …" = PD; "Photo courtesy
  of Lockheed Martin/Boeing/…" = contractor copyright even on a .mil page —
  skip). **Layer 2 (civil long tail, auto-harvested):** every type code
  seen in proxy logs ranked by frequency → Wikidata entity via the ICAO
  type-designator property → **P18** canonical image (Commons search ranked
  by Quality/Featured assessments when P18 is absent/bad); human eyes on
  the top ~100 by traffic, sampling below; **re-harvest quarterly** (the
  immutable-key + pointer-flip storage absorbs this natively). Tier lists,
  sprite-fit selection criteria, and the pick-sheet workflow live in the
  playbook. PD covers copyright only: card display is factual use, but
  keep this imagery out of marketing art (implied-endorsement risk).
- **Manifest + license gate (hard requirement, per-layer):** the upload
  script maintains `proxy/photos/manifest.json` — key → source URL, author,
  credit line, license, **layer** (`mil-tier` / `auto`), **autoPicked**
  flag — and **refuses any upload missing required fields**. License rules:
  military tiers accept `PD-USGov` / `CC-BY` / `OGL` / `own` (**no SA** —
  the flagship library stays maximally clean); Layer 2 additionally accepts
  `CC-BY-SA`, whose entries the credits page must render with attribution
  + license link + a changes-noted line ("resized for device display").
  `NC`, `ND`, and anything ambiguous are rejected in both layers.
  `autoPicked` is manifest-only curation state (drives re-harvest and
  spot-check policy); the wire keeps the `pk` semantics — the card's
  "representative photo" label keys off `pk:"type"`, which honestly covers
  hand-picked and auto-picked type shots alike, **and is suppressed on
  `pk:"hex"`**: a per-airframe override IS that aircraft, and the uncaptioned
  photo is the override system's payoff. Attribution: the
  `photo-credits` page is generated from the manifest (Worker-served or on
  valarsystems.com), linked from the device config page — satisfies CC-BY,
  CC-BY-SA, and OGL in one place, courtesy-credits the PD shots. Never
  planespotters / JetPhotos / airliners.net rips, regardless of quality.
- **Ingest:** resize/re-encode to exactly the device sprite dims
  (`PHOTO_W` = 150; read `PHOTO_H` from the firmware when implementing),
  baseline JPEG only (progressive will not decode on-device), **EXIF
  stripped on ingest** — ~8–15 KB each.
- **Storage, immutable:** content-addressed KV keys
  (`photo:<TYPE|hex>-<hash8>`); a small pointer entry per type/hex records
  the current key. Re-upload = new blob + pointer flip; blobs are never
  mutated in place.
- **Serving:** `GET /v1/photo/<key>`, same `X-Blip-Key` auth as the rest,
  `Cache-Control: public, max-age=31536000, immutable` — safe because keys
  are content-addressed.
- **Enrich join:** resolve the pointer (per-hex first, then type) and add
  `p: "/v1/photo/<key>"` **plus a kind flag distinguishing per-hex from
  generic type stock** (e.g. `pk: "hex" | "type"`), so the card can label a
  type-generic image "representative photo" honestly.
- **Firmware delta (small):** cloud enrich path reads `p` into
  `tracked.photoUrl` (absolute = cloud base + `p`) instead of blanking it —
  the existing RequestPhoto/JPEG-decode path does the rest — and the card
  captions the photo "representative photo" when `pk` says type.
- Tests: photo route auth + content-type + immutable cache headers, enrich
  `p`+`pk` join (hex beats type), pointer flip on re-upload, absent-key →
  no `p` field, upload script rejects manifest-incomplete entries AND
  enforces the per-layer license gate (SA rejected in `mil-tier`, accepted
  in `auto` with credits-page obligations; NC/ND/unrecognized rejected in
  both layers).
