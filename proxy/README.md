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
{"v":1,"r":"HB-JMB","t":"A343","tn":"Airbus A340-313","op":"Swiss","o":"ZRH","d":"JFK",
 "p":"/v1/photo/photo:A343-1a2b3c4d","pk":"type"}
```

| Key | Meaning | Unknown |
|-----|---------|---------|
| r | registration | `""` |
| t | ICAO type designator | `""` |
| tn | friendly type name (`desc` upstream, else KV `tn:<CODE>`, else baked table) | `""` |
| op | operator / registered owner | `""` |
| o / d | route origin / destination (IATA preferred, else ICAO; first/last leg of multi-leg) | `""` |
| p | relative path of a licensed stock photo for this aircraft, when the library has one | *(key absent)* |
| pk | `hex` (per-airframe override) \| `type` (generic stock — card labels it "representative photo") | *(key absent)* |

`p`/`pk` are **appended, append-only** (no schema bump): the per-hex override is
resolved first, then the generic type shot; when the library has neither, both
keys are omitted. See the stock-photo section below and `GET /v1/photo/<key>`.

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

### `GET /v1/airports?lat&lon&r`

The airport overlay's long tail beyond the firmware's baked ~260 majors
(include/Airports.h stays as the BYO/offline fallback). Backed by the
**OurAirports** dataset (public domain), pre-tiled into KV as 1° `apt:<lat>:<lon>`
tiles by `npm run ingest:airports` (~48k open large/medium/small airports —
heliports, seaplane bases and closed fields dropped). The handler walks the
tiles the radius circle touches (bounded), distance-filters, sorts
large > medium > small then nearest, and caps at **60 rows** so the reply
parses off the socket on a C3-class heap. Devices fetch once the location is
known, then daily; geography is static so responses edge-cache for 24 h on a
~0.1°-rounded key.

```json
{"v":1,"a":[[44.25,-121.15,"RDM","M"],[44.09,-121.2,"BDN","S"]]}
```

Row order is frozen `[lat, lon, code, kind]` (code = IATA, else FAA local,
else ident, ≤4 chars; kind `L`/`M`/`S`); extra trailing fields are the
evolution path, same rule as blips.

### `GET /v1/photo/<key>`

Serves one immutable, content-addressed stock photo (`key` =
`photo:<TYPE|hex>-<hash8>`, exactly the value returned in the enrich `p` field).
Baseline JPEG, pre-sized to the device sprite at ingest (EXIF stripped),
`Cache-Control: public, max-age=31536000, immutable` — safe because the key is
content-addressed (a re-upload lands on a new key + pointer flip). Same
`X-Blip-Key` auth + rate limiting as the other `/v1/*` routes; a malformed key
is `400`, an absent one `404` (so the firmware falls back to the silhouette).

### `GET /credits`

**Public, unauthenticated** photo-attribution page (HTML), rendered from the
manifest the ingest script publishes to KV — satisfies CC-BY / CC-BY-SA / OGL
attribution in one place and courtesy-credits the PD shots. Linked from the
device config page; a browser follows the link, so no device key.

### `POST /v1/leaderboard` + public `GET /leaderboard[.json]` / `/leaderboard/<id>`

The public spotting leaderboard ([src/leaderboard.ts](src/leaderboard.ts)).
**Opt-in, off by default** on the device. The submit is authed (same
`X-Blip-Key` as the rest); the read pages are public like `/credits`.

- `POST /v1/leaderboard` — hourly submission: `{id, name, radiusKm, counts:{types,
  airlines, countries, airports}, typeCodes:[...]}`. `id` is a salted MAC hash
  (the raw MAC never leaves the device); `typeCodes` is the ONE list sent (rarity
  scoring needs it) — airline/country/airport *lists* stay counts-only because
  they'd hint at location. Server merges monotonically (counts only grow),
  clamps implausible per-day jumps, claims the display name (collisions get a
  numeric suffix), stamps each new type's first-seen month, tracks streaks, and
  claims "First!" ownership of never-before-seen types. Responds with this
  device's `{rank, points, seasonRank, seasonPoints, total}` for the Stats block.
- `GET /leaderboard` — public HTML board (lifetime + `?view=season`), per-category
  leaders, badges, verified check, percentile framing.
- `GET /leaderboard.json` — the same data as JSON (`?view=season` too).
- `GET /leaderboard/<id>` — a device's public profile + badge case.

**Scoring** (server-side, tunable without a firmware change): unique type ×10×
rarity, airline ×5, country ×25, airport ×2, raw contacts ×0. Rarity multiplier
by fleet-wide type frequency (<5% ×5, <25% ×2, else ×1) — the equalizer so a
dense-airspace device can't just out-volume everyone. Seasons are monthly (score
over entries new this month). Board is aggregated lazily on read with a 5-min KV
cache; a cron-built board + D1 is the scale path.

### Per-device keys (additive; [src/deviceauth.ts](src/deviceauth.ts))

The `/v1/*` auth accepts, in addition to the shared `BLIP_KEYS`, **per-device
keys** — HMAC-SHA256(`DEVICE_KEY_SECRET`, deviceId) presented as `X-Blip-Key`
alongside an `X-Blip-Device` id. This is **fully additive**: the device-key path
only activates when `DEVICE_KEY_SECRET` is set AND a request carries
`X-Blip-Device`, so the live fleet (shared key only) is unaffected, and rollout
is gradual. The server holds one secret and recomputes the expected key per
request — no key database. Keys are minted at manufacture:

```sh
DEVICE_KEY_SECRET=… npm run derive-device-key <deviceId>   # prints the device's key
npx wrangler secret put DEVICE_KEY_SECRET --env staging     # set the Worker's secret
```

Because minting needs the secret (unextractable from open-source firmware,
unlike the shared build key), a device-authed request is trustworthy enough to
back the leaderboard's **verified** tier. Device-authed requests also get their
own rate-limit bucket (`dev:<id>`) instead of sharing the fleet's. **Staged:**
the server foundation + minting tool ship now; the firmware storing/sending a
per-device key and the leaderboard keying "verified" off it are the follow-ups.

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
zone must be on the same Cloudflare account).

### Going to production

The `[env.production]` block already exists in `wrangler.toml` (mirrors staging,
with failover feeds off pending licensing + `head_sampling_rate = 0.05`). Three
account-specific steps remain, then deploy + seed the data:

```sh
cd proxy

# 1. Create the production KV namespace, then paste the printed id into
#    wrangler.toml under [[env.production.kv_namespaces]] (replace REPLACE_ME).
npx wrangler kv namespace create ENRICH_KV --env production

# 2. Secrets.
npx wrangler secret put BLIP_KEYS --env production          # device build key(s)
npx wrangler secret put DEVICE_KEY_SECRET --env production   # if using per-device keys
npx wrangler secret put ADSB_LOL_API_KEY --env production    # once issued (launch to-do)

# 3. Add scopes.valarsystems.com as a custom domain (the route is already in
#    wrangler.toml; the zone must be on this account). Then deploy:
npm run deploy:production

# 4. Seed the KV-backed datasets into the fresh production namespace:
npm run ingest -- --env production            # 68 stock photos + manifest/credits
npm run ingest:mildb -- --env production      # ~17k military airframes
npm run ingest:airports -- --env production   # ~9.4k airport tiles

# 5. Set the repo secret CLOUDFLARE_API_TOKEN so the weekly refresh can write KV.
#    (.github/workflows/refresh-data.yml already refreshes BOTH envs -- see
#    "Weekly data refresh" below; no default to flip any more.)
```

### Revert to adsb.lol-only

The failover feeds are enabled pending commercial permission from their
operators. To pull them at any moment, set **both** of these in
`[env.production.vars]` in `wrangler.toml` and redeploy. It is two vars, not one
line -- do not miss the second:

```toml
UPSTREAM_ADSB_FI_ENABLED        = "false"
UPSTREAM_AIRPLANES_LIVE_ENABLED = "false"
```

```sh
npm run deploy:production
```

Leave `ROUTE_ADSBDB_ENABLED = "true"` alone -- adsbdb supplies routes and the
type backfill, not aircraft positions, so it is not part of this revert.

Expect the feed to go empty during adsb.lol's shared-egress 429 windows once the
failovers are off; that is the trade being made, and it is why they were enabled
for the bench soak in the first place.

### Weekly data refresh

`.github/workflows/refresh-data.yml` runs Mondays 06:17 UTC and refreshes the
military side table + airport tiles for **both** production and staging, as a
matrix with `fail-fast: false` (a staging hiccup must never skip production).
Staging is deliberately kept current: it is the pre-flight rig, and stale
military/airport data there makes it a misleading place to check a change.

Manual run: Actions -> refresh-data -> Run workflow, choosing `production`
(default), `staging`, or `both`.

Then point the shipping firmware env at `https://scopes.valarsystems.com`
(`CLOUD_FEED_BASE`) and bake its key (`CLOUD_FEED_KEY`, ideally per-device via
`npm run derive-device-key`) so a customer device works with nothing to paste.
**Gate:** the overnight-slowdown gate was retired 2026-07-21 (closed, not
reproduced). The remaining gates before cutting a firmware release are a clean
24 h bench soak and the shipping-feature bench pass — see ROADMAP
"Release readiness".

### Bench burn-in against production

Before the pilot batch is flashed, burn a bench board in against the real
backend using the throwaway envs in `platformio.ini` — they point
`CLOUD_FEED_BASE` at production while every shipping `*-cloud` env stays on
staging, so this is not the shipping switch:

```sh
pio run -e blipscope-s3-128-prodburn -t upload   # or -s3-146-prodburn
```

No key is baked (repo key policy): paste the access key into the config page
once after flashing. Delete these envs when the pilot switch happens.

### Smoke test

`scripts/smoke-prod.sh` runs the full checklist — `/healthz`, `/v1/blips` over
Bend, `/v1/enrich` on a live hex pulled from that response, `/v1/config` for
every `variant::SLUG`, and `/v1/photo` via the enrich pointer — printing
PASS/FAIL per check plus raw bodies. It reads the key from **your** environment
and never prints it:

```sh
export BLIP_KEY='<your key>'          # never committed, never echoed
./scripts/smoke-prod.sh                                  # production
BASE=https://scopes-staging.valarsystems.com ./scripts/smoke-prod.sh
```

## Observability

- **Logs:** one JSON line per request:
  `{"evt":"req","ep":"/v1/blips","status":200,"ms":4,"model":"s3-146","colo":"ZRH","cache":"HIT","upstream":"adsb_lol","upstreamMs":0}`
  plus `evt:upstream` lines per upstream attempt and `evt:error` on 500s.
  Both staging and production currently keep **100%** (`head_sampling_rate = 1`).

  **Request log sampling — revisit around a few hundred devices.** Production ran
  at 1-in-20 (`0.05`) until the pilot. That was the wrong trade for a ~55-board
  fleet: at ~250k req/day an individual customer's requests were statistically
  invisible, so the logs could not answer "what happened on *this* device?" --
  the main reason to have them. Full sampling may tip into billed log volume,
  which is a rounding error against that. Once the fleet is in the hundreds the
  volume, not the debugging value, starts to dominate: drop back toward 1–5%
  (~200 B × requests) and lean on Analytics Engine for aggregates instead.
### Finding enrichment gaps (what to add to the library next)

Every enrich that can't fully resolve emits one `enrich_gap` point — the **root**
gap only, so one unknown airframe is counted once rather than three times:

| gap | meaning | fix |
| --- | --- | --- |
| `type` | no ICAO type resolved at all | per-hex fix (mil side table / hex photo override), or it's simply in no upstream DB |
| `name` | type resolved, no friendly name | add to `TYPE_NAMES`, or a `tn:<CODE>` KV key (no deploy needed) |
| `photo` | type + name resolved, no stock photo | run suggest → harvest → ingest for that code |

This is the point: the backlog stops being guesswork and becomes a list ranked by
what the fleet actually looks at. Rank the photo gaps with:

```sql
SELECT blob3 AS type, SUM(_sample_interval) AS lookups
FROM blipscope_proxy
WHERE blob1 = 'enrich_gap' AND blob2 = 'photo'
  AND timestamp > NOW() - INTERVAL '7' DAY
GROUP BY type ORDER BY lookups DESC LIMIT 25
```

Swap `blob2` for `'name'` (cheapest wins) or `'type'` and read `blob4` for the
hex. Run it against the Analytics Engine SQL API with an account API token that
has Analytics Read. Grepping Workers Logs for `"evt":"enrich_gap"` works too for
a live spot-check on one device.

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

### OTA memory telemetry (`X-Blip-OTA-Mem`)

A device attaches this header to the **first check-in after a firmware-update
attempt**, once, then forgets it (firmware: `OtaUpdater.h` `TakeOtaMemReport`,
persisted in NVS because a successful update reboots before anything else could
report it). Value:

```
X-Blip-OTA-Mem: <fwFrom>,<fwTo>,<preLargest>,<postLargest>,<result>
X-Blip-OTA-Mem: 4,5,46068,71668,ok
```

`result` is `ok`, `fail-<httpUpdateErr>`, or `incomplete` — the last meaning the
device rebooted mid-update (watchdog/power loss) and left only the pre-armed
record behind, which is the sole trace that case produces.

**Why it exists:** an OTA is easy to prove at a freshly-booted heap; devices
update from a *fragmented* one, and no bench can honestly reproduce weeks of
fragmentation. `preLargest` is the number the bench gate could never obtain — the
contiguous block actually available when a real update ran. Compare it against
the ~24–28 KB a TLS handshake needs.

**Handling** (`metrics.ts` `recordOtaMem`): validated to a fixed shape (device
input — malformed values are dropped silently), recorded past auth + rate
limiting only, and written as an Analytics Engine point with its **own index**
(`ota`) so it queries separately from per-request points. It is deliberately
**not** written to the request logs: log volume is real money at fleet scale
(see the cost model), and this adds zero requests, zero endpoints, zero log
bytes. Blobs `[("ota"), result, model]`, doubles `[fwFrom, fwTo, preLargest,
postLargest]`.

```sql
-- Did updates complete, and at what contiguous heap?
SELECT blob2 AS result, COUNT(*) AS n,
       MIN(double3) AS min_pre_largest, AVG(double3) AS avg_pre_largest
FROM blipscope_proxy WHERE index1 = 'ota' AND timestamp > NOW() - INTERVAL '30' DAY
GROUP BY result
```

Privacy stance (the user-facing wording lives in the root [README](../README.md#privacy--telemetry)):
operational telemetry only — heap numbers, firmware versions, a result code. No
identifiers beyond the headers a device must already send to be served
(`X-Blip-Key`, `X-Blip-Model`), and it rides a request cloud mode requires
anyway.

**Qualified 2026-07-21 — this used to say local-receiver users "never reach this
Worker".** That is no longer unconditionally true. A local-receiver device now
has an *Aircraft details* setting (config key `local-details`) that defaults to
routing detail-card lookups through this Worker, because the alternative
(api.adsbdb.com plus a second host for thumbnails) gave the users running their
own receiver the weakest cards and the heaviest network path. What that sends,
exactly — verified against `RequestCloudEnrich` and `CloudFeed::Headers`:

| sent | not sent |
| --- | --- |
| tapped aircraft's ICAO hex (in the path) | the receiver's address or hostname |
| its callsign, and **its** lat/lon (`cs`, `lat`, `lon`) | the device's configured location |
| `X-Blip-Key`, `X-Blip-Model`, `X-Blip-FW` | anything about other tracked aircraft |
| `X-Blip-OTA-Mem`, once, after an OTA attempt | |

Two honest caveats, because "no device position is sent" is true but incomplete:

- **A tapped aircraft is by definition near the receiver.** Its position is not
  the user's position, but across several taps the centroid approximates it.
  Anyone reasoning about this mode's privacy should treat it as coarse location,
  not as no location.
- **The device's IP reaches the Worker**, as with any HTTP request. It is used
  for per-IP rate limiting and appears in request logs (currently sampled at
  100% for the pilot).

**The architectural opt-out still exists, and is now explicit rather than
implied:** *Aircraft details → Off* contacts nothing at all (no background
enrichment, no tap lookup, no photo), and *adsbdb direct* keeps the old
third-party behaviour. Neither reaches this Worker.

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

1. **Cache-policy fix (P0)** — ✅ **shipped 2026-07-16:** all-empty metas now
   cache at the negative TTL (1 d) regardless of `found`, so an airframe whose
   DB record appears later is blank for a day, not a month (`resolveMeta`).
2. **Military floor (P1)** — ✅ **shipped 2026-07-16:** [src/military.ts](src/military.ts)
   carries the VRS military-allocation table (kept identical to the firmware's
   `SpecialAircraft.cpp` so the proxy and the on-screen MIL tag can't disagree);
   when the hex is in a mil block and `op` resolved empty, `op` is filled with
   the national label ("US military", …). `dbFlags` bit 0 is recorded at build
   time and labels non-block hexes "Military". Applied at serve time so
   pre-floor cached entries heal without waiting out their TTL. Never guesses
   types or registrations.
3. **Static airframe dataset (P2)** — ✅ **shipped 2026-07-16:** a `mil:<hex>`
   KV side table consulted at serve time only when the live record resolved
   with neither reg nor type (one extra KV read on that path alone; never
   overrides live data; heals negatively-cached entries like the floor). It
   fills `r`/`t`/`tn` — the operator stays the floor's job — and runs before
   the photo join, so a resolved type unlocks the generic type shot.
   Refreshed weekly by CI ([.github/workflows/refresh-data.yml](../.github/workflows/refresh-data.yml),
   Mondays 06:17 UTC — gated on a `CLOUDFLARE_API_TOKEN` secret, skipped until set).
   Loader: `npm run ingest:mildb -- --env staging`
   ([scripts/ingest-mildb.ts](scripts/ingest-mildb.ts)) — downloads the
   [Mictronics/aircraft-database](https://github.com/Mictronics/aircraft-database)
   export, selects mil-flagged (`f[0]==='1'`) or VRS-mil-block hexes (via the
   same `militaryOperator` table as the floor) that carry a reg or type
   (~17.3k rows on the 2026-07-12 export), and bulk-loads KV in ≤10k chunks.
   Idempotent; re-run weekly-ish to track upstream. **License review passed
   2026-07-16:** the exports are **ODC-By 1.0** (attribution-only) — credited
   on the device config page next to the adsb.lol ODbL line. Do **not**
   switch the source to the tar1090-db aggregate: it mixes in the ADSBx
   basic-ac-db, whose terms are nowhere stated. Note the KV write budget:
   ~17k writes per run needs the Workers Paid plan (free tier caps at
   1k/day).
4. **Callsign color (P3, optional):** static prefix table (RCH → Air
   Mobility Command, …) filling `op` when a callsign is broadcast and
   nothing else resolved. Military photos (by-hex photo APIs) are a separate
   licensing/attribution decision — explicitly not part of this item.

Tests per phase in the existing vitest suite: empty-meta TTL, mil-block
floor, KV side-table hit, prefix fill.

### Stock photo library for cloud mode (accepted 2026-07-09; pipeline shipped 2026-07-16)

> **Status: wire + serving + firmware shipped; content population is the
> remaining (post-launch) step.** The pipeline below is live and tested:
> `GET /v1/photo/<key>` serving ([src/photos.ts](src/photos.ts)), the enrich
> `p`/`pk` join ([src/enrich.ts](src/enrich.ts)), the per-layer license gate +
> content-addressed keys + credits render ([src/photolicense.ts](src/photolicense.ts)),
> the `GET /credits` page, the `npm run ingest` tool
> ([scripts/ingest-photos.ts](scripts/ingest-photos.ts)), and the firmware delta
> (cloud enrich reads `p` into `photoUrl`, authenticated photo fetch, the
> "representative photo" caption on `pk:"type"`). **Harvest under way:** the
> Commons harvest tool ([scripts/harvest-commons.ts](scripts/harvest-commons.ts),
> `npm run harvest`) turns a pick sheet of Commons file titles into
> license-gated manifest rows + downloaded sources, and the first real batch —
> **Tier-1 military (C17 F16 F15 F35 A10 B52 C30J K35R E6 H60, all PD service
> photos) + the first civil airliners (B738 A320 A321 B77W B789 A20N)** — was
> ingested to staging 2026-07-16 (21 types live on `/credits`). Same-day
> follow-ups driven by live bench reports: **military batch 2** (H64 TEX2 H47
> C130 T38 V22 P8 F18S — ~65% of the mil side-table fleet now photo-covered)
> and the **civil long tail** (the 737/A320neo families, E-Jets, CRJs, DH8D,
> widebodies, and common GA/bizjet/helo types) — **58 types live on
> `/credits`**. The tool now skips already-held files, paces downloads, and
> backs off on Wikimedia 429s. Remaining: further long tail ranked by
> proxy-log traffic (extend `photos/picksheet.json`, re-run harvest +
> ingest), and production ingest at launch.

**Harvest-phase checklist (when content population begins):**

1. **First-article discipline (like hardware).** Seed **3–5 hand-picked photos
   first** — a C-17, a 737, one GA type — and verify the *whole* chain on a
   bench unit before any bulk ingest: enrich carries `p`/`pk`, the photo rides
   the keep-alive connection, the "representative photo" caption renders on
   `pk:"type"`, and the `/credits` page lists the entries. Only then bulk-ingest.
2. **Photo-fetch outcome telemetry (noted, not built).** If photo-fetch failures
   show up at fleet scale, add fetch outcomes to the existing `[ota-mem]`-style
   one-shot header telemetry family (see `X-Blip-OTA-Mem` / `recordOtaMem`).
   Deferred until there's evidence it's needed.

Driver: cloud mode was deliberately photo-less (`AircraftManager.cpp` blanked
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
