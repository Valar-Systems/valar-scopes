# Blipscope egress relay

A dedicated-IP reverse-proxy cache in front of adsb.lol. It exists for one reason:
**Cloudflare Workers egress from a shared per-colo IP pool, and adsb.lol's
anonymous limiter counts other tenants' traffic against us.** A direct
`api.adsb.lol` curl from any dedicated IP returns live data at the same instant
the Worker path is throttled — the only variable is the IP. The relay gives
adsb.lol one stable, non-shared IP to rate-limit; the Worker consumes tiles from
the relay instead of hitting adsb.lol directly.

The Worker architecture is unchanged (edge cache, SWR, breaker, failover chain);
only the upstream base URL moves (`UPSTREAM_ADSB_LOL_BASE`). The relay adds one
thing the Worker can't do across colos: **request collapsing** (`proxy_cache_lock`),
so the whole fleet's polls for a tile become one upstream fetch per cache window.

## Topology (shipping HA pair)

| relay | host | box | IP | base URL var |
|---|---|---|---|---|
| relay-a (primary)   | `relay-a.valarsystems.com` | DigitalOcean NYC1 (1 GB) | `67.205.155.80`   | `UPSTREAM_ADSB_LOL_BASE` |
| relay-b (secondary) | `relay-b.valarsystems.com` | Vultr Seattle (vc2-1c-1gb) | `104.238.156.243` | `UPSTREAM_ADSB_LOL_BASE_B` |

relay-a leads the chain because **NYC is closer to adsb.lol's Hetzner-EU
upstream** than Seattle. Both hosts are orange-clouded (Cloudflare proxied) with
a Cloudflare Origin cert,
so Worker → Cloudflare → relay is TLS end to end and the origin IP stays hidden.
The relays are modelled as **two feeds in the Worker's failover chain**: relay-a
primary → relay-b secondary. relay-b is the *terminal* feed, so the breaker never
skips it (PR #119) — a relay-a outage fails over rather than blanking the fleet.

**Fleet failure story:** relay-a down → chain uses relay-b (invisible). Both down
→ Worker serves SWR cache (up to `SWR_MAX_AGE_S = 600 s`), then the device's stale
ladder escalates honestly. A cold tile during a total double-relay outage still
503s (no data to serve) — the residual gap the HA pair makes rare.

## Files on each box (scp these first)

| path | perms | what |
|---|---|---|
| `/etc/ssl/cloudflare/origin.pem` | 0644 root | Cloudflare Origin **certificate** |
| `/etc/ssl/cloudflare/origin.key` | 0600 root | Origin cert **private key** |
| `/etc/nginx/relay.key`           | 0600 root | the **X-Relay-Key** value (same string as the `RELAY_KEY` Worker secret) |

`setup-relay.sh` reads `relay.key` and generates the root-only nginx map; the key
value is never in the script or in git.

### scp (run locally, per box)

Use the **raw IPs** for scp/ssh — the hostnames are orange-clouded, so
`relay-a.valarsystems.com` resolves to Cloudflare, not the box.

```sh
# relay-a  (DigitalOcean NYC1)
scp origin.pem origin.key relay.key setup-relay.sh root@67.205.155.80:/root/
ssh root@67.205.155.80 'install -D -m600 /root/origin.key /etc/ssl/cloudflare/origin.key && \
                        install -D -m644 /root/origin.pem /etc/ssl/cloudflare/origin.pem && \
                        install -D -m600 /root/relay.key  /etc/nginx/relay.key && \
                        sudo CACHE_TTL=6s bash /root/setup-relay.sh'

# relay-b  (Vultr Seattle) -- identical
scp origin.pem origin.key relay.key setup-relay.sh root@104.238.156.243:/root/
ssh root@104.238.156.243 'install -D -m600 /root/origin.key /etc/ssl/cloudflare/origin.key && \
                          install -D -m644 /root/origin.pem /etc/ssl/cloudflare/origin.pem && \
                          install -D -m600 /root/relay.key  /etc/nginx/relay.key && \
                          sudo CACHE_TTL=6s bash /root/setup-relay.sh'
```

The same `relay.key` string goes on **both** boxes and into the Worker secret
`RELAY_KEY` (one shared relay key for the fleet).

## Worker secrets / vars

- **Secret:** `RELAY_KEY` — `npx wrangler secret put RELAY_KEY --env staging`
  (then `--env production` at cutover). Value = the contents of `relay.key`.
- **Vars** (already in `wrangler.toml`, not secret): `UPSTREAM_ADSB_LOL_BASE` and
  `UPSTREAM_ADSB_LOL_BASE_B` = the two relay hostnames.

Two adapter instances need **nothing beyond this**: `adsb_lol` (relay-a) and
`adsb_lol_b` (relay-b) are one code shape parameterised by base URL + id; they
share the `RELAY_KEY` header and get independent circuit breakers automatically.

## Verification sequence (staging first, then prod cutover)

1. Boxes up: `curl https://relay-a.valarsystems.com/healthz` → `ok`; a keyless
   `/v2/...` → `403`; with `-H "X-Relay-Key: <key>"` → aircraft JSON + an
   `X-Relay-Cache` header.
2. `npx wrangler secret put RELAY_KEY --env staging`, then
   `npx wrangler deploy --env staging`.
3. Board on **staging** cloud mode; confirm `X-Cache` flips MISS → **HIT** on
   `scopes-staging.valarsystems.com/v1/blips` and the device shows a live,
   non-stale picture.
4. **Cutover:** `npx wrangler secret put RELAY_KEY --env production`, then
   `npx wrangler deploy --env production`.

## Soak (one week) — pass criteria defined up front

Measured from the relay logs + fleet-side `X-Cache`:

| metric | source | PASS |
|---|---|---|
| adsb.lol 429 rate from the relay | `grep -c 'ustatus=429' relay.log` ÷ total | **< 1%** of upstream requests |
| upstream request rate (one IP) | `grep -c 'cache=MISS' relay.log` over time | **< 10 req/s** sustained at pilot volume |
| fleet freshness | Worker `X-Cache` HIT+STALE served vs MISS | **≥ 99%** served warm; STALE runs bounded |
| longest unbroken degraded run | device `[health]` DATA STALE spans | **< 90 s** (never reaches the NoData cap) |
| relay-a kill → failover | stop nginx on relay-a; watch `X-Upstream` | flips to `adsb_lol_b`, **no fleet-visible gap** |

Fail any → tune `CACHE_TTL` up (fewer upstream calls) or coarsen tiles before
adding a third relay IP. The design degrades by a knob, not off a cliff.

## Second upstream: adsb.fi under `/fi` (bench measurement only)

Each relay also proxies **adsb.fi** under a `/fi` path prefix (`/fi/v3/lat/...` →
`opendata.adsb.fi/api/v3/lat/...`), in its own cache zone but under **identical**
TTL and 429 hold-down policy, so a comparison measures the upstream rather than
our tuning.

**This is not a serving path.** adsb.fi granted permission to *test*, but their
terms are "personal, non-commercial use only … you may not license, sell, rent, or
lease any part of the data or the service" — no ODbL-style redistribution right,
which is exactly what this relay does. The Worker therefore ships with
`UPSTREAM_ADSB_FI_ENABLED = "false"` in every env. **Do not wire `/fi` into a
serving path without a written commercial grant** (see the reply draft in
[proxy/FEED-SOURCING.md](../proxy/FEED-SOURCING.md)).

Because it is out of the chain, a chain-order test would never exercise it. The
comparison instead comes from `fi-bench.sh`, a poller that runs on each box and
drives `/fi` directly — same tile, same radius, same ~15 s cadence as the adsb.lol
soak, so `measure.mjs` reports both upstreams side by side from one log:

```sh
bash fi-bench.sh --install                        # per box; systemd, restarts on boot
systemctl disable --now blipscope-fi-bench        # stop when the window closes
ssh root@<ip> "cat /var/log/nginx/relay.log" | node measure.mjs --hours 24
```

**Rate budget:** adsb.fi's public limit is **1 req/s per IP**, strictly enforced
(4 back-to-back requests → `200,429,429,429`; the same 4 spaced 1.2 s → all `200`),
and 4xx/429 responses *count toward the limit*, so the hold-down is load-bearing
rather than merely polite. The bench runs at ~0.08 req/s per relay (~8% of the
limit). Keep it there.

That 1 req/s is also the reason adsb.fi could not simply replace adsb.lol even if
licensing cleared: upstream rate here is `(distinct hot tiles) ÷ CACHE_TTL`, so at
`CACHE_TTL=8s` it supports only **~8 hot tiles per relay IP**.

## Operator courtesy

Relay IPs are **announced to adsb.lol** as courtesy identification (we feed them).
The production upstream table in [proxy/README.md](../proxy/README.md) records
which IPs are announced — keep it current when relays change.
